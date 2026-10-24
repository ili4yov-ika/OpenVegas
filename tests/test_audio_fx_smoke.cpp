// Native Sound Forge editor -> project model -> AudioEngine graph integration.
// Run: python tools/run_ui_stubs_smoke.py --build build-msvc --suite audio-fx
#include "app/MainWindow.h"
#include "audio/AudioDecodeCache.h"
#include "audio/AudioEngine.h"
#include "audio/SoundForgeDsHost.h"
#include "plugins/SoundForgeHost.h"
#include "ui/AudioEventFxDialog.h"
#include <QApplication>
#include <QCheckBox>
#include <QDataStream>
#include <QElapsedTimer>
#include <QDoubleSpinBox>
#include <QVariantMap>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTabWidget>
#include <QThread>
#include <QThreadPool>
#include <QToolButton>
#include <QUndoStack>
#include <windows.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace openvegas;
static int checks = 0;
static void require(bool value, const char *message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
static void tick(QApplication &app, int ms = 100) {
    QElapsedTimer elapsed; elapsed.start();
    while (elapsed.elapsed() < ms) { app.processEvents(); QThread::msleep(5); }
}
static void writeTone(const QString &path) {
    QFile file(path); require(file.open(QIODevice::WriteOnly), "Create tone fixture");
    QDataStream out(&file); out.setByteOrder(QDataStream::LittleEndian);
    constexpr int samples = 48000 * 2, bytes = samples * 4;
    out.writeRawData("RIFF", 4); out << quint32(bytes + 36);
    out.writeRawData("WAVEfmt ", 8); out << quint32(16) << quint16(1) << quint16(2);
    out << quint32(48000) << quint32(192000) << quint16(4) << quint16(16);
    out.writeRawData("data", 4); out << quint32(bytes);
    for (int i = 0; i < samples; ++i) {
        // Four complete cycles per 512-frame block: RMS comparisons remain valid
        // when the EQ changes phase while its gain is neutral.
        const auto value = qint16(12000.0 * std::sin(2.0 * 3.14159265358979 * 375.0 * i / 48000.0));
        out << value << value;
    }
}
int main(int argc, char **argv) {
    std::cout << std::unitbuf;
    QApplication app(argc, argv);
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    try {
        AudioPluginDesc eq;
        for (const auto &descriptor : SoundForgeHost::pluginDescriptors())
            if (normalizeVegasPluginKey(descriptor.name) == QStringLiteral("expressfx graphic eq")) eq = descriptor;
        require(!eq.id.isEmpty(), "Registered ExpressFX Graphic EQ required for this integration check");
        int editorCount = 0;
        for (const auto &descriptor : SoundForgeHost::pluginDescriptors()) {
            QWidget probe; probe.setAttribute(Qt::WA_DontShowOnScreen);
            FxSlot slot; auto &native = SoundForgeDsHost::instance();
            require(native.createInstance(descriptor, &slot), "Native editor probe instance");
            native.prepare(&slot, 48000.0, 512);
            const bool opened = native.openEditor(&slot, &probe);
            std::cout << "Editor: " << descriptor.name.toStdString() << " = " << opened << std::endl;
            require(opened, "Registered Shared effect exposes its native editor");
            const auto titles = native.propertyPageTitles(SoundForgeHost::clsidFromPluginId(descriptor.id));
            auto *tabs = probe.findChild<QTabWidget *>();
            require((tabs ? tabs->count() : 1) == titles.size(), "Every advertised native property page activates");
            ++editorCount; native.releaseInstance(&slot);
        }
        std::cout << "Native editors opened: " << editorCount << " effects" << std::endl;
        const QString wav = temporary.filePath(QStringLiteral("tone.wav"));
        writeTone(wav);
        auto decoded = AudioDecodeCache::instance().get(wav, 48000);
        require(decoded && decoded->ready, "Tone decoded through application audio cache");
        MainWindow window;
        window.setAttribute(Qt::WA_DontShowOnScreen);
        window.onNewProject();
        auto &model = window.projectModel();
        model.addTrack(TrackKind::Audio);
        auto &track = model.tracks().last();
        track.fxChain.clear();
        TrackEvent event; event.id = 501; event.mediaKind = EventMediaKind::Audio;
        event.name = QStringLiteral("tone"); event.mediaPath = wav; event.lengthSec = 2.0;
        event.mediaLengthSec = 2.0; event.looped = false;
        event.fxChain.push_back(makeFxSlot(eq.name, eq.format, eq.id));
        track.events.push_back(event);
        window.refreshTimeline();
        auto *engine = window.findChild<AudioEngine *>();
        require(engine != nullptr, "MainWindow audio engine");
        auto rms = [&]() {
            std::vector<float> left(512), right(512);
            engine->graph().reset();
            for (int block = 0; block < 8; ++block)
                engine->graph().process(0.25 + block * 512.0 / engine->sampleRate(), left.data(), right.data(), 512);
            double energy = 0.0; for (float value : left) energy += value * value;
            return std::sqrt(energy / 512);
        };
        const double dry = rms(); require(dry > 0.01, "Dry reference is signal");
        window.onAudioEventFx(501);
        auto *dialog = window.findChild<AudioEventFxDialog *>();
        require(dialog != nullptr, "Actual Event FX dialog");
        dialog->setAttribute(Qt::WA_DontShowOnScreen);
        tick(app);
        auto *embed = dialog->findChild<QWidget *>(QStringLiteral("aefxVstEmbed"));
        require(embed != nullptr, "Native editor embed");
        std::vector<HWND> faders;
        EnumChildWindows(reinterpret_cast<HWND>(embed->winId()), [](HWND child, LPARAM pointer) -> BOOL {
            wchar_t name[128]{}; GetClassNameW(child, name, 128);
            if (QString::fromWCharArray(name).endsWith(QStringLiteral("FaderClass")))
                reinterpret_cast<std::vector<HWND> *>(pointer)->push_back(child);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&faders));
        require(faders.size() >= 11, "Real native EQ faders");
        const QByteArray before = model.findEvent(501)->fxChain.first().state;
        HWND gain = faders.back(); RECT rect{}; GetClientRect(gain, &rect);
        const LPARAM point = MAKELPARAM(rect.right / 2, rect.bottom * 3 / 4);
        SendMessageW(gain, WM_LBUTTONDOWN, MK_LBUTTON, point);
        SendMessageW(gain, WM_MOUSEMOVE, MK_LBUTTON, point);
        SendMessageW(gain, WM_LBUTTONUP, 0, point);
        tick(app);
        const QByteArray edited = model.findEvent(501)->fxChain.first().state;
        require(edited != before, "Native edit reaches project state without closing dialog");
        const double wet = rms();
        std::cout << "Native EQ output/dry RMS=" << wet / dry << std::endl;
        require(wet / dry < 0.95, "Native edit reaches actual event FX output");
        auto *node = dialog->findChild<FxChainNodeWidget *>();
        auto *enabled = node ? node->findChild<QCheckBox *>() : nullptr;
        require(enabled != nullptr, "FX enable checkbox");
        enabled->setChecked(false);
        require(std::abs(rms() / dry - 1.0) < 0.01, "Live bypass restores dry audio");
        enabled->setChecked(true);
        require(rms() / dry < 0.95, "Re-enable restores processed audio");
        dialog->close();
        auto *undo = window.findChild<QUndoStack *>();
        require(undo && undo->canUndo(), "Native FX edit recorded in Undo");
        undo->undo();
        require(std::abs(rms() / dry - 1.0) < 0.01, "Undo restores DSP, not only serialized state");
        undo->redo();
        require(rms() / dry < 0.95, "Redo restores native DSP");
        tick(app);
        QToolButton *remove = nullptr;
        for (auto *button : dialog->findChildren<QToolButton *>())
            if (button->toolTip() == QStringLiteral("Remove selected plug-in")) remove = button;
        require(remove != nullptr, "Remove plug-in button");
        remove->click();
        require(model.findEvent(501)->fxChain.isEmpty(), "Remove updates project chain");
        require(std::abs(rms() / dry - 1.0) < 0.01, "Structural edit updates graph while FX window is open");
        MixerBus bus; bus.id = 9001; bus.name = QStringLiteral("EQ bus");
        auto busFx = makeFxSlot(eq.name, eq.format, eq.id); busFx.state = edited;
        busFx.bypass = true;
        bus.fxChain.push_back(busFx); model.mixerBuses().push_back(bus);
        model.tracks().last().busId = bus.id;
        window.refreshTimeline();
        // Compare the same route: the bus adds its own pan law at the centre.
        const double busDry = rms();
        model.mixerBuses().last().fxChain.first().bypass = false;
        engine->syncMixerLive();
        require(rms() / busDry < 0.95, "Mixer bus loads and processes native FX");
        model.mixerBuses().last().fxChain.first().bypass = true;
        engine->syncMixerLive();
        require(std::abs(rms() / busDry - 1.0) < 0.01, "Live mixer-bus FX bypass");
        model.mixerBuses().clear(); model.tracks().last().busId = -1;
        AssignableFxBus assignable; assignable.id = 9101; assignable.name = QStringLiteral("Master EQ");
        busFx.bypass = false;
        assignable.fxChain.push_back(busFx);
        model.assignableFxBuses().push_back(assignable);
        window.refreshTimeline();
        require(rms() / dry < 0.95, "Assignable FX processes on master");
        model.assignableFxBuses().last().fxChain.first().bypass = true;
        engine->syncMixerLive();
        require(std::abs(rms() / dry - 1.0) < 0.01, "Live assignable FX bypass");
        window.onNewProject();
        tick(app);
        require(!dialog->isVisible(), "New project clears open FX target");
        model.addTrack(TrackKind::Audio);
        model.tracks().last().fxChain.clear();
        event.id = 502; event.fxChain = {makeFxSlot(QStringLiteral("Track EQ"), PluginFormat::Builtin, QStringLiteral("builtin:Track EQ"))};
        model.tracks().last().events.push_back(event);
        window.refreshTimeline(); window.onAudioEventFx(502); tick(app);
        QDoubleSpinBox *eqGain = nullptr, *eqFrequency = nullptr;
        for (auto *spin : dialog->findChildren<QDoubleSpinBox *>()) {
            if (spin->suffix() == QStringLiteral(" dB")) eqGain = spin;
            if (spin->suffix() == QStringLiteral(" Hz")) eqFrequency = spin;
        }
        require(eqGain && eqFrequency, "Builtin EQ controls");
        auto eqParams = [&]() {
            QDataStream stream(model.findEvent(502)->fxChain.first().state);
            stream.setVersion(QDataStream::Qt_6_0); QVariantMap result; stream >> result; return result;
        };
        eqFrequency->setValue(1000.0); eqGain->setValue(-6.0); tick(app);
        require(eqParams().value(QStringLiteral("band0.gain")).toDouble() == -6.0, "First live builtin EQ edit");
        window.refreshTimeline(); tick(app);
        eqGain->setValue(-12.0); tick(app);
        require(eqParams().value(QStringLiteral("band0.gain")).toDouble() == -12.0, "Repeated edit resolves current slot after graph copy");
        require(rms() / dry < 0.8, "Repeated builtin edit changes audio");
        dialog->close();
        require(QThreadPool::globalInstance()->waitForDone(10000), "Preview jobs finished");
        std::cout << "Audio FX smoke passed: " << checks << " checks" << std::endl;
    } catch (const std::exception &error) {
        std::cerr << "Audio FX smoke failed: " << error.what() << std::endl;
        QThreadPool::globalInstance()->waitForDone(10000);
        return 1;
    }
    return 0;
}
