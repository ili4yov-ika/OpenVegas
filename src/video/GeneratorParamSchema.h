#pragma once

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>

namespace openvegas {

/**
 * One parameter of a VEGAS OFX generator as the plug-in itself declares it: what the
 * Video Media Generators window needs to lay out its control. Generated from the plug-ins
 * (tools/gen_generator_presets.py --schema), so the window has it with no VEGAS installed.
 */
struct GeneratorParamDef {
    QString name;
    /** OFX param type without the prefix: Double, Boolean, Choice, RGBA, Group, Integer… */
    QString type;
    QString label;
    /** The Group parameter this one sits in, or empty for the top level. */
    QString parent;
    /** One entry per component (four for RGBA). */
    QVariantList defaultValue;
    /** The slider's span (the display range where declared, else the hard range). */
    double min = 0.0;
    double max = 1.0;
    /** A Choice parameter's options, in order. */
    QStringList choices;
};

struct GeneratorParamSchema {
    QString pluginId; ///< "{Svfx:com.vegascreativesoftware:…}"
    QVector<GeneratorParamDef> params;
};

/** Every OFX generator's schema, in browser order. */
const QVector<GeneratorParamSchema> &generatorParamSchemas();

/** The schema for one generator, or nullptr (legacy generators and Titles & Text have none). */
const QVector<GeneratorParamDef> *generatorParamSchema(const QString &pluginId);

} // namespace openvegas
