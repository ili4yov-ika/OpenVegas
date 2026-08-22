#include "audio/BuiltinDsp.h"

#include "audio/AudioUtil.h"

#include <QDataStream>
#include <QIODevice>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace openvegas {

QVariantMap unpackFxParams(const QByteArray &state)
{
    if (state.isEmpty()) {
        return {};
    }
    QDataStream in(state);
    in.setVersion(QDataStream::Qt_6_0);
    QVariantMap m;
    in >> m;
    if (in.status() != QDataStream::Ok) {
        return {};
    }
    return m;
}

QByteArray packFxParams(const QVariantMap &params)
{
    QByteArray ba;
    QDataStream out(&ba, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_0);
    out << params;
    return ba;
}

bool nameHas(const QString &s, const char *needle)
{
    return s.contains(QLatin1String(needle), Qt::CaseInsensitive);
}

bool isBuiltinGate(const QString &n)
{
    return nameHas(n, "Noise Gate") || nameHas(n, "Gate");
}
bool isBuiltinEq(const QString &n)
{
    return nameHas(n, "Track EQ") || (nameHas(n, "EQ") && !nameHas(n, "Equalizer FX"));
}
bool isBuiltinComp(const QString &n)
{
    return nameHas(n, "Compressor");
}
bool isBuiltinChorus(const QString &n)
{
    return nameHas(n, "Chorus");
}
bool isBuiltinDelay(const QString &n)
{
    return nameHas(n, "Delay") && !nameHas(n, "Chorus");
}
bool isBuiltinReverb(const QString &n)
{
    return nameHas(n, "Reverb");
}

void BuiltinDspState::Biquad::process(float *L, float *R, int n)
{
    for (int i = 0; i < n; ++i) {
        for (int ch = 0; ch < 2; ++ch) {
            float &x = (ch == 0) ? L[i] : R[i];
            const float y = b0 * x + z1[ch];
            z1[ch] = b1 * x - a1 * y + z2[ch];
            z2[ch] = b2 * x - a2 * y;
            x = y;
        }
    }
}

void BuiltinDspState::Biquad::setPeaking(double sr, double freq, double gainDb, double q)
{
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * 3.14159265358979323846 * freq / sr;
    const double alpha = std::sin(w0) / (2.0 * std::max(0.1, q));
    const double cosw = std::cos(w0);
    const double b0n = 1.0 + alpha * A;
    const double b1n = -2.0 * cosw;
    const double b2n = 1.0 - alpha * A;
    const double a0 = 1.0 + alpha / A;
    const double a1n = -2.0 * cosw;
    const double a2n = 1.0 - alpha / A;
    b0 = float(b0n / a0);
    b1 = float(b1n / a0);
    b2 = float(b2n / a0);
    a1 = float(a1n / a0);
    a2 = float(a2n / a0);
}

void BuiltinDspState::Biquad::setShelf(double sr, double freq, double gainDb, bool highShelf)
{
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * 3.14159265358979323846 * freq / sr;
    const double cosw = std::cos(w0);
    const double sinw = std::sin(w0);
    const double S = 1.0;
    const double beta = std::sqrt(A) / S;
    double b0n, b1n, b2n, a0, a1n, a2n;
    if (!highShelf) {
        b0n = A * ((A + 1) - (A - 1) * cosw + beta * sinw);
        b1n = 2 * A * ((A - 1) - (A + 1) * cosw);
        b2n = A * ((A + 1) - (A - 1) * cosw - beta * sinw);
        a0 = (A + 1) + (A - 1) * cosw + beta * sinw;
        a1n = -2 * ((A - 1) + (A + 1) * cosw);
        a2n = (A + 1) + (A - 1) * cosw - beta * sinw;
    } else {
        b0n = A * ((A + 1) + (A - 1) * cosw + beta * sinw);
        b1n = -2 * A * ((A - 1) + (A + 1) * cosw);
        b2n = A * ((A + 1) + (A - 1) * cosw - beta * sinw);
        a0 = (A + 1) - (A - 1) * cosw + beta * sinw;
        a1n = 2 * ((A - 1) - (A + 1) * cosw);
        a2n = (A + 1) - (A - 1) * cosw - beta * sinw;
    }
    b0 = float(b0n / a0);
    b1 = float(b1n / a0);
    b2 = float(b2n / a0);
    a1 = float(a1n / a0);
    a2 = float(a2n / a0);
}

