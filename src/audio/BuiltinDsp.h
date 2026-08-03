#pragma once

#include "plugins/AudioPluginTypes.h"

#include <QByteArray>
#include <QString>
#include <QVariantMap>

#include <vector>

namespace openvegas {

/** Unpack FxSlot.state (QDataStream QVariantMap) or empty. */
QVariantMap unpackFxParams(const QByteArray &state);
QByteArray packFxParams(const QVariantMap &params);

/** Store/load opaque plugin blob under FxSlot.state via params key "chunk". */
inline void setFxStateChunk(FxSlot *slot, const QByteArray &chunk)
{
    if (!slot) {
        return;
    }
    QVariantMap m = unpackFxParams(slot->state);
    if (chunk.isEmpty()) {
        m.remove(QStringLiteral("chunk"));
    } else {
        m.insert(QStringLiteral("chunk"), chunk);
    }
    slot->state = packFxParams(m);
}
inline QByteArray fxStateChunk(const FxSlot &slot)
{
    return unpackFxParams(slot.state).value(QStringLiteral("chunk")).toByteArray();
}

/** Per-instance realtime state for builtin processors. */
struct BuiltinDspState {
    QString pluginId;
    double sampleRate = 48000.0;
    // Gate
    float gateEnv = 0.f;
    // Comp
    float compEnv = 0.f;
    // EQ biquad (4 bands × 2 ch)
    struct Biquad {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1[2] = {0, 0};
        float z2[2] = {0, 0};
        void process(float *L, float *R, int n);
        void setPeaking(double sr, double freq, double gainDb, double q);
        void setShelf(double sr, double freq, double gainDb, bool highShelf);
    };
    Biquad eq[4];
    bool eqReady = false;
    // Chorus
    std::vector<float> chorusDelayL;
    std::vector<float> chorusDelayR;
    int chorusWrite = 0;
    float chorusPhase = 0.f;
    // Delay (up to ~2 s)
    std::vector<float> delayL;
    std::vector<float> delayR;
    int delayWrite = 0;
    // Simple Schroeder-ish reverb
    static constexpr int kReverbCombs = 4;
    static constexpr int kReverbAllpass = 2;
    std::vector<float> revCombL[kReverbCombs];
    std::vector<float> revCombR[kReverbCombs];
    int revCombIdx[kReverbCombs] = {};
    float revCombFilterL[kReverbCombs] = {};
    float revCombFilterR[kReverbCombs] = {};
    std::vector<float> revApL[kReverbAllpass];
    std::vector<float> revApR[kReverbAllpass];
    int revApIdx[kReverbAllpass] = {};

    void prepare(double sr);
    void reset();
};

/** Process one builtin slot in-place (stereo interleaved or separate). */
void processBuiltinFx(FxSlot *slot, BuiltinDspState *st, float *left, float *right, int frames);

bool isBuiltinGate(const QString &nameOrId);
bool isBuiltinEq(const QString &nameOrId);
bool isBuiltinComp(const QString &nameOrId);
bool isBuiltinChorus(const QString &nameOrId);
bool isBuiltinDelay(const QString &nameOrId);
bool isBuiltinReverb(const QString &nameOrId);

} // namespace openvegas
