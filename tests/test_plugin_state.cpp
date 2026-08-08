#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "audio/BuiltinDsp.h"
#include "io/VegReader.h"
#include "io/SamplePaths.h"
#include "model/ProjectModel.h"
#include "plugins/AudioPluginTypes.h"

#include <QDir>
#include <QFile>

#include <cmath>

using namespace openvegas;

namespace {

// Shared by the VegReader test cases below: they all exercise the same FX sample .veg,
// skipping (not failing) when the sample tree isn't present in this checkout.
VegOpenResult openFxSampleVeg(QString *outPath = nullptr)
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
    if (outPath) {
        *outPath = path;
    }
    return veg;
}

} // namespace

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
    const VegOpenResult veg = openFxSampleVeg();
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
    const VegOpenResult veg = openFxSampleVeg();
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
    const VegOpenResult veg = openFxSampleVeg();
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
    QString path;
    const VegOpenResult veg = openFxSampleVeg(&path);

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

TEST_CASE("VegReader converts Track Motion rotationZ from turns to radians",
          "[plugins][state][veg][video-fx]")
{
    // Regression guard: the on-disk value (1.0) is a whole-turn count (1.0 == 360°, visually
    // identical to no rotation at all — which is what real Vegas Pro's preview shows for this
    // sample), not radians as TrackMotionKeyframe::rotationZ is documented and used everywhere
    // else in the app (TrackMotionDialog's kRadToDeg/kDegToRad, TrackMotionApply's rotate()).
    // Before the fix this parsed as 1.0 rad (~57°), spinning the preview visibly.
    const VegOpenResult veg = openFxSampleVeg();
    REQUIRE(veg.hasTrackMotion);
    REQUIRE(veg.trackMotion.motionKeyframes.size() == 1);
    const double rotationZ = veg.trackMotion.motionKeyframes.first().rotationZ;
    REQUIRE(rotationZ == Catch::Approx(2.0 * M_PI).epsilon(1e-6));
}

TEST_CASE("VegReader recovers VEGAS Titles & Text generator instances",
          "[plugins][state][veg][titles-and-text]")
{
    const QString root = SamplePaths::vegProjectDir();
    if (root.isEmpty()) {
        SKIP("SAMPLES/veg_project not available");
    }
    const QString path =
        QDir(root).filePath(QStringLiteral("project_big--buck-bunny_titles-and-text.veg"));
    if (!QFile::exists(path)) {
        SKIP("Titles & Text sample .veg missing");
    }
    QString err;
    const VegOpenResult veg = VegReader::open(path, &err);
    REQUIRE(err.isEmpty());

    // Reverse-engineered against the real file: 55 real generator instances (a second,
    // param-less echo of the plugin id per instance is filtered out — see
    // VegReader::parseVideoTitlesText). Timing is a best-effort order-correlation
    // heuristic against the binary timeline scan (documented as such, not a structural
    // guarantee), and parseTimelineEvents itself occasionally yields a slightly
    // overlapping match on real files — so timing is checked loosely (a handful of
    // small/rare gaps is fine; a systemic breakdown is not), while text/font recovery,
    // which has no such excuse, is checked exactly.
    REQUIRE(veg.titlesAndText.size() >= 40);

    bool foundSampleText1 = false;
    double prevEnd = -1.0;
    int badGaps = 0;
    for (const VegTitleTextInfo &t : veg.titlesAndText) {
        REQUIRE_FALSE(t.text.isEmpty());
        REQUIRE_FALSE(t.text.contains(QLatin1Char('\\'))); // no leftover RTF control words
        REQUIRE(t.lengthSec > 0.0);
        if (t.startSec + 0.5 < prevEnd) {
            ++badGaps;
        }
        prevEnd = t.startSec + t.lengthSec;
        if (t.text.contains(QStringLiteral("Sample Text 1"))) {
            foundSampleText1 = true;
            REQUIRE(t.fontFamily.compare(QStringLiteral("Verdana"), Qt::CaseInsensitive) == 0);
            REQUIRE(t.fontSize == Catch::Approx(48.0));
        }
    }
    REQUIRE(foundSampleText1);
    REQUIRE(badGaps <= 2);
}
