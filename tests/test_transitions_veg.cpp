#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "io/SamplePaths.h"
#include "io/VegReader.h"
#include "io/VegRiff.h"
#include "model/ProjectModel.h"
#include "video/TransitionApply.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QPair>
#include <QSet>

#include <cstring>

using namespace openvegas;

// --- Recovery from a real Vegas project -------------------------------------------
// SAMPLES/veg_project/project_transitions_3d-blinds.veg is the user's own test bed:
// all four presets placed on a fade-in, a fade-out and a crossfade (12 instances). The
// binary layout parseTransitions() reads was reverse-engineered from this file, so the
// recovered values double as a cross-check of the screenshot-derived preset defaults.

TEST_CASE("VegReader recovers every 3D Blinds transition from the sample project",
         "[video][transitions][veg]")
{
    const QString root = SamplePaths::vegProjectDir();
    if (root.isEmpty()) {
        SKIP("SAMPLES/veg_project not available");
    }
    const QString path =
        QDir(root).filePath(QStringLiteral("project_transitions_3d-blinds.veg"));
    if (!QFile::exists(path)) {
        SKIP("transitions sample .veg missing");
    }

    QString err;
    const VegOpenResult veg = VegReader::open(path, &err);
    CHECK(err.isEmpty());
    REQUIRE(veg.transitions.size() == 12);

    int fadeOutCount = 0;
    QSet<QString> presets;
    for (const VegTransitionInfo &t : veg.transitions) {
        presets.insert(t.presetName);
        if (t.fadeOut) {
            ++fadeOutCount;
        }
        CHECK(t.eventStartSec >= 0.0); // every one resolved to an owning event
    }
    // 4 presets x (fade-in, fade-out, crossfade); the crossfade is stored fade-in side.
    CHECK(presets.size() == 4);
    CHECK(fadeOutCount == 4);
}

TEST_CASE("Recovered 3D Blinds parameters match the catalog defaults for every preset",
         "[video][transitions][veg]")
{
    const QString root = SamplePaths::vegProjectDir();
    if (root.isEmpty()) {
        SKIP("SAMPLES/veg_project not available");
    }
    const QString path =
        QDir(root).filePath(QStringLiteral("project_transitions_3d-blinds.veg"));
    if (!QFile::exists(path)) {
        SKIP("transitions sample .veg missing");
    }
    QString err;
    const VegOpenResult veg = VegReader::open(path, &err);
    REQUIRE_FALSE(veg.transitions.isEmpty());

    for (const VegTransitionInfo &t : veg.transitions) {
        const TransitionPresetInfo *preset =
            transitionPreset(transition3dBlindsId(), t.presetName);
        INFO("preset: " << t.presetName.toStdString());
        REQUIRE(preset);
        CHECK(t.divisions == preset->params.value(QStringLiteral("divisions")).toInt());
        CHECK(t.extraSpins == preset->params.value(QStringLiteral("extraSpins")).toInt());
        CHECK(t.stagger
              == Catch::Approx(preset->params.value(QStringLiteral("stagger")).toDouble()));
        CHECK(t.specularLight
              == Catch::Approx(preset->params.value(QStringLiteral("specularLight")).toDouble()));
        // Direction is deliberately not compared against the preset. The catalog now
        // carries VEGAS's shipped values, and those give Direction 0 for all four 3D
        // Blinds presets — but this project stores 2 on "Slot Machine", i.e. it was
        // rotated after the preset was applied. That is a normal thing for a project to
        // contain, so requiring equality here would be asserting the user never touched
        // a slider. The four fields above still pin the preset down.
        CHECK(t.direction >= 0);
        CHECK(t.direction <= 3);
    }
}

