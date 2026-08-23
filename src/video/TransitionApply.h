#pragma once

#include "plugins/AudioPluginTypes.h"

#include <QImage>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

namespace openvegas {

/** Stable ids of the transition groups with a real renderer. */
inline QString transition3dBlindsId()
{
    return QStringLiteral("builtin:Transition:3DBlinds");
}

/**
 * VEGAS Venetian Blinds. Its parameters were recovered from a real project: the record
 * stores three doubles, and the shipped preset names state their own values — "Seven
 * Horizontal Blinds" is 7 blinds at 90 degrees — which is what identified the fields.
 * See MARKDOWN/VEG_TRANSITIONS_REVERSE.md.
 */
inline QString transitionVenetianBlindsId()
{
    return QStringLiteral("builtin:Transition:VenetianBlinds");
}

// The OFX-hosted groups VEGAS ships. Their parameters and preset values were read out of
// a real project — every number below appears in project_transitions_othersmores.veg.
inline QString transitionLinearWipeId()
{
    return QStringLiteral("builtin:Transition:LinearWipe");
}
inline QString transitionBarnDoorId()
{
    return QStringLiteral("builtin:Transition:BarnDoor");
}
inline QString transitionIrisId()
{
    return QStringLiteral("builtin:Transition:Iris");
}
inline QString transitionClockWipeId()
{
    return QStringLiteral("builtin:Transition:ClockWipe");
}
inline QString transitionZoomId()
{
    return QStringLiteral("builtin:Transition:Zoom");
}

// Recognised but not drawn yet. They exist in the catalog so an imported project shows
// the right name and preset list instead of borrowing another group's identity; the
// renderer falls through to a cross-fade for them.
inline QString transitionCascade3dId()
{
    return QStringLiteral("builtin:Transition:3DCascade");
}
inline QString transitionShuffle3dId()
{
    return QStringLiteral("builtin:Transition:3DShuffle");
}
inline QString transitionFlyInOut3dId()
{
    return QStringLiteral("builtin:Transition:3DFlyInOut");
}
inline QString transitionGradientWipeId()
{
    return QStringLiteral("builtin:Transition:GradientWipe");
}
inline QString transitionPortalsId()
{
    return QStringLiteral("builtin:Transition:Portals");
}

/**
 * Catalog id for a transition VEGAS stores as "{Svfx:…:key}". `key` is the tail of that
 * identifier ("iris", "push"), so a project and the catalog agree without either side
 * carrying the full string around.
 */
inline QString transitionOfxId(const QString &key)
{
    return QStringLiteral("builtin:Transition:ofx:") + key;
}

/** Catalog id for a "{Svfx:…}" identifier out of a project; empty when unknown. */
QString transitionIdForOfxPlugin(const QString &svfxId);

/** One tweakable parameter of a transition group (a row in its properties window). */
struct TransitionParamInfo {
    QString key;
    QString label;
    double minValue = 0.0;
    double maxValue = 1.0;
    /** Slider/spin precision; 0 renders as an integer row (Divisions, Extra spins). */
    int decimals = 4;
    /** Non-empty turns the row into a combo box (Direction) whose value is the index. */
    QStringList choices;
};

/** A named preset = the parameter values Vegas ships for it. */
struct TransitionPresetInfo {
    QString name;
    QVariantMap params;
};

/** A transition group as listed in the Transitions dock (e.g. "3D Blinds"). */
struct TransitionPluginInfo {
    QString id;
    QString name;
    QString format; // "DXT, 32-bit floating point" — matches the dock's status line
    QString description;
    QVector<TransitionParamInfo> params;
    QVector<TransitionPresetInfo> presets;
};

/** Everything the Transitions dock can offer; currently 3D Blinds only. */
const QVector<TransitionPluginInfo> &transitionCatalog();
const TransitionPluginInfo *transitionPluginById(const QString &pluginId);
const TransitionPresetInfo *transitionPreset(const QString &pluginId, const QString &presetName);

/**
 * A transition placed on a clip's fade (or on the crossfade between two clips).
 * `params` holds only the group's own parameter keys; anything missing falls back to the
 * catalog default, so an older project that predates a new parameter still loads.
 */
struct TransitionInstance {
    QString pluginId;
    QString presetName;
    QVariantMap params;

    bool isValid() const { return !pluginId.isEmpty(); }
    bool operator==(const TransitionInstance &o) const
    {
        return pluginId == o.pluginId && presetName == o.presetName && params == o.params;
    }
    bool operator!=(const TransitionInstance &o) const { return !(*this == o); }
};

/** Instance carrying `presetName`'s values, or an invalid one for an unknown id. */
TransitionInstance makeTransitionInstance(const QString &pluginId, const QString &presetName);
/** Parameter value with the catalog default as fallback. */
double transitionParamValue(const TransitionInstance &t, const QString &key);
void transitionSetParamValue(TransitionInstance *t, const QString &key, double value);

QVariantMap transitionToMap(const TransitionInstance &t);
TransitionInstance transitionFromMap(const QVariantMap &m);

/**
 * Blend `from` into `to` at `progress` (0 = fully `from`, 1 = fully `to`) using the
 * transition's own look. Falls back to a plain cross-dissolve for an unknown plugin id,
 * so an unsupported transition still produces a sane picture instead of nothing.
 *
 * `from` may be null (a fade-in from black/transparent); `to` may be null (fade-out).
 */
QImage renderTransition(const QImage &from, const QImage &to, double progress,
                        const TransitionInstance &t);

/**
 * Poster/demo frame for a preset tile: renders the transition between two synthetic
 * "A" and "B" test cards over a transparency checkerboard, the way Vegas's own preset
 * thumbnails do.
 */
QImage renderTransitionPreview(const TransitionInstance &t, const QSize &size, double progress);

} // namespace openvegas
