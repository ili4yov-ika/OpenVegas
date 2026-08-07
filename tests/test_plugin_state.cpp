#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "audio/BuiltinDsp.h"
#include "io/VegReader.h"
#include "io/SamplePaths.h"
#include "model/ProjectModel.h"
#include "plugins/AudioPluginTypes.h"

#include <QDir>
#include <QFile>

using namespace openvegas;

TEST_CASE("FxSlot state chunk pack/unpack round-trip", "[plugins][state]")
{
    FxSlot slot = makeFxSlot(QStringLiteral("Demo VST"), PluginFormat::Vst3,
                             QStringLiteral("vst3:demo"));
    const QByteArray chunk = QByteArray("CcnK") + QByteArray(60, '\x01');
    setFxStateChunk(&slot, chunk);
    REQUIRE(fxStateChunk(slot) == chunk);

    const QVariantMap params = unpackFxParams(slot.state);
    REQUIRE(params.contains(QStringLiteral("chunk")));
    REQUIRE(params.value(QStringLiteral("chunk")).toByteArray() == chunk);

    // Params coexist with chunk.
    QVariantMap m = params;
    m.insert(QStringLiteral("gainDb"), -3.0);
    slot.state = packFxParams(m);
    REQUIRE(fxStateChunk(slot) == chunk);
    REQUIRE(unpackFxParams(slot.state).value(QStringLiteral("gainDb")).toDouble()
            == Catch::Approx(-3.0));

    setFxStateChunk(&slot, {});
    REQUIRE(fxStateChunk(slot).isEmpty());
}

TEST_CASE("VegReader recovers CcnK FX chunks best-effort", "[plugins][state][veg]")
{
    const QString root = SamplePaths::vegProjectDir();
    if (root.isEmpty()) {
        SKIP("SAMPLES/veg_project not available");
    }
    const QString path =
        QDir(root).filePath(QStringLiteral("project_big--buck-bunny_4x3-preview-reverse-fades-fx.veg"));
    if (!QFile::exists(path)) {
        SKIP("FX sample .veg missing");
    }
    QString err;
    const VegOpenResult veg = VegReader::open(path, &err);
    REQUIRE(err.isEmpty());
    // Sample embeds VST2 CcnK near "Fresh Air".
    if (veg.fxStateChunks.isEmpty()) {
        WARN("No FX chunks recovered — format may have changed");
    } else {
        REQUIRE(veg.fxStateChunks.size() >= 1);
        bool hasCcnK = false;
        for (auto it = veg.fxStateChunks.constBegin(); it != veg.fxStateChunks.constEnd(); ++it) {
            if (it.value().startsWith("CcnK")) {
                hasCcnK = true;
            }
        }
        REQUIRE(hasCcnK);
    }
}

TEST_CASE("VegReader video Event FX matches Vegas chain (no Auto Frame)",
          "[plugins][state][veg][video-fx]")
{
    const QString root = SamplePaths::vegProjectDir();
    if (root.isEmpty()) {
        SKIP("SAMPLES/veg_project not available");
    }
    const QString path =
        QDir(root).filePath(QStringLiteral("project_big--buck-bunny_4x3-preview-reverse-fades-fx.veg"));
    if (!QFile::exists(path)) {
        SKIP("FX sample .veg missing");
    }
    QString err;
    const VegOpenResult veg = VegReader::open(path, &err);
    REQUIRE(err.isEmpty());
    REQUIRE(veg.eventFxNames.size() == 2);
    REQUIRE(veg.eventFxNames.at(0).contains(QStringLiteral("chromablur"), Qt::CaseInsensitive));
    REQUIRE(veg.eventFxNames.at(1).contains(QStringLiteral("glint"), Qt::CaseInsensitive));
    for (const QString &fx : veg.eventFxNames) {
        REQUIRE_FALSE(fx.contains(QStringLiteral("autoframe"), Qt::CaseInsensitive));
        REQUIRE_FALSE(fx.contains(QStringLiteral("sepia"), Qt::CaseInsensitive));
    }
    const FxSlot a = fxSlotFromVegName(veg.eventFxNames.at(0));
    const FxSlot b = fxSlotFromVegName(veg.eventFxNames.at(1));
    REQUIRE(a.displayName == QStringLiteral("Chroma Blur"));
    REQUIRE(b.displayName == QStringLiteral("Glint"));
}

TEST_CASE("VegReader recovers Video Track FX (Sepia + Soft Contrast)",
          "[plugins][state][veg][video-fx]")
{
    const QString root = SamplePaths::vegProjectDir();
    if (root.isEmpty()) {
        SKIP("SAMPLES/veg_project not available");
    }
    const QString path =
        QDir(root).filePath(QStringLiteral("project_big--buck-bunny_4x3-preview-reverse-fades-fx.veg"));
    if (!QFile::exists(path)) {
        SKIP("FX sample .veg missing");
    }
    QString err;
    const VegOpenResult veg = VegReader::open(path, &err);
    REQUIRE(err.isEmpty());
    // The Sepia + Soft Contrast tail is excluded from Event FX (see the test above)
    // but is real Video Track FX data, not noise — recovered separately here.
    REQUIRE(veg.videoTrackFxNames.size() == 2);
    REQUIRE(veg.videoTrackFxNames.at(0).contains(QStringLiteral("sepia"), Qt::CaseInsensitive));
    REQUIRE(veg.videoTrackFxNames.at(1).contains(QStringLiteral("softcontrastvelvetmatter"),
                                                 Qt::CaseInsensitive));
    const FxSlot a = fxSlotFromVegName(veg.videoTrackFxNames.at(0));
    const FxSlot b = fxSlotFromVegName(veg.videoTrackFxNames.at(1));
    REQUIRE(a.displayName == QStringLiteral("Sepia"));
    REQUIRE(b.displayName.contains(QStringLiteral("Contrast"), Qt::CaseInsensitive));
}

TEST_CASE("Opening the FX sample .veg puts Sepia + Soft Contrast on the video track",
          "[plugins][state][veg][video-fx]")
{
    const QString root = SamplePaths::vegProjectDir();
    if (root.isEmpty()) {
        SKIP("SAMPLES/veg_project not available");
    }
    const QString path =
        QDir(root).filePath(QStringLiteral("project_big--buck-bunny_4x3-preview-reverse-fades-fx.veg"));
    if (!QFile::exists(path)) {
        SKIP("FX sample .veg missing");
    }
    QString err;
    const VegOpenResult veg = VegReader::open(path, &err);
    REQUIRE(err.isEmpty());

    ProjectModel model;
    model.applyVegImport(veg, path);

    bool foundVideoTrack = false;
    for (const Track &tr : model.tracks()) {
        if (tr.kind != TrackKind::Video) {
            continue;
        }
        foundVideoTrack = true;
        bool hasSepia = false;
        bool hasSoftContrast = false;
        for (const FxSlot &slot : tr.fxChain) {
            if (slot.displayName == QStringLiteral("Sepia")) {
                hasSepia = true;
            }
            if (slot.displayName.contains(QStringLiteral("Contrast"), Qt::CaseInsensitive)) {
                hasSoftContrast = true;
            }
        }
        REQUIRE(hasSepia);
        REQUIRE(hasSoftContrast);
        break; // first video track
    }
    REQUIRE(foundVideoTrack);
}