TEST_CASE("Opening the sample project puts transitions on the events' fades",
         "[video][transitions][veg]")
{
    const QString root = SamplePaths::vegProjectDir();
    if (root.isEmpty()) {
        SKIP("SAMPLES/veg_project not available");
    }
    const QString path =
        QDir(root).filePath(QStringLiteral("project_transitions_3d-blinds.veg"));
    if (!QFile::exists(path)) {
        SKIP("transitions sample .veg missing");
    }
    QString err;
    const VegOpenResult veg = VegReader::open(path, &err);

    ProjectModel model;
    model.applyVegImport(veg, path);

    int withIn = 0;
    int withOut = 0;
    for (const Track &track : model.tracks()) {
        for (const TrackEvent &ev : track.events) {
            if (ev.transitionIn.isValid()) {
                ++withIn;
                CHECK(ev.transitionIn.pluginId == transition3dBlindsId());
            }
            if (ev.transitionOut.isValid()) {
                ++withOut;
                CHECK(ev.transitionOut.pluginId == transition3dBlindsId());
            }
        }
    }
    // This is the regression the user hit: opening the project showed no transitions
    // at all because nothing parsed or applied them.
    CHECK(withIn > 0);
    CHECK(withOut > 0);
    CHECK(withIn + withOut == 12);
}

// --- The other transition groups ---------------------------------------------------
// project_transitions_othersmores.veg is the user's second test bed, holding the groups
// beyond 3D Blinds. Each record is keyed by a 16-byte plug-in GUID; those GUIDs were
// identified by locating them inside VEGAS's own binaries (vfx1.dll, vidpcore.dll,
// sftrans1.dll, SfPagePeel.dll) and reading the class names beside them, so the mapping
// rests on the shipped code rather than on preset-name guesswork.

TEST_CASE("VegReader recognises the transition groups beyond 3D Blinds",
          "[video][transitions][veg]")
{
    const QString root = SamplePaths::vegProjectDir();
    if (root.isEmpty()) {
        SKIP("SAMPLES/veg_project not available");
    }
    const QString path =
        QDir(root).filePath(QStringLiteral("project_transitions_othersmores.veg"));
    if (!QFile::exists(path)) {
        SKIP("othersmores sample .veg missing");
    }

    QString err;
    const VegOpenResult veg = VegReader::open(path, &err);
    CHECK(err.isEmpty());
    REQUIRE_FALSE(veg.transitions.isEmpty());

    QSet<QString> plugins;
    for (const VegTransitionInfo &t : veg.transitions) {
        plugins.insert(t.pluginName);
        INFO(t.pluginName.toStdString() << " / " << t.presetName.toStdString());
        CHECK_FALSE(t.pluginName.isEmpty());
        CHECK_FALSE(t.presetName.isEmpty());
        CHECK(t.kind != VegTransitionKind::Unknown);
    }

    // All six groups this project exercises must come back named.
    CHECK(plugins.contains(QStringLiteral("Gradient Wipe")));
    CHECK(plugins.contains(QStringLiteral("Venetian Blinds")));
    CHECK(plugins.contains(QStringLiteral("Portals")));
    CHECK(plugins.contains(QStringLiteral("3D Cascade")));
    CHECK(plugins.contains(QStringLiteral("3D Fly In/Out")));
    CHECK(plugins.contains(QStringLiteral("3D Shuffle")));
}

