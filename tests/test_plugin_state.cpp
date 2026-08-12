#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "audio/BuiltinDsp.h"
#include "io/VegReader.h"
#include "io/SamplePaths.h"
#include "model/ProjectModel.h"
#include "plugins/AudioPluginTypes.h"
#include "plugins/OfxHost.h"
#include "video/TitlesTextApply.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

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

    // Reverse-engineered against the real file: 55 real generator instances. Vegas
    // stores each one's parameters TWICE, back-to-back (110 raw marker occurrences,
    // every adjacent pair byte-identical in AnimationName+Text with zero exceptions —
    // a cached/default copy alongside the live one, not a second on-timeline
    // occurrence); parseVideoTitlesText() collapses verified adjacent duplicates before
    // returning. findNameValue() itself also used to under-read: a short property name
    // (e.g. "Background") can occur as a literal substring of a longer one earlier in
    // the same record ("FitBackgroundColor"), and the old single-shot search gave up on
    // that false match instead of continuing past it, so this file's true raw-marker
    // count (110) was previously only half-recovered too. Timing is a best-effort
    // order-correlation heuristic against the binary timeline scan (documented as such,
    // not a structural guarantee), and parseTimelineEvents itself occasionally yields a
    // slightly overlapping match on real files — so timing is checked loosely (a
    // handful of small/rare gaps is fine; a systemic breakdown is not), while text/font
    // recovery, which has no such excuse, is checked exactly.
    REQUIRE(veg.titlesAndText.size() >= 50);

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

TEST_CASE("Real VEG-recovered Titles & Text events keep their animation on import",
          "[plugins][state][veg][titles-and-text]")
{
    // Regression guard: titlesTextMotionForPreset() used to derive its lookup key from the
    // preset's display label ("Drop Split" -> "_DropSplit"), which never matches Vegas's
    // real AnimationName format ("_Drop_split") recovered here — every multi-word named
    // preset silently lost its animation (fell back to None) on import. See
    // ISSUES_AND_PLANS.md 2026-08-08 and tests/test_titles_text.cpp for the isolated
    // registry-level coverage of the same bug.
    const QString root = SamplePaths::vegProjectDir();
    if (root.isEmpty()) {
        SKIP("SAMPLES/veg_project not available");
    }
    const QString path =
        QDir(root).filePath(QStringLiteral("project_titles-and-text.veg"));
    if (!QFile::exists(path)) {
        SKIP("Titles & Text sample .veg missing");
    }
    QString err;
    const VegOpenResult veg = VegReader::open(path, &err);
    REQUIRE(err.isEmpty());

    ProjectModel model;
    REQUIRE(model.applyVegImport(veg, path));

    // Regression guard for a third bug found the same day: real Vegas keeps every
    // Titles & Text clip on ONE video track spanning ~4.5 minutes (confirmed against a
    // real Vegas Pro screenshot of this exact project, and against its own EDL sidecar
    // export — 56 events, authoritative for the real timeline). Before
    // VegReader::parseVideoTitlesText() collapsed the verified duplicate parameter
    // blocks described above, applyTitlesTextFromVeg() saw 110 "instances" against only
    // 56 real timeline placeholders; its fallback for the shortfall (append the leftover
    // half using the binary scan's own best-effort sequential timing) kept everything on
    // one track but stretched it to a bogus ~13-minute tail. With the dedup fix, real
    // instance count (55) and real placeholder count (56) are back in near-lockstep, so
    // this should hold with real, non-fabricated timing throughout.
    int videoTrackCount = 0;
    double maxEnd = 0.0;
    for (const Track &tr : model.tracks()) {
        if (tr.kind != TrackKind::Video) {
            continue;
        }
        ++videoTrackCount;
        for (const TrackEvent &ev : tr.events) {
            maxEnd = std::max(maxEnd, ev.startSec + ev.lengthSec);
        }
    }
    REQUIRE(videoTrackCount == 1);
    REQUIRE(maxEnd < 400.0); // real timeline is ~273s; old bug stretched this to ~805s

    int titleEventCount = 0;
    int namedAnimationCount = 0;
    int namedAnimationResolvedCount = 0;
    int dropSplitCount = 0;
    int menaceCount = 0;
    int roughDayCount = 0;
    for (const Track &tr : model.tracks()) {
        if (tr.kind != TrackKind::Video) {
            continue;
        }
        for (const TrackEvent &ev : tr.events) {
            if (ev.mediaKind != EventMediaKind::Title || ev.fxChain.isEmpty()) {
                continue;
            }
            ++titleEventCount;
            const TitlesTextParams p = titlesTextFromSlot(ev.fxChain.first());
            if (p.animationName.isEmpty() || p.animationName == QStringLiteral("_None")) {
                continue;
            }
            ++namedAnimationCount;
            if (titlesTextMotionForPreset(p.animationName).kind != TitlesTextMotion::None) {
                ++namedAnimationResolvedCount;
            }

            // Regression guard for a second bug found the same day: VegReader's
            // findNameValue() matched "Background" as a literal substring of the earlier
            // "FitBackgroundColor" property, failed its length check, and gave up instead
            // of continuing the search — silently defaulting every instance's real
            // Background to transparent. Only 3 of the 51 presets actually have an opaque
            // one; verify VEG import now recovers all three correctly, and that an
            // ordinary transparent preset doesn't get a bogus fill.
            if (p.animationName == QStringLiteral("_Drop_split")) {
                ++dropSplitCount;
                CHECK(p.backgroundColor == QColor(0, 255, 255, 255));
            } else if (p.animationName == QStringLiteral("_Menace")) {
                ++menaceCount;
                CHECK(p.backgroundColor == QColor(255, 255, 255, 255));
            } else if (p.animationName == QStringLiteral("_Rough_Day")) {
                ++roughDayCount;
                CHECK(p.backgroundColor == QColor(255, 255, 0, 255));
            } else {
                CHECK(p.backgroundColor.alpha() == 0);
            }
        }
    }
    REQUIRE(titleEventCount >= 40);
    REQUIRE(namedAnimationCount >= 20); // most of this sample's instances name a real preset
    // Before the fix, this was 0/namedAnimationCount (every named preset silently fell
    // back to None) — now every real recovered key must resolve.
    REQUIRE(namedAnimationResolvedCount == namedAnimationCount);
    REQUIRE(dropSplitCount >= 1);
    REQUIRE(menaceCount >= 1);
    REQUIRE(roughDayCount >= 1);
}