void BuiltinDspState::prepare(double sr)
{
    sampleRate = sr;
    const int maxChorus = int(sr * 0.05) + 8;
    chorusDelayL.assign(size_t(maxChorus), 0.f);
    chorusDelayR.assign(size_t(maxChorus), 0.f);
    chorusWrite = 0;
    chorusPhase = 0.f;
    const int maxDelay = int(sr * 2.0) + 8;
    delayL.assign(size_t(maxDelay), 0.f);
    delayR.assign(size_t(maxDelay), 0.f);
    delayWrite = 0;
    // Tunings ~ Freeverb-ish (samples @ 44.1k), scaled to SR
    static const int combTunings[kReverbCombs] = {1116, 1188, 1277, 1356};
    static const int apTunings[kReverbAllpass] = {556, 441};
    const double scale = sr / 44100.0;
    for (int i = 0; i < kReverbCombs; ++i) {
        const int n = std::max(16, int(combTunings[i] * scale));
        revCombL[i].assign(size_t(n), 0.f);
        revCombR[i].assign(size_t(n), 0.f);
        revCombIdx[i] = 0;
        revCombFilterL[i] = 0.f;
        revCombFilterR[i] = 0.f;
    }
    for (int i = 0; i < kReverbAllpass; ++i) {
        const int n = std::max(16, int(apTunings[i] * scale));
        revApL[i].assign(size_t(n), 0.f);
        revApR[i].assign(size_t(n), 0.f);
        revApIdx[i] = 0;
    }
    gateEnv = 1.f;
    compEnv = 1.f;
    eqReady = false;
}

void BuiltinDspState::reset()
{
    gateEnv = 1.f;
    compEnv = 1.f;
    for (auto &b : eq) {
        b.z1[0] = b.z1[1] = b.z2[0] = b.z2[1] = 0.f;
    }
    std::fill(chorusDelayL.begin(), chorusDelayL.end(), 0.f);
    std::fill(chorusDelayR.begin(), chorusDelayR.end(), 0.f);
    chorusWrite = 0;
    chorusPhase = 0.f;
    std::fill(delayL.begin(), delayL.end(), 0.f);
    std::fill(delayR.begin(), delayR.end(), 0.f);
    delayWrite = 0;
    for (int i = 0; i < kReverbCombs; ++i) {
        std::fill(revCombL[i].begin(), revCombL[i].end(), 0.f);
        std::fill(revCombR[i].begin(), revCombR[i].end(), 0.f);
        revCombIdx[i] = 0;
        revCombFilterL[i] = 0.f;
        revCombFilterR[i] = 0.f;
    }
    for (int i = 0; i < kReverbAllpass; ++i) {
        std::fill(revApL[i].begin(), revApL[i].end(), 0.f);
        std::fill(revApR[i].begin(), revApR[i].end(), 0.f);
        revApIdx[i] = 0;
    }
}

void processGate(BuiltinDspState *st, const QVariantMap &p, float *L, float *R, int n)
{
    const double thresholdDb = p.value(QStringLiteral("thresholdDb"), -60.0).toDouble();
    // The fader's bottom position reads "-Inf", and in VEGAS that means the gate does
    // nothing at all — its Track Noise Gate is bit-transparent at defaults. Ours treated
    // the same position as a literal -60 dB and still muted anything below it, which ate
    // the tail of a fade-out.
    if (thresholdDb <= -59.5) {
        return;
    }
    const float thr = dbToLinear(thresholdDb);
    const float atk = float(std::exp(-1.0 / (st->sampleRate * std::max(0.001,
        p.value(QStringLiteral("attackMs"), 3.0).toDouble() / 1000.0))));
    const float rel = float(std::exp(-1.0 / (st->sampleRate * std::max(0.001,
        p.value(QStringLiteral("releaseMs"), 100.0).toDouble() / 1000.0))));
    for (int i = 0; i < n; ++i) {
        const float level = std::max(std::abs(L[i]), std::abs(R[i]));
        const float target = (level >= thr) ? 1.f : 0.f;
        if (target > st->gateEnv) {
            st->gateEnv = atk * st->gateEnv + (1.f - atk) * target;
        } else {
            st->gateEnv = rel * st->gateEnv + (1.f - rel) * target;
        }
        L[i] *= st->gateEnv;
        R[i] *= st->gateEnv;
    }
}

