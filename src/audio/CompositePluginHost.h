#pragma once

#include "plugins/AudioPluginHost.h"

namespace openvegas {

class BuiltinPluginHost;
class Vst3Host;
class Vst2Host;

/** Routes Builtin → BuiltinDsp host, VST3 → Vst3Host, VST1/2 → Vst2Host. */
class CompositePluginHost : public AudioPluginHost {
public:
    static CompositePluginHost &instance();

    bool createInstance(const AudioPluginDesc &desc, FxSlot *slot) override;
    void releaseInstance(FxSlot *slot) override;
    void prepare(FxSlot *slot, double sampleRate, int blockSize) override;
    void reset(FxSlot *slot) override;
    void process(FxSlot *slot, float **in, float **out, int channels, int frames) override;
    bool openEditor(FxSlot *slot, QWidget *parent) override;
    int parameterCount(const FxSlot *slot) const override;
    bool parameterInfo(const FxSlot *slot, int index, QString *name, float *min, float *max,
                       float *step) const override;
    float getParameter(const FxSlot *slot, int index) const override;
    void setParameter(FxSlot *slot, int index, float value) override;
    QByteArray getState(const FxSlot *slot) const override;
    bool setState(FxSlot *slot, const QByteArray &state) override;

private:
    AudioPluginHost *hostFor(const FxSlot *slot) const;
    AudioPluginHost *hostForFormat(PluginFormat format) const;
};

class BuiltinPluginHost : public AudioPluginHost {
public:
    static BuiltinPluginHost &instance();
    bool createInstance(const AudioPluginDesc &desc, FxSlot *slot) override;
    void releaseInstance(FxSlot *slot) override;
    void prepare(FxSlot *slot, double sampleRate, int blockSize) override;
    void reset(FxSlot *slot) override;
    void process(FxSlot *slot, float **in, float **out, int channels, int frames) override;
    bool openEditor(FxSlot *slot, QWidget *parent) override;
};

} // namespace openvegas
