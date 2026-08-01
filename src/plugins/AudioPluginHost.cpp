#include "plugins/AudioPluginHost.h"

#include <QMessageBox>
#include <QWidget>

namespace openvegas {

NullAudioPluginHost &NullAudioPluginHost::instance()
{
    static NullAudioPluginHost host;
    return host;
}

bool NullAudioPluginHost::createInstance(const AudioPluginDesc &desc, FxSlot *slot)
{
    if (!slot) {
        return false;
    }
    *slot = fxSlotFromDesc(desc);
    return true;
}

void NullAudioPluginHost::releaseInstance(FxSlot *slot)
{
    if (!slot) {
        return;
    }
    slot->state.clear();
}

void NullAudioPluginHost::process(FxSlot *slot, float **in, float **out, int channels, int frames)
{
    Q_UNUSED(slot);
    if (!in || !out || channels <= 0 || frames <= 0) {
        return;
    }
    // Pass-through (identity) so future mix graphs can call the host safely.
    for (int c = 0; c < channels; ++c) {
        if (in[c] && out[c] && in[c] != out[c]) {
            for (int i = 0; i < frames; ++i) {
                out[c][i] = in[c][i];
            }
        }
    }
}

bool NullAudioPluginHost::openEditor(FxSlot *slot, QWidget *parent)
{
    if (!slot) {
        return false;
    }
    QMessageBox::information(
        parent, QObject::tr("Audio Plug-In"),
        QObject::tr("%1\n\nPlug-in editor host is not implemented yet "
                    "(VST1/VST2/VST3 SDK + audio engine — next stage).\n"
                    "Format: %2")
            .arg(slot->displayName)
            .arg(slot->format == PluginFormat::Vst3   ? QStringLiteral("VST3")
                 : slot->format == PluginFormat::Vst2 ? QStringLiteral("VST2")
                 : slot->format == PluginFormat::Vst1 ? QStringLiteral("VST1")
                 : slot->format == PluginFormat::Ofx  ? QStringLiteral("OFX")
                                                      : QStringLiteral("Builtin")));
    return false;
}

} // namespace openvegas
