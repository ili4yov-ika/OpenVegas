#include "audio/Vst3Host.h"

#include <QFileInfo>
#include <QHash>
#include <QMessageBox>
#include <QWidget>
#include <QWindow>

#include <cstring>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// VeSTige VST2 ABI (LMMS aeffectx.h) — GPL-compatible substitute for Steinberg SDK
#include "aeffectx.h"

#ifdef OPENVGAS_HAS_VST3_SDK
// Steinberg VST3 SDK integration when OPENVGAS_VST3_SDK_PATH is set.
#endif

namespace openvegas {
namespace {

void passThrough(float **in, float **out, int channels, int frames)
{
    if (!in || !out) {
        return;
    }
    for (int c = 0; c < channels; ++c) {
        if (in[c] && out[c] && in[c] != out[c]) {
            for (int i = 0; i < frames; ++i) {
                out[c][i] = in[c][i];
            }
        }
    }
}

QString pathFromPluginId(const QString &pluginId)
{
    const int colon = pluginId.indexOf(QLatin1Char(':'));
    if (colon >= 0 && colon + 1 < pluginId.size()) {
        return pluginId.mid(colon + 1);
    }
    return pluginId;
}

struct HostUserData {
    double sampleRate = 48000.0;
    int blockSize = 512;
};

QHash<AEffect *, HostUserData *> &vstHostDataMap()
{
    static QHash<AEffect *, HostUserData *> map;
    return map;
}

intptr_t VST_CALL_CONV hostCallback(AEffect *effect, int32_t opcode, int32_t index, intptr_t value,
                                    void *ptr, float opt)
{
    Q_UNUSED(index);
    Q_UNUSED(value);
    Q_UNUSED(opt);
    HostUserData *ud = effect ? vstHostDataMap().value(effect, nullptr) : nullptr;
    switch (opcode) {
    case audioMasterVersion:
        return 2400;
    case audioMasterGetSampleRate:
        return intptr_t(ud ? ud->sampleRate : 48000);
    case audioMasterGetBlockSize:
        return intptr_t(ud ? ud->blockSize : 512);
    case audioMasterGetCurrentProcessLevel:
        return 2; // realtime
    case audioMasterWantMidi:
        return 0;
    case audioMasterGetVendorString:
        if (ptr) {
            std::strncpy(static_cast<char *>(ptr), "OpenVegas", 63);
        }
        return 1;
    case audioMasterGetProductString:
        if (ptr) {
            std::strncpy(static_cast<char *>(ptr), "OpenVegas", 63);
        }
        return 1;
    case audioMasterGetVendorVersion:
        return 1000;
    case audioMasterCanDo:
        if (ptr) {
            const char *s = static_cast<const char *>(ptr);
            if (std::strcmp(s, "supplyIdle") == 0 || std::strcmp(s, "sendVstEvents") == 0
                || std::strcmp(s, "sendVstMidiEvent") == 0 || std::strcmp(s, "receiveVstEvents") == 0
                || std::strcmp(s, "receiveVstMidiEvent") == 0 || std::strcmp(s, "sizeWindow") == 0) {
                return 1;
            }
        }
        return 0;
    default:
        return 0;
    }
}

struct Vst2Instance {
    QString path;
#ifdef _WIN32
    HMODULE module = nullptr;
#else
    void *module = nullptr;
#endif
    AEffect *effect = nullptr;
    HostUserData hostData;
    std::vector<float *> inPtrs;
    std::vector<float *> outPtrs;
    std::vector<float> silentIn;
    bool opened = false;
};

QHash<FxSlot *, std::shared_ptr<Vst2Instance>> g_vst2;

} // namespace

Vst3Host &Vst3Host::instance()
{
    static Vst3Host h;
    return h;
}

bool Vst3Host::createInstance(const AudioPluginDesc &desc, FxSlot *slot)
{
    if (!slot) {
        return false;
    }
    *slot = fxSlotFromDesc(desc);
    if (!desc.path.isEmpty()) {
        slot->pluginId = QStringLiteral("vst3:") + desc.path;
    }
    auto inst = std::make_shared<Instance>();
    inst->path = desc.path.isEmpty() ? pathFromPluginId(slot->pluginId) : desc.path;
#ifdef OPENVGAS_HAS_VST3_SDK
    inst->loaded = false;
#else
    inst->loaded = false;
#endif
    m_instances.insert(slot, inst);
    return true;
}

void Vst3Host::releaseInstance(FxSlot *slot)
{
    m_instances.remove(slot);
    if (slot) {
        slot->state.clear();
    }
}

void Vst3Host::prepare(FxSlot *slot, double sampleRate, int blockSize)
{
    auto it = m_instances.find(slot);
    if (it == m_instances.end() || !it.value()) {
        return;
    }
    it.value()->sampleRate = sampleRate;
    it.value()->blockSize = blockSize;
}

void Vst3Host::reset(FxSlot *slot)
{
    Q_UNUSED(slot);
}

void Vst3Host::process(FxSlot *slot, float **in, float **out, int channels, int frames)
{
    if (slot && slot->bypass) {
        passThrough(in, out, channels, frames);
        return;
    }
#ifdef OPENVGAS_HAS_VST3_SDK
#endif
    passThrough(in, out, channels, frames);
}

bool Vst3Host::openEditor(FxSlot *slot, QWidget *parent)
{
    if (!slot) {
        return false;
    }
#ifdef OPENVGAS_HAS_VST3_SDK
#else
    QMessageBox::information(
        parent, QObject::tr("VST3"),
        QObject::tr("%1\n\nVST3 editor requires the Steinberg VST3 SDK "
                    "(CMake: OPENVGAS_VST3_SDK_PATH). Module path: %2")
            .arg(slot->displayName)
            .arg(m_instances.value(slot) ? m_instances.value(slot)->path : QString()));
#endif
    return false;
}

int Vst3Host::parameterCount(const FxSlot *slot) const
{
    Q_UNUSED(slot);
    return 0;
}

bool Vst3Host::parameterInfo(const FxSlot *slot, int index, QString *name, float *min, float *max,
                             float *step) const
{
    Q_UNUSED(slot);
    Q_UNUSED(index);
    Q_UNUSED(name);
    Q_UNUSED(min);
    Q_UNUSED(max);
    Q_UNUSED(step);
    return false;
}

float Vst3Host::getParameter(const FxSlot *slot, int index) const
{
    Q_UNUSED(slot);
    Q_UNUSED(index);
    return 0.f;
}

void Vst3Host::setParameter(FxSlot *slot, int index, float value)
{
    Q_UNUSED(slot);
    Q_UNUSED(index);
    Q_UNUSED(value);
}

QByteArray Vst3Host::getState(const FxSlot *slot) const
{
    return slot ? slot->state : QByteArray{};
}

bool Vst3Host::setState(FxSlot *slot, const QByteArray &state)
{
    if (!slot) {
        return false;
    }
    slot->state = state;
    return true;
}

// ---------------- VST2 / VST1 (VeSTige + LoadLibrary) ----------------

Vst2Host &Vst2Host::instance()
{
    static Vst2Host h;
    return h;
}

bool Vst2Host::createInstance(const AudioPluginDesc &desc, FxSlot *slot)
{
    if (!slot) {
        return false;
    }
    *slot = fxSlotFromDesc(desc);
    const QString path = desc.path.isEmpty() ? pathFromPluginId(slot->pluginId) : desc.path;
    if (!path.isEmpty()) {
        const QString prefix =
            (desc.format == PluginFormat::Vst1) ? QStringLiteral("vst1:") : QStringLiteral("vst2:");
        slot->pluginId = prefix + path;
    }

    auto inst = std::make_shared<Vst2Instance>();
    inst->path = path;
#ifdef _WIN32
    if (!path.isEmpty() && QFileInfo::exists(path)) {
        inst->module = LoadLibraryW(reinterpret_cast<LPCWSTR>(path.utf16()));
        if (inst->module) {
            using MainProc = AEffect *(VST_CALL_CONV *)(audioMasterCallback);
            auto mainProc = reinterpret_cast<MainProc>(GetProcAddress(inst->module, "VSTPluginMain"));
            if (!mainProc) {
                mainProc = reinterpret_cast<MainProc>(GetProcAddress(inst->module, "main"));
            }
            if (mainProc) {
                inst->effect = mainProc(hostCallback);
                if (inst->effect && inst->effect->magic == kEffectMagic) {
                    vstHostDataMap().insert(inst->effect, &inst->hostData);
                    inst->effect->dispatcher(inst->effect, effOpen, 0, 0, nullptr, 0.f);
                    inst->opened = true;
                } else {
                    inst->effect = nullptr;
                }
            }
        }
    }
#else
    Q_UNUSED(path);
#endif
    g_vst2.insert(slot, inst);
    return true;
}

void Vst2Host::releaseInstance(FxSlot *slot)
{
    auto it = g_vst2.find(slot);
    if (it != g_vst2.end() && it.value()) {
        auto &inst = *it.value();
        if (inst.effect && inst.opened) {
            inst.effect->dispatcher(inst.effect, effMainsChanged, 0, 0, nullptr, 0.f);
            inst.effect->dispatcher(inst.effect, effClose, 0, 0, nullptr, 0.f);
            vstHostDataMap().remove(inst.effect);
            inst.effect = nullptr;
        }
#ifdef _WIN32
        if (inst.module) {
            FreeLibrary(inst.module);
            inst.module = nullptr;
        }
#endif
    }
    g_vst2.remove(slot);
    if (slot) {
        slot->state.clear();
    }
}

void Vst2Host::prepare(FxSlot *slot, double sampleRate, int blockSize)
{
    auto it = g_vst2.find(slot);
    if (it == g_vst2.end() || !it.value() || !it.value()->effect) {
        return;
    }
    auto &inst = *it.value();
    inst.hostData.sampleRate = sampleRate;
    inst.hostData.blockSize = blockSize;
    inst.effect->dispatcher(inst.effect, effSetSampleRate, 0, 0, nullptr, float(sampleRate));
    inst.effect->dispatcher(inst.effect, effSetBlockSize, 0, intptr_t(blockSize), nullptr, 0.f);
    inst.effect->dispatcher(inst.effect, effMainsChanged, 0, 1, nullptr, 0.f);
    const int nIn = std::max(2, int(inst.effect->numInputs));
    const int nOut = std::max(2, int(inst.effect->numOutputs));
    inst.inPtrs.assign(size_t(nIn), nullptr);
    inst.outPtrs.assign(size_t(nOut), nullptr);
    inst.silentIn.assign(size_t(blockSize), 0.f);
}

void Vst2Host::reset(FxSlot *slot)
{
    auto it = g_vst2.find(slot);
    if (it == g_vst2.end() || !it.value() || !it.value()->effect) {
        return;
    }
    // Soft reset via mains off/on
    it.value()->effect->dispatcher(it.value()->effect, effMainsChanged, 0, 0, nullptr, 0.f);
    it.value()->effect->dispatcher(it.value()->effect, effMainsChanged, 0, 1, nullptr, 0.f);
}

void Vst2Host::process(FxSlot *slot, float **in, float **out, int channels, int frames)
{
    if (!slot || !in || !out || channels < 1 || frames <= 0) {
        return;
    }
    if (slot->bypass) {
        passThrough(in, out, channels, frames);
        return;
    }
    auto it = g_vst2.find(slot);
    if (it == g_vst2.end() || !it.value() || !it.value()->effect) {
        passThrough(in, out, channels, frames);
        return;
    }
    auto &inst = *it.value();
    AEffect *fx = inst.effect;
    passThrough(in, out, channels, frames);

    if (inst.inPtrs.empty()) {
        prepare(slot, inst.hostData.sampleRate, std::max(frames, inst.hostData.blockSize));
    }

    const int nIn = int(inst.inPtrs.size());
    const int nOut = int(inst.outPtrs.size());
    for (int c = 0; c < nIn; ++c) {
        if (c < channels && in[c]) {
            inst.inPtrs[size_t(c)] = in[c];
        } else {
            if (int(inst.silentIn.size()) < frames) {
                inst.silentIn.assign(size_t(frames), 0.f);
            }
            inst.inPtrs[size_t(c)] = inst.silentIn.data();
        }
    }
    for (int c = 0; c < nOut; ++c) {
        if (c < channels && out[c]) {
            inst.outPtrs[size_t(c)] = out[c];
        } else if (!inst.outPtrs.empty()) {
            inst.outPtrs[size_t(c)] = inst.outPtrs[0];
        }
    }

    if (fx->flags & effFlagsCanReplacing && fx->processReplacing) {
        fx->processReplacing(fx, inst.inPtrs.data(), inst.outPtrs.data(), frames);
    } else if (fx->process) {
        fx->process(fx, inst.inPtrs.data(), inst.outPtrs.data(), frames);
    }
}

bool Vst2Host::openEditor(FxSlot *slot, QWidget *parent)
{
    if (!slot) {
        return false;
    }
    auto it = g_vst2.find(slot);
    if (it == g_vst2.end() || !it.value() || !it.value()->effect) {
        QMessageBox::information(parent, QObject::tr("VST"),
                                 QObject::tr("%1\n\nCould not load plug-in DLL:\n%2")
                                     .arg(slot->displayName)
                                     .arg(pathFromPluginId(slot->pluginId)));
        return false;
    }
    AEffect *fx = it.value()->effect;
    if (!(fx->flags & effFlagsHasEditor)) {
        QMessageBox::information(parent, QObject::tr("VST"),
                                 QObject::tr("%1 has no native editor.").arg(slot->displayName));
        return false;
    }
#ifdef _WIN32
    auto *win = new QWidget(parent, Qt::Window);
    win->setAttribute(Qt::WA_DeleteOnClose);
    win->setWindowTitle(slot->displayName);
    win->resize(400, 300);
    win->show();
    WId wid = win->winId();
    fx->dispatcher(fx, effEditOpen, 0, 0, reinterpret_cast<void *>(wid), 0.f);
    QObject::connect(win, &QWidget::destroyed, win, [fx]() {
        fx->dispatcher(fx, effEditClose, 0, 0, nullptr, 0.f);
    });
    return true;
#else
    Q_UNUSED(parent);
    return false;
#endif
}

int Vst2Host::parameterCount(const FxSlot *slot) const
{
    auto it = g_vst2.find(const_cast<FxSlot *>(slot));
    if (it == g_vst2.end() || !it.value() || !it.value()->effect) {
        return 0;
    }
    return int(it.value()->effect->numParams);
}

bool Vst2Host::parameterInfo(const FxSlot *slot, int index, QString *name, float *min, float *max,
                             float *step) const
{
    auto it = g_vst2.find(const_cast<FxSlot *>(slot));
    if (it == g_vst2.end() || !it.value() || !it.value()->effect) {
        return false;
    }
    if (index < 0 || index >= it.value()->effect->numParams) {
        return false;
    }
    char buf[64] = {};
    it.value()->effect->dispatcher(it.value()->effect, effGetParamName, index, 0, buf, 0.f);
    if (name) {
        *name = QString::fromUtf8(buf);
    }
    if (min) {
        *min = 0.f;
    }
    if (max) {
        *max = 1.f;
    }
    if (step) {
        *step = 0.01f;
    }
    return true;
}

float Vst2Host::getParameter(const FxSlot *slot, int index) const
{
    auto it = g_vst2.find(const_cast<FxSlot *>(slot));
    if (it == g_vst2.end() || !it.value() || !it.value()->effect || !it.value()->effect->getParameter) {
        return 0.f;
    }
    return it.value()->effect->getParameter(it.value()->effect, index);
}

void Vst2Host::setParameter(FxSlot *slot, int index, float value)
{
    auto it = g_vst2.find(slot);
    if (it == g_vst2.end() || !it.value() || !it.value()->effect || !it.value()->effect->setParameter) {
        return;
    }
    it.value()->effect->setParameter(it.value()->effect, index, value);
}

QByteArray Vst2Host::getState(const FxSlot *slot) const
{
    auto it = g_vst2.find(const_cast<FxSlot *>(slot));
    if (it != g_vst2.end() && it.value() && it.value()->effect) {
        void *data = nullptr;
        const intptr_t sz =
            it.value()->effect->dispatcher(it.value()->effect, effGetChunk, 0, 0, &data, 0.f);
        if (sz > 0 && data) {
            return QByteArray(static_cast<const char *>(data), int(sz));
        }
    }
    return slot ? slot->state : QByteArray{};
}

bool Vst2Host::setState(FxSlot *slot, const QByteArray &state)
{
    if (!slot) {
        return false;
    }
    slot->state = state;
    auto it = g_vst2.find(slot);
    if (it != g_vst2.end() && it.value() && it.value()->effect && !state.isEmpty()) {
        it.value()->effect->dispatcher(it.value()->effect, effSetChunk, 0, state.size(),
                                       const_cast<char *>(state.data()), 0.f);
    }
    return true;
}

} // namespace openvegas