void processEq(BuiltinDspState *st, const QVariantMap &p, float *L, float *R, int n)
{
    if (!st->eqReady) {
        for (int b = 0; b < 4; ++b) {
            const QString pref = QStringLiteral("band%1.").arg(b);
            if (!p.value(pref + QStringLiteral("enabled"), true).toBool()) {
                st->eq[b] = BuiltinDspState::Biquad{};
                continue;
            }
            const int type = p.value(pref + QStringLiteral("type"), 1).toInt();
            const double freq = p.value(pref + QStringLiteral("freq"), 1000.0).toDouble();
            const double gain = p.value(pref + QStringLiteral("gain"), 0.0).toDouble();
            const double rolloff = p.value(pref + QStringLiteral("rolloff"), 12.0).toDouble();
            const double q = std::max(0.5, rolloff / 12.0);
            if (type == 0) {
                st->eq[b].setShelf(st->sampleRate, freq, gain, false);
            } else if (type == 2) {
                st->eq[b].setShelf(st->sampleRate, freq, gain, true);
            } else {
                st->eq[b].setPeaking(st->sampleRate, freq, gain, q);
            }
        }
        st->eqReady = true;
    }
    for (int b = 0; b < 4; ++b) {
        const QString pref = QStringLiteral("band%1.").arg(b);
        if (!p.value(pref + QStringLiteral("enabled"), true).toBool()) {
            continue;
        }
        if (std::abs(p.value(pref + QStringLiteral("gain"), 0.0).toDouble()) < 1e-6) {
            continue;
        }
        st->eq[b].process(L, R, n);
    }
}

void processComp(BuiltinDspState *st, const QVariantMap &p, float *L, float *R, int n)
{
    const double thresholdDb = p.value(QStringLiteral("threshold"), 0.0).toDouble();
    // "Amount (x:1)" is the ratio itself, exactly as the dialog spells it — 1,0 means
    // 1:1, i.e. no compression. This used to be read as `1 + amount`, so a compressor
    // sitting at its default setting squeezed 2:1 while the dialog claimed it was doing
    // nothing. The clamp follows the dialog's range too (it was 0…10 against a 1…20 UI,
    // which quietly capped the top half of the control).
    const double ratio = std::clamp(p.value(QStringLiteral("amount"), 1.0).toDouble(), 1.0, 20.0);
    const bool autoGain = p.value(QStringLiteral("autoGain"), true).toBool();
    const bool smoothSat = p.value(QStringLiteral("smoothSat"), false).toBool();
    const float inG = dbToLinear(p.value(QStringLiteral("inputGain"), 0.0).toDouble());
    const float outG = dbToLinear(p.value(QStringLiteral("outputGain"), 0.0).toDouble());
    const float atk = float(std::exp(-1.0 / (st->sampleRate * std::max(0.001,
        p.value(QStringLiteral("attackMs"), 15.0).toDouble() / 1000.0))));
    const float rel = float(std::exp(-1.0 / (st->sampleRate * std::max(0.001,
        p.value(QStringLiteral("releaseMs"), 250.0).toDouble() / 1000.0))));

    // Auto gain compensation: cancel the reduction a full-scale signal would take, so
    // raising Amount does not simply make the track quieter. The dialog ships this on,
    // and until now the checkbox was wired to nothing at all.
    const float makeup =
        autoGain ? dbToLinear(-thresholdDb * (1.0 - 1.0 / ratio)) : 1.f;
    // Smooth saturation softens the corner of the gain computer rather than adding any
    // waveshaping — the knee is the part that is audible as harshness on a hard corner.
    const double kneeDb = smoothSat ? 6.0 : 0.0;
    const double slope = 1.0 / ratio - 1.0;

    for (int i = 0; i < n; ++i) {
        float xL = L[i] * inG;
        float xR = R[i] * inG;
        const float level = std::max(std::abs(xL), std::abs(xR));
        float target = 1.f;
        if (level > 1e-8f) {
            const double levelDb = 20.0 * std::log10(double(level));
            const double over = levelDb - thresholdDb;
            double reductionDb = 0.0;
            if (kneeDb > 0.0 && over > -kneeDb / 2.0 && over < kneeDb / 2.0) {
                const double x = over + kneeDb / 2.0;
                reductionDb = slope * x * x / (2.0 * kneeDb);
            } else if (over > 0.0) {
                reductionDb = slope * over;
            }
            target = dbToLinear(reductionDb);
        }
        if (target < st->compEnv) {
            st->compEnv = atk * st->compEnv + (1.f - atk) * target;
        } else {
            st->compEnv = rel * st->compEnv + (1.f - rel) * target;
        }
        L[i] = xL * st->compEnv * makeup * outG;
        R[i] = xR * st->compEnv * makeup * outG;
    }
}