TEST_CASE("Venetian Blinds parameters match what the preset names state",
          "[video][transitions][veg]")
{
    const QString root = SamplePaths::vegProjectDir();
    if (root.isEmpty()) {
        SKIP("SAMPLES/veg_project not available");
    }
    const QString path =
        QDir(root).filePath(QStringLiteral("project_transitions_othersmores.veg"));
    if (!QFile::exists(path)) {
        SKIP("othersmores sample .veg missing");
    }

    QString err;
    const VegOpenResult veg = VegReader::open(path, &err);
    CHECK(err.isEmpty());

    // The presets name their own values, which is exactly what identified these two
    // fields: "Seven Horizontal Blinds" must decode to 7 blinds at 90 degrees. If the
    // field order were wrong, these would not line up.
    QHash<QString, QPair<double, double>> expected;
    expected.insert(QStringLiteral("One Vertical Blind"), {1.0, 0.0});
    expected.insert(QStringLiteral("Five Vertical Blinds"), {5.0, 0.0});
    expected.insert(QStringLiteral("Seven Horizontal Blinds"), {7.0, 90.0});
    expected.insert(QStringLiteral("Sloped Blinds"), {3.0, 45.0});
    expected.insert(QStringLiteral("Fifteen Tilted Blinds"), {15.0, 105.0});
    expected.insert(QStringLiteral("Many Crooked Blinds"), {30.0, 190.0});

    int checked = 0;
    for (const VegTransitionInfo &t : veg.transitions) {
        if (t.kind != VegTransitionKind::VenetianBlinds) {
            continue;
        }
        auto it = expected.constFind(t.presetName);
        if (it == expected.constEnd()) {
            continue;
        }
        INFO(t.presetName.toStdString());
        CHECK(t.blindCount == Catch::Approx(it->first));
        CHECK(t.blindAngleDeg == Catch::Approx(it->second));
        CHECK_FALSE(t.paramsUndecoded);
        ++checked;
    }
    CHECK(checked == expected.size());
}

TEST_CASE("3D Shuffle specular light separates Bright Light from Low Light",
          "[video][transitions][veg]")
{
    const QString root = SamplePaths::vegProjectDir();
    if (root.isEmpty()) {
        SKIP("SAMPLES/veg_project not available");
    }
    const QString path =
        QDir(root).filePath(QStringLiteral("project_transitions_othersmores.veg"));
    if (!QFile::exists(path)) {
        SKIP("othersmores sample .veg missing");
    }

    QString err;
    const VegOpenResult veg = VegReader::open(path, &err);
    CHECK(err.isEmpty());

    double bright = -1.0;
    double low = -1.0;
    for (const VegTransitionInfo &t : veg.transitions) {
        if (t.kind != VegTransitionKind::Shuffle3D) {
            continue;
        }
        // The plug-in's dialog has exactly one control, "Specular light"; VEGAS shows
        // 1.0000 for Bright Light, and the file stores 0.2 for Low Light.
        if (t.presetName == QLatin1String("Bright Light")) {
            bright = t.specularLight;
        } else if (t.presetName == QLatin1String("Low Light")) {
            low = t.specularLight;
        }
    }
    INFO("bright=" << bright << " low=" << low);
    CHECK(bright == Catch::Approx(1.0));
    CHECK(low == Catch::Approx(0.2));
}

TEST_CASE("Groups whose layout is not pinned down report it rather than guess",
          "[video][transitions][veg]")
{
    const QString root = SamplePaths::vegProjectDir();
    if (root.isEmpty()) {
        SKIP("SAMPLES/veg_project not available");
    }
    const QString path =
        QDir(root).filePath(QStringLiteral("project_transitions_othersmores.veg"));
    if (!QFile::exists(path)) {
        SKIP("othersmores sample .veg missing");
    }

    QString err;
    const VegOpenResult veg = VegReader::open(path, &err);
    CHECK(err.isEmpty());

    for (const VegTransitionInfo &t : veg.transitions) {
        const bool undecodedKind = t.kind == VegTransitionKind::GradientWipe
                                   || t.kind == VegTransitionKind::FlyInOut3D
                                   || t.kind == VegTransitionKind::Portals;
        INFO(t.pluginName.toStdString());
        // Recovering the preset name is the useful part for these three: it is what
        // selects the gradient or height map. Inventing numbers on top would be worse
        // than admitting the layout is unknown.
        CHECK(t.paramsUndecoded == undecodedKind);
    }
}

// --- Venetian Blinds: the second group with a real renderer ------------------------

