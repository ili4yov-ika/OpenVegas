#pragma once

#include "plugins/AudioPluginTypes.h"

#include <QByteArray>
#include <QString>
#include <QtGlobal>

class QWidget;

namespace openvegas {

/**
 * Abstract audio plug-in host.
 * CompositePluginHost routes Builtin / VST3 / VST2; NullAudioPluginHost is a no-op stand-in.
 */
class AudioPluginHost {
public:
    virtual ~AudioPluginHost() = default;

    virtual bool createInstance(const AudioPluginDesc &desc, FxSlot *slot) = 0;
    virtual void releaseInstance(FxSlot *slot) = 0;

    virtual void prepare(FxSlot *slot, double sampleRate, int blockSize)
    {
        Q_UNUSED(slot);
        Q_UNUSED(sampleRate);
        Q_UNUSED(blockSize);
    }
    virtual void reset(FxSlot *slot) { Q_UNUSED(slot); }

    /** Realtime process — may be in-place (in == out). */
    virtual void process(FxSlot *slot, float **in, float **out, int channels, int frames) = 0;
    /** Open native editor; returns false if unavailable. */
    virtual bool openEditor(FxSlot *slot, QWidget *parent) = 0;

    virtual int parameterCount(const FxSlot *slot) const
    {
        Q_UNUSED(slot);
        return 0;
    }
    virtual bool parameterInfo(const FxSlot *slot, int index, QString *name, float *min, float *max,
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
    virtual float getParameter(const FxSlot *slot, int index) const
    {
        Q_UNUSED(slot);
        Q_UNUSED(index);
        return 0.f;
    }
    virtual void setParameter(FxSlot *slot, int index, float value)
    {
        Q_UNUSED(slot);
        Q_UNUSED(index);
        Q_UNUSED(value);
    }
    virtual QByteArray getState(const FxSlot *slot) const
    {
        return slot ? slot->state : QByteArray{};
    }
    virtual bool setState(FxSlot *slot, const QByteArray &state)
    {
        if (!slot) {
            return false;
        }
        slot->state = state;
        return true;
    }
};

class NullAudioPluginHost : public AudioPluginHost {
public:
    static NullAudioPluginHost &instance();

    bool createInstance(const AudioPluginDesc &desc, FxSlot *slot) override;
    void releaseInstance(FxSlot *slot) override;
    void process(FxSlot *slot, float **in, float **out, int channels, int frames) override;
    bool openEditor(FxSlot *slot, QWidget *parent) override;
};

} // namespace openvegas
