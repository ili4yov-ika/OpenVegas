#include "audio/CompositePluginHost.h"

#include "audio/BuiltinDsp.h"
#include "audio/Vst3Host.h"

#include <QHash>
#include <QMessageBox>
#include <QWidget>

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

} // namespace openvegas
