#pragma once

#include <QByteArray>
#include <QString>
#include <QVariant>
#include <QVariantMap>
#include <QVector>

namespace openvegas {

/** One animated moment of an OFX parameter, as VEGAS stores it. */
struct VegOfxKeyframe {
    /** Seconds from the start of the event. */
    double timeSec = 0.0;
    double value = 0.0;
    /** Bezier handle before the key, in the same units as `timeSec` and `value`. */
    double inTimeSec = 0.0;
    double inValue = 0.0;
    /** Bezier handle after the key. */
    double outTimeSec = 0.0;
    double outValue = 0.0;
    /** Interpolation code; 1 everywhere seen so far. */
    quint32 flags = 0;
};

/** One OFX parameter recovered from a project. */
struct VegOfxParam {
    QString name;
    /**
     * The value VEGAS was showing when it saved: a number, or a list for a parameter with
     * more than one component — a point, a colour.
     *
     * Not a default and not necessarily the first keyframe: on an animated parameter it is
     * whatever the curve read at the playhead. It is the right thing to hand a plug-in
     * being rendered at a single time.
     */
    QVariant value;
    QVector<VegOfxKeyframe> keys;

    /** First component as a number, for the many parameters that have only one. */
    double scalar() const;
};

/** An OFX effect's stored state: which plug-in, which preset, and its parameters. */
struct VegOfxEffect {
    /** The full identifier as stored, e.g. "{Svfx:com.vegascreativesoftware:chromablur}". */
    QString pluginId;
    QString presetName;
    QVector<VegOfxParam> params;

    bool isValid() const { return !pluginId.isEmpty(); }
};

/**
 * Decode the parameter block a `.veg` stores after an OFX identifier.
 *
 * One reading for both places this record appears — a transition on a fade and an effect on
 * an event. They are the same structure; the only difference is that transitions have never
 * been seen carrying keyframes.
 *
 * @param data   the whole project file.
 * @param idPos  byte offset of the first character of the UTF-16 `{Svfx:…}` identifier —
 *               which is what a search for that marker already gives.
 *
 * The record checks itself. Each parameter declares the size of its value block, and that
 * size has to equal the value's own width plus the keyframes the count implies — two
 * numbers written independently. When they disagree this is not the record it looks like,
 * and nothing is returned rather than a plausible-looking guess: numbers taken from the
 * middle of some other structure reach a plug-in and render as something that looks
 * deliberate.
 *
 * @return false when there is no decodable record at `idPos`.
 */
bool vegOfxDecodeEffect(const QByteArray &data, int idPos, VegOfxEffect *out);

/**
 * Parameter values in the shape `FxSlot::state` carries — name to value.
 *
 * Only the value at save time; the animation travels separately, in `vegOfxCurveMap()`,
 * because a parameter that is not animated should cost nothing per frame.
 */
QVariantMap vegOfxParamMap(const VegOfxEffect &effect);

/**
 * Animation curves, as `{parameter: [t0, v0, t1, v1, …]}` with times in seconds.
 *
 * Only parameters that actually move: VEGAS writes every parameter into every keyframe, so
 * a curve of one repeated value is not animation and would only add work per frame.
 */
QVariantMap vegOfxCurveMap(const VegOfxEffect &effect);

} // namespace openvegas