void processChorus(BuiltinDspState *st, const QVariantMap &p, float *L, float *R, int n)
{
    if (st->chorusDelayL.empty()) {
        st->prepare(st->sampleRate);
    }
    const float rate = float(p.value(QStringLiteral("rate"), 0.5).toDouble());
    const float depth = float(p.value(QStringLiteral("depth"), 0.4).toDouble());
    const float mix = float(p.value(QStringLiteral("dryWet"),
                                    p.value(QStringLiteral("wet"), 0.35).toDouble())
                                .toDouble());
    const float baseDelay = 0.012f;
    const int maxD = int(st->chorusDelayL.size());
    constexpr float kPi = 3.14159265358979323846f;
    for (int i = 0; i < n; ++i) {
        st->chorusPhase += rate / float(st->sampleRate);
        if (st->chorusPhase > 1.f) {
            st->chorusPhase -= 1.f;
        }
        const float mod = baseDelay + depth * 0.004f * std::sin(2.f * kPi * st->chorusPhase);
        const float delaySamp = mod * float(st->sampleRate);
        float readPos = float(st->chorusWrite) - delaySamp;
        while (readPos < 0.f) {
            readPos += float(maxD);
        }
        const int i0 = int(readPos) % maxD;
        const int i1 = (i0 + 1) % maxD;
        const float frac = readPos - std::floor(readPos);
        const float dL = st->chorusDelayL[size_t(i0)] * (1.f - frac)
                         + st->chorusDelayL[size_t(i1)] * frac;
        const float dR = st->chorusDelayR[size_t(i0)] * (1.f - frac)
                         + st->chorusDelayR[size_t(i1)] * frac;
        st->chorusDelayL[size_t(st->chorusWrite)] = L[i];
        st->chorusDelayR[size_t(st->chorusWrite)] = R[i];
        st->chorusWrite = (st->chorusWrite + 1) % maxD;
        L[i] = L[i] * (1.f - mix) + dL * mix;
        R[i] = R[i] * (1.f - mix) + dR * mix;
    }
}

void processDelay(BuiltinDspState *st, const QVariantMap &p, float *L, float *R, int n)
{
    if (st->delayL.empty()) {
        st->prepare(st->sampleRate);
    }
    const float delayMs = float(std::clamp(p.value(QStringLiteral("delayMs"), 250.0).toDouble(), 1.0, 2000.0));
    const float feedback = float(std::clamp(p.value(QStringLiteral("feedback"), 0.35).toDouble(), 0.0, 0.95));
    const float mix = float(std::clamp(
        p.value(QStringLiteral("mix"), p.value(QStringLiteral("wet"), 0.4).toDouble()).toDouble(), 0.0,
        1.0));
    const int maxD = int(st->delayL.size());
    const int delaySamp =
        std::clamp(int(delayMs * 0.001f * float(st->sampleRate) + 0.5f), 1, maxD - 1);
    for (int i = 0; i < n; ++i) {
        const int read = (st->delayWrite - delaySamp + maxD) % maxD;
        const float dL = st->delayL[size_t(read)];
        const float dR = st->delayR[size_t(read)];
        st->delayL[size_t(st->delayWrite)] = L[i] + dL * feedback;
        st->delayR[size_t(st->delayWrite)] = R[i] + dR * feedback;
        st->delayWrite = (st->delayWrite + 1) % maxD;
        L[i] = L[i] * (1.f - mix) + dL * mix;
        R[i] = R[i] * (1.f - mix) + dR * mix;
    }
}

