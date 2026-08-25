#pragma once

#include "plugins/AudioPluginTypes.h"

#include <QString>
#include <QVariantMap>

namespace openvegas {

/**
 * Animated parameter values carried inside an effect's own state.
 *
 * A project can animate any OFX parameter, and the curve has to reach the renderer. It
 * travels in the slot rather than in the event's automation lanes because the chain is
 * applied from several places that never see the event — the preview compositor, a render
 * pass, an FX dialog's own preview — and a curve that only some of them could reach would
 * animate in one window and stand still in the next.
 *
 * The curve is stored as a flat list of alternating time and value, which survives the
 * QVariantMap round-trip a slot's state already makes through the project file.
 */
inline QString fxParamCurvesStateKey()
{
    return QStringLiteral("__paramCurves");
}

/** Value of a flat [t0, v0, t1, v1, …] curve at `timeSec`, held flat outside its ends. */
double fxCurveValueAt(const QVariantList &flatCurve, double timeSec);

/**
 * The parameters an effect should be rendered with at `timeSec`.
 *
 * Values without a curve are returned unchanged; the curve entry itself and other private
 * keys stay out of the result, since they are not the plug-in's parameters.
 */
QVariantMap fxParamsAtTime(const QVariantMap &state, double timeSec);

/**
 * A copy of `slot` whose stored values are the ones its curves give at `timeSec`.
 *
 * Returns the slot untouched when it carries no curves, so the common case costs one
 * lookup and no copying of state.
 */
FxSlot fxSlotAtTime(const FxSlot &slot, double timeSec);

} // namespace openvegas