TEST_CASE("Venetian Blinds presets carry the values VEGAS stored for them",
          "[video][transitions]")
{
    const TransitionPluginInfo *info = transitionPluginById(transitionVenetianBlindsId());
    REQUIRE(info != nullptr);
    CHECK(info->name == QLatin1String("Venetian Blinds"));
    REQUIRE(info->params.size() == 3);
    REQUIRE(info->presets.size() == 6);

    // Same numbers the .veg holds, so the dock and a project round-trip agree.
    const TransitionInstance seven =
        makeTransitionInstance(transitionVenetianBlindsId(),
                               QStringLiteral("Seven Horizontal Blinds"));
    CHECK(transitionParamValue(seven, QStringLiteral("count")) == Catch::Approx(7.0));
    CHECK(transitionParamValue(seven, QStringLiteral("angle")) == Catch::Approx(90.0));

    const TransitionInstance one =
        makeTransitionInstance(transitionVenetianBlindsId(),
                               QStringLiteral("One Vertical Blind"));
    // VEGAS ships 0 here, not 1: the plug-in reads zero as a single blind. A project
    // saved with this preset stores the effective 1 instead, which is why the file-based
    // test above expects a different number for the same preset name. The renderer
    // clamps to at least one blind, so both produce the same picture.
    CHECK(transitionParamValue(one, QStringLiteral("count")) == Catch::Approx(0.0));
    CHECK(transitionParamValue(one, QStringLiteral("angle")) == Catch::Approx(0.0));
}

TEST_CASE("Venetian Blinds renders slats whose count and angle follow the parameters",
          "[video][transitions]")
{
    const QSize size(160, 120);
    QImage a(size, QImage::Format_ARGB32_Premultiplied);
    a.fill(QColor(0, 0, 0));
    QImage b(size, QImage::Format_ARGB32_Premultiplied);
    b.fill(QColor(255, 255, 255));

    auto edgesAlong = [&](const QString &preset, bool scanRow) {
        const TransitionInstance t =
            makeTransitionInstance(transitionVenetianBlindsId(), preset);
        const QImage mid = renderTransition(a, b, 0.5, t);
        REQUIRE_FALSE(mid.isNull());
        // Count black/white flips along the middle row (or column): one slat produces two.
        int flips = 0;
        int prev = -1;
        const int n = scanRow ? size.width() : size.height();
        for (int i = 0; i < n; ++i) {
            const QRgb px = scanRow ? mid.pixel(i, size.height() / 2)
                                    : mid.pixel(size.width() / 2, i);
            const int lit = qRed(px) > 127 ? 1 : 0;
            if (prev >= 0 && lit != prev) {
                ++flips;
            }
            prev = lit;
        }
        return flips;
    };

    // Vertical slats show their structure across a row and none down a column;
    // horizontal ones are the other way round. If angle were ignored, or the axis
    // swapped, these two would not separate.
    const int fiveAcross = edgesAlong(QStringLiteral("Five Vertical Blinds"), true);
    const int fiveDown = edgesAlong(QStringLiteral("Five Vertical Blinds"), false);
    INFO("five vertical: across=" << fiveAcross << " down=" << fiveDown);
    CHECK(fiveAcross > 4);
    CHECK(fiveDown == 0);

    const int sevenAcross = edgesAlong(QStringLiteral("Seven Horizontal Blinds"), true);
    const int sevenDown = edgesAlong(QStringLiteral("Seven Horizontal Blinds"), false);
    INFO("seven horizontal: across=" << sevenAcross << " down=" << sevenDown);
    CHECK(sevenDown > 4);
    CHECK(sevenAcross == 0);

    // More blinds must mean more slat edges along the same scan line.
    CHECK(edgesAlong(QStringLiteral("Many Crooked Blinds"), true)
          > edgesAlong(QStringLiteral("One Vertical Blind"), true));

    // The ends of the transition are the plain source and destination.
    const TransitionInstance t =
        makeTransitionInstance(transitionVenetianBlindsId(),
                               QStringLiteral("Five Vertical Blinds"));
    CHECK(qRed(renderTransition(a, b, 0.0, t).pixel(80, 60)) < 8);
    CHECK(qRed(renderTransition(a, b, 1.0, t).pixel(80, 60)) > 247);
}

// --- The wipe family ---------------------------------------------------------------
// Linear Wipe, Barn Door, Iris and Clock Wipe are the OFX groups whose parameters and
// preset values were read out of project_transitions_othersmores.veg. Every number in
// the catalog appears in that file.

