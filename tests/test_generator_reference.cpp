// VEGAS's own OFX generators, run through our host, as the reference for ours.
//
// Not part of the regular run: it needs SAMPLES/VEGAS-PRO-22-PROGRAM-FILES (or an
// installed VEGAS) and writes files. Tagged [.reference] so Catch2 skips it unless asked:
//
//   OPENVGAS_GENERATOR_REF=<dir> openvegas_media_tests "[generator-ref]"
//
// For each of Checkerboard, Noise Texture, Solid Color and Test Pattern it writes
// <dir>/<generator>.json — every parameter the plug-in declares in the Generator context,
// in its own order, with type, label, group, default, ranges and choices — and
// <dir>/<generator>/<preset>.png, every stock preset rendered by the plug-in itself.
// tools/gen_generator_presets.py turns the JSON into src/video/GeneratorParamSchema.cpp.

#include "TestQtApp.h"

#include "io/SamplePaths.h"
#include "plugins/OfxHost.h"
#include "plugins/VegasVideoPluginCatalog.h"
#include "video/GeneratorPresetData.h"
#include "video/VegasGenerators.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>

using namespace openvegas;

namespace {

QJsonValue variantToJson(const QVariant &v)
{
    return QJsonValue::fromVariant(v);
}

/** Mean absolute difference of two frames over R, G, B and A, 0..255, premultiplied. */
double meanDiff(const QImage &a, const QImage &b)
{
    const QImage x = a.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const QImage y = b.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (x.size() != y.size()) {
        return 255.0;
    }
    double sum = 0.0;
    for (int r = 0; r < x.height(); ++r) {
        const auto *p = reinterpret_cast<const QRgb *>(x.constScanLine(r));
        const auto *q = reinterpret_cast<const QRgb *>(y.constScanLine(r));
        for (int c = 0; c < x.width(); ++c) {
            sum += std::abs(qRed(p[c]) - qRed(q[c])) + std::abs(qGreen(p[c]) - qGreen(q[c]))
                   + std::abs(qBlue(p[c]) - qBlue(q[c])) + std::abs(qAlpha(p[c]) - qAlpha(q[c]));
        }
    }
    return sum / (4.0 * x.width() * x.height());
}

} // namespace

