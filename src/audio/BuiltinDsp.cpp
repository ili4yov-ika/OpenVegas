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
    const int maxDelay = int(sr * 0.05) + 8;
    chorusDelayL.assign(size_t(maxDelay), 0.f);
    chorusDelayR.assign(size_t(maxDelay), 0.f);
    chorusWrite = 0;
    chorusPhase = 0.f;
    gateEnv = 0.f;
    compEnv = 0.f;
    eqReady = false;
}

void BuiltinDspState::reset()
{
    gateEnv = 0.f;
    compEnv = 0.f;
    for (auto &b : eq) {
        b.z1[0] = b.z1[1] = b.z2[0] = b.z2[1] = 0.f;
    }
    std::fill(chorusDelayL.begin(), chorusDelayL.end(), 0.f);
    std::fill(chorusDelayR.begin(), chorusDelayR.end(), 0.f);
    chorusWrite = 0;
    chorusPhase = 0.f;
}

void processGate(BuiltinDspState *st, const QVariantMap &p, float *L, float *R, int n)
{
    const float thr = dbToLinear(p.value(QStringLiteral("thresholdDb"), -60.0).toDouble());
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
    const float thr = dbToLinear(p.value(QStringLiteral("threshold"), 0.0).toDouble());
    const float amount = float(std::clamp(p.value(QStringLiteral("amount"), 1.0).toDouble(), 0.0, 10.0));
    const float ratio = 1.f + amount; // soft knee-ish
    const float inG = dbToLinear(p.value(QStringLiteral("inputGain"), 0.0).toDouble());
    const float outG = dbToLinear(p.value(QStringLiteral("outputGain"), 0.0).toDouble());
    const float atk = float(std::exp(-1.0 / (st->sampleRate * std::max(0.001,
        p.value(QStringLiteral("attackMs"), 15.0).toDouble() / 1000.0))));
    const float rel = float(std::exp(-1.0 / (st->sampleRate * std::max(0.001,
        p.value(QStringLiteral("releaseMs"), 250.0).toDouble() / 1000.0))));
    for (int i = 0; i < n; ++i) {
        float xL = L[i] * inG;
        float xR = R[i] * inG;
        const float level = std::max(std::abs(xL), std::abs(xR));
        float target = 1.f;
        if (level > thr && thr > 1e-8f) {
            const float over = level / thr;
            target = std::pow(over, 1.f / ratio - 1.f);
        }
        if (target < st->compEnv) {
            st->compEnv = atk * st->compEnv + (1.f - atk) * target;
        } else {
            st->compEnv = rel * st->compEnv + (1.f - rel) * target;
        }
        L[i] = xL * st->compEnv * outG;
        R[i] = xR * st->compEnv * outG;
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