TEST_CASE("Wipe presets carry the values the project stored", "[video][transitions]")
{
    struct Expect {
        QString id;
        QString preset;
        QString key;
        double value;
    };
    // Linear Wipe angles are named by their own presets: 0 sweeps left to right,
    // 90 downwards, 180 back, 270 upwards.
    const QVector<Expect> checks = {
        {transitionLinearWipeId(), QStringLiteral("Left-Right, Hard Edge"),
         QStringLiteral("angle"), 0.0},
        {transitionLinearWipeId(), QStringLiteral("Top-Down, Hard Edge"),
         QStringLiteral("angle"), 90.0},
        {transitionLinearWipeId(), QStringLiteral("Right-Left, Hard Edge"),
         QStringLiteral("angle"), 180.0},
        {transitionLinearWipeId(), QStringLiteral("Bottom-Up, Hard Edge"),
         QStringLiteral("angle"), 270.0},
        {transitionLinearWipeId(), QStringLiteral("Left-Right, Soft Edge"),
         QStringLiteral("feather"), 0.5},
        {transitionClockWipeId(), QStringLiteral("Clockwise, Hard Edge"),
         QStringLiteral("featherAngle"), 0.0},
        {transitionClockWipeId(), QStringLiteral("Clockwise, Blend In"),
         QStringLiteral("featherAngle"), 220.0},
        {transitionClockWipeId(), QStringLiteral("Counter Clockwise, Soft Edge"),
         QStringLiteral("direction"), 1.0},
        {transitionIrisId(), QStringLiteral("Diamond, Out, Center"), QStringLiteral("shape"), 4.0},
        {transitionIrisId(), QStringLiteral("Square, In, Center"), QStringLiteral("shape"), 5.0},
        {transitionIrisId(), QStringLiteral("Square, In, Center"), QStringLiteral("direction"), 0.0},
        {transitionBarnDoorId(), QStringLiteral("Horizontal, Out, No Border"),
         QStringLiteral("orientation"), 1.0},
        {transitionBarnDoorId(), QStringLiteral("Vertical, In, No Border"),
         QStringLiteral("direction"), 0.0},
    };
    for (const Expect &e : checks) {
        const TransitionInstance t = makeTransitionInstance(e.id, e.preset);
        INFO(e.preset.toStdString() << " / " << e.key.toStdString());
        REQUIRE(t.isValid());
        CHECK(transitionParamValue(t, e.key) == Catch::Approx(e.value));
    }
}

TEST_CASE("Wipes start on the outgoing clip and end on the incoming one",
          "[video][transitions]")
{
    const QSize size(120, 90);
    QImage a(size, QImage::Format_ARGB32_Premultiplied);
    a.fill(QColor(0, 0, 0));
    QImage b(size, QImage::Format_ARGB32_Premultiplied);
    b.fill(QColor(255, 255, 255));

    // A transition that has not finished by progress 1 is the failure that shape-agnostic
    // sizing caused: an iris sized by a circle's radius left a diamond unfinished.
    struct Case { QString id; QString preset; };
    const QVector<Case> cases = {
        {transitionLinearWipeId(), QStringLiteral("Left-Right, Hard Edge")},
        {transitionLinearWipeId(), QStringLiteral("Top-Left, Diagonal, Soft Edge")},
        {transitionBarnDoorId(), QStringLiteral("Vertical, Out, No Border")},
        {transitionBarnDoorId(), QStringLiteral("Horizontal, In, No Border")},
        {transitionClockWipeId(), QStringLiteral("Clockwise, Hard Edge")},
        {transitionIrisId(), QStringLiteral("Circle, Out, Center")},
        {transitionIrisId(), QStringLiteral("Diamond, Out, Center")},
        {transitionIrisId(), QStringLiteral("Triangle Down, Out, Center")},
        {transitionIrisId(), QStringLiteral("Square, In, Center")},
    };
    for (const Case &c : cases) {
        const TransitionInstance t = makeTransitionInstance(c.id, c.preset);
        REQUIRE(t.isValid());
        const QImage start = renderTransition(a, b, 0.0, t);
        const QImage end = renderTransition(a, b, 1.0, t);
        REQUIRE_FALSE(start.isNull());
        REQUIRE_FALSE(end.isNull());

        // Sample a grid rather than one pixel: a shape that stops short leaves corners
        // behind, and the middle alone would not notice.
        int darkAtStart = 0;
        int lightAtEnd = 0;
        int total = 0;
        for (int gy = 1; gy < 8; ++gy) {
            for (int gx = 1; gx < 8; ++gx) {
                const int px = size.width() * gx / 8;
                const int py = size.height() * gy / 8;
                darkAtStart += qRed(start.pixel(px, py)) < 8 ? 1 : 0;
                lightAtEnd += qRed(end.pixel(px, py)) > 247 ? 1 : 0;
                ++total;
            }
        }
        INFO(c.id.toStdString() << " / " << c.preset.toStdString());
        CHECK(darkAtStart == total);
        CHECK(lightAtEnd == total);
    }
}

