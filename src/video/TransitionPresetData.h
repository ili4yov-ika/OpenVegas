#pragma once

#include <QString>
#include <QVector>

namespace openvegas {

/** One stock preset: its name and the parameter values VEGAS ships for it. */
struct StockTransitionPreset {
    QString name;
    QVector<QPair<QString, double>> params;
};

/** All stock presets of one transition, keyed by the tail of its OFX identifier. */
struct StockTransitionGroup {
    QString key; ///< "zoom" in "com.vegascreativesoftware:zoom"
    QVector<StockTransitionPreset> presets;
};

/**
 * Every stock transition preset VEGAS ships — 215 of them across 24 transitions.
 *
 * Generated from VEGAS's own `PresetPackage.xml` (inside the Vfx1 OFX bundle) by
 * tools/gen_transition_presets.py, so the names and numbers are the shipped ones rather
 * than transcribed from screenshots or recovered from whichever presets a sample project
 * happened to use.
 */
const QVector<StockTransitionGroup> &stockTransitionPresets();

/** Presets for one transition key, or empty when it has none. */
const QVector<StockTransitionPreset> *stockPresetsFor(const QString &key);

} // namespace openvegas
