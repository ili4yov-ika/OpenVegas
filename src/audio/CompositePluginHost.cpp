#include "audio/CompositePluginHost.h"

#include "audio/BuiltinDsp.h"
#include "audio/SoundForgeDsHost.h"
#include "audio/Vst3Host.h"
#include "plugins/AudioPluginScanner.h"

#include <QFileInfo>
#include <QHash>
#include <QMessageBox>
#include <QRegularExpression>
#include <QWidget>

#include <algorithm>
#include <memory>

namespace openvegas {
namespace {

QHash<FxSlot *, std::shared_ptr<BuiltinDspState>> g_builtinStates;

void passThrough(float **in, float **out, int channels, int frames)
{
    if (!in || !out || channels <= 0 || frames <= 0) {
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

} // namespace

BuiltinPluginHost &BuiltinPluginHost::instance()
{
    static BuiltinPluginHost h;
    return h;
}

bool BuiltinPluginHost::createInstance(const AudioPluginDesc &desc, FxSlot *slot)
{
    if (!slot) {
        return false;
    }
    *slot = fxSlotFromDesc(desc);
    auto st = std::make_shared<BuiltinDspState>();
    st->prepare(48000.0);
    g_builtinStates.insert(slot, st);
    return true;
}

void BuiltinPluginHost::releaseInstance(FxSlot *slot)
{
    g_builtinStates.remove(slot);
}

void BuiltinPluginHost::prepare(FxSlot *slot, double sampleRate, int blockSize)
{
    Q_UNUSED(blockSize);
    auto it = g_builtinStates.find(slot);
    if (it != g_builtinStates.end() && it.value()) {
        it.value()->prepare(sampleRate);
    }
}

void BuiltinPluginHost::reset(FxSlot *slot)
{
    auto it = g_builtinStates.find(slot);
    if (it != g_builtinStates.end() && it.value()) {
        it.value()->reset();
    }
}

void BuiltinPluginHost::process(FxSlot *slot, float **in, float **out, int channels, int frames)
{
    if (!slot || !in || !out || channels < 1 || frames <= 0) {
        return;
    }
    passThrough(in, out, channels, frames);
    float *L = out[0];
    float *R = channels > 1 ? out[1] : out[0];
    auto it = g_builtinStates.find(slot);
    std::shared_ptr<BuiltinDspState> st;
    if (it == g_builtinStates.end() || !it.value()) {
        st = std::make_shared<BuiltinDspState>();
        st->prepare(48000.0);
        g_builtinStates.insert(slot, st);
    } else {
        st = it.value();
    }
    processBuiltinFx(slot, st.get(), L, R, frames);
}

bool BuiltinPluginHost::openEditor(FxSlot *slot, QWidget *parent)
{
    Q_UNUSED(slot);
    Q_UNUSED(parent);
    // Builtin editors live in AudioEventFxDialog — not a floating plug-in window.
    return false;
}

CompositePluginHost &CompositePluginHost::instance()
{
    static CompositePluginHost h;
    return h;
}

AudioPluginHost *CompositePluginHost::hostForFormat(PluginFormat format) const
{
    switch (format) {
    case PluginFormat::Builtin:
        return &BuiltinPluginHost::instance();
    case PluginFormat::Vst3:
        return &Vst3Host::instance();
    case PluginFormat::Vst1:
    case PluginFormat::Vst2:
        return &Vst2Host::instance();
    case PluginFormat::DirectShow:
        return &SoundForgeDsHost::instance();
    default:
        return &BuiltinPluginHost::instance();
    }
}

AudioPluginHost *CompositePluginHost::hostFor(const FxSlot *slot) const
{
    if (!slot) {
        return &BuiltinPluginHost::instance();
    }
    return hostForFormat(slot->format);
}

bool CompositePluginHost::createInstance(const AudioPluginDesc &desc, FxSlot *slot)
{
    return hostForFormat(desc.format)->createInstance(desc, slot);
}

void CompositePluginHost::releaseInstance(FxSlot *slot)
{
    hostFor(slot)->releaseInstance(slot);
}

void CompositePluginHost::prepare(FxSlot *slot, double sampleRate, int blockSize)
{
    hostFor(slot)->prepare(slot, sampleRate, blockSize);
}

void CompositePluginHost::reset(FxSlot *slot)
{
    hostFor(slot)->reset(slot);
}

void CompositePluginHost::process(FxSlot *slot, float **in, float **out, int channels, int frames)
{
    hostFor(slot)->process(slot, in, out, channels, frames);
}

bool CompositePluginHost::openEditor(FxSlot *slot, QWidget *parent)
{
    return hostFor(slot)->openEditor(slot, parent);
}

int CompositePluginHost::parameterCount(const FxSlot *slot) const
{
    return hostFor(slot)->parameterCount(slot);
}

bool CompositePluginHost::parameterInfo(const FxSlot *slot, int index, QString *name, float *min,
                                        float *max, float *step) const
{
    return hostFor(slot)->parameterInfo(slot, index, name, min, max, step);
}

float CompositePluginHost::getParameter(const FxSlot *slot, int index) const
{
    return hostFor(slot)->getParameter(slot, index);
}

void CompositePluginHost::setParameter(FxSlot *slot, int index, float value)
{
    hostFor(slot)->setParameter(slot, index, value);
}

QByteArray CompositePluginHost::getState(const FxSlot *slot) const
{
    return hostFor(slot)->getState(slot);
}

bool CompositePluginHost::setState(FxSlot *slot, const QByteArray &state)
{
    return hostFor(slot)->setState(slot, state);
}

AudioPluginDesc CompositePluginHost::resolveDesc(const FxSlot &slot)
{
    AudioPluginDesc d;
    d.name = slot.displayName;
    d.format = slot.format;
    d.id = slot.pluginId;

    QString path;
    const int colon = slot.pluginId.indexOf(QLatin1Char(':'));
    if (colon >= 0
        && (slot.pluginId.startsWith(QLatin1String("vst1:"))
            || slot.pluginId.startsWith(QLatin1String("vst2:"))
            || slot.pluginId.startsWith(QLatin1String("vst3:")))) {
        path = slot.pluginId.mid(colon + 1);
    } else if (QFileInfo::exists(slot.pluginId)
               && (slot.pluginId.endsWith(QLatin1String(".dll"), Qt::CaseInsensitive)
                   || slot.pluginId.endsWith(QLatin1String(".vst3"), Qt::CaseInsensitive))) {
        path = slot.pluginId;
    }
    if (!path.isEmpty() && QFileInfo::exists(path)) {
        d.path = path;
        return d;
    }

    // Strip Vegas-style " (VST2, 64 Bit)" already handled at import; fuzzy-match scanner.
    QString want = slot.displayName.trimmed();
    static const QRegularExpression vstTag(
        QStringLiteral(R"([\t ]*\((VST[123]?|OFX)[^)]*\)\s*$)"),
        QRegularExpression::CaseInsensitiveOption);
    want.replace(vstTag, QString());
    want = want.trimmed();

    AudioPluginScanner scanner;
    QStringList v1, v2, v3;
    AudioPluginScanner::loadPathsFromSettings(&v1, &v2, &v3);
    scanner.setVst1Paths(v1.isEmpty() ? AudioPluginScanner::defaultVst1Roots() : v1);
    scanner.setVst2Paths(v2.isEmpty() ? AudioPluginScanner::defaultVst2Roots() : v2);
    scanner.setVst3Paths(v3.isEmpty() ? AudioPluginScanner::defaultVst3Roots() : v3);
    const QVector<AudioPluginDesc> all = scanner.scan();

    AudioPluginDesc best;
    int bestScore = 0;
    for (const AudioPluginDesc &cand : all) {
        if (slot.format == PluginFormat::Vst3 && cand.format != PluginFormat::Vst3) {
            continue;
        }
        if ((slot.format == PluginFormat::Vst1 || slot.format == PluginFormat::Vst2)
            && cand.format != PluginFormat::Vst1 && cand.format != PluginFormat::Vst2) {
            continue;
        }
        if (slot.format == PluginFormat::Vst1 && cand.format == PluginFormat::Vst2) {
            // allow VST2 for VST1 tag
        }
        const QString cn = cand.name.trimmed();
        int score = 0;
        if (cn.compare(want, Qt::CaseInsensitive) == 0) {
            score = 100;
        } else if (cn.contains(want, Qt::CaseInsensitive) || want.contains(cn, Qt::CaseInsensitive)) {
            score = 50 + int(std::min(cn.size(), want.size()));
        }
        if (score > bestScore) {
            bestScore = score;
            best = cand;
        }
    }
    if (bestScore > 0) {
        return best;
    }
    d.path = path;
    return d;
}

bool CompositePluginHost::ensureInstance(FxSlot *slot, QString *errorOut)
{
    if (!slot) {
        return false;
    }
    ensureFxHostKey(slot);
    if (slot->format == PluginFormat::Builtin || slot->format == PluginFormat::Ofx) {
        return true;
    }
    if (slot->format == PluginFormat::DirectShow) {
        // Identified by CLSID in the pluginId, not by a DLL path: the COM registration
        // already points at the server, so there is nothing to search for on disk.
        if (SoundForgeDsHost::instance().hasInstance(*slot)) {
            return true;
        }
        AudioPluginDesc d;
        d.id = slot->pluginId;
        d.name = slot->displayName;
        d.format = PluginFormat::DirectShow;
        const bool ok = SoundForgeDsHost::instance().createInstance(d, slot);
        if (ok) {
            if (!slot->state.isEmpty()) {
                SoundForgeDsHost::instance().setState(slot, slot->state);
            }
        } else if (errorOut) {
            *errorOut = QObject::tr("\"%1\" is not a registered Shared Plug-In effect.")
                            .arg(slot->displayName);
        }
        return ok;
    }

    AudioPluginDesc d = resolveDesc(*slot);
    if (d.path.isEmpty() || !QFileInfo::exists(d.path)) {
        const QString msg =
            QObject::tr("Could not find plug-in DLL for \"%1\". "
                        "Add the VST/VST3 folder in Preferences → Plug-Ins.")
                .arg(slot->displayName);
        if (errorOut) {
            *errorOut = msg;
        }
        return false;
    }
    d.format = slot->format;
    if (d.format == PluginFormat::Vst1) {
        d.format = PluginFormat::Vst2; // load via Vst2Host
    }
    if (d.name.isEmpty()) {
        d.name = slot->displayName;
    }

    const QByteArray state = slot->state;
    const bool bypass = slot->bypass;
    const QString name = slot->displayName;
    const QString key = slot->hostKey;
    const bool ok = createInstance(d, slot);
    slot->hostKey = key;
    slot->state = state;
    slot->bypass = bypass;
    if (!name.isEmpty()) {
        slot->displayName = name;
    }
    if (!ok && errorOut) {
        *errorOut = QObject::tr("Failed to load \"%1\" from %2").arg(name, d.path);
    }
    return ok;
}

void CompositePluginHost::ensureChainLoaded(QVector<FxSlot> *chain)
{
    if (!chain) {
        return;
    }
    for (FxSlot &slot : *chain) {
        ensureInstance(&slot, nullptr);
    }
}

void CompositePluginHost::captureChainState(QVector<FxSlot> *chain)
{
    if (!chain) {
        return;
    }
    for (FxSlot &slot : *chain) {
        if (slot.format == PluginFormat::Builtin) {
            // Builtin slots (Titles & Text, Media Generator, …) own their blob outright —
            // the UI writes it into the slot, so there is no instance to ask.
            continue;
        }
        const QByteArray blob = getState(&slot);
        if (!blob.isEmpty()) {
            slot.state = blob;
        }
    }
}

} // namespace openvegas