TEST_CASE("Linear Wipe angle picks the sweep direction", "[video][transitions]")
{
    const QSize size(120, 90);
    QImage a(size, QImage::Format_ARGB32_Premultiplied);
    a.fill(QColor(0, 0, 0));
    QImage b(size, QImage::Format_ARGB32_Premultiplied);
    b.fill(QColor(255, 255, 255));

    auto litAt = [&](const QString &preset, int x, int y) {
        const TransitionInstance t = makeTransitionInstance(transitionLinearWipeId(), preset);
        return qRed(renderTransition(a, b, 0.5, t).pixel(x, y)) > 127;
    };

    // Half way through "Left-Right" the left edge has turned over and the right has not.
    CHECK(litAt(QStringLiteral("Left-Right, Hard Edge"), 5, 45));
    CHECK_FALSE(litAt(QStringLiteral("Left-Right, Hard Edge"), 114, 45));
    // "Right-Left" is the mirror image, which is what tells the angle is honoured
    // rather than ignored.
    CHECK_FALSE(litAt(QStringLiteral("Right-Left, Hard Edge"), 5, 45));
    CHECK(litAt(QStringLiteral("Right-Left, Hard Edge"), 114, 45));
    // "Top-Down" splits the other axis entirely.
    CHECK(litAt(QStringLiteral("Top-Down, Hard Edge"), 60, 5));
    CHECK_FALSE(litAt(QStringLiteral("Top-Down, Hard Edge"), 60, 84));
}

TEST_CASE("A .veg walks as a riff64 container, exactly to its last byte",
          "[io][veg][riff]")
{
    const QString root = SamplePaths::vegProjectDir();
    if (root.isEmpty()) {
        SKIP("SAMPLES/veg_project not available");
    }
    const QString veg = QDir(root).filePath(QStringLiteral("project_transitions_3d-blinds.veg"));
    if (!QFile::exists(veg)) {
        SKIP("transition sample missing");
    }
    QFile f(veg);
    REQUIRE(f.open(QIODevice::ReadOnly));
    const QByteArray data = f.readAll();
    f.close();

    const QVector<VegChunk> chunks = vegRiffChunks(data);
    REQUIRE_FALSE(chunks.isEmpty());

    // The outermost chunk is the file. If the size field were the payload alone rather
    // than payload plus its 24-byte header, this would be short by exactly 24 — which is
    // the reading the host's own riff64_ReadChunkHeader settles, subtracting 0x18 from
    // what it read before handing the size back.
    CHECK(chunks.first().offset == 0);
    CHECK(chunks.first().isList);
    CHECK(chunks.first().end == data.size());

    // A container walk either lands on every boundary or derails; there is no partial
    // credit. Every child must sit inside its parent and none may overlap a sibling.
    for (const VegChunk &c : chunks) {
        INFO(c.id.toStdString() << " at " << c.offset);
        CHECK(c.offset >= 0);
        CHECK(c.payload > c.offset);
        // An empty list is legal and this project has one: header and no children.
        CHECK(c.end >= c.payload);
        CHECK(c.end <= data.size());
    }

    // The chunks this project reads by name are all there.
    auto countOf = [&](const QString &id) {
        int n = 0;
        for (const VegChunk &c : chunks) {
            if ((c.isList ? c.listType : c.id) == id) {
                ++n;
            }
        }
        return n;
    };
    CHECK(countOf(VegChunkIds::event()) == 32);
    CHECK(countOf(VegChunkIds::fxChain()) == 16);
    CHECK(countOf(VegChunkIds::fxRecord()) == 16);
    CHECK(countOf(VegChunkIds::marker()) == 12);
}