TEST_CASE("Titles & Text project imports onto one video track without an EDL sidecar",
          "[plugins][state][veg][titles-and-text]")
{
    // Regression guard for a fourth bug found the same day as the EDL-path "2 tracks"
    // fix above: the "Binary timeline timings" (no-EDL) import path has the identical
    // symptom for a completely different reason. VegReader's own timeline-name heuristic
    // (assignEventNames) guesses a pooled media filename for EVERY "Video kind" position
    // record, real clip or not — so applyTitlesTextFromVeg() never finds a blank
    // ("no media") placeholder to convert in place and falls back to a second, separate
    // video track for every recovered instance. Fixed by recognizing a "Video kind"
    // position as a generator slot when it lines up with one of the recovered Titles &
    // Text instances' own start times (parseVideoTitlesText() already pairs them the
    // same way) — independent of whatever name VegReader guessed for it.
    //
    // Built as a fully synthetic VegOpenResult (not a real sample file) so this test
    // can't be sidestepped by SamplePaths::sidecarEdlPath()'s embedded-projectPathHint
    // fallback (real Vegas copies embed their original save path, and a plain renamed
    // copy of a real sample still resolves back to its real EDL through that hint), and
    // so it never has to touch real sample assets to exercise the no-EDL path.
    QTemporaryDir tmpDir;
    REQUIRE(tmpDir.isValid());
    const QString path = tmpDir.filePath(QStringLiteral("synthetic_no_edl.veg"));
    {
        QFile f(path);
        REQUIRE(f.open(QIODevice::WriteOnly));
    }
    REQUIRE(SamplePaths::sidecarEdlPath(path).isEmpty());

    VegOpenResult veg;
    veg.hasTimelineTimings = true;
    // 3 generator-claimed positions + 1 real trailing video clip, mirroring the real
    // sample's shape (many short generator slots, one real clip at the end).
    auto addVideoEvent = [&](double start, double len, const QString &name) {
        VegEventInfo e;
        e.kind = VegEventInfo::Kind::Video;
        e.startSec = start;
        e.lengthSec = len;
        e.name = name; // VegReader always guesses a name — never actually empty
        e.offset = veg.events.size();
        veg.events.push_back(e);
    };
    addVideoEvent(0.0, 5.0, QStringLiteral("clip"));
    addVideoEvent(5.0, 5.0, QStringLiteral("clip 2"));
    addVideoEvent(10.0, 5.0, QStringLiteral("clip 3"));
    addVideoEvent(15.0, 8.0, QStringLiteral("clip 4"));

    auto addTitle = [&](double start, double len, const QString &text) {
        VegTitleTextInfo t;
        t.text = text;
        t.animationName = QStringLiteral("_None");
        t.startSec = start;
        t.lengthSec = len;
        veg.titlesAndText.push_back(t);
    };
    addTitle(0.0, 5.0, QStringLiteral("Sample Text"));
    addTitle(5.0, 5.0, QStringLiteral("Second Title"));
    addTitle(10.0, 5.0, QStringLiteral("Third Title"));
    // Position 15..23 has no matching titlesAndText entry — it's the one real clip.

    ProjectModel model;
    const bool usedEdl = model.applyVegImport(veg, path);
    REQUIRE_FALSE(usedEdl);

    int videoTrackCount = 0;
    int titleEventCount = 0;
    int realVideoEventCount = 0;
    double maxEnd = 0.0;
    for (const Track &tr : model.tracks()) {
        if (tr.kind != TrackKind::Video) {
            continue;
        }
        ++videoTrackCount;
        for (const TrackEvent &ev : tr.events) {
            maxEnd = std::max(maxEnd, ev.startSec + ev.lengthSec);
            if (ev.mediaKind == EventMediaKind::Title) {
                ++titleEventCount;
            } else {
                ++realVideoEventCount;
            }
        }
    }
    REQUIRE(videoTrackCount == 1);
    REQUIRE(titleEventCount == 3);
    REQUIRE(realVideoEventCount == 1);
    REQUIRE(maxEnd == Catch::Approx(23.0));
}

