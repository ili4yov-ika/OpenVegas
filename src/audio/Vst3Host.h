#pragma once

#include "plugins/AudioPluginHost.h"

#include <QHash>
#include <memory>

namespace openvegas {

/**
 * VST3 host. Full Steinberg SDK path when OPENVGAS_HAS_VST3_SDK is defined;
 * otherwise createInstance succeeds for bookkeeping and process is pass-through.
 */
class Vst3Host : public AudioPluginHost {
public:
    static Vst3Host &instance();

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
    struct Instance {
        QString path;
        double sampleRate = 48000.0;
        int blockSize = 512;
        bool loaded = false;
    };
    QHash<FxSlot *, std::shared_ptr<Instance>> m_instances;
};

/**
 * VST2 / VST1 host via VeSTige ABI (thirdparty/lmms/include/aeffectx.h) + LoadLibrary.
 */
class Vst2Host : public AudioPluginHost {
public:
    static Vst2Host &instance();

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
};

} // namespace openvegas