TEST_CASE("A plug-in record names its own event by nesting", "[io][veg][riff]")
{
    const QString root = SamplePaths::vegProjectDir();
    if (root.isEmpty()) {
        SKIP("SAMPLES/veg_project not available");
    }
    const QString veg = QDir(root).filePath(QStringLiteral("project_transitions_3d-blinds.veg"));
    if (!QFile::exists(veg)) {
        SKIP("transition sample missing");
    }
    QFile f(veg);
    REQUIRE(f.open(QIODevice::ReadOnly));
    const QByteArray data = f.readAll();
    f.close();

    const QVector<VegChunk> chunks = vegRiffChunks(data);
    REQUIRE_FALSE(chunks.isEmpty());

    int records = 0;
    int withPlugin = 0;
    for (const VegChunk &c : chunks) {
        if (c.id != VegChunkIds::fxRecord()) {
            continue;
        }
        ++records;
        // Every slot sits inside a chain, which sits inside an event. That nesting is
        // what identifies the owner; before this the owner was taken to be the last event
        // header earlier in the file, which cannot tell a record inside an event from one
        // that merely follows it.
        const VegChunk *chain = vegRiffEnclosing(chunks, c.offset, VegChunkIds::fxChain());
        REQUIRE(chain);
        const VegChunk *event = vegRiffEnclosing(chunks, c.offset, VegChunkIds::event());
        REQUIRE(event);
        CHECK(event->offset < chain->offset);
        CHECK(chain->end <= event->end);

        // The fixed header's length is the first field, and it is what places the CLSID —
        // not a constant offset. A slot exactly that long carries no plug-in, which is
        // how an event with a plain fade is stored.
        REQUIRE(c.payload + 4 <= data.size());
        qint32 headerLen = 0;
        std::memcpy(&headerLen, data.constData() + c.payload, sizeof(headerLen));
        CHECK(headerLen == 0x90);
        if (c.end - c.payload == headerLen) {
            continue;
        }
        ++withPlugin;
        const int clsidAt = c.payload + headerLen + 4;
        REQUIRE(clsidAt + 16 <= data.size());
        static const unsigned char kBlinds[16] = {0x52, 0xa5, 0x43, 0xc8, 0xeb, 0xaa,
                                                  0x1c, 0x41, 0x84, 0x81, 0xcf, 0x4c,
                                                  0x52, 0xa3, 0x36, 0xe3};
        CHECK(data.mid(clsidAt, 16)
              == QByteArray(reinterpret_cast<const char *>(kBlinds), 16));
    }
    // Sixteen slots, twelve of them filled — which is exactly the transition count this
    // project is known for, the other four being fades with nothing on them.
    CHECK(records == 16);
    CHECK(withPlugin == 12);
}

TEST_CASE("A file that is not a container walks to nothing rather than guessing",
          "[io][veg][riff]")
{
    // The scanning path stays for these, so an empty result has to mean "not a
    // container", never "no records" — otherwise the fallback would never run.
    CHECK(vegRiffChunks(QByteArray()).isEmpty());
    CHECK(vegRiffChunks(QByteArray(200, '\0')).isEmpty());
    CHECK(vegRiffChunks(QByteArray("riff but not really, no size field here")).isEmpty());
}