TEST_CASE("VegReader recovers real Glint parameter values, keyframes and preset",
          "[plugins][state][veg][video-fx]")
{
    const VegOpenResult veg = openFxSampleVeg();

    // Glint ("Мерцание") is NOT an OFX plug-in in VEGAS Pro 22 — no installed .ofx binary
    // registers com.vegascreativesoftware:glintvelvetmatter — so its whole state lives in
    // the project as <Glint> XML. Recovering only the effect's name and then showing an
    // invented default for every slider is what made OpenVegas look like it had swapped in
    // a stand-in plug-in. These are the values VEGAS's own dialog shows for this project.
    REQUIRE(veg.legacyFxStates.contains(QStringLiteral("glint")));
    const VegLegacyFxState &glint = veg.legacyFxStates.value(QStringLiteral("glint"));

    REQUIRE(glint.baseParams.value(QStringLiteral("Threshold")).toDouble()
            == Catch::Approx(23.456906968236335).margin(0.01));
    REQUIRE(glint.baseParams.value(QStringLiteral("Boost")).toDouble()
            == Catch::Approx(-35.401167937256339).margin(0.01));
    REQUIRE(glint.baseParams.value(QStringLiteral("VerticalRadius")).toDouble()
            == Catch::Approx(62.040967702820227).margin(0.01));
    // Degrees stay degrees; percentages get scaled — the two must not be confused.
    REQUIRE(glint.baseParams.value(QStringLiteral("Hue")).toDouble()
            == Catch::Approx(116.77641274810867).margin(0.01));
    REQUIRE(glint.baseParams.value(QStringLiteral("Saturation")).toDouble()
            == Catch::Approx(79.491525423728815).margin(0.01));
    // Booleans arrive as 0/1 rather than being dropped.
    REQUIRE(glint.baseParams.value(QStringLiteral("ReduceFlicker")).toDouble() == Catch::Approx(0.0));
    REQUIRE(glint.baseParams.value(QStringLiteral("EffectOnly")).toDouble() == Catch::Approx(0.0));

    // The effect is animated in this project; times come from the tick field VEGAS writes
    // ahead of each keyframe blob.
    REQUIRE(glint.keyframes.size() >= 4);
    REQUIRE(glint.keyframes.first().timeSec == Catch::Approx(0.0).margin(0.001));
    for (int i = 1; i < glint.keyframes.size(); ++i) {
        REQUIRE(glint.keyframes[i].timeSec > glint.keyframes[i - 1].timeSec);
    }
    // Hue is one of the parameters that actually moves across the animation.
    REQUIRE(glint.keyframes.last().params.value(QStringLiteral("Hue")).toDouble()
            != Catch::Approx(glint.keyframes.first().params.value(QStringLiteral("Hue")).toDouble()));

    REQUIRE(glint.presetName == QStringLiteral("Sparkle"));
}

