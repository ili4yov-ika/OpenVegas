#include "plugins/FxParamCurves.h"

#include "audio/BuiltinDsp.h"

#include <QVariantList>

#include <algorithm>

namespace openvegas {

double fxCurveValueAt(const QVariantList &flatCurve, double timeSec)
{
    const int pairs = flatCurve.size() / 2;
    if (pairs == 0) {
        return 0.0;
    }
    // Held flat outside the ends rather than extrapolated: VEGAS shows the first key's
    // value before the first key and the last one's after the last, and a curve that kept
    // rising past its final key would take a parameter somewhere it was never set to.
    if (timeSec <= flatCurve[0].toDouble()) {
        return flatCurve[1].toDouble();
    }
    if (timeSec >= flatCurve[(pairs - 1) * 2].toDouble()) {
        return flatCurve[(pairs - 1) * 2 + 1].toDouble();
    }
    for (int i = 1; i < pairs; ++i) {
        const double t1 = flatCurve[i * 2].toDouble();
        if (timeSec > t1) {
            continue;
        }
        const double t0 = flatCurve[(i - 1) * 2].toDouble();
        const double v0 = flatCurve[(i - 1) * 2 + 1].toDouble();
        const double v1 = flatCurve[i * 2 + 1].toDouble();
        if (t1 <= t0) {
            return v1;
        }
        // Straight lines between keys. The project also stores Bezier handles, but every
        // one seen so far sits a tenth of a millisecond either side of its key with the
        // key's own value — which is a straight line.
        const double u = (timeSec - t0) / (t1 - t0);
        return v0 + (v1 - v0) * u;
    }
    return flatCurve[(pairs - 1) * 2 + 1].toDouble();
}

QVariantMap fxParamsAtTime(const QVariantMap &state, double timeSec)
{
    const QVariantMap curves = state.value(fxParamCurvesStateKey()).toMap();
    QVariantMap out;
    for (auto it = state.constBegin(); it != state.constEnd(); ++it) {
        if (it.key().startsWith(QLatin1String("__"))) {
            continue; // private: the curves themselves, the preset name
        }
        const auto curve = curves.constFind(it.key());
        if (curve != curves.constEnd()) {
            out.insert(it.key(), fxCurveValueAt(curve.value().toList(), timeSec));
        } else {
            out.insert(it.key(), it.value());
        }
    }
    return out;
}

FxSlot fxSlotAtTime(const FxSlot &slot, double timeSec)
{
    if (slot.state.isEmpty()) {
        return slot;
    }
    QVariantMap state = unpackFxParams(slot.state);
    const QVariantMap curves = state.value(fxParamCurvesStateKey()).toMap();
    if (curves.isEmpty()) {
        return slot;
    }
    for (auto it = curves.constBegin(); it != curves.constEnd(); ++it) {
        state.insert(it.key(), fxCurveValueAt(it.value().toList(), timeSec));
    }
    FxSlot out = slot;
    out.state = packFxParams(state);
    return out;
}

} // namespace openvegas