void processReverb(BuiltinDspState *st, const QVariantMap &p, float *L, float *R, int n)
{
    if (st->revCombL[0].empty()) {
        st->prepare(st->sampleRate);
    }
    const float room =
        float(std::clamp(p.value(QStringLiteral("roomSize"), 0.55).toDouble(), 0.0, 1.0));
    const float damp =
        float(std::clamp(p.value(QStringLiteral("damp"), 0.45).toDouble(), 0.0, 1.0));
    const float mix = float(std::clamp(
        p.value(QStringLiteral("mix"), p.value(QStringLiteral("wet"), 0.35).toDouble()).toDouble(),
        0.0, 1.0));
    const float feedback = 0.28f + room * 0.60f;
    constexpr float kApFeedback = 0.5f;
    for (int i = 0; i < n; ++i) {
        const float inL = L[i];
        const float inR = R[i];
        float accL = 0.f;
        float accR = 0.f;
        for (int c = 0; c < BuiltinDspState::kReverbCombs; ++c) {
            const int len = int(st->revCombL[c].size());
            float &bufL = st->revCombL[c][size_t(st->revCombIdx[c])];
            float &bufR = st->revCombR[c][size_t(st->revCombIdx[c])];
            const float outL = bufL;
            const float outR = bufR;
            st->revCombFilterL[c] = outL * (1.f - damp) + st->revCombFilterL[c] * damp;
            st->revCombFilterR[c] = outR * (1.f - damp) + st->revCombFilterR[c] * damp;
            bufL = inL + st->revCombFilterL[c] * feedback;
            bufR = inR + st->revCombFilterR[c] * feedback;
            st->revCombIdx[c] = (st->revCombIdx[c] + 1) % len;
            accL += outL;
            accR += outR;
        }
        accL *= 0.25f;
        accR *= 0.25f;
        for (int a = 0; a < BuiltinDspState::kReverbAllpass; ++a) {
            const int len = int(st->revApL[a].size());
            float &bufL = st->revApL[a][size_t(st->revApIdx[a])];
            float &bufR = st->revApR[a][size_t(st->revApIdx[a])];
            const float bufOutL = bufL;
            const float bufOutR = bufR;
            const float yL = -accL + bufOutL;
            const float yR = -accR + bufOutR;
            bufL = accL + bufOutL * kApFeedback;
            bufR = accR + bufOutR * kApFeedback;
            st->revApIdx[a] = (st->revApIdx[a] + 1) % len;
            accL = yL;
            accR = yR;
        }
        L[i] = inL * (1.f - mix) + accL * mix;
        R[i] = inR * (1.f - mix) + accR * mix;
    }
}

void processBuiltinFx(FxSlot *slot, BuiltinDspState *st, float *left, float *right, int frames)
{
    if (!slot || !st || !left || !right || frames <= 0 || slot->bypass) {
        return;
    }
    if (slot->format != PluginFormat::Builtin) {
        return;
    }
    const QVariantMap p = unpackFxParams(slot->state);
    const QString key = slot->displayName.isEmpty() ? slot->pluginId : slot->displayName;
    st->pluginId = slot->pluginId;
    if (isBuiltinGate(key)) {
        processGate(st, p, left, right, frames);
    } else if (isBuiltinEq(key)) {
        // Invalidate coeffs when params change — simple: rebuild each block if gain etc.
        st->eqReady = false;
        processEq(st, p, left, right, frames);
    } else if (isBuiltinComp(key)) {
        processComp(st, p, left, right, frames);
    } else if (isBuiltinChorus(key)) {
        processChorus(st, p, left, right, frames);
    } else if (isBuiltinDelay(key)) {
        processDelay(st, p, left, right, frames);
    } else if (isBuiltinReverb(key)) {
        processReverb(st, p, left, right, frames);
    } else {
        // Generic gain / dry-wet from state if present
        const float g = dbToLinear(p.value(QStringLiteral("gainDb"), 0.0).toDouble());
        if (std::abs(g - 1.f) > 1e-6f) {
            for (int i = 0; i < frames; ++i) {
                left[i] *= g;
                right[i] *= g;
            }
        }
    }
}

} // namespace openvegas