TEST_CASE("VegReader recovers real Soft Contrast parameter values",
          "[plugins][state][veg][video-fx]")
{
    const VegOpenResult veg = openFxSampleVeg();

    REQUIRE(veg.legacyFxStates.contains(QStringLiteral("soft contrast")));
    const VegLegacyFxState &soft = veg.legacyFxStates.value(QStringLiteral("soft contrast"));
    REQUIRE(soft.baseParams.value(QStringLiteral("EffectContrast")).toDouble()
            == Catch::Approx(0.6652542372881356).margin(1e-6));
    REQUIRE(soft.baseParams.value(QStringLiteral("EffectDiffusion")).toDouble()
            == Catch::Approx(0.12443438917398453).margin(1e-6));
    REQUIRE(soft.baseParams.value(QStringLiteral("EffectLowTrim")).toDouble()
            == Catch::Approx(0.57466065883636475).margin(1e-6));
    // The nested <VignetteEffect>/<VignetteMask> block repeats element names like
    // Strength and Enabled; taking them would silently overwrite the effect's own values.
    REQUIRE(soft.baseParams.value(QStringLiteral("EffectStretchRange")).toDouble()
            == Catch::Approx(1.0).margin(1e-6));
    REQUIRE(soft.presetName == QStringLiteral("Soft Moderate Contrast"));
}

TEST_CASE("Opened project carries the real Glint values and its keyframe lanes",
          "[plugins][state][veg][video-fx]")
{
    QString path;
    const VegOpenResult veg = openFxSampleVeg(&path);

    ProjectModel model;
    REQUIRE(model.applyVegImport(veg, path));

    const TrackEvent *fxEvent = nullptr;
    int glintIndex = -1;
    for (const Track &tr : model.tracks()) {
        if (tr.kind != TrackKind::Video) {
            continue;
        }
        for (const TrackEvent &ev : tr.events) {
            for (int i = 0; i < ev.fxChain.size(); ++i) {
                if (ev.fxChain[i].displayName.compare(QStringLiteral("Glint"),
                                                      Qt::CaseInsensitive) == 0) {
                    fxEvent = &ev;
                    glintIndex = i;
                }
            }
        }
    }
    REQUIRE(fxEvent != nullptr);
    REQUIRE(glintIndex >= 0);

    const FxSlot &slot = fxEvent->fxChain[glintIndex];
    const QVariantMap params = unpackFxParams(slot.state);
    // Was empty before: the panel then showed the approximate table's defaults
    // (Threshold 67, Hue 0, Saturation 100 …) instead of anything from the project.
    REQUIRE(params.value(QStringLiteral("Threshold")).toDouble()
            == Catch::Approx(23.456906968236335).margin(0.01));
    REQUIRE(params.value(QStringLiteral("Hue")).toDouble()
            == Catch::Approx(116.77641274810867).margin(0.01));

    // The animation is imported as automation lanes, which is what the FX dialog draws as
    // VEGAS-style keyframe diamonds.
    const QString masterId = fxMasterAutomationTargetId(slot);
    const QString hueId = fxParamAutomationTargetId(slot, QStringLiteral("Hue"));
    bool hasMaster = false;
    bool hasHue = false;
    for (const AutomationLane &lane : fxEvent->automationLanes) {
        if (lane.targetId == masterId) {
            hasMaster = true;
            REQUIRE(lane.points.size() >= 4);
        }
        if (lane.targetId == hueId) {
            hasHue = true;
        }
    }
    REQUIRE(hasMaster);
    REQUIRE(hasHue);
}
