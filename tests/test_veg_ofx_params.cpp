#include "io/SamplePaths.h"
#include "io/VegOfxParams.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace openvegas;

namespace {

QByteArray readSample(const QString &name)
{
    const QString path = SamplePaths::resolveProjectPath(QStringLiteral("SAMPLES/veg_project/")
                                                         + name);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    return f.readAll();
}

/** Byte offset of the UTF-16 text `needle`, or -1. */
int findUtf16(const QByteArray &data, const QString &needle)
{
    QByteArray raw(reinterpret_cast<const char *>(needle.utf16()), needle.size() * 2);
    return int(data.indexOf(raw));
}

} // namespace

TEST_CASE("OFX parameters come out of a project with their values and curves",
          "[veg][ofx][video-fx]")
{
    const QByteArray data =
        readSample(QStringLiteral("project_big--buck-bunny_4x3-preview-reverse-fades-fx.veg"));
    if (data.isEmpty()) {
        SKIP("sample project not available");
    }
    const int at = findUtf16(data, QStringLiteral("{Svfx:com.vegascreativesoftware:chromablur}"));
    REQUIRE(at > 0);

    VegOfxEffect effect;
    REQUIRE(vegOfxDecodeEffect(data, at, &effect));
    CHECK(effect.pluginId
          == QStringLiteral("{Svfx:com.vegascreativesoftware:chromablur}"));
    CHECK(effect.presetName == QStringLiteral("(Default)"));
    REQUIRE(effect.params.size() == 2);

    // This is the whole point: until these numbers were read, an effect out of a project
    // reached the plug-in with an empty parameter map, took the plug-in's own defaults —
    // zero radius for Chroma Blur — and rendered as very nearly nothing.
    CHECK(effect.params[0].name == QStringLiteral("HorizontalPixels"));
    CHECK(effect.params[0].scalar() == Catch::Approx(2.993197).epsilon(1e-6));
    CHECK(effect.params[1].name == QStringLiteral("VerticalPixels"));
    CHECK(effect.params[1].scalar() == Catch::Approx(4.489796).epsilon(1e-6));

    // One parameter is animated and the other is not, which is what makes this sample
    // worth keeping: both shapes appear in one record.
    REQUIRE(effect.params[0].keys.size() == 5);
    CHECK(effect.params[1].keys.isEmpty());

    const QVector<VegOfxKeyframe> &keys = effect.params[0].keys;
    // Times are milliseconds in the file; seconds here, because nothing else in the
    // project model counts in milliseconds.
    CHECK(keys[0].timeSec == Catch::Approx(0.0));
    CHECK(keys[1].timeSec == Catch::Approx(1.037001).epsilon(1e-5));
    CHECK(keys[4].timeSec == Catch::Approx(6.354293).epsilon(1e-5));
    CHECK(keys[0].value == Catch::Approx(0.0));
    CHECK(keys[1].value == Catch::Approx(5.442177).epsilon(1e-6));
    CHECK(keys[4].value == Catch::Approx(10.0));

    // The Bezier handles are absolute times, not offsets: a key at zero has one at -0.1.
    CHECK(keys[0].inTimeSec == Catch::Approx(-0.0001).epsilon(1e-3));
    CHECK(keys[1].inTimeSec == Catch::Approx(keys[1].timeSec - 0.0001).epsilon(1e-3));
    CHECK(keys[1].outTimeSec == Catch::Approx(keys[1].timeSec + 0.0001).epsilon(1e-3));

    // The saved value is what VEGAS was showing, which on an animated parameter is the
    // curve read at the playhead — here the fourth key, not the first.
    CHECK(effect.params[0].scalar() == Catch::Approx(keys[3].value).epsilon(1e-6));

    const QVariantMap flat = vegOfxParamMap(effect);
    CHECK(flat.value(QStringLiteral("HorizontalPixels")).toDouble()
          == Catch::Approx(2.993197).epsilon(1e-6));
    CHECK(flat.value(QStringLiteral("VerticalPixels")).toDouble()
          == Catch::Approx(4.489796).epsilon(1e-6));
}

TEST_CASE("A record that does not add up is refused rather than guessed at",
          "[veg][ofx][video-fx]")
{
    QByteArray data =
        readSample(QStringLiteral("project_big--buck-bunny_4x3-preview-reverse-fades-fx.veg"));
    if (data.isEmpty()) {
        SKIP("sample project not available");
    }
    const int at = findUtf16(data, QStringLiteral("{Svfx:com.vegascreativesoftware:chromablur}"));
    REQUIRE(at > 0);

    VegOfxEffect effect;
    REQUIRE(vegOfxDecodeEffect(data, at, &effect));

    // The block size and the keyframe count are written independently, so they check each
    // other. Break one and the reading must stop: numbers taken from the middle of some
    // other structure would reach a plug-in and render as something plausible and wrong,
    // which is far worse than an effect that renders with its defaults.
    const int keyCountAt = at + 88 + 20 + 8 + 8 + 34 + 8;
    QByteArray broken = data;
    REQUIRE(quint8(broken[keyCountAt]) == 5);
    broken[keyCountAt] = char(6);
    VegOfxEffect ruined;
    CHECK_FALSE(vegOfxDecodeEffect(broken, at, &ruined));

    // And a position that is not a record at all yields nothing.
    CHECK_FALSE(vegOfxDecodeEffect(data, 64, &ruined));
    CHECK_FALSE(vegOfxDecodeEffect(data, 4, &ruined));
}

TEST_CASE("The same reading serves a transition record", "[veg][ofx][transitions]")
{
    const QByteArray data = readSample(QStringLiteral("project_transitions_othersmores.veg"));
    if (data.isEmpty()) {
        SKIP("transitions sample not available");
    }
    const int at = findUtf16(data, QStringLiteral("{Svfx:com.vegascreativesoftware:barndoor}"));
    REQUIRE(at > 0);

    VegOfxEffect effect;
    REQUIRE(vegOfxDecodeEffect(data, at, &effect));
    CHECK(effect.presetName == QStringLiteral("(Default)"));

    QVariantMap byName;
    for (const VegOfxParam &p : effect.params) {
        byName.insert(p.name, p.value);
    }
    // A transition declares its own progress parameter, which is what makes hosting one
    // possible; the rest are the group's settings.
    CHECK(byName.contains(QStringLiteral("Transition")));
    CHECK(byName.contains(QStringLiteral("Direction")));

    // A colour keeps its three components instead of collapsing to one number. The reader
    // this replaced had no notion of width at all — it derived a count of doubles from the
    // block size, which happens to agree only while nothing is animated.
    REQUIRE(byName.contains(QStringLiteral("BorderColor")));
    CHECK(byName.value(QStringLiteral("BorderColor")).toList().size() == 3);

    // And an integer stays an integer rather than being read as a double's worth of bytes.
    CHECK(byName.value(QStringLiteral("Orientation")).typeId() == QMetaType::Int);
}