TEST_CASE("VEGAS's OFX generators, described and rendered by the plug-ins themselves",
          "[.reference][generator-ref]")
{
    openvegas::test::ensureQtApp();
    const QString outDir = QString::fromLocal8Bit(qgetenv("OPENVGAS_GENERATOR_REF"));
    if (outDir.isEmpty()) {
        SKIP("OPENVGAS_GENERATOR_REF not set");
    }
    const QString root = QDir(SamplePaths::samplesDir())
                             .absoluteFilePath(QStringLiteral("VEGAS-PRO-22-PROGRAM-FILES"));
    if (root.isEmpty() || !QDir(root).exists()) {
        SKIP("SAMPLES/VEGAS-PRO-22-PROGRAM-FILES not available");
    }
    VegasVideoPluginCatalog::invalidateCache();
    VegasVideoPluginCatalog::discover({root});
    QDir().mkpath(outDir);

    const int w = int(qEnvironmentVariableIntValue("OPENVGAS_GENERATOR_REF_W"));
    const QSize size(w > 0 ? w : 320, w > 0 ? w * 9 / 16 : 180);

    for (const StockGeneratorGroup &group : stockGeneratorPresets()) {
        if (!group.pluginId.startsWith(QStringLiteral("{Svfx:"))) {
            continue; // the legacy DirectX generators are not OFX
        }
        const QString effectId = group.pluginId.mid(6, group.pluginId.size() - 7);
        const VegasVideoPluginEntry *entry = VegasVideoPluginCatalog::findByEffectId(effectId);
        INFO(effectId.toStdString());
        REQUIRE(entry != nullptr);
        REQUIRE(entry->hasBinary);

        // The schema: what the plug-in declares in the Generator context.
        QJsonArray params;
        for (const OfxContextReport &rep :
             OfxHost::describeContexts(entry->binaryPath, entry->pluginIndex)) {
            if (rep.context != QLatin1String("OfxImageEffectContextGenerator") || !rep.accepted) {
                continue;
            }
            for (const OfxParamDescriptor &d : rep.descriptors) {
                QJsonObject o;
                o.insert(QStringLiteral("name"), d.name);
                o.insert(QStringLiteral("type"), d.type);
                o.insert(QStringLiteral("label"), d.label);
                o.insert(QStringLiteral("parent"), d.parent);
                o.insert(QStringLiteral("hint"), d.hint);
                QJsonArray def;
                for (const QVariant &v : d.defaultValue) def.append(variantToJson(v));
                o.insert(QStringLiteral("default"), def);
                if (d.hasRange) {
                    o.insert(QStringLiteral("min"), d.min);
                    o.insert(QStringLiteral("max"), d.max);
                }
                if (d.hasDisplayRange) {
                    o.insert(QStringLiteral("displayMin"), d.displayMin);
                    o.insert(QStringLiteral("displayMax"), d.displayMax);
                }
                o.insert(QStringLiteral("choices"), QJsonArray::fromStringList(d.choices));
                o.insert(QStringLiteral("animates"), d.animates);
                params.append(o);
            }
        }
        CHECK_FALSE(params.isEmpty());
        QFile json(QDir(outDir).filePath(group.name + QStringLiteral(".json")));
        REQUIRE(json.open(QIODevice::WriteOnly));
        json.write(QJsonDocument(QJsonObject{{QStringLiteral("pluginId"), group.pluginId},
                                             {QStringLiteral("params"), params}})
                       .toJson());
        json.close();

        // The renders: every stock preset, as the plug-in draws it.
        OfxPluginDesc desc;
        desc.path = entry->binaryPath;
        desc.bundlePath = entry->bundlePath;
        desc.effectId = entry->effectId;
        desc.pluginIndex = entry->pluginIndex;
        desc.hasBinary = true;
        QString err;
        const int id = OfxHost::instance().createInstance(desc, &err);
        INFO(err.toStdString());
        REQUIRE(id > 0);
        const QString dir = QDir(outDir).filePath(group.name);
        QDir().mkpath(dir);
        int rendered = 0;
        for (const StockGeneratorPreset &preset : group.presets) {
            QImage frame(size, QImage::Format_ARGB32);
            frame.fill(Qt::transparent);
            if (OfxHost::instance().processFrame(id, &frame, 0.0, preset.params, &err)) {
                QString file = preset.name;
                file.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9 ()_-]")),
                             QStringLiteral("_"));
                frame.save(QDir(dir).filePath(file + QStringLiteral(".png")));
                ++rendered;
                // Ours beside it, and how far apart the two are.
                const QImage ours = renderVegasGenerator(group.pluginId, preset.params, size);
                ours.save(QDir(dir).filePath(QStringLiteral("ours_") + file + QStringLiteral(".png")));
                const double diff = meanDiff(ours, frame);
                std::printf("%-14s %-28s mean %6.2f\n", group.name.toUtf8().constData(),
                            preset.name.toUtf8().constData(), diff);
            } else {
                WARN(QStringLiteral("%1 / %2: %3").arg(group.name, preset.name, err)
                         .toStdString());
            }
        }
        CHECK(rendered == group.presets.size());
    }
}

TEST_CASE("VEGAS's legacy generators as we draw them, for comparing with VEGAS's thumbnails",
          "[.reference][generator-ref]")
{
    openvegas::test::ensureQtApp();
    const QString outDir = QString::fromLocal8Bit(qgetenv("OPENVGAS_GENERATOR_REF"));
    if (outDir.isEmpty()) {
        SKIP("OPENVGAS_GENERATOR_REF not set");
    }
    for (const StockGeneratorGroup &group : stockGeneratorPresets()) {
        if (group.pluginId.startsWith(QStringLiteral("{Svfx:"))) {
            continue;
        }
        const QString dir = QDir(outDir).filePath(group.name);
        QDir().mkpath(dir);
        const bool credits = group.name == QLatin1String("Credit Roll");
        for (const StockGeneratorPreset &preset : group.presets) {
            const QImage ours = credits ? renderCreditRollStill(preset.params, QSize(320, 180))
                                        : renderVegasGenerator(group.pluginId, preset.params,
                                                               QSize(320, 180));
            REQUIRE_FALSE(ours.isNull());
            QString file = preset.name;
            file.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9 ()_-]")), QStringLiteral("_"));
            ours.save(QDir(dir).filePath(QStringLiteral("ours_") + file + QStringLiteral(".png")));
        }
    }
}
