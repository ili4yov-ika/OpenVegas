#include "video/TransitionApply.h"

#include "video/TransitionPresetData.h"

#include <QHash>
#include <QSet>

#include <QFont>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace openvegas {
namespace {

/** Direction choice order — index is what gets stored in the params map. */
enum BlindsDirection {
    DirLeftToRight = 0,
    DirRightToLeft,
    DirTopToBottom,
    DirBottomToTop,
};

QStringList blindsDirections()
{
    // Left to Right and Top to Bottom are the two confirmed by the reference
    // screenshots (Simple/Left to Right/Spin vs Slot Machine); the two reverses are the
    // natural completion of the set.
    return {QStringLiteral("Left to Right"), QStringLiteral("Right to Left"),
            QStringLiteral("Top to Bottom"), QStringLiteral("Bottom to Top")};
}

/** Stock presets for a group, straight from VEGAS's shipped preset package. */
QVector<TransitionPresetInfo> stockPresets(const QString &key);

// The border rows are three plain 0…1 sliders rather than a colour swatch: the generic
// properties window has no colour control, and three honest sliders beat a colour that
// cannot be edited at all.
QVector<TransitionParamInfo> borderParams(const QString &featherKey,
                                          const QString &featherLabel)
{
    return {
        {QStringLiteral("borderSize"), QStringLiteral("Border size"), 0.0, 1.0, 4, {}},
        {featherKey, featherLabel, 0.0, 1.0, 4, {}},
        {QStringLiteral("borderRed"), QStringLiteral("Border red"), 0.0, 1.0, 3, {}},
        {QStringLiteral("borderGreen"), QStringLiteral("Border green"), 0.0, 1.0, 3, {}},
        {QStringLiteral("borderBlue"), QStringLiteral("Border blue"), 0.0, 1.0, 3, {}},
    };
}

TransitionPluginInfo makeBlinds()
{
    TransitionPluginInfo info;
    info.id = transition3dBlindsId();
    info.name = QStringLiteral("3D Blinds");
    info.format = QStringLiteral("DXT, 32-bit floating point");
    info.description = QStringLiteral("VEGAS 3D Blinds");

    // Ranges read straight off the reference screenshots' extreme-value captures:
    // Divisions 1…16, Extra spins 0…10, Stagger 0…1, Specular light 0…1.
    info.params = {
        {QStringLiteral("divisions"), QStringLiteral("Divisions"), 1.0, 16.0, 0, {}},
        {QStringLiteral("extraSpins"), QStringLiteral("Extra spins"), 0.0, 10.0, 0, {}},
        {QStringLiteral("stagger"), QStringLiteral("Stagger"), 0.0, 1.0, 4, {}},
        {QStringLiteral("specularLight"), QStringLiteral("Specular light"), 0.0, 1.0, 4, {}},
        {QStringLiteral("direction"), QStringLiteral("Direction"), 0.0, 3.0, 0,
         blindsDirections()},
    };

    // The full shipped set, from VEGAS's own preset package — the values
    // previously written out here matched, but only covered part of it.
    info.presets = stockPresets(QStringLiteral("3dblinds"));
    return info;
}

TransitionPluginInfo makeVenetianBlinds()
{
    TransitionPluginInfo info;
    info.id = transitionVenetianBlindsId();
    info.name = QStringLiteral("Venetian Blinds");
    info.format = QStringLiteral("DXT, 32-bit floating point");
    info.description = QStringLiteral("VEGAS Venetian Blinds");
    // Three doubles, in the order the .veg stores them. The plug-in names them
    // NumberOfBlinds / Angle / Feather, which is what the preset package confirmed.
    info.params = {
        {QStringLiteral("count"), QStringLiteral("Blinds"), 1.0, 32.0, 0, {}},
        {QStringLiteral("angle"), QStringLiteral("Angle"), 0.0, 360.0, 1, {}},
        {QStringLiteral("feather"), QStringLiteral("Feather"), 0.0, 1.0, 4, {}},
    };
    info.presets = stockPresets(QStringLiteral("venetianblinds"));
    return info;
}

TransitionPluginInfo makeLinearWipe()
{
    TransitionPluginInfo info;
    info.id = transitionLinearWipeId();
    info.name = QStringLiteral("Linear Wipe");
    info.format = QStringLiteral("OFX");
    info.description = QStringLiteral("VEGAS Linear Wipe");
    info.params = {
        {QStringLiteral("angle"), QStringLiteral("Angle"), 0.0, 360.0, 1, {}},
        {QStringLiteral("feather"), QStringLiteral("Feather"), 0.0, 1.0, 4, {}},
    };
    info.presets = stockPresets(QStringLiteral("linearwipe"));
    return info;
}

TransitionPluginInfo makeBarnDoor()
{
    TransitionPluginInfo info;
    info.id = transitionBarnDoorId();
    info.name = QStringLiteral("Barn Door");
    info.format = QStringLiteral("OFX");
    info.description = QStringLiteral("VEGAS Barn Door");
    info.params = {
        {QStringLiteral("orientation"), QStringLiteral("Orientation"), 0.0, 1.0, 0,
         {QStringLiteral("Vertical"), QStringLiteral("Horizontal")}},
        {QStringLiteral("direction"), QStringLiteral("Direction"), 0.0, 1.0, 0,
         {QStringLiteral("In"), QStringLiteral("Out")}},
    };
    info.params += borderParams(QStringLiteral("borderFeather"),
                                QStringLiteral("Border feather"));
    info.presets = stockPresets(QStringLiteral("barndoor"));
    return info;
}

TransitionPluginInfo makeIris()
{
    TransitionPluginInfo info;
    info.id = transitionIrisId();
    info.name = QStringLiteral("Iris");
    info.format = QStringLiteral("OFX");
    info.description = QStringLiteral("VEGAS Iris");
    // Shape indices are the plug-in's own. Only the five the shipped presets actually
    // use are named; the gaps keep their numbers rather than being given invented names.
    info.params = {
        {QStringLiteral("shape"), QStringLiteral("Shape"), 0.0, 8.0, 0,
         {QStringLiteral("Circle"), QStringLiteral("1"), QStringLiteral("2"),
          QStringLiteral("Rectangle"), QStringLiteral("Diamond"), QStringLiteral("Square"),
          QStringLiteral("6"), QStringLiteral("7"), QStringLiteral("Triangle Down")}},
        {QStringLiteral("direction"), QStringLiteral("Direction"), 0.0, 1.0, 0,
         {QStringLiteral("In"), QStringLiteral("Out")}},
        {QStringLiteral("centerX"), QStringLiteral("Center X"), 0.0, 1.0, 4, {}},
        {QStringLiteral("centerY"), QStringLiteral("Center Y"), 0.0, 1.0, 4, {}},
    };
    info.params += borderParams(QStringLiteral("feather"), QStringLiteral("Feather"));

    // The full shipped set, from VEGAS's own preset package — the values
    // previously written out here matched, but only covered part of it.
    info.presets = stockPresets(QStringLiteral("iris"));
    return info;
}

TransitionPluginInfo makeClockWipe()
{
    TransitionPluginInfo info;
    info.id = transitionClockWipeId();
    info.name = QStringLiteral("Clock Wipe");
    info.format = QStringLiteral("OFX");
    info.description = QStringLiteral("VEGAS Clock Wipe");
    info.params = {
        {QStringLiteral("direction"), QStringLiteral("Direction"), 0.0, 1.0, 0,
         {QStringLiteral("Clockwise"), QStringLiteral("Counter Clockwise")}},
        // The plug-in states this one in degrees, not in screen units.
        {QStringLiteral("featherAngle"), QStringLiteral("Feather angle"), 0.0, 360.0, 1, {}},
    };
    // The full shipped set, from VEGAS's own preset package — the values
    // previously written out here matched, but only covered part of it.
    info.presets = stockPresets(QStringLiteral("clockwipe"));
    return info;
}

/**
 * Groups that are recognised but not drawn yet.
 *
 * They earn a catalog entry so a project that uses them comes back with the right name
 * on the timeline strip and a properties window listing the presets VEGAS ships — before
 * this they were all mislabelled "3D Blinds", because the importer had only one id to
 * hand out. `renderTransition` has no case for them, so they fall through to the plain
 * cross-dissolve it uses for anything unknown: a sane picture under an honest name,
 * rather than some other transition's geometry wearing this one's label.
 *
 * Parameters are listed only where the layout is actually known (3D Cascade, 3D Shuffle).
 * For the rest the preset name is the whole of what was recovered — for Gradient Wipe and
 * Portals it is also what the plug-in itself keys off, since the preset picks a gradient
 * image or a height map rather than a set of numbers.
 */
TransitionPluginInfo makeStubGroup(const QString &id, const QString &name,
                                   const QString &format,
                                   const QVector<TransitionParamInfo> &params,
                                   const QVector<TransitionPresetInfo> &presets)
{
    TransitionPluginInfo info;
    info.id = id;
    info.name = name;
    info.format = format;
    info.description = QStringLiteral("VEGAS %1 — recognised; drawn as a cross-fade until "
                                      "its own geometry is implemented.")
                           .arg(name);
    info.params = params;
    info.presets = presets;
    return info;
}

TransitionPluginInfo makeCascade3D()
{
    // Layout confirmed from the project: two ints then two doubles, the same opening
    // shape as 3D Blinds but without the extra-spins field.
    //
    // The third field is the plug-in's **Twist**, not a stagger. The name came from
    // 3D Blinds, where a field in that position really is Stagger; VEGAS's own preset
    // package calls this one Twist, and its presets store it under that name — so with
    // the wrong key the shipped values would not have reached the renderer at all.
    QVector<TransitionParamInfo> params = {
        {QStringLiteral("divisions"), QStringLiteral("Divisions"), 1.0, 16.0, 0, {}},
        // 0 and 1 are what the shipped presets use and name; 2 and 3 are their reverses.
        {QStringLiteral("direction"), QStringLiteral("Direction"), 0.0, 3.0, 0,
         {QStringLiteral("Left to Right"), QStringLiteral("Top to Bottom"),
          QStringLiteral("Right to Left"), QStringLiteral("Bottom to Top")}},
        {QStringLiteral("twist"), QStringLiteral("Twist"), 0.0, 1.0, 4, {}},
        {QStringLiteral("specularLight"), QStringLiteral("Specular light"), 0.0, 1.0, 4, {}},
    };
    return makeStubGroup(transitionCascade3dId(), QStringLiteral("3D Cascade"),
                         QStringLiteral("DXT, 32-bit floating point"), params,
                         stockPresets(QStringLiteral("3dcascade")));
}

TransitionPluginInfo makeShuffle3D()
{
    // One control, exactly as the plug-in's own window shows: Specular light.
    QVector<TransitionParamInfo> params = {
        {QStringLiteral("specularLight"), QStringLiteral("Specular light"), 0.0, 1.0, 4, {}},
    };
    return makeStubGroup(transitionShuffle3dId(), QStringLiteral("3D Shuffle"),
                         QStringLiteral("DXT, 32-bit floating point"), params,
                         stockPresets(QStringLiteral("3dshuffle")));
}

TransitionPluginInfo makeFlyInOut3D()
{
    // These were written off as "four doubles whose meaning the sample does not pin
    // down". They were never a mystery: VEGAS's preset package names all eight, and its
    // two shipped presets carry values for them.
    QVector<TransitionParamInfo> params = {
        {QStringLiteral("farXPosition"), QStringLiteral("Far X position"), -10.0, 10.0, 4, {}},
        {QStringLiteral("farYPosition"), QStringLiteral("Far Y position"), -10.0, 10.0, 4, {}},
        {QStringLiteral("farZPosition"), QStringLiteral("Far Z position"), -10.0, 10.0, 4, {}},
        {QStringLiteral("xRotations"), QStringLiteral("X rotations"), -10.0, 10.0, 4, {}},
        {QStringLiteral("yRotations"), QStringLiteral("Y rotations"), -10.0, 10.0, 4, {}},
        {QStringLiteral("zRotations"), QStringLiteral("Z rotations"), -10.0, 10.0, 4, {}},
        {QStringLiteral("specularLight"), QStringLiteral("Specular light"), 0.0, 1.0, 4, {}},
        {QStringLiteral("direction"), QStringLiteral("Direction"), 0.0, 1.0, 0,
         {QStringLiteral("In"), QStringLiteral("Out")}},
    };
    return makeStubGroup(transitionFlyInOut3dId(), QStringLiteral("3D Fly In/Out"),
                         QStringLiteral("DXT, 32-bit floating point"), params,
                         stockPresets(QStringLiteral("3dflyinout")));
}

TransitionPluginInfo makeGradientWipe()
{
    // The preset picks a gradient image; the numeric fields barely move between presets,
    // which is why none are exposed here.
    auto p = [](const QString &n) {
        TransitionPresetInfo i;
        i.name = n;
        return i;
    };
    return makeStubGroup(
        transitionGradientWipeId(), QStringLiteral("Gradient Wipe"),
        QStringLiteral("DXT, 32-bit floating point"), {},
        {p(QStringLiteral("Linear Left-Right")), p(QStringLiteral("Linear Right-Left")),
         p(QStringLiteral("Linear Top-Bottom")), p(QStringLiteral("Linear Bottom-Top")),
         p(QStringLiteral("Box In")), p(QStringLiteral("Box Out")),
         p(QStringLiteral("Circle In")), p(QStringLiteral("Circle Out")),
         p(QStringLiteral("Horizontal Open")), p(QStringLiteral("Vertical Open")),
         p(QStringLiteral("Spiral")), p(QStringLiteral("Star")), p(QStringLiteral("Heart")),
         p(QStringLiteral("Nebulous")), p(QStringLiteral("Paint Splatter")),
         p(QStringLiteral("Puzzle Pieces")), p(QStringLiteral("Soft Noise")),
         p(QStringLiteral("Turbulent Noise"))});
}

/** The light the curl catches — the same three fields in all three page groups. */
QVector<TransitionParamInfo> pageLightParams()
{
    return {
        {QStringLiteral("lightColorRed"), QStringLiteral("Light color red"), 0.0, 1.0, 4, {}},
        {QStringLiteral("lightColorGreen"), QStringLiteral("Light color green"), 0.0, 1.0, 4, {}},
        {QStringLiteral("lightColorBlue"), QStringLiteral("Light color blue"), 0.0, 1.0, 4, {}},
    };
}

TransitionPluginInfo makePagePeel()
{
    // Every field is named by the preset package; the angle names the corner being lifted
    // (30 Bottom-Right, 150 Bottom-Left, 210 Top-Left, 330 Top-Right), which is how the
    // shipped preset names read once the angle is taken in screen coordinates.
    QVector<TransitionParamInfo> params = {
        {QStringLiteral("peelAngle"), QStringLiteral("Peel angle"), 0.0, 360.0, 2, {}},
        {QStringLiteral("foldRadius"), QStringLiteral("Fold radius"), 0.0, 1.0, 4, {}},
        {QStringLiteral("slideAmount"), QStringLiteral("Slide amount"), 0.0, 1.0, 4, {}},
        {QStringLiteral("peelOpacity"), QStringLiteral("Peel opacity"), 0.0, 1.0, 4, {}},
        {QStringLiteral("perspective"), QStringLiteral("Perspective"), 0.0, 1.0, 4, {}},
    };
    params += pageLightParams();
    TransitionPluginInfo info = makeStubGroup(transitionPagePeelId(), QStringLiteral("Page Peel"),
                                              QStringLiteral("DXT, 32-bit floating point"), params,
                                              stockPresets(QStringLiteral("pagepeel")));
    info.description = QStringLiteral("VEGAS Page Peel");
    return info;
}

TransitionPluginInfo makePageRoll()
{
    QVector<TransitionParamInfo> params = {
        {QStringLiteral("rollAngle"), QStringLiteral("Roll angle"), 0.0, 360.0, 2, {}},
        {QStringLiteral("foldRadius"), QStringLiteral("Fold radius"), 0.0, 1.0, 4, {}},
        {QStringLiteral("slideAmount"), QStringLiteral("Slide amount"), 0.0, 1.0, 4, {}},
        {QStringLiteral("rollOpacity"), QStringLiteral("Roll opacity"), 0.0, 1.0, 4, {}},
        {QStringLiteral("perspective"), QStringLiteral("Perspective"), 0.0, 1.0, 4, {}},
    };
    params += pageLightParams();
    TransitionPluginInfo info = makeStubGroup(transitionPageRollId(), QStringLiteral("Page Roll"),
                                              QStringLiteral("DXT, 32-bit floating point"), params,
                                              stockPresets(QStringLiteral("pageroll")));
    info.description = QStringLiteral("VEGAS Page Roll");
    return info;
}

TransitionPluginInfo makePageLoop()
{
    QVector<TransitionParamInfo> params = {
        {QStringLiteral("paperAngle"), QStringLiteral("Paper angle"), 0.0, 360.0, 2, {}},
        {QStringLiteral("paperOpacity"), QStringLiteral("Paper opacity"), 0.0, 1.0, 4, {}},
        {QStringLiteral("loopRadius"), QStringLiteral("Loop radius"), 0.0, 1.0, 4, {}},
        {QStringLiteral("loopPosition"), QStringLiteral("Loop position"), 0.0, 1.0, 4, {}},
        {QStringLiteral("perspective"), QStringLiteral("Perspective"), 0.0, 1.0, 4, {}},
    };
    params += pageLightParams();
    TransitionPluginInfo info = makeStubGroup(transitionPageLoopId(), QStringLiteral("Page Loop"),
                                              QStringLiteral("DXT, 32-bit floating point"), params,
                                              stockPresets(QStringLiteral("pageloop")));
    info.description = QStringLiteral("VEGAS Page Loop");
    return info;
}

TransitionPluginInfo makePortals()
{
    // Likewise: the preset package names every field, so the "the preset names a height
    // map, not a set of numbers" note was wrong — it names both.
    QVector<TransitionParamInfo> params = {
        {QStringLiteral("randomPatternSeed"), QStringLiteral("Random pattern seed"), 0.0,
         9999.0, 0, {}},
        {QStringLiteral("squares"), QStringLiteral("Squares"), 1.0, 64.0, 0, {}},
        {QStringLiteral("maxTransparency"), QStringLiteral("Max transparency"), 0.0, 1.0, 4, {}},
        {QStringLiteral("maxOffset"), QStringLiteral("Max offset"), 0.0, 1.0, 4, {}},
        {QStringLiteral("maxScale"), QStringLiteral("Max scale"), 0.0, 4.0, 4, {}},
    };
    params += borderParams(QStringLiteral("borderFeather"), QStringLiteral("Border feather"));
    return makeStubGroup(transitionPortalsId(), QStringLiteral("Portals"),
                         QStringLiteral("DXT, 32-bit floating point"), params,
                         stockPresets(QStringLiteral("portals")));
}

/**
 * The OFX-stored groups that are recognised but not drawn yet.
 *
 * Table-driven rather than fifteen near-identical functions, and the preset lists were
 * generated from a real project rather than typed: these are the names VEGAS itself
 * stores. A group here gets an id, a name and its presets, which is enough for an
 * imported project to show the right strip and a properties window — before this the
 * whole family was invisible, because nothing parsed the "{Svfx:…}" form at all.
 */
struct OfxStubSpec {
    QString key;   ///< tail of the identifier, e.g. "iris" in "{Svfx:…:iris}"
    QString name;
    QStringList presets;
};

const QVector<OfxStubSpec> &ofxStubSpecs()
{
    static const QVector<OfxStubSpec> specs = {
    // Warp Flow ships only a default preset, so it has none listed here.
    {QStringLiteral("WarpFlowTransition"), QStringLiteral("Warp Flow"), {}},
    {QStringLiteral("crosseffect"), QStringLiteral("Cross Effect"),
     {QStringLiteral("Cross Blur A Only"),
         QStringLiteral("Cross Blur A/B"),
         QStringLiteral("Cross Blur B Only"),
         QStringLiteral("Cross Pixelate A Only"),
         QStringLiteral("Cross Pixelate A/B"),
         QStringLiteral("Cross Pixelate B Only"),
         QStringLiteral("Cross Zoom A Only"),
         QStringLiteral("Cross Zoom A/B"),
         QStringLiteral("Cross Zoom A/B Slow"),
         QStringLiteral("Cross Zoom B Only")}},
    {QStringLiteral("dissolve"), QStringLiteral("Dissolve"),
     {QStringLiteral("Additive Dissolve"),
         QStringLiteral("Color Bleed"),
         QStringLiteral("Color Bleed Fast Alpha"),
         QStringLiteral("Color Bleed Fast Blue"),
         QStringLiteral("Color Bleed Fast Green"),
         QStringLiteral("Color Bleed Fast Red"),
         QStringLiteral("Color Morph"),
         QStringLiteral("Color Morph Fast Alpha"),
         QStringLiteral("Color Morph Fast Blue"),
         QStringLiteral("Color Morph Fast Green"),
         QStringLiteral("Color Morph Fast Red"),
         QStringLiteral("Fade Through Black"),
         QStringLiteral("Fade Through Blue"),
         QStringLiteral("Fade Through Grayscale")}},
    {QStringLiteral("flash"), QStringLiteral("Flash"),
     {QStringLiteral("Hard Flash"),
         QStringLiteral("Soft Flash"),
         QStringLiteral("Yellow Flash")}},
    {QStringLiteral("glTransition"), QStringLiteral("GL Transition"),
     {QStringLiteral("Bounce"),
         QStringLiteral("Bow Tie Horizontal"),
         QStringLiteral("Bow Tie Vertical"),
         QStringLiteral("Burn"),
         QStringLiteral("Butterfly Wave Scrawler"),
         QStringLiteral("Circle Crop"),
         QStringLiteral("Circle Open"),
         QStringLiteral("Color Distance"),
         QStringLiteral("Color Fade"),
         QStringLiteral("Color Phase"),
         QStringLiteral("Color Planes"),
         QStringLiteral("Crazy Parametric Fun"),
         QStringLiteral("Cross Hatch"),
         QStringLiteral("Cross Warp")}},
    {QStringLiteral("pageloop"), QStringLiteral("Page Loop"),
     {QStringLiteral("Bottom-Left, Small Translucent Loop"),
         QStringLiteral("Bottom-Right, Large Opaque Loop"),
         QStringLiteral("Bottom-Right, Small Translucent Loop"),
         QStringLiteral("Left, Small Loop, Yellow Light"),
         QStringLiteral("Top, Large Loop, Red Light"),
         QStringLiteral("Top-Left, Medium Loop"),
         QStringLiteral("Top-Left, Opaque, No Loop"),
         QStringLiteral("Top-Right, Medium Loop")}},
    {QStringLiteral("pagepeel"), QStringLiteral("Page Peel"),
     {QStringLiteral("Bottom-Left, Medium Fold"),
         QStringLiteral("Bottom-Right, Loose Fold, Opaque"),
         QStringLiteral("Bottom-Right, Medium Fold"),
         QStringLiteral("Left, Tight Fold, Translucent, Yellow Light"),
         QStringLiteral("Top, Tight Fold, Opaque, Red Light"),
         QStringLiteral("Top-Left, Medium Fold"),
         QStringLiteral("Top-Left, Slide, Tight Fold, Opaque"),
         QStringLiteral("Top-Right, Medium Fold")}},
    {QStringLiteral("pageroll"), QStringLiteral("Page Roll"),
     {QStringLiteral("Bottom-Left, Medium Curl"),
         QStringLiteral("Bottom-Right, Medium Curl"),
         QStringLiteral("Bottom-Right, Slide, Loose Curl, Opaque"),
         QStringLiteral("Left, Tight Curl, Translucent, Yellow Light"),
         QStringLiteral("Top, Tight Curl, Opaque, Red Light"),
         QStringLiteral("Top-Left, Medium Curl"),
         QStringLiteral("Top-Left, Slide, Tight Curl, Opaque"),
         QStringLiteral("Top-Right, Medium Curl")}},
    {QStringLiteral("push"), QStringLiteral("Push"),
     {QStringLiteral("Push Down"),
         QStringLiteral("Push Down, Blue Border"),
         QStringLiteral("Push In, Down"),
         QStringLiteral("Push In, Left"),
         QStringLiteral("Push In, Left, White Border"),
         QStringLiteral("Push In, Right"),
         QStringLiteral("Push In, Right, Yellow Border"),
         QStringLiteral("Push In, Up"),
         QStringLiteral("Push Left"),
         QStringLiteral("Push Right"),
         QStringLiteral("Push Up"),
         QStringLiteral("Push Up, Red Border")}},
    {QStringLiteral("slide"), QStringLiteral("Slide"),
     {QStringLiteral("Slide In, Bottom-Up"),
         QStringLiteral("Slide In, Left-Right"),
         QStringLiteral("Slide In, Right-Left"),
         QStringLiteral("Slide In, Top-Down"),
         QStringLiteral("Slide In, Top-Left Corner"),
         QStringLiteral("Slide Out, Bottom-Right Corner"),
         QStringLiteral("Slide Out, Bottom-Up"),
         QStringLiteral("Slide Out, Left-Right"),
         QStringLiteral("Slide Out, Right-Left"),
         QStringLiteral("Slide Out, Top-Down")}},
    {QStringLiteral("spiral"), QStringLiteral("Spiral"),
     {QStringLiteral("Spiral In, Down, Counter Clockwise"),
         QStringLiteral("Spiral In, Left, Clockwise"),
         QStringLiteral("Spiral In, Right, Clockwise"),
         QStringLiteral("Spiral In, Up, Counter Clockwise"),
         QStringLiteral("Spiral In, Yellow Border"),
         QStringLiteral("Spiral Out Down, Counter Clockwise"),
         QStringLiteral("Spiral Out, Left, Clockwise"),
         QStringLiteral("Spiral Out, Red Border"),
         QStringLiteral("Spiral Out, Right, Clockwise"),
         QStringLiteral("Spiral Out, Up, Counter Clockwise")}},
    {QStringLiteral("split"), QStringLiteral("Split"),
     {QStringLiteral("Push, In, Center"),
         QStringLiteral("Push, In, Top-Left Corner, White Border"),
         QStringLiteral("Push, Out, Center"),
         QStringLiteral("Push, Out, Top-Right Corner, Blue Border"),
         QStringLiteral("Squeeze, In, Center"),
         QStringLiteral("Squeeze, Out, Center"),
         QStringLiteral("Wipe, In, Center"),
         QStringLiteral("Wipe, Out, Center")}},
    {QStringLiteral("squeeze"), QStringLiteral("Squeeze"),
     {QStringLiteral("Lateral"),
         QStringLiteral("Lateral In, White Border"),
         QStringLiteral("Squeeze Down"),
         QStringLiteral("Squeeze In, Down"),
         QStringLiteral("Squeeze In, Down, Blue Border"),
         QStringLiteral("Squeeze In, Left-Right"),
         QStringLiteral("Squeeze In, Left-Right, Green Border"),
         QStringLiteral("Squeeze In, Right-Left"),
         QStringLiteral("Squeeze In, Up"),
         QStringLiteral("Squeeze Left-Right"),
         QStringLiteral("Squeeze Right-Left"),
         QStringLiteral("Squeeze Up"),
         QStringLiteral("Vertical"),
         QStringLiteral("Vertical In, Red Border")}},
    {QStringLiteral("starwipe"), QStringLiteral("Star Wipe"),
     {QStringLiteral("Corner Sun Rays"),
         QStringLiteral("Corner Tooth"),
         QStringLiteral("Diamond"),
         QStringLiteral("Double Circles"),
         QStringLiteral("Double Gears"),
         QStringLiteral("Double Squares"),
         QStringLiteral("Four Diamonds"),
         QStringLiteral("Four Way Split"),
         QStringLiteral("Gear"),
         QStringLiteral("Hexagon"),
         QStringLiteral("Horizontal Tooth Blinds"),
         QStringLiteral("Opening Eye"),
         QStringLiteral("Rings"),
         QStringLiteral("Six Way Split")}},
    {QStringLiteral("swap"), QStringLiteral("Swap"),
     {QStringLiteral("Swap Down"),
         QStringLiteral("Swap Down, Black Border"),
         QStringLiteral("Swap Left"),
         QStringLiteral("Swap Left, White Border"),
         QStringLiteral("Swap Right"),
         QStringLiteral("Swap Right, Blue Border"),
         QStringLiteral("Swap Up"),
         QStringLiteral("Swap Up, Yellow Border")}},
    {QStringLiteral("zoom"), QStringLiteral("Zoom"),
     {QStringLiteral("Zoom In, Bottom-Left"),
         QStringLiteral("Zoom In, Bottom-Right"),
         QStringLiteral("Zoom In, Bottom-Right, Yellow Border"),
         QStringLiteral("Zoom In, Center"),
         QStringLiteral("Zoom In, Center, White Border"),
         QStringLiteral("Zoom In, Top-Left"),
         QStringLiteral("Zoom In, Top-Right"),
         QStringLiteral("Zoom Out, Bottom-Left"),
         QStringLiteral("Zoom Out, Bottom-Right"),
         QStringLiteral("Zoom Out, Center"),
         QStringLiteral("Zoom Out, Center, Red Border"),
         QStringLiteral("Zoom Out, Top-Left"),
         QStringLiteral("Zoom Out, Top-Left, Blue Border"),
         QStringLiteral("Zoom Out, Top-Right")}},
    };
    return specs;
}

QVector<TransitionPresetInfo> stockPresets(const QString &key)
{
    QVector<TransitionPresetInfo> out;
    const QVector<StockTransitionPreset> *stock = stockPresetsFor(key);
    if (!stock) {
        return out;
    }
    for (const StockTransitionPreset &sp : *stock) {
        TransitionPresetInfo info;
        info.name = sp.name;
        for (const auto &pair : sp.params) {
            info.params.insert(pair.first, pair.second);
        }
        out.push_back(info);
    }
    return out;
}

TransitionPluginInfo makePush()
{
    TransitionPluginInfo info;
    info.id = transitionPushId();
    info.name = QStringLiteral("Push");
    info.format = QStringLiteral("OFX");
    info.description = QStringLiteral("VEGAS Push");
    info.params = {
        {QStringLiteral("direction"), QStringLiteral("Direction"), 0.0, 3.0, 0,
         {QStringLiteral("Up"), QStringLiteral("Down"), QStringLiteral("Left"),
          QStringLiteral("Right")}},
        {QStringLiteral("pushOffPreviousImage"), QStringLiteral("Push off previous image"), 0.0,
         1.0, 0, {QStringLiteral("Off"), QStringLiteral("On")}},
    };
    info.params += borderParams(QStringLiteral("borderFeather"),
                                QStringLiteral("Border feather"));
    info.presets = stockPresets(QStringLiteral("push"));
    return info;
}

TransitionPluginInfo makeSlide()
{
    TransitionPluginInfo info;
    info.id = transitionSlideId();
    info.name = QStringLiteral("Slide");
    info.format = QStringLiteral("OFX");
    info.description = QStringLiteral("VEGAS Slide");
    info.params = {
        {QStringLiteral("angle"), QStringLiteral("Angle"), 0.0, 360.0, 1, {}},
        {QStringLiteral("direction"), QStringLiteral("Direction"), 0.0, 1.0, 0,
         {QStringLiteral("In"), QStringLiteral("Out")}},
    };
    info.params += borderParams(QStringLiteral("borderFeather"),
                                QStringLiteral("Border feather"));
    info.presets = stockPresets(QStringLiteral("slide"));
    return info;
}

TransitionPluginInfo makeSqueeze()
{
    TransitionPluginInfo info;
    info.id = transitionSqueezeId();
    info.name = QStringLiteral("Squeeze");
    info.format = QStringLiteral("OFX");
    info.description = QStringLiteral("VEGAS Squeeze");
    info.params = {
        {QStringLiteral("start"), QStringLiteral("Start"), 0.0, 5.0, 0,
         {QStringLiteral("Down"), QStringLiteral("Up"), QStringLiteral("Left-Right"),
          QStringLiteral("Right-Left"), QStringLiteral("Vertical"), QStringLiteral("Lateral")}},
        {QStringLiteral("squeezePreviousImage"), QStringLiteral("Squeeze previous image"), 0.0,
         1.0, 0, {QStringLiteral("Off"), QStringLiteral("On")}},
    };
    info.params += borderParams(QStringLiteral("borderFeather"),
                                QStringLiteral("Border feather"));
    info.presets = stockPresets(QStringLiteral("squeeze"));
    return info;
}

TransitionPluginInfo makeSplit()
{
    TransitionPluginInfo info;
    info.id = transitionSplitId();
    info.name = QStringLiteral("Split");
    info.format = QStringLiteral("OFX");
    info.description = QStringLiteral("VEGAS Split");
    // "Split mode" and its three choices are the plug-in own labels, out of its binary.
    info.params = {
        {QStringLiteral("splitMode"), QStringLiteral("Split mode"), 0.0, 2.0, 0,
         {QStringLiteral("Push"), QStringLiteral("Wipe"), QStringLiteral("Squeeze")}},
        {QStringLiteral("direction"), QStringLiteral("Direction"), 0.0, 1.0, 0,
         {QStringLiteral("In"), QStringLiteral("Out")}},
        {QStringLiteral("centerX"), QStringLiteral("Center X"), 0.0, 1.0, 4, {}},
        {QStringLiteral("centerY"), QStringLiteral("Center Y"), 0.0, 1.0, 4, {}},
    };
    info.params += borderParams(QStringLiteral("borderFeather"),
                                QStringLiteral("Border feather"));
    info.presets = stockPresets(QStringLiteral("split"));
    return info;
}

TransitionPluginInfo makeFlash()
{
    TransitionPluginInfo info;
    info.id = transitionFlashId();
    info.name = QStringLiteral("Flash");
    info.format = QStringLiteral("OFX");
    info.description = QStringLiteral("VEGAS Flash");
    // Flash has no border: the burst covers the whole frame, so there is no seam.
    info.params = {
        {QStringLiteral("horizontalDiffusion"), QStringLiteral("Horizontal diffusion"), 0.0, 1.0,
         3, {}},
        {QStringLiteral("verticalDiffusion"), QStringLiteral("Vertical diffusion"), 0.0, 1.0, 3,
         {}},
        {QStringLiteral("tintRed"), QStringLiteral("Tint red"), 0.0, 1.0, 3, {}},
        {QStringLiteral("tintGreen"), QStringLiteral("Tint green"), 0.0, 1.0, 3, {}},
        {QStringLiteral("tintBlue"), QStringLiteral("Tint blue"), 0.0, 1.0, 3, {}},
    };
    info.presets = stockPresets(QStringLiteral("flash"));
    return info;
}

TransitionPluginInfo makeStarWipe()
{
    TransitionPluginInfo info;
    info.id = transitionStarWipeId();
    info.name = QStringLiteral("Star Wipe");
    info.format = QStringLiteral("OFX");
    info.description = QStringLiteral("VEGAS Star Wipe");
    info.params = {
        {QStringLiteral("arms"), QStringLiteral("Arms"), 2.0, 32.0, 0, {}},
        {QStringLiteral("ratio"), QStringLiteral("Ratio"), 0.0, 1.0, 4, {}},
        {QStringLiteral("angle"), QStringLiteral("Angle"), 0.0, 360.0, 1, {}},
        {QStringLiteral("cycles"), QStringLiteral("Cycles"), 1.0, 16.0, 0, {}},
        {QStringLiteral("waveform"), QStringLiteral("Waveform"), 0.0, 1.0, 0,
         {QStringLiteral("Pointed"), QStringLiteral("Rounded")}},
        {QStringLiteral("direction"), QStringLiteral("Direction"), 0.0, 1.0, 0,
         {QStringLiteral("In"), QStringLiteral("Out")}},
        {QStringLiteral("centerX"), QStringLiteral("Center X"), 0.0, 1.0, 4, {}},
        {QStringLiteral("centerY"), QStringLiteral("Center Y"), 0.0, 1.0, 4, {}},
        {QStringLiteral("horizontalMirror"), QStringLiteral("Horizontal mirror"), 0.0, 1.0, 0,
         {QStringLiteral("Off"), QStringLiteral("On")}},
        {QStringLiteral("verticalMirror"), QStringLiteral("Vertical mirror"), 0.0, 1.0, 0,
         {QStringLiteral("Off"), QStringLiteral("On")}},
        {QStringLiteral("horizontalFlip"), QStringLiteral("Horizontal flip"), 0.0, 1.0, 0,
         {QStringLiteral("Off"), QStringLiteral("On")}},
        {QStringLiteral("verticalFlip"), QStringLiteral("Vertical flip"), 0.0, 1.0, 0,
         {QStringLiteral("Off"), QStringLiteral("On")}},
    };
    info.params += borderParams(QStringLiteral("feather"), QStringLiteral("Feather"));
    info.presets = stockPresets(QStringLiteral("starwipe"));
    return info;
}

TransitionPluginInfo makeSwap()
{
    TransitionPluginInfo info;
    info.id = transitionSwapId();
    info.name = QStringLiteral("Swap");
    info.format = QStringLiteral("OFX");
    info.description = QStringLiteral("VEGAS Swap");
    info.params = {
        {QStringLiteral("direction"), QStringLiteral("Direction"), 0.0, 3.0, 0,
         {QStringLiteral("Up"), QStringLiteral("Down"), QStringLiteral("Left"),
          QStringLiteral("Right")}},
    };
    info.params += borderParams(QStringLiteral("borderFeather"),
                                QStringLiteral("Border feather"));
    info.presets = stockPresets(QStringLiteral("swap"));
    return info;
}

TransitionPluginInfo makeSpiral()
{
    TransitionPluginInfo info;
    info.id = transitionSpiralId();
    info.name = QStringLiteral("Spiral");
    info.format = QStringLiteral("OFX");
    info.description = QStringLiteral("VEGAS Spiral");
    info.params = {
        {QStringLiteral("turns"), QStringLiteral("Turns"), 0.1, 8.0, 2, {}},
        {QStringLiteral("zoom"), QStringLiteral("Zoom"), 1.0, 200.0, 1, {}},
        {QStringLiteral("orientation"), QStringLiteral("Orientation"), 0.0, 3.0, 0,
         {QStringLiteral("Left"), QStringLiteral("Up"), QStringLiteral("Right"),
          QStringLiteral("Down")}},
        {QStringLiteral("motion"), QStringLiteral("Motion"), 0.0, 1.0, 0,
         {QStringLiteral("Clockwise"), QStringLiteral("Counter clockwise")}},
        {QStringLiteral("direction"), QStringLiteral("Direction"), 0.0, 1.0, 0,
         {QStringLiteral("In"), QStringLiteral("Out")}},
    };
    info.params += borderParams(QStringLiteral("borderFeather"),
                                QStringLiteral("Border feather"));
    info.presets = stockPresets(QStringLiteral("spiral"));
    return info;
}

TransitionPluginInfo makeDissolve()
{
    TransitionPluginInfo info;
    info.id = transitionDissolveId();
    info.name = QStringLiteral("Dissolve");
    info.format = QStringLiteral("OFX");
    info.description = QStringLiteral("VEGAS Dissolve");
    // Type names come from the preset names: each index is used by presets that say what
    // it is ("Additive Dissolve" is 0, "Fade Through Black" is 8).
    info.params = {
        {QStringLiteral("type"), QStringLiteral("Type"), 0.0, 9.0, 0,
         {QStringLiteral("Additive"), QStringLiteral("Subtractive"),
          QStringLiteral("Subtractive crossfade"), QStringLiteral("Color bleed"),
          QStringLiteral("Color morph"), QStringLiteral("Threshold dissolve"),
          QStringLiteral("Threshold appear"), QStringLiteral("Fade through grayscale"),
          QStringLiteral("Fade through color"), QStringLiteral("RGB crossfade")}},
        {QStringLiteral("colorBleedSpeedRed"), QStringLiteral("Bleed speed red"), 0.0, 1.0, 3, {}},
        {QStringLiteral("colorBleedSpeedGreen"), QStringLiteral("Bleed speed green"), 0.0, 1.0, 3,
         {}},
        {QStringLiteral("colorBleedSpeedBlue"), QStringLiteral("Bleed speed blue"), 0.0, 1.0, 3,
         {}},
        {QStringLiteral("colorBleedSpeedAlpha"), QStringLiteral("Bleed speed alpha"), 0.0, 1.0, 3,
         {}},
        {QStringLiteral("colorMorphSpeedRed"), QStringLiteral("Morph speed red"), 0.0, 1.0, 3, {}},
        {QStringLiteral("colorMorphSpeedGreen"), QStringLiteral("Morph speed green"), 0.0, 1.0, 3,
         {}},
        {QStringLiteral("colorMorphSpeedBlue"), QStringLiteral("Morph speed blue"), 0.0, 1.0, 3,
         {}},
        {QStringLiteral("colorMorphSpeedAlpha"), QStringLiteral("Morph speed alpha"), 0.0, 1.0, 3,
         {}},
        {QStringLiteral("fadeThroughColorRed"), QStringLiteral("Fade color red"), 0.0, 1.0, 3, {}},
        {QStringLiteral("fadeThroughColorGreen"), QStringLiteral("Fade color green"), 0.0, 1.0, 3,
         {}},
        {QStringLiteral("fadeThroughColorBlue"), QStringLiteral("Fade color blue"), 0.0, 1.0, 3,
         {}},
        {QStringLiteral("fadeThroughColorAlpha"), QStringLiteral("Fade color alpha"), 0.0, 1.0, 3,
         {}},
    };
    info.presets = stockPresets(QStringLiteral("dissolve"));
    return info;
}

TransitionPluginInfo makeCrossEffect()
{
    TransitionPluginInfo info;
    info.id = transitionCrossEffectId();
    info.name = QStringLiteral("Cross Effect");
    info.format = QStringLiteral("OFX");
    info.description = QStringLiteral("VEGAS Cross Effect");
    info.params = {
        {QStringLiteral("effectType"), QStringLiteral("Effect"), 1.0, 3.0, 0,
         {QStringLiteral("0"), QStringLiteral("Zoom"), QStringLiteral("Pixelate"),
          QStringLiteral("Blur")}},
        {QStringLiteral("applyTo"), QStringLiteral("Apply to"), 0.0, 2.0, 0,
         {QStringLiteral("Outgoing"), QStringLiteral("Incoming"), QStringLiteral("Both")}},
        {QStringLiteral("fadeRange"), QStringLiteral("Fade range"), 0.0, 1.0, 3, {}},
        {QStringLiteral("effectSettingsMaxZoom"), QStringLiteral("Max zoom"), 1.0, 64.0, 2, {}},
        {QStringLiteral("effectSettingsMaxBlur"), QStringLiteral("Max blur"), 0.0, 8.0, 3, {}},
        {QStringLiteral("effectSettingsMaxX"), QStringLiteral("Max X"), 0.0, 8.0, 3, {}},
        {QStringLiteral("effectSettingsMaxY"), QStringLiteral("Max Y"), 0.0, 8.0, 3, {}},
        {QStringLiteral("effectSettingsSourceX"), QStringLiteral("Source X"), 0.0, 1.0, 4, {}},
        {QStringLiteral("effectSettingsSourceY"), QStringLiteral("Source Y"), 0.0, 1.0, 4, {}},
        {QStringLiteral("effectSettingsDestinationX"), QStringLiteral("Destination X"), 0.0, 1.0,
         4, {}},
        {QStringLiteral("effectSettingsDestinationY"), QStringLiteral("Destination Y"), 0.0, 1.0,
         4, {}},
    };
    info.presets = stockPresets(QStringLiteral("crosseffect"));
    return info;
}

QVector<TransitionPluginInfo> makeOfxStubs()
{
    QVector<TransitionPluginInfo> out;
    // Groups with a renderer of their own are built above; listing them here too would
    // put two entries with the same name in the dock, one of which quietly cross-fades.
    QSet<QString> implemented;
    for (const auto &pair : renderedOfxGroups()) {
        implemented.insert(pair.first);
    }
    for (const OfxStubSpec &spec : ofxStubSpecs()) {
        if (implemented.contains(spec.key)) {
            continue;
        }
        // Names and values now come from PresetPackage.xml — the full shipped set, not
        // the handful a sample project happened to contain. The hand-listed names in
        // ofxStubSpecs() remain only as the fallback for a group the package omits.
        QVector<TransitionPresetInfo> presets = stockPresets(spec.key);
        if (presets.isEmpty()) {
            for (const QString &n : spec.presets) {
                TransitionPresetInfo p;
                p.name = n;
                presets.push_back(p);
            }
        }
        out.push_back(makeStubGroup(transitionOfxId(spec.key), spec.name,
                                    QStringLiteral("OFX"), {}, presets));
    }
    return out;
}

TransitionPluginInfo makeZoom()
{
    TransitionPluginInfo info;
    info.id = transitionZoomId();
    info.name = QStringLiteral("Zoom");
    info.format = QStringLiteral("OFX");
    info.description = QStringLiteral("VEGAS Zoom");
    info.params = {
        {QStringLiteral("direction"), QStringLiteral("Direction"), 0.0, 1.0, 0,
         {QStringLiteral("In"), QStringLiteral("Out")}},
        {QStringLiteral("centerX"), QStringLiteral("Center X"), 0.0, 1.0, 4, {}},
        {QStringLiteral("centerY"), QStringLiteral("Center Y"), 0.0, 1.0, 4, {}},
    };
    info.params += borderParams(QStringLiteral("borderFeather"),
                                QStringLiteral("Border feather"));
    info.presets = stockPresets(QStringLiteral("zoom"));
    return info;
}

/** Per-strip local progress, honouring stagger and the sweep direction. */
double stripProgress(double progress, int index, int count, double stagger, bool reverse)
{
    const double frac = count > 1 ? double(reverse ? (count - 1 - index) : index) / (count - 1) : 0.0;
    // Spread the strip start times over at most 60% of the transition so even a full
    // stagger still lands every strip on "fully B" exactly at progress 1.
    const double spread = std::clamp(stagger, 0.0, 1.0) * 0.6;
    const double start = spread * frac;
    const double span = std::max(1e-6, 1.0 - spread);
    return std::clamp((progress - start) / span, 0.0, 1.0);
}

QImage toArgb(const QImage &img, const QSize &size)
{
    if (img.isNull()) {
        return QImage();
    }
    QImage out = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (out.size() != size) {
        out = out.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    return out;
}

/** Draws one rotating blind. `scale` collapses it about its own centre line. */
void drawStrip(QPainter &p, const QImage &face, const QRectF &stripRect, double scale,
               bool horizontalSplit, double specular)
{
    if (face.isNull() || scale <= 0.001) {
        return;
    }
    QRectF dest = stripRect;
    if (horizontalSplit) {
        const double w = stripRect.width() * scale;
        dest.setX(stripRect.center().x() - w / 2.0);
        dest.setWidth(w);
    } else {
        const double h = stripRect.height() * scale;
        dest.setY(stripRect.center().y() - h / 2.0);
        dest.setHeight(h);
    }
    p.drawImage(dest, face, stripRect);
    if (specular > 0.001) {
        // Edge-on blinds catch the light: brightest as the panel turns away from the
        // viewer, gone when it faces front.
        p.save();
        p.setCompositionMode(QPainter::CompositionMode_Plus);
        const int a = int(std::clamp(specular, 0.0, 1.0) * 255.0);
        p.fillRect(dest, QColor(255, 255, 255, a));
        p.restore();
    }
}

QImage renderBlinds(const QImage &from, const QImage &to, double progress,
                    const TransitionInstance &t, const QSize &size)
{
    QImage out(size, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);

    const QImage a = toArgb(from, size);
    const QImage b = toArgb(to, size);
    const int divisions =
        std::clamp(int(std::lround(transitionParamValue(t, QStringLiteral("divisions")))), 1, 16);
    const int extraSpins =
        std::clamp(int(std::lround(transitionParamValue(t, QStringLiteral("extraSpins")))), 0, 10);
    const double stagger = transitionParamValue(t, QStringLiteral("stagger"));
    const double specular = transitionParamValue(t, QStringLiteral("specularLight"));
    const int direction =
        std::clamp(int(std::lround(transitionParamValue(t, QStringLiteral("direction")))), 0, 3);

    const bool horizontalSplit = direction == DirLeftToRight || direction == DirRightToLeft;
    const bool reverse = direction == DirRightToLeft || direction == DirBottomToTop;

    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const double total = horizontalSplit ? size.width() : size.height();
    for (int i = 0; i < divisions; ++i) {
        const double lo = total * i / divisions;
        const double hi = total * (i + 1) / divisions;
        const QRectF strip = horizontalSplit ? QRectF(lo, 0, hi - lo, size.height())
                                             : QRectF(0, lo, size.width(), hi - lo);

        const double lp = stripProgress(progress, i, divisions, stagger, reverse);
        const double angle = lp * (M_PI + 2.0 * M_PI * extraSpins);
        const double cosA = std::cos(angle);
        // Front face (cos >= 0) still shows the outgoing clip; once the panel passes
        // edge-on it is the incoming clip's back face that faces the viewer.
        const QImage &face = cosA >= 0.0 ? a : b;
        const double sinA = std::sin(angle);
        drawStrip(p, face, strip, std::abs(cosA), horizontalSplit,
                  specular * sinA * sinA * 0.55);
    }
    p.end();
    return out;
}

QImage crossDissolve(const QImage &from, const QImage &to, double progress, const QSize &size)
{
    QImage out(size, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter p(&out);
    const QImage a = toArgb(from, size);
    const QImage b = toArgb(to, size);
    if (!a.isNull()) {
        p.setOpacity(1.0 - progress);
        p.drawImage(0, 0, a);
    }
    if (!b.isNull()) {
        p.setOpacity(progress);
        p.drawImage(0, 0, b);
    }
    p.end();
    return out;
}

/** "A" / "B" test cards Vegas uses for its own preset thumbnails. */
QImage testCard(const QSize &size, const QString &letter, const QColor &top, const QColor &bottom)
{
    QImage img(size, QImage::Format_ARGB32_Premultiplied);
    QPainter p(&img);
    QLinearGradient g(0, 0, 0, size.height());
    g.setColorAt(0.0, top);
    g.setColorAt(1.0, bottom);
    p.fillRect(img.rect(), g);
    QFont f = p.font();
    f.setPointSizeF(std::max(8.0, size.height() * 0.55));
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor(255, 255, 255, 235));
    p.drawText(img.rect(), Qt::AlignCenter, letter);
    p.end();
    return img;
}

} // namespace

const QVector<QPair<QString, QString>> &renderedOfxGroups()
{
    static const QVector<QPair<QString, QString>> groups = {
        {QStringLiteral("linearwipe"), transitionLinearWipeId()},
        {QStringLiteral("barndoor"), transitionBarnDoorId()},
        {QStringLiteral("iris"), transitionIrisId()},
        {QStringLiteral("clockwipe"), transitionClockWipeId()},
        {QStringLiteral("zoom"), transitionZoomId()},
        {QStringLiteral("venetianblinds"), transitionVenetianBlindsId()},
        {QStringLiteral("push"), transitionPushId()},
        {QStringLiteral("slide"), transitionSlideId()},
        {QStringLiteral("squeeze"), transitionSqueezeId()},
        {QStringLiteral("split"), transitionSplitId()},
        {QStringLiteral("flash"), transitionFlashId()},
        {QStringLiteral("starwipe"), transitionStarWipeId()},
        {QStringLiteral("swap"), transitionSwapId()},
        {QStringLiteral("spiral"), transitionSpiralId()},
        {QStringLiteral("dissolve"), transitionDissolveId()},
        {QStringLiteral("crosseffect"), transitionCrossEffectId()},
        {QStringLiteral("3dblinds"), transition3dBlindsId()},
        {QStringLiteral("3dcascade"), transitionCascade3dId()},
        {QStringLiteral("3dshuffle"), transitionShuffle3dId()},
        {QStringLiteral("3dflyinout"), transitionFlyInOut3dId()},
        {QStringLiteral("portals"), transitionPortalsId()},
        {QStringLiteral("pagepeel"), transitionPagePeelId()},
        {QStringLiteral("pageroll"), transitionPageRollId()},
        {QStringLiteral("pageloop"), transitionPageLoopId()},
    };
    return groups;
}

QString transitionIdForOfxPlugin(const QString &svfxId)
{
    // "{Svfx:com.vegascreativesoftware:iris}" -> "iris"
    const int colon = svfxId.lastIndexOf(QLatin1Char(':'));
    if (colon < 0) {
        return {};
    }
    QString key = svfxId.mid(colon + 1);
    if (key.endsWith(QLatin1Char('}'))) {
        key.chop(1);
    }
    if (key.isEmpty()) {
        return {};
    }
    // Groups with a renderer of their own answer to their own ids; everything else
    // resolves to the table-driven stub, which exists so the name and presets survive.
    for (const auto &pair : renderedOfxGroups()) {
        if (pair.first == key) {
            return pair.second;
        }
    }
    const QString id = transitionOfxId(key);
    return transitionPluginById(id) ? id : QString();
}

const QVector<TransitionPluginInfo> &transitionCatalog()
{
    static const QVector<TransitionPluginInfo> catalog = [] {
        QVector<TransitionPluginInfo> c = {
            makeBlinds(),     makeVenetianBlinds(), makeLinearWipe(), makeBarnDoor(),
            makeIris(),       makeClockWipe(),      makeCascade3D(),  makeShuffle3D(),
            makeFlyInOut3D(), makeGradientWipe(),   makePortals(),    makeZoom(),
            makePagePeel(),   makePageRoll(),       makePageLoop(),
            makePush(),       makeSlide(),          makeSqueeze(),    makeSplit(),
            makeFlash(),      makeStarWipe(),       makeSwap(),
            makeSpiral(),     makeDissolve(),       makeCrossEffect()};
        c += makeOfxStubs();
        return c;
    }();
    return catalog;
}

const TransitionPluginInfo *transitionPluginById(const QString &pluginId)
{
    for (const TransitionPluginInfo &info : transitionCatalog()) {
        if (info.id == pluginId) {
            return &info;
        }
    }
    return nullptr;
}

const TransitionPresetInfo *transitionPreset(const QString &pluginId, const QString &presetName)
{
    const TransitionPluginInfo *info = transitionPluginById(pluginId);
    if (!info) {
        return nullptr;
    }
    for (const TransitionPresetInfo &preset : info->presets) {
        if (preset.name.compare(presetName, Qt::CaseInsensitive) == 0) {
            return &preset;
        }
    }
    return nullptr;
}

TransitionInstance makeTransitionInstance(const QString &pluginId, const QString &presetName)
{
    TransitionInstance t;
    const TransitionPluginInfo *info = transitionPluginById(pluginId);
    if (!info) {
        return t;
    }
    t.pluginId = pluginId;
    if (const TransitionPresetInfo *preset = transitionPreset(pluginId, presetName)) {
        t.presetName = preset->name;
        t.params = preset->params;
    } else if (!info->presets.isEmpty()) {
        t.presetName = info->presets.first().name;
        t.params = info->presets.first().params;
    }
    return t;
}

double transitionParamValue(const TransitionInstance &t, const QString &key)
{
    const auto it = t.params.constFind(key);
    if (it != t.params.cend()) {
        return it->toDouble();
    }
    // Missing key: fall back to the group's first preset so a project saved before a
    // parameter existed still renders with a sane value instead of 0.
    if (const TransitionPluginInfo *info = transitionPluginById(t.pluginId)) {
        if (!info->presets.isEmpty()) {
            const auto pit = info->presets.first().params.constFind(key);
            if (pit != info->presets.first().params.cend()) {
                return pit->toDouble();
            }
        }
    }
    return 0.0;
}

void transitionSetParamValue(TransitionInstance *t, const QString &key, double value)
{
    if (!t) {
        return;
    }
    t->params.insert(key, value);
    // Any hand edit takes the instance off its preset, exactly like Vegas showing
    // "(Untitled)" once a preset's slider is touched.
    if (const TransitionPresetInfo *preset = transitionPreset(t->pluginId, t->presetName)) {
        if (preset->params != t->params) {
            t->presetName.clear();
        }
    }
}

QVariantMap transitionToMap(const TransitionInstance &t)
{
    QVariantMap m;
    if (!t.isValid()) {
        return m;
    }
    m.insert(QStringLiteral("pluginId"), t.pluginId);
    m.insert(QStringLiteral("presetName"), t.presetName);
    m.insert(QStringLiteral("params"), t.params);
    return m;
}

TransitionInstance transitionFromMap(const QVariantMap &m)
{
    TransitionInstance t;
    t.pluginId = m.value(QStringLiteral("pluginId")).toString();
    t.presetName = m.value(QStringLiteral("presetName")).toString();
    t.params = m.value(QStringLiteral("params")).toMap();
    return t;
}

/**
 * Venetian Blinds: `count` slats laid across the frame at `angle`, each opening about
 * its own centre line to reveal B. Feather softens the moving edge.
 *
 * Worked on a rotated axis rather than by rotating images: every pixel's position along
 * the blind normal decides which slat it belongs to and how far into that slat it sits,
 * which keeps the slats exact at any angle and costs one pass.
 */
QImage renderVenetianBlinds(const QImage &from, const QImage &to, double progress,
                            const TransitionInstance &t, const QSize &size)
{
    const QImage a = toArgb(from, size);
    const QImage b = toArgb(to, size);
    const int count =
        std::clamp(int(std::lround(transitionParamValue(t, QStringLiteral("count")))), 1, 64);
    const double angleDeg = transitionParamValue(t, QStringLiteral("angle"));
    const double feather = std::clamp(transitionParamValue(t, QStringLiteral("feather")), 0.0, 1.0);

    QImage out(size, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);

    const double rad = angleDeg * 3.14159265358979323846 / 180.0;
    // Normal of the blinds. Angle 0 gives vertical slats (normal along x), 90 gives
    // horizontal ones — matching "Five Vertical Blinds" vs "Seven Horizontal Blinds".
    const double nx = std::cos(rad);
    const double ny = std::sin(rad);
    const double w = size.width();
    const double h = size.height();
    // Extent of the projection over the whole frame, so the slats always span it.
    const double span = std::abs(nx) * w + std::abs(ny) * h;
    const double slat = span / double(count);
    const double origin = -(std::abs(nx) * w + std::abs(ny) * h) * 0.5;

    for (int y = 0; y < size.height(); ++y) {
        const auto *ar = a.isNull() ? nullptr : reinterpret_cast<const QRgb *>(a.constScanLine(y));
        const auto *br = b.isNull() ? nullptr : reinterpret_cast<const QRgb *>(b.constScanLine(y));
        auto *orow = reinterpret_cast<QRgb *>(out.scanLine(y));
        const double cy = double(y) - h * 0.5;
        for (int x = 0; x < size.width(); ++x) {
            const double cx = double(x) - w * 0.5;
            const double d = cx * nx + cy * ny - origin;
            // Position within this slat, 0…1 measured from its centre outwards.
            double local = std::fmod(d, slat) / slat;
            if (local < 0.0) {
                local += 1.0;
            }
            const double fromCentre = std::abs(local - 0.5) * 2.0;
            // The opening grows from the slat's centre; feather blurs its edge instead of
            // leaving the hard step a plain threshold would give.
            const double edge = std::max(1e-4, feather);
            const double mix =
                std::clamp((progress - fromCentre * (1.0 - edge)) / edge, 0.0, 1.0);

            const QRgb pa = ar ? ar[x] : qRgba(0, 0, 0, 0);
            const QRgb pb = br ? br[x] : qRgba(0, 0, 0, 0);
            const auto lerp = [&](int ca, int cb) {
                return int(std::lround(ca + (cb - ca) * mix));
            };
            orow[x] = qRgba(lerp(qRed(pa), qRed(pb)), lerp(qGreen(pa), qGreen(pb)),
                            lerp(qBlue(pa), qBlue(pb)), lerp(qAlpha(pa), qAlpha(pb)));
        }
    }
    return out;
}

/**
 * Shared machinery for the edge-driven transitions (Linear Wipe, Barn Door, Iris,
 * Clock Wipe).
 *
 * Each one is just a different *signed field*: negative where the outgoing clip still
 * shows, positive where the incoming one has taken over, measured in normalised screen
 * units. Feather and the coloured border both fall out of that one number — feather is a
 * soft ramp across zero, the border is a band around it — so the four transitions share
 * their whole edge treatment and differ only in a few lines of geometry.
 */
struct EdgeStyle {
    double feather = 0.0;     ///< width of the soft ramp, normalised
    double borderSize = 0.0;  ///< width of the coloured band, normalised
    QColor borderColor = Qt::black;
    /**
     * Fades the band away as the transition reaches either end. Without it the border
     * sits on screen at progress 0 and 1, where there is no moving edge for it to trace
     * and VEGAS shows nothing.
     */
    double borderStrength = 1.0;
};

double smoothStep(double edge0, double edge1, double x)
{
    if (edge1 <= edge0) {
        return x < edge0 ? 0.0 : 1.0;
    }
    const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

template <typename Field>
QImage blendByField(const QImage &from, const QImage &to, const QSize &size,
                    const EdgeStyle &style, Field field)
{
    const QImage a = toArgb(from, size);
    const QImage b = toArgb(to, size);
    QImage out(size, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);

    const double halfFeather = std::max(1e-4, style.feather * 0.5);
    const double border = std::max(0.0, style.borderSize);
    const int br = style.borderColor.red();
    const int bg = style.borderColor.green();
    const int bb = style.borderColor.blue();

    for (int y = 0; y < size.height(); ++y) {
        const auto *ar = a.isNull() ? nullptr : reinterpret_cast<const QRgb *>(a.constScanLine(y));
        const auto *brow = b.isNull() ? nullptr : reinterpret_cast<const QRgb *>(b.constScanLine(y));
        auto *orow = reinterpret_cast<QRgb *>(out.scanLine(y));
        const double ny = size.height() > 1 ? double(y) / double(size.height() - 1) : 0.0;
        for (int x = 0; x < size.width(); ++x) {
            const double nx = size.width() > 1 ? double(x) / double(size.width() - 1) : 0.0;
            const double s = field(nx, ny);
            const double mix = smoothStep(-halfFeather, halfFeather, s);

            const QRgb pa = ar ? ar[x] : qRgba(0, 0, 0, 0);
            const QRgb pb = brow ? brow[x] : qRgba(0, 0, 0, 0);
            const auto lerp = [&](int ca, int cb, double t) {
                return int(std::lround(ca + (cb - ca) * t));
            };
            int r = lerp(qRed(pa), qRed(pb), mix);
            int g = lerp(qGreen(pa), qGreen(pb), mix);
            int bl = lerp(qBlue(pa), qBlue(pb), mix);
            int al = lerp(qAlpha(pa), qAlpha(pb), mix);

            if (border > 0.0 && style.borderStrength > 0.0) {
                // Band hugging the moving edge, fading out over its own width.
                const double edge =
                    (1.0 - smoothStep(0.0, border, std::abs(s))) * style.borderStrength;
                if (edge > 0.0) {
                    r = lerp(r, br, edge);
                    g = lerp(g, bg, edge);
                    bl = lerp(bl, bb, edge);
                    al = lerp(al, 255, edge);
                }
            }
            orow[x] = qRgba(r, g, bl, al);
        }
    }
    return out;
}

QColor borderColourOf(const TransitionInstance &t)
{
    const auto ch = [&](const char *key) {
        return int(std::lround(std::clamp(transitionParamValue(t, QString::fromLatin1(key)), 0.0,
                                          1.0)
                               * 255.0));
    };
    return QColor(ch("borderRed"), ch("borderGreen"), ch("borderBlue"));
}

EdgeStyle edgeStyleOf(const TransitionInstance &t, const char *featherKey)
{
    EdgeStyle s;
    s.feather = std::clamp(transitionParamValue(t, QString::fromLatin1(featherKey)), 0.0, 1.0);
    const double size = std::clamp(transitionParamValue(t, QStringLiteral("borderSize")), 0.0, 1.0);
    // The shipped "… Black/Yellow/Red Border" presets set BorderSize to 0 and give only
    // BorderFeather, so a band driven by size alone would leave them borderless despite
    // their names. Feather stands in for the missing width there — but it does not add to
    // a width that is set: Split ships 0.1 with a feather of 0.3, and adding them put a
    // quarter of the frame under border.
    s.borderSize = size > 0.0 ? size : s.feather * 0.5;
    s.borderColor = borderColourOf(t);
    return s;
}

// --------------------------------------------------------------------- Linear Wipe

QImage renderLinearWipe(const QImage &from, const QImage &to, double progress,
                        const TransitionInstance &t, const QSize &size)
{
    // Angle names the sweep exactly as the shipped presets do: 0 = Left-Right,
    // 90 = Top-Down, 180 = Right-Left, 270 = Bottom-Up, 45 = the Top-Left diagonal.
    const double deg = transitionParamValue(t, QStringLiteral("angle"));
    const double rad = deg * 3.14159265358979323846 / 180.0;
    const double dx = std::cos(rad);
    const double dy = std::sin(rad);
    EdgeStyle style;
    style.feather = std::clamp(transitionParamValue(t, QStringLiteral("feather")), 0.0, 1.0);

    // Extent of the projection over the frame, so progress 0…1 always sweeps it fully
    // whatever the angle — including the diagonals.
    const double span = std::abs(dx) + std::abs(dy);
    const double threshold = -0.5 * span + progress * (span + style.feather);
    return blendByField(from, to, size, style, [=](double x, double y) {
        return threshold - ((x - 0.5) * dx + (y - 0.5) * dy);
    });
}

// ----------------------------------------------------------------------- Barn Door

QImage renderBarnDoor(const QImage &from, const QImage &to, double progress,
                      const TransitionInstance &t, const QSize &size)
{
    const bool horizontal =
        int(std::lround(transitionParamValue(t, QStringLiteral("orientation")))) == 1;
    // Direction 0 = In (doors close towards the centre), 1 = Out (they open from it).
    const bool out = int(std::lround(transitionParamValue(t, QStringLiteral("direction")))) == 1;
    EdgeStyle style = edgeStyleOf(t, "borderFeather");
    style.borderStrength = smoothStep(0.0, 0.06, progress) * smoothStep(0.0, 0.06, 1.0 - progress);

    return blendByField(from, to, size, style, [=](double x, double y) {
        const double c = std::abs((horizontal ? y : x) - 0.5) * 2.0; // 0 centre … 1 edge
        return out ? progress - c : c - (1.0 - progress);
    });
}

// ---------------------------------------------------------------------------- Iris

QImage renderIris(const QImage &from, const QImage &to, double progress,
                  const TransitionInstance &t, const QSize &size)
{
    // Shape indices are the ones the .veg stores: 0 Circle, 3 Rectangle, 4 Diamond,
    // 5 Square, 8 Triangle Down. The rest fall back to a circle rather than guessing.
    const int shape = int(std::lround(transitionParamValue(t, QStringLiteral("shape"))));
    const bool out = int(std::lround(transitionParamValue(t, QStringLiteral("direction")))) == 1;
    const double cx = transitionParamValue(t, QStringLiteral("centerX"));
    const double cy = transitionParamValue(t, QStringLiteral("centerY"));
    EdgeStyle style = edgeStyleOf(t, "feather");
    style.borderStrength = smoothStep(0.0, 0.06, progress) * smoothStep(0.0, 0.06, 1.0 - progress);

    auto shapeDistance = [shape](double dx, double dy) {
        switch (shape) {
        case 4: // Diamond
            return std::abs(dx) + std::abs(dy);
        case 5: // Square
            return std::max(std::abs(dx), std::abs(dy));
        case 3: // Rectangle — wider than tall, as the preset draws it
            return std::max(std::abs(dx) * 0.75, std::abs(dy));
        case 8: { // Triangle Down
            const double a = dy * 0.5 + 0.5;
            return std::max(std::abs(dx) * 0.9 + a * 0.4, -dy);
        }
        default: // Circle / Oval
            return std::hypot(dx, dy);
        }
    };

    // How far the shape must grow to clear the frame, measured with the shape's own
    // distance rather than a circle's: a diamond reaches |dx|+|dy| at the corners, which
    // is well past the circular radius, and using the wrong one leaves it unfinished at
    // progress 1.
    double reach = 0.0;
    for (const double px : {0.0, 1.0}) {
        for (const double py : {0.0, 1.0}) {
            reach = std::max(reach, shapeDistance(px - cx, py - cy));
        }
    }
    reach += style.borderSize + style.feather;

    const double radius = progress * reach;
    return blendByField(from, to, size, style, [=](double x, double y) {
        const double d = shapeDistance(x - cx, y - cy);
        return out ? radius - d : d - (reach - radius);
    });
}

// ---------------------------------------------------------------------- Clock Wipe

QImage renderClockWipe(const QImage &from, const QImage &to, double progress,
                       const TransitionInstance &t, const QSize &size)
{
    const bool counter =
        int(std::lround(transitionParamValue(t, QStringLiteral("direction")))) == 1;
    // Feather is given in degrees here, not in screen units — the presets use 0 for a
    // hard edge, 30 for a soft one and 220 for the "Blend In" look.
    const double featherDeg =
        std::clamp(transitionParamValue(t, QStringLiteral("featherAngle")), 0.0, 360.0);

    constexpr double kPi = 3.14159265358979323846;
    EdgeStyle style;
    style.feather = std::max(1e-4, featherDeg / 360.0);
    const double sweep = progress * (1.0 + style.feather);

    return blendByField(from, to, size, style, [=](double x, double y) {
        // Angle from twelve o'clock, 0…1 around the dial.
        double a = std::atan2(x - 0.5, 0.5 - y) / (2.0 * kPi);
        if (a < 0.0) {
            a += 1.0;
        }
        if (counter) {
            a = 1.0 - a;
        }
        return sweep - a;
    });
}

/**
 * Zoom: the incoming clip grows out of a point (In) or the outgoing one shrinks into it
 * (Out). The centre is a corner in most of the shipped presets, which is what makes the
 * two directions read differently rather than as one mirrored move.
 */
QImage renderZoom(const QImage &from, const QImage &to, double progress,
                  const TransitionInstance &t, const QSize &size)
{
    const bool out = int(std::lround(transitionParamValue(t, QStringLiteral("direction")))) == 1;
    const double cx = std::clamp(transitionParamValue(t, QStringLiteral("centerX")), 0.0, 1.0);
    // The preset package measures Y from the bottom, the way the plug-in's own control
    // does — "Top-Left" stores (0, 1). Images are top-down, so it flips here.
    const double cy =
        1.0 - std::clamp(transitionParamValue(t, QStringLiteral("centerY")), 0.0, 1.0);
    const double borderSize =
        std::clamp(transitionParamValue(t, QStringLiteral("borderSize")), 0.0, 1.0);
    const double borderFeather =
        std::clamp(transitionParamValue(t, QStringLiteral("borderFeather")), 0.0, 1.0);
    const QColor borderColor = borderColourOf(t);

    const QImage a = toArgb(from, size);
    const QImage b = toArgb(to, size);
    QImage result(size, QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);

    QPainter p(&result);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    // The clip that stays put is the backdrop; the other one scales over it.
    const QImage &still = out ? b : a;
    const QImage &moving = out ? a : b;
    if (!still.isNull()) {
        p.drawImage(0, 0, still);
    }

    // In grows 0 -> 1, Out shrinks 1 -> 0, both about the chosen centre.
    const double scale = out ? (1.0 - progress) : progress;
    if (scale > 0.001 && !moving.isNull()) {
        const double w = size.width() * scale;
        const double h = size.height() * scale;
        const double x = cx * size.width() * (1.0 - scale);
        const double y = cy * size.height() * (1.0 - scale);
        const QRectF target(x, y, w, h);
        p.drawImage(target, moving, QRectF(QPointF(0, 0), QSizeF(size)));

        // Border traces the moving edge, and fades out at both ends where there is none.
        const double band = (borderSize + borderFeather * 0.5) * std::min(w, h);
        if (band > 0.5) {
            const double strength =
                smoothStep(0.0, 0.06, progress) * smoothStep(0.0, 0.06, 1.0 - progress);
            if (strength > 0.0) {
                QColor c = borderColor;
                c.setAlphaF(float(strength));
                QPen pen(c);
                pen.setWidthF(band);
                pen.setJoinStyle(Qt::MiterJoin);
                p.setPen(pen);
                p.setBrush(Qt::NoBrush);
                p.drawRect(target.adjusted(band / 2, band / 2, -band / 2, -band / 2));
            }
        }
    }
    p.end();
    return result;
}

// --------------------------------------------------------------- moving-edge border

/**
 * Border band traced along the edges of an image that moves as a whole.
 *
 * Push, Slide, Squeeze and Split all show the same thing at the seam: a coloured band
 * travelling with the moving picture. It fades in and out at the two ends, where there is
 * no seam to draw and a band would sit on the frame edge looking like a frame.
 */
void strokeMovingEdge(QPainter &p, const QRectF &rect, const EdgeStyle &style, double progress,
                      double refSize)
{
    const double band = style.borderSize * refSize;
    if (band < 0.5) {
        return;
    }
    const double strength =
        smoothStep(0.0, 0.06, progress) * smoothStep(0.0, 0.06, 1.0 - progress);
    if (strength <= 0.0) {
        return;
    }
    QColor c = style.borderColor;
    c.setAlphaF(float(std::clamp(strength, 0.0, 1.0)));
    QPen pen(c);
    pen.setWidthF(band);
    pen.setJoinStyle(Qt::MiterJoin);
    p.save();
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRect(rect.adjusted(band / 2, band / 2, -band / 2, -band / 2));
    p.restore();
}

// ----------------------------------------------------------------------------- Push

/**
 * Push: the incoming clip drives in from one side. With "Push off previous image" the
 * outgoing clip is shoved out ahead of it — the two move together, which is the classic
 * push; without it only the new clip moves and the old one stays put ("Push In, ...").
 *
 * Direction is the plug-in own numbering, read off its preset names: 0 Up, 1 Down,
 * 2 Left, 3 Right, naming the way the picture travels.
 */
QImage renderPush(const QImage &from, const QImage &to, double progress,
                  const TransitionInstance &t, const QSize &size)
{
    const int dir = int(std::lround(transitionParamValue(t, QStringLiteral("direction"))));
    const bool pushOff = transitionParamValue(t, QStringLiteral("pushOffPreviousImage")) >= 0.5;
    const EdgeStyle style = edgeStyleOf(t, "borderFeather");

    // Where the picture travels, in frame widths/heights.
    double ux = 0.0;
    double uy = 0.0;
    switch (dir) {
    case 0: uy = -1.0; break; // Up
    case 1: uy = 1.0; break;  // Down
    case 2: ux = -1.0; break; // Left
    default: ux = 1.0; break; // Right
    }

    const QImage a = toArgb(from, size);
    const QImage b = toArgb(to, size);
    QImage result(size, QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);

    QPainter p(&result);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const double w = size.width();
    const double h = size.height();
    if (!a.isNull()) {
        const double ax = pushOff ? ux * w * progress : 0.0;
        const double ay = pushOff ? uy * h * progress : 0.0;
        p.drawImage(QRectF(ax, ay, w, h), a, QRectF(0, 0, w, h));
    }
    if (!b.isNull()) {
        // B starts one frame back along the travel and ends flush.
        const QRectF target(ux * w * (progress - 1.0), uy * h * (progress - 1.0), w, h);
        p.drawImage(target, b, QRectF(0, 0, w, h));
        strokeMovingEdge(p, target, style, progress, std::min(w, h));
    }
    p.end();
    return result;
}

// ---------------------------------------------------------------------------- Slide

/**
 * Slide: one clip slides over the other, which stays where it is.
 *
 * Angle names the travel the same way Linear Wipe does — 0 Left-Right, 90 Top-Down,
 * 180 Right-Left, 270 Bottom-Up, 45 the top-left diagonal. Direction 0 slides the new
 * clip in; 1 slides the old one out, and its presets pair each angle with the opposite
 * name ("Slide Out, Left-Right" stores 180), so Out travels against the angle.
 */
QImage renderSlide(const QImage &from, const QImage &to, double progress,
                   const TransitionInstance &t, const QSize &size)
{
    constexpr double kPi = 3.14159265358979323846;
    const double rad = transitionParamValue(t, QStringLiteral("angle")) * kPi / 180.0;
    const bool out = int(std::lround(transitionParamValue(t, QStringLiteral("direction")))) == 1;
    const EdgeStyle style = edgeStyleOf(t, "borderFeather");
    const double ux = std::cos(rad);
    const double uy = std::sin(rad);

    const QImage a = toArgb(from, size);
    const QImage b = toArgb(to, size);
    QImage result(size, QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);

    QPainter p(&result);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const double w = size.width();
    const double h = size.height();
    // The still one is the backdrop; the other travels over it.
    const QImage &still = out ? b : a;
    const QImage &moving = out ? a : b;
    if (!still.isNull()) {
        p.drawImage(0, 0, still);
    }
    if (!moving.isNull()) {
        // In: B arrives from off-frame along +u. Out: A leaves along -u.
        const double k = out ? -progress : (progress - 1.0);
        const QRectF target(ux * w * k, uy * h * k, w, h);
        p.drawImage(target, moving, QRectF(0, 0, w, h));
        strokeMovingEdge(p, target, style, progress, std::min(w, h));
    }
    p.end();
    return result;
}

// -------------------------------------------------------------------------- Squeeze

/**
 * Squeeze: one clip is scaled away along an axis while the other is revealed.
 *
 * Start is the plug-in own numbering, again read off the preset names: 0 Down, 1 Up,
 * 2 Left-Right, 3 Right-Left, 4 Vertical, 5 Lateral. The first four name the direction
 * the squeeze travels and pin the far edge; the last two squeeze about the centre line
 * from both sides at once. "Squeeze previous image" chooses which clip moves — the old
 * one collapsing away, or the new one opening out.
 */
QImage renderSqueeze(const QImage &from, const QImage &to, double progress,
                     const TransitionInstance &t, const QSize &size)
{
    const int start = int(std::lround(transitionParamValue(t, QStringLiteral("start"))));
    const bool squeezeOld = transitionParamValue(t, QStringLiteral("squeezePreviousImage")) >= 0.5;
    const EdgeStyle style = edgeStyleOf(t, "borderFeather");

    const QImage a = toArgb(from, size);
    const QImage b = toArgb(to, size);
    QImage result(size, QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);

    const double w = size.width();
    const double h = size.height();
    // The moving clip extent: the old one shrinks 1 -> 0, the new one grows 0 -> 1.
    const double extent = squeezeOld ? (1.0 - progress) : progress;

    QPainter p(&result);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const QImage &still = squeezeOld ? b : a;
    const QImage &moving = squeezeOld ? a : b;
    if (!still.isNull()) {
        p.drawImage(0, 0, still);
    }
    if (moving.isNull() || extent <= 0.001) {
        p.end();
        return result;
    }

    // Which axis the extent runs along, and which way the squeeze travels.
    bool vertical = true;
    bool positive = true;
    bool centred = false;
    switch (start) {
    case 0: vertical = true;  positive = true;  break; // Down
    case 1: vertical = true;  positive = false; break; // Up
    case 2: vertical = false; positive = true;  break; // Left-Right
    case 3: vertical = false; positive = false; break; // Right-Left
    case 4: vertical = true;  centred = true;   break; // Vertical
    default: vertical = false; centred = true;  break; // Lateral
    }

    // The anchor is the edge that stays put, and it differs between the two cases so the
    // motion reads the same in both. Squeezing the old clip away, the edge it travels
    // towards is pinned and the trailing one chases it; opening the new clip out, the
    // edge it starts from is pinned and the leading one runs ahead. Anchoring both the
    // same way would send "Squeeze In, Down" upwards.
    const double full = vertical ? h : w;
    const double span = full * extent;
    double offset = 0.0;
    if (centred) {
        offset = (full - span) / 2.0;
    } else if (squeezeOld == positive) {
        offset = full - span;
    }
    const QRectF target = vertical ? QRectF(0, offset, w, span) : QRectF(offset, 0, span, h);
    p.drawImage(target, moving, QRectF(0, 0, w, h));
    strokeMovingEdge(p, target, style, progress, std::min(w, h));
    p.end();
    return result;
}

// ---------------------------------------------------------------------------- Split

/**
 * Split: the frame is cut at a point and the pieces move apart (Out) or close in (In).
 *
 * The cut is made on both axes, giving four quadrants. That the plug-in stores Center as
 * a 2D point is the evidence for it: a split into two halves would use one coordinate and
 * leave the other doing nothing, and the shipped presets do set both ("Top-Left Corner"
 * is 0.06, 0.94). With the centre on an edge this degenerates to two halves on its own.
 * The plug-in carries no orientation parameter — its binary lists only the three Split
 * mode choices — so nothing VEGAS ships settles the axis question further.
 *
 * Split mode says what the pieces do: Push slides them out whole, Wipe uncovers them
 * without moving them, Squeeze scales them away.
 */
QImage renderSplit(const QImage &from, const QImage &to, double progress,
                   const TransitionInstance &t, const QSize &size)
{
    const int mode = int(std::lround(transitionParamValue(t, QStringLiteral("splitMode"))));
    const bool out = int(std::lround(transitionParamValue(t, QStringLiteral("direction")))) == 1;
    const double cx = std::clamp(transitionParamValue(t, QStringLiteral("centerX")), 0.0, 1.0);
    // The preset package measures Y from the bottom, as the plug-in own control does.
    const double cy =
        1.0 - std::clamp(transitionParamValue(t, QStringLiteral("centerY")), 0.0, 1.0);
    const EdgeStyle style = edgeStyleOf(t, "borderFeather");

    const QImage a = toArgb(from, size);
    const QImage b = toArgb(to, size);
    QImage result(size, QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);

    const double w = size.width();
    const double h = size.height();
    // Out: the old clip pieces leave over the new one. In: the new clip pieces arrive
    // over the old one, so the same geometry runs backwards.
    const double travel = out ? progress : (1.0 - progress);
    const QImage &still = out ? b : a;
    const QImage &moving = out ? a : b;

    QPainter p(&result);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    if (!still.isNull()) {
        p.drawImage(0, 0, still);
    }
    if (moving.isNull()) {
        p.end();
        return result;
    }

    const double mx = cx * w;
    const double my = cy * h;
    // The four pieces, each with the direction it leaves in.
    const struct {
        QRectF src;
        double ux;
        double uy;
    } quads[4] = {
        {QRectF(0, 0, mx, my), -1.0, -1.0},
        {QRectF(mx, 0, w - mx, my), 1.0, -1.0},
        {QRectF(0, my, mx, h - my), -1.0, 1.0},
        {QRectF(mx, my, w - mx, h - my), 1.0, 1.0},
    };
    for (const auto &q : quads) {
        if (q.src.width() < 0.5 || q.src.height() < 0.5) {
            continue;
        }
        QRectF dest = q.src;
        QRectF src = q.src;
        switch (mode) {
        case 1: { // Wipe — the piece stays put and is eaten away from the cut outwards
            const double kw = q.src.width() * (1.0 - travel);
            const double kh = q.src.height() * (1.0 - travel);
            dest.setX(q.ux < 0 ? q.src.x() : q.src.right() - kw);
            dest.setWidth(kw);
            dest.setY(q.uy < 0 ? q.src.y() : q.src.bottom() - kh);
            dest.setHeight(kh);
            src = dest; // uncovering, so the source keeps its place
            break;
        }
        case 2: // Squeeze — scaled away towards its own outer corner
            dest.setWidth(q.src.width() * (1.0 - travel));
            dest.setHeight(q.src.height() * (1.0 - travel));
            if (q.ux < 0) {
                dest.moveLeft(q.src.x());
            } else {
                dest.moveRight(q.src.right());
            }
            if (q.uy < 0) {
                dest.moveTop(q.src.y());
            } else {
                dest.moveBottom(q.src.bottom());
            }
            break;
        default: // 0 Push — slides out whole
            dest.translate(q.ux * q.src.width() * travel, q.uy * q.src.height() * travel);
            break;
        }
        if (dest.width() < 0.5 || dest.height() < 0.5) {
            continue;
        }
        p.drawImage(dest, moving, src);
        strokeMovingEdge(p, dest, style, progress, std::min(w, h));
    }
    p.end();
    return result;
}

// ---------------------------------------------------------------------------- Flash

/** Separable box blur, radius in pixels per axis. Cheap, and enough for a flash. */
QImage boxBlur(const QImage &src, int rx, int ry)
{
    if (rx < 1 && ry < 1) {
        return src;
    }
    const auto pass = [](const QImage &in, QImage &dst, int radius, bool horizontal) {
        const int w = in.width();
        const int h = in.height();
        const int n = radius * 2 + 1;
        for (int y = 0; y < h; ++y) {
            auto *drow = reinterpret_cast<QRgb *>(dst.scanLine(y));
            for (int x = 0; x < w; ++x) {
                int r = 0;
                int g = 0;
                int b = 0;
                int a = 0;
                for (int k = -radius; k <= radius; ++k) {
                    const int sx = horizontal ? std::clamp(x + k, 0, w - 1) : x;
                    const int sy = horizontal ? y : std::clamp(y + k, 0, h - 1);
                    const QRgb px = reinterpret_cast<const QRgb *>(in.constScanLine(sy))[sx];
                    r += qRed(px);
                    g += qGreen(px);
                    b += qBlue(px);
                    a += qAlpha(px);
                }
                drow[x] = qRgba(r / n, g / n, b / n, a / n);
            }
        }
    };
    QImage out = src;
    if (rx >= 1) {
        pass(src, out, rx, true);
    }
    if (ry >= 1) {
        const QImage mid = out;
        pass(mid, out, ry, false);
    }
    return out;
}

/**
 * Flash: a burst of colour covers the cut between the two clips.
 *
 * The two diffusion amounts blur the picture under the burst, per axis — that is what
 * separates "Hard Flash" (no diffusion, a clean strobe) from "Soft Flash" and the yellow
 * one, which smear as they peak. The cut itself happens at the peak, where the tint is at
 * full strength and hides it.
 */
QImage renderFlash(const QImage &from, const QImage &to, double progress,
                   const TransitionInstance &t, const QSize &size)
{
    constexpr double kPi = 3.14159265358979323846;
    const double hDiff =
        std::clamp(transitionParamValue(t, QStringLiteral("horizontalDiffusion")), 0.0, 1.0);
    const double vDiff =
        std::clamp(transitionParamValue(t, QStringLiteral("verticalDiffusion")), 0.0, 1.0);
    const auto ch = [&](const char *key) {
        return int(std::lround(
            std::clamp(transitionParamValue(t, QString::fromLatin1(key)), 0.0, 1.0) * 255.0));
    };
    const QColor tint(ch("tintRed"), ch("tintGreen"), ch("tintBlue"));

    const double strength = std::sin(kPi * std::clamp(progress, 0.0, 1.0));
    QImage base = toArgb(progress < 0.5 ? from : to, size);
    if (base.isNull()) {
        base = QImage(size, QImage::Format_ARGB32_Premultiplied);
        base.fill(Qt::black);
    }

    // Diffusion is a fraction of the frame; a tenth of it at full strength reads as a
    // smear without turning the picture to soup.
    const int rx = int(std::lround(hDiff * strength * size.width() * 0.1));
    const int ry = int(std::lround(vDiff * strength * size.height() * 0.1));
    QImage result = boxBlur(base, rx, ry);

    QPainter p(&result);
    QColor c = tint;
    // The tint peaks harder than the blur does, so the diffused picture is still visible
    // on the way in and out. Sharing one curve made "Soft Flash" indistinguishable from
    // the hard one: wherever the blur was wide the tint had already covered everything.
    c.setAlphaF(float(strength * strength));
    p.fillRect(result.rect(), c);
    p.end();
    return result;
}

// ------------------------------------------------------------------------- Star Wipe

/**
 * Star Wipe: a shape with `arms` points opens out of a centre, or closes into it.
 *
 * Every field here is named by VEGAS's own preset package, and its eighteen presets are
 * enough to read all of them:
 *
 *   Arms      how many points. 32 with Ratio 1 is a circle ("Double Circles"), 32 with a
 *             small ratio is a burst of spikes ("Sun Rays"), 2 is a diamond.
 *   Ratio     inner radius over outer. 1 flattens the star into a circle.
 *   Waveform  how a point is shaped: 0 comes to a tip, 1 is rounded. "Gear" and
 *             "Double Gears" differ by this and nothing else.
 *   Cycles    concentric repeats. "Rings" is a circle with five of them.
 *   Angle     turns the shape; "Three/Four/Six Way Split" are stars rotated to sit square.
 *   Center    where it opens from, measured with Y running up as the package always does.
 *   Horizontal/VerticalMirror  reflect the field about the frame's middle, which is how
 *             "Four Diamonds" gets four of them and how "Opening Eye" gets its two lids.
 *   Direction 0 opens, 1 closes.
 */
QImage renderStarWipe(const QImage &from, const QImage &to, double progress,
                      const TransitionInstance &t, const QSize &size)
{
    constexpr double kPi = 3.14159265358979323846;
    const double arms = std::max(2.0, transitionParamValue(t, QStringLiteral("arms")));
    const double ratio = std::clamp(transitionParamValue(t, QStringLiteral("ratio")), 0.0, 1.0);
    const double angle = transitionParamValue(t, QStringLiteral("angle")) * kPi / 180.0;
    const double cycles = std::max(1.0, transitionParamValue(t, QStringLiteral("cycles")));
    const bool rounded =
        int(std::lround(transitionParamValue(t, QStringLiteral("waveform")))) == 1;
    const bool out = int(std::lround(transitionParamValue(t, QStringLiteral("direction")))) == 1;
    const bool hMirror = transitionParamValue(t, QStringLiteral("horizontalMirror")) >= 0.5;
    const bool vMirror = transitionParamValue(t, QStringLiteral("verticalMirror")) >= 0.5;
    const double cx = std::clamp(transitionParamValue(t, QStringLiteral("centerX")), 0.0, 1.0);
    // The package measures Y from the bottom, as it does everywhere else.
    const double cy =
        1.0 - std::clamp(transitionParamValue(t, QStringLiteral("centerY")), 0.0, 1.0);

    EdgeStyle style = edgeStyleOf(t, "feather");
    style.borderStrength = smoothStep(0.0, 0.06, progress) * smoothStep(0.0, 0.06, 1.0 - progress);
    // Even a hard-edged preset needs a sliver of overshoot to land exactly on B.
    const double feather = std::max(1e-3, style.feather);
    const double halfFeather = feather * 0.5;

    // A mirror folds the frame onto the half that holds the centre, so whatever is drawn
    // there appears again reflected.
    const auto fold = [](double v, bool mirror) {
        return mirror ? 0.5 - std::abs(v - 0.5) : v;
    };
    const double fcx = fold(cx, hMirror);
    const double fcy = fold(cy, vMirror);

    // Plain distance from the centre to the farthest corner: the shape is normalised
    // against this, not against itself. Measuring the reach with the shape's own distance
    // does not work here, because a star with Ratio 0 has zero radius between its arms
    // and the reach runs away to infinity — which collapsed Sun Rays and the three Way
    // Split presets into an instant cut.
    double reachR = 1e-6;
    for (const double px : {0.0, 0.5, 1.0}) {
        for (const double py : {0.0, 0.5, 1.0}) {
            reachR = std::max(reachR, std::hypot(fold(px, hMirror) - fcx,
                                                 fold(py, vMirror) - fcy));
        }
    }

    // How far the edge sits in a given direction, as a fraction of the outer radius.
    // Ratio 0 means the arms meet at a point, so the trough would have no extent at all
    // and could never close; a small floor keeps those presets thin and still lets the
    // wipe finish.
    const auto edgeAt = [=](double dx, double dy) {
        const double phase = (std::atan2(dy, dx) + angle) * arms / (2.0 * kPi);
        double w = phase - std::floor(phase); // 0…1 within one arm
        // 0…1…0 across the arm, either pointed or rounded.
        w = rounded ? 0.5 - 0.5 * std::cos(2.0 * kPi * w) : 1.0 - std::abs(2.0 * w - 1.0);
        return std::max(0.06, ratio + (1.0 - ratio) * w);
    };

    return blendByField(from, to, size, style, [=](double x, double y) {
        const double px = fold(x, hMirror);
        const double py = fold(y, vMirror);
        const double dx = px - fcx;
        const double dy = py - fcy;
        const double r = std::hypot(dx, dy);
        const double d = r / (edgeAt(dx, dy) * reachR);
        // Cycles cuts the run into concentric bands that open together. The distance has
        // to be clamped first: past the frame it climbs to many times one — the arms of a
        // Ratio 0 star are thin, so the troughs are far "outside" — and wrapping that
        // unbounded value produced a dozen phantom rings instead of the asked-for few.
        const double dn = std::min(1.0, d);
        const double v = dn * cycles;
        const double frac = dn >= 1.0 ? 1.0 : v - std::floor(v);
        // The sweep runs half a feather short of the start and half past the end, the
        // same way Linear Wipe does it. Driving it from 0 to 1 exactly leaves the far
        // corners sitting on the threshold at progress 1 — half blended rather than
        // finished — and with a Ratio 0 star that was most of the frame.
        const double sweep = -halfFeather + progress * (1.0 + feather);
        return out ? frac - (1.0 - sweep) : sweep - frac;
    });
}

// ----------------------------------------------------------------------------- Swap

/**
 * Swap: the two clips trade places, one passing in front of the other.
 *
 * Direction is numbered as Push's is, and its presets name it the same way: 0 Up, 1 Down,
 * 2 Left, 3 Right. The outgoing clip travels that way and the incoming one comes back
 * along the opposite side, so they cross in the middle — which is where the one in front
 * has to be decided, and VEGAS puts the arriving clip there.
 */
QImage renderSwap(const QImage &from, const QImage &to, double progress,
                  const TransitionInstance &t, const QSize &size)
{
    const int dir = int(std::lround(transitionParamValue(t, QStringLiteral("direction"))));
    const EdgeStyle style = edgeStyleOf(t, "borderFeather");

    double ux = 0.0;
    double uy = 0.0;
    switch (dir) {
    case 0: uy = -1.0; break;
    case 1: uy = 1.0; break;
    case 2: ux = -1.0; break;
    default: ux = 1.0; break;
    }

    const QImage a = toArgb(from, size);
    const QImage b = toArgb(to, size);
    QImage result(size, QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);

    QPainter p(&result);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const double w = size.width();
    const double h = size.height();

    // Both travel the full frame; the outgoing one shrinks a little on its way past, which
    // is what makes one read as passing behind the other rather than as a plain slide.
    const double back = 1.0 - 0.25 * std::sin(progress * 3.14159265358979323846);
    if (!a.isNull()) {
        const double cx = w * 0.5 + ux * w * progress;
        const double cy = h * 0.5 + uy * h * progress;
        const QRectF target(cx - w * back * 0.5, cy - h * back * 0.5, w * back, h * back);
        p.drawImage(target, a, QRectF(0, 0, w, h));
    }
    if (!b.isNull()) {
        const QRectF target(-ux * w * (1.0 - progress), -uy * h * (1.0 - progress), w, h);
        p.drawImage(target, b, QRectF(0, 0, w, h));
        strokeMovingEdge(p, target, style, progress, std::min(w, h));
    }
    p.end();
    return result;
}

// --------------------------------------------------------------------------- Spiral

/**
 * Spiral: the wipe edge winds out of the centre, or into it.
 *
 * Turns says how many times round, Zoom how tightly the arm is wound against the radius,
 * Orientation which side it starts from (0 Left, 1 Up, 2 Right, 3 Down, as the preset
 * names spell out), Motion which way it turns and Direction whether it opens or closes.
 */
QImage renderSpiral(const QImage &from, const QImage &to, double progress,
                    const TransitionInstance &t, const QSize &size)
{
    constexpr double kPi = 3.14159265358979323846;
    const double turns = std::max(0.1, transitionParamValue(t, QStringLiteral("turns")));
    const double zoom = std::max(1.0, transitionParamValue(t, QStringLiteral("zoom")));
    const int orientation =
        int(std::lround(transitionParamValue(t, QStringLiteral("orientation"))));
    const bool counter = int(std::lround(transitionParamValue(t, QStringLiteral("motion")))) == 1;
    const bool out = int(std::lround(transitionParamValue(t, QStringLiteral("direction")))) == 1;

    EdgeStyle style = edgeStyleOf(t, "borderFeather");
    style.borderStrength = smoothStep(0.0, 0.06, progress) * smoothStep(0.0, 0.06, 1.0 - progress);
    const double feather = std::max(1e-3, style.feather);
    const double halfFeather = feather * 0.5;

    // Where the arm starts, as a quarter turn per orientation step.
    const double phase = orientation * 0.25;
    // Zoom is a percentage in the presets; at 50 the arm makes about half its winding
    // against the radius, which is what keeps a single turn readable across the frame.
    const double radial = turns * (zoom / 100.0);

    return blendByField(from, to, size, style, [=](double x, double y) {
        const double dx = x - 0.5;
        const double dy = y - 0.5;
        const double r = std::min(1.0, std::hypot(dx, dy) / 0.7071);
        double ang = std::atan2(dy, dx) / (2.0 * kPi); // −0.5…0.5
        if (counter) {
            ang = -ang;
        }
        // The spiral: angle and radius wound together, then taken modulo one turn so the
        // arm repeats instead of running away past the edge of the frame.
        double v = (ang + phase) * turns + r * radial;
        v -= std::floor(v);
        const double d = std::clamp((v + r) * 0.5, 0.0, 1.0);
        const double sweep = -halfFeather + progress * (1.0 + feather);
        return out ? d - (1.0 - sweep) : sweep - d;
    });
}

// -------------------------------------------------------------------------- Dissolve

/**
 * Dissolve: a family of cross-fades, told apart by Type.
 *
 * The preset package names the type indices and the colour to fade through, and its
 * twenty-three presets pin which index is which by name — Additive is 0, Fade Through
 * Colour is 8, and so on. What it does not carry is the curve each one uses, so the
 * behaviours below are the plain reading of their names rather than a recovered formula:
 * additive brightens through the middle, subtractive darkens, threshold switches pixel by
 * pixel as the picture's own brightness is passed, and the colour-speed types let each
 * channel cross at its own rate.
 */
QImage renderDissolve(const QImage &from, const QImage &to, double progress,
                      const TransitionInstance &t, const QSize &size)
{
    const int type = int(std::lround(transitionParamValue(t, QStringLiteral("type"))));
    const auto chan = [&](const char *key) {
        return std::clamp(transitionParamValue(t, QString::fromLatin1(key)), 0.0, 1.0);
    };
    const double fadeR = chan("fadeThroughColorRed");
    const double fadeG = chan("fadeThroughColorGreen");
    const double fadeB = chan("fadeThroughColorBlue");
    const double fadeA = chan("fadeThroughColorAlpha");

    // Speed 1 makes that channel cross in half the time; 0 leaves it at the common rate.
    const auto rate = [&](const char *bleed, const char *morph) {
        const double s = type == 4 ? chan(morph) : chan(bleed);
        return 1.0 + s; // 1 or 2
    };
    const double rR = rate("colorBleedSpeedRed", "colorMorphSpeedRed");
    const double rG = rate("colorBleedSpeedGreen", "colorMorphSpeedGreen");
    const double rB = rate("colorBleedSpeedBlue", "colorMorphSpeedBlue");
    // Alpha has its own rate too. On opaque footage it cannot show — which is why
    // "Color Bleed" and "Color Bleed Fast Alpha" render identically over solid pictures,
    // in VEGAS as much as here — but ignoring it would lose the setting on footage that
    // does carry alpha.
    const double rA = rate("colorBleedSpeedAlpha", "colorMorphSpeedAlpha");

    const QImage a = toArgb(from, size);
    const QImage b = toArgb(to, size);
    QImage out(size, QImage::Format_ARGB32_Premultiplied);

    const double p = std::clamp(progress, 0.0, 1.0);
    const auto sat = [](double v) { return int(std::lround(std::clamp(v, 0.0, 255.0))); };
    const auto ramp = [](double v, double speed) { return std::clamp(v * speed, 0.0, 1.0); };

    for (int y = 0; y < size.height(); ++y) {
        const auto *ar = a.isNull() ? nullptr : reinterpret_cast<const QRgb *>(a.constScanLine(y));
        const auto *br = b.isNull() ? nullptr : reinterpret_cast<const QRgb *>(b.constScanLine(y));
        auto *orow = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < size.width(); ++x) {
            const QRgb pa = ar ? ar[x] : qRgba(0, 0, 0, 255);
            const QRgb pb = br ? br[x] : qRgba(0, 0, 0, 255);
            double r = 0;
            double g = 0;
            double bl = 0;
            double al = 255;

            switch (type) {
            case 0: // Additive — bright through the middle
                if (p < 0.5) {
                    const double k = p * 2.0;
                    r = qRed(pa) + qRed(pb) * k;
                    g = qGreen(pa) + qGreen(pb) * k;
                    bl = qBlue(pa) + qBlue(pb) * k;
                } else {
                    const double k = (1.0 - p) * 2.0;
                    r = qRed(pa) * k + qRed(pb);
                    g = qGreen(pa) * k + qGreen(pb);
                    bl = qBlue(pa) * k + qBlue(pb);
                }
                break;
            case 1: { // Subtractive — the two pictures darken each other as they meet
                // Not a fade to black: that is what "Fade Through Black" is, and writing
                // it here made the two presets pixel-identical. This is the subtractive
                // blend of the pair, strongest in the middle, so what darkens depends on
                // both pictures rather than on nothing.
                const double mix = std::sin(p * 3.14159265358979323846);
                const double cr = qRed(pa) * (1 - p) + qRed(pb) * p;
                const double cg = qGreen(pa) * (1 - p) + qGreen(pb) * p;
                const double cb = qBlue(pa) * (1 - p) + qBlue(pb) * p;
                r = cr + (std::max(0.0, double(qRed(pa) + qRed(pb) - 255)) - cr) * mix;
                g = cg + (std::max(0.0, double(qGreen(pa) + qGreen(pb) - 255)) - cg) * mix;
                bl = cb + (std::max(0.0, double(qBlue(pa) + qBlue(pb) - 255)) - cb) * mix;
                break;
            }
            case 2: { // Subtractive crossfade — a cross-fade with the middle pulled down
                const double dip = 1.0 - 0.6 * std::sin(p * 3.14159265358979323846);
                r = (qRed(pa) * (1 - p) + qRed(pb) * p) * dip;
                g = (qGreen(pa) * (1 - p) + qGreen(pb) * p) * dip;
                bl = (qBlue(pa) * (1 - p) + qBlue(pb) * p) * dip;
                break;
            }
            case 3:   // Colour bleed — each channel crosses at its own rate
            case 4: { // Colour morph — same, with the channels set off one after another
                // The stagger delays a channel rather than advancing it. Shifting progress
                // forward put red at 0.15 before the transition had begun, so the first
                // frame was not the outgoing picture at all.
                const double lag = (type == 4) ? 0.15 : 0.0;
                const auto start = [&](double at, double speed) {
                    return ramp(std::max(0.0, p - at) / std::max(1e-6, 1.0 - at), speed);
                };
                const double tr = start(0.0, rR);
                const double tg = start(lag, rG);
                const double tb = start(lag * 2.0, rB);
                const double ta = start(0.0, rA);
                r = qRed(pa) * (1 - tr) + qRed(pb) * tr;
                g = qGreen(pa) * (1 - tg) + qGreen(pb) * tg;
                bl = qBlue(pa) * (1 - tb) + qBlue(pb) * tb;
                al = qAlpha(pa) * (1 - ta) + qAlpha(pb) * ta;
                break;
            }
            case 5:   // Threshold dissolve — a pixel switches once its own brightness is passed
            case 6: { // Threshold appear — the same, taken from the incoming picture
                const QRgb ref = (type == 5) ? pa : pb;
                const double luma =
                    (0.2126 * qRed(ref) + 0.7152 * qGreen(ref) + 0.0722 * qBlue(ref)) / 255.0;
                const double edge = smoothStep(luma - 0.08, luma + 0.08, p);
                r = qRed(pa) * (1 - edge) + qRed(pb) * edge;
                g = qGreen(pa) * (1 - edge) + qGreen(pb) * edge;
                bl = qBlue(pa) * (1 - edge) + qBlue(pb) * edge;
                break;
            }
            case 7: { // Fade through grayscale
                const double mix = std::sin(p * 3.14159265358979323846);
                const double cr = qRed(pa) * (1 - p) + qRed(pb) * p;
                const double cg = qGreen(pa) * (1 - p) + qGreen(pb) * p;
                const double cb = qBlue(pa) * (1 - p) + qBlue(pb) * p;
                const double grey = 0.2126 * cr + 0.7152 * cg + 0.0722 * cb;
                r = cr * (1 - mix) + grey * mix;
                g = cg * (1 - mix) + grey * mix;
                bl = cb * (1 - mix) + grey * mix;
                break;
            }
            case 8: { // Fade through a colour — alpha 0 means fade through nothing at all
                const double half = p < 0.5 ? p * 2.0 : (1.0 - p) * 2.0;
                const QRgb src = p < 0.5 ? pa : pb;
                r = qRed(src) * (1 - half) + fadeR * 255.0 * half * fadeA;
                g = qGreen(src) * (1 - half) + fadeG * 255.0 * half * fadeA;
                bl = qBlue(src) * (1 - half) + fadeB * 255.0 * half * fadeA;
                al = 255.0 * (1.0 - half * (1.0 - fadeA));
                break;
            }
            default: { // 9 — the channels cross one after another
                const double tr = std::clamp(p * 3.0, 0.0, 1.0);
                const double tg = std::clamp(p * 3.0 - 1.0, 0.0, 1.0);
                const double tb = std::clamp(p * 3.0 - 2.0, 0.0, 1.0);
                r = qRed(pa) * (1 - tr) + qRed(pb) * tr;
                g = qGreen(pa) * (1 - tg) + qGreen(pb) * tg;
                bl = qBlue(pa) * (1 - tb) + qBlue(pb) * tb;
                break;
            }
            }
            orow[x] = qRgba(sat(r), sat(g), sat(bl), sat(al));
        }
    }
    return out;
}

// ---------------------------------------------------------------------- Cross Effect

/** Blocky pixelation, used by Cross Effect's Pixelate type. */
QImage pixelate(const QImage &src, int block)
{
    if (block < 2 || src.isNull()) {
        return src;
    }
    QImage out = src;
    for (int y = 0; y < src.height(); y += block) {
        for (int x = 0; x < src.width(); x += block) {
            long r = 0;
            long g = 0;
            long b = 0;
            long n = 0;
            for (int yy = y; yy < std::min(y + block, src.height()); ++yy) {
                const auto *row = reinterpret_cast<const QRgb *>(src.constScanLine(yy));
                for (int xx = x; xx < std::min(x + block, src.width()); ++xx) {
                    r += qRed(row[xx]);
                    g += qGreen(row[xx]);
                    b += qBlue(row[xx]);
                    ++n;
                }
            }
            if (n == 0) {
                continue;
            }
            const QRgb avg = qRgb(int(r / n), int(g / n), int(b / n));
            for (int yy = y; yy < std::min(y + block, src.height()); ++yy) {
                auto *row = reinterpret_cast<QRgb *>(out.scanLine(yy));
                for (int xx = x; xx < std::min(x + block, src.width()); ++xx) {
                    row[xx] = avg;
                }
            }
        }
    }
    return out;
}

/**
 * Cross Effect: the two clips cross-fade while an effect swells and dies away.
 *
 * Effect type is 1 Zoom, 2 Pixelate, 3 Blur, and "Apply to" is 0 the outgoing clip, 1 the
 * incoming one, 2 both — which is exactly how the ten presets are named. The strength
 * peaks in the middle of the transition, so the pair meets at its blurriest, blockiest or
 * most enlarged and comes out clean.
 */
QImage renderCrossEffect(const QImage &from, const QImage &to, double progress,
                         const TransitionInstance &t, const QSize &size)
{
    constexpr double kPi = 3.14159265358979323846;
    const int effect = int(std::lround(transitionParamValue(t, QStringLiteral("effectType"))));
    const int applyTo = int(std::lround(transitionParamValue(t, QStringLiteral("applyTo"))));
    const double maxZoom =
        std::max(1.0, transitionParamValue(t, QStringLiteral("effectSettingsMaxZoom")));

    const double strength = std::sin(std::clamp(progress, 0.0, 1.0) * kPi);
    const bool onA = applyTo == 0 || applyTo == 2;
    const bool onB = applyTo == 1 || applyTo == 2;

    const auto apply = [&](const QImage &src) {
        if (src.isNull() || strength <= 1e-3) {
            return src;
        }
        switch (effect) {
        case 2: // Pixelate
            return pixelate(src, int(std::lround(1.0 + strength * size.width() / 24.0)));
        case 3: // Blur
            return boxBlur(src, int(std::lround(strength * size.width() / 24.0)),
                           int(std::lround(strength * size.height() / 24.0)));
        default: { // 1 Zoom — grows about the middle, the frame filled by the enlargement
            // MaxZoom runs to 64 in the presets, which is far past anything readable as a
            // picture; the visible part of that range is taken rather than all of it.
            const double scale = 1.0 + strength * std::min(3.0, std::log2(maxZoom) / 2.0);
            QImage grown(size, QImage::Format_ARGB32_Premultiplied);
            grown.fill(Qt::transparent);
            QPainter gp(&grown);
            gp.setRenderHint(QPainter::SmoothPixmapTransform, true);
            const double w = size.width() * scale;
            const double h = size.height() * scale;
            gp.drawImage(QRectF((size.width() - w) * 0.5, (size.height() - h) * 0.5, w, h), src,
                         QRectF(0, 0, size.width(), size.height()));
            gp.end();
            return grown;
        }
        }
    };

    const QImage a = onA ? apply(toArgb(from, size)) : toArgb(from, size);
    const QImage b = onB ? apply(toArgb(to, size)) : toArgb(to, size);
    return crossDissolve(a, b, progress, size);
}

// ----------------------------------------------------------------------- 3D Cascade

/**
 * 3D Cascade: the frame is cut into strips that fall away one after another.
 *
 * Same strip machinery as 3D Blinds, with two differences the presets make plain: there
 * is no extra-spins field, and the third control is **Twist**, which leans each strip
 * further than the one before it. "Curtain" is ten strips with a twist of 0.4; the two
 * plain ones set it to zero and differ only in how the strips are laid out.
 */
QImage renderCascade3D(const QImage &from, const QImage &to, double progress,
                       const TransitionInstance &t, const QSize &size)
{
    QImage out(size, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);

    const QImage a = toArgb(from, size);
    const QImage b = toArgb(to, size);
    const int divisions =
        std::clamp(int(std::lround(transitionParamValue(t, QStringLiteral("divisions")))), 1, 16);
    const double twist = std::clamp(transitionParamValue(t, QStringLiteral("twist")), 0.0, 1.0);
    const double specular = transitionParamValue(t, QStringLiteral("specularLight"));
    const int direction =
        std::clamp(int(std::lround(transitionParamValue(t, QStringLiteral("direction")))), 0, 3);

    // Cascade numbers its directions differently from 3D Blinds, and its presets say so:
    // "Left to Right" stores 0 and "Top to Bottom" stores 1, where Blinds puts Top to
    // Bottom at 2. Borrowing the Blinds order laid both of them out the same way.
    const bool horizontalSplit = direction == 0 || direction == 2;
    const bool reverse = direction == 2 || direction == 3;

    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // The incoming clip is already there; the strips of the outgoing one fall off it.
    if (!b.isNull()) {
        p.drawImage(0, 0, b);
    }

    const double total = horizontalSplit ? size.width() : size.height();
    for (int i = 0; i < divisions; ++i) {
        const double lo = total * i / divisions;
        const double hi = total * (i + 1) / divisions;
        const QRectF strip = horizontalSplit ? QRectF(lo, 0, hi - lo, size.height())
                                             : QRectF(0, lo, size.width(), hi - lo);

        // A cascade is a stagger by definition: each strip waits for the one before it.
        const double lp = stripProgress(progress, i, divisions, 0.85, reverse);
        // Twist leans the later strips further, so the fall fans out instead of being
        // the same move repeated.
        const double lean = twist * double(reverse ? divisions - 1 - i : i) / std::max(1, divisions - 1);
        const double angle = lp * (M_PI * 0.5) * (1.0 + lean);
        const double cosA = std::cos(angle);
        if (cosA <= 0.001 || a.isNull()) {
            continue; // this strip has turned edge-on and gone
        }
        const double sinA = std::sin(angle);
        drawStrip(p, a, strip, cosA, horizontalSplit, specular * sinA * sinA * 0.55);
    }
    p.end();
    return out;
}

// ----------------------------------------------------------------------- 3D Shuffle

/**
 * 3D Shuffle: the two clips are shuffled past one another like cards.
 *
 * The only control VEGAS exposes is Specular light, and its two presets are that setting
 * at full and at a fifth — so the motion is fixed and the light is all that varies.
 */
QImage renderShuffle3D(const QImage &from, const QImage &to, double progress,
                       const TransitionInstance &t, const QSize &size)
{
    const double specular = transitionParamValue(t, QStringLiteral("specularLight"));
    const QImage a = toArgb(from, size);
    const QImage b = toArgb(to, size);

    QImage out(size, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const double w = size.width();
    const double h = size.height();
    // Both cards swing out to the side and back, the incoming one arriving over the top
    // of the outgoing one halfway through — which is what a shuffle looks like.
    const double swing = std::sin(progress * M_PI);
    const auto card = [&](const QImage &img, double slide, double lift) {
        if (img.isNull()) {
            return;
        }
        const double scale = 1.0 - 0.18 * lift;
        const double cx = w * (0.5 + slide);
        const QRectF target(cx - w * scale * 0.5, h * (0.5 - scale * 0.5) + h * 0.04 * lift,
                            w * scale, h * scale);
        p.drawImage(target, img, QRectF(0, 0, w, h));
        if (specular > 0.001 && lift > 0.001) {
            p.save();
            p.setCompositionMode(QPainter::CompositionMode_Plus);
            p.fillRect(target, QColor(255, 255, 255,
                                      int(std::clamp(specular * lift, 0.0, 1.0) * 90.0)));
            p.restore();
        }
    };

    if (progress < 0.5) {
        card(b, -0.35 * (1.0 - progress * 2.0), swing);
        card(a, 0.35 * swing, swing);
    } else {
        card(a, 0.35 * swing, swing);
        card(b, -0.35 * (1.0 - progress) * 2.0 * 0.35, swing);
    }
    p.end();
    return out;
}

// -------------------------------------------------------------------- 3D Fly In/Out

/**
 * 3D Fly In/Out: one clip flies in from a point in space, or away to it, turning as it
 * goes.
 *
 * Every field is named by the preset package and both shipped presets fill them in:
 * "Tumble In" comes from far off (Z 8) with eight turns about X, "Spin Away" leaves to a
 * nearer point with fewer. There is no perspective renderer here, so Z is taken as
 * distance — how small the clip starts or ends — and the three rotation counts drive a
 * turn about the frame's centre with the X and Y ones squashing it as they pass edge-on.
 */
QImage renderFlyInOut3D(const QImage &from, const QImage &to, double progress,
                        const TransitionInstance &t, const QSize &size)
{
    const auto num = [&](const char *key) {
        return transitionParamValue(t, QString::fromLatin1(key));
    };
    const double farX = num("farXPosition");
    const double farY = num("farYPosition");
    const double farZ = std::max(1.0, num("farZPosition"));
    const double rotX = num("xRotations");
    const double rotY = num("yRotations");
    const double rotZ = num("zRotations");
    const double specular = num("specularLight");
    const bool out = int(std::lround(num("direction"))) == 1;

    const QImage a = toArgb(from, size);
    const QImage b = toArgb(to, size);
    QImage result(size, QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);

    QPainter p(&result);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const double w = size.width();
    const double h = size.height();

    // The clip that stays is the backdrop; the other one travels.
    const QImage &still = out ? b : a;
    const QImage &moving = out ? a : b;
    if (!still.isNull()) {
        p.drawImage(0, 0, still);
    }
    if (moving.isNull()) {
        p.end();
        return result;
    }

    // u runs 0 at the far point to 1 at the frame, whichever way round the move goes.
    const double u = out ? (1.0 - progress) : progress;
    const double dist = 1.0 / farZ;                       // size at the far point
    const double scale = dist + (1.0 - dist) * u;         // …growing to full frame
    const double cx = w * (0.5 + farX * (1.0 - u));
    const double cy = h * (0.5 + farY * (1.0 - u));

    // Turning: X and Y rotations squash the clip as it passes edge-on, Z spins it.
    const double ax = rotX * 2.0 * M_PI * (1.0 - u);
    const double ay = rotY * 2.0 * M_PI * (1.0 - u);
    const double az = rotZ * 360.0 * (1.0 - u);
    const double sx = std::max(0.02, std::abs(std::cos(ay)));
    const double sy = std::max(0.02, std::abs(std::cos(ax)));

    // At the far end the clip has to be gone, or the transition never finishes. A near
    // far-point does not shrink it away on its own — "Spin Away" sits at Z 2.54, so a
    // third of the picture was still on screen at progress 1 — so it fades over the last
    // stretch of the flight as well as shrinking.
    p.setOpacity(smoothStep(0.0, 0.15, u));
    p.translate(cx, cy);
    p.rotate(az);
    p.scale(scale * sx, scale * sy);
    p.drawImage(QRectF(-w * 0.5, -h * 0.5, w, h), moving, QRectF(0, 0, w, h));
    if (specular > 0.001) {
        // Brightest as a face turns away, gone when it faces front — the same reading the
        // blinds use for their own specular term.
        const double glint = std::max(std::sin(ax) * std::sin(ax), std::sin(ay) * std::sin(ay));
        p.save();
        p.setCompositionMode(QPainter::CompositionMode_Plus);
        p.fillRect(QRectF(-w * 0.5, -h * 0.5, w, h),
                   QColor(255, 255, 255, int(std::clamp(specular * glint, 0.0, 1.0) * 110.0)));
        p.restore();
    }
    p.end();
    return result;
}

// -------------------------------------------------------------------------- Portals

/**
 * Portals: a grid of squares opens onto the incoming clip, each in its own time.
 *
 * Squares says how many across, and Random pattern seed which order they go in — the two
 * presets differ mostly there and in how far each square drifts (Max offset), how much it
 * shrinks (Max scale) and how transparent it gets (Max transparency) on its way out.
 */
QImage renderPortals(const QImage &from, const QImage &to, double progress,
                     const TransitionInstance &t, const QSize &size)
{
    const int squares =
        std::clamp(int(std::lround(transitionParamValue(t, QStringLiteral("squares")))), 1, 64);
    const int seed = int(std::lround(transitionParamValue(t, QStringLiteral("randomPatternSeed"))));
    const double maxTransparency =
        std::clamp(transitionParamValue(t, QStringLiteral("maxTransparency")), 0.0, 1.0);
    const double maxOffset = std::clamp(transitionParamValue(t, QStringLiteral("maxOffset")), 0.0, 2.0);
    const double maxScale = std::clamp(transitionParamValue(t, QStringLiteral("maxScale")), 0.0, 2.0);
    const EdgeStyle style = edgeStyleOf(t, "borderFeather");

    const QImage a = toArgb(from, size);
    const QImage b = toArgb(to, size);
    QImage out(size, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);

    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    if (!b.isNull()) {
        p.drawImage(0, 0, b);
    }
    if (a.isNull()) {
        p.end();
        return out;
    }

    const double w = size.width();
    const double h = size.height();
    const int rows = std::max(1, int(std::lround(squares * h / std::max(1.0, w))));
    const double cellW = w / squares;
    const double cellH = h / rows;

    // A stable order from the seed: the same project always opens its squares the same
    // way, which a call to a global generator would not give.
    const auto hashed = [seed](int i) {
        quint32 x = quint32(i) * 2654435761u + quint32(seed) * 40503u;
        x ^= x >> 15;
        x *= 2246822519u;
        x ^= x >> 13;
        return double(x % 10000u) / 10000.0;
    };

    const double band = 0.45; // how much of the run one square takes
    for (int ry = 0; ry < rows; ++ry) {
        for (int rx = 0; rx < squares; ++rx) {
            const double start = hashed(ry * squares + rx) * (1.0 - band);
            const double lp = std::clamp((progress - start) / band, 0.0, 1.0);
            if (lp >= 1.0) {
                continue; // this square has gone
            }
            const QRectF cell(rx * cellW, ry * cellH, cellW, cellH);
            const double scale = 1.0 - maxScale * lp;
            const double drift = maxOffset * lp * cellW;
            const double ang = hashed(ry * squares + rx + 7919) * 2.0 * M_PI;
            QRectF dest(0, 0, cell.width() * scale, cell.height() * scale);
            dest.moveCenter(cell.center()
                            + QPointF(std::cos(ang) * drift, std::sin(ang) * drift));

            p.setOpacity(1.0 - maxTransparency * lp);
            p.drawImage(dest, a, cell);
            p.setOpacity(1.0);
            strokeMovingEdge(p, dest, style, progress, std::min(cellW, cellH));
        }
    }
    p.end();
    return out;
}

// ---------------------------------------------------------------- Page Peel / Roll / Loop

/**
 * The geometry the three page groups share.
 *
 * All of them lift the outgoing clip off the frame along one axis. The angle names the
 * corner being lifted — the preset names say so outright, and they line up with screen
 * coordinates (y down): 30° is Bottom-Right, 150° Bottom-Left, 210° Top-Left, 330°
 * Top-Right, 180° Left, 270° Top. So the direction is simply (cos, sin) with y down, and
 * the fold is the line perpendicular to it, sweeping from that corner to the far one.
 */
struct PageAxis {
    QPointF dir;      ///< unit vector towards the corner being lifted
    double atCorner;  ///< projection of the lifted corner onto dir
    double atFar;     ///< projection of the far corner
    double span;      ///< distance the fold travels
};

PageAxis pageAxisFor(double angleDeg, const QSizeF &size)
{
    PageAxis a;
    const double rad = angleDeg * M_PI / 180.0;
    a.dir = QPointF(std::cos(rad), std::sin(rad));
    const QPointF corners[4] = {QPointF(0, 0), QPointF(size.width(), 0),
                                QPointF(size.width(), size.height()), QPointF(0, size.height())};
    a.atCorner = -std::numeric_limits<double>::max();
    a.atFar = std::numeric_limits<double>::max();
    for (const QPointF &c : corners) {
        const double u = c.x() * a.dir.x() + c.y() * a.dir.y();
        a.atCorner = std::max(a.atCorner, u);
        a.atFar = std::min(a.atFar, u);
    }
    a.span = a.atCorner - a.atFar;
    return a;
}

/** The half-plane `u <= at` as a polygon big enough to cover the frame from any angle. */
QPolygonF pageHalfPlane(const PageAxis &axis, double at, const QSizeF &size)
{
    const double reach = size.width() + size.height();
    const QPointF n = axis.dir;
    const QPointF t(-n.y(), n.x()); // along the fold
    const QPointF onLine(n.x() * at, n.y() * at);
    QPolygonF poly;
    poly << onLine + t * reach << onLine - t * reach << onLine - t * reach - n * reach
         << onLine + t * reach - n * reach;
    return poly;
}

/** Places an image mirrored about the fold, so the lifted part folds back over itself. */
QTransform pageFoldTransform(const PageAxis &axis, double at)
{
    // Reflect about the line u = at: p' = p - 2 (u - at) n.
    const double nx = axis.dir.x();
    const double ny = axis.dir.y();
    return QTransform(1.0 - 2.0 * nx * nx, -2.0 * nx * ny,
                      -2.0 * nx * ny, 1.0 - 2.0 * ny * ny,
                      2.0 * at * nx, 2.0 * at * ny);
}

/** Shading across the curl: dark in the crease, catching the light as it turns over. */
QLinearGradient pageCurlShade(const PageAxis &axis, double at, double width,
                              const QColor &light)
{
    const QPointF n = axis.dir;
    QLinearGradient g(QPointF(n.x() * (at - width), n.y() * (at - width)),
                      QPointF(n.x() * (at + width), n.y() * (at + width)));
    QColor lit = light;
    lit.setAlphaF(0.55);
    g.setColorAt(0.0, QColor(0, 0, 0, 0));
    g.setColorAt(0.35, QColor(0, 0, 0, 115));
    g.setColorAt(0.62, lit);
    g.setColorAt(1.0, QColor(0, 0, 0, 0));
    return g;
}

/**
 * One page transition, told apart by how much of the lifted part is still facing us.
 *
 * Peel folds the page back on itself, so nearly all of it stays visible as a mirrored
 * flap. Roll winds it onto a cylinder, so only the first half-turn — pi times the radius —
 * of the lifted length ever faces us and the rest is wound out of sight; that is what
 * makes a tight radius read as a tube rather than a fold. Loop is Roll with the cylinder
 * drawn as a visible ring at a point along the fold. How much of the flap survives is the
 * whole difference between the three, so they share one function and differ by two
 * arguments.
 */
QImage renderPageFamily(const QImage &from, const QImage &to, double progress,
                        const TransitionInstance &t, const QSize &size,
                        const char *angleKey, const char *radiusKey, const char *opacityKey,
                        bool ringed)
{
    const auto num = [&](const char *key) {
        return transitionParamValue(t, QString::fromLatin1(key));
    };
    const double angle = num(angleKey);
    const double radius = std::max(0.0, num(radiusKey));
    const double opacity = std::clamp(num(opacityKey), 0.0, 1.0);
    const double perspective = std::clamp(num("perspective"), 0.0, 1.0);
    const double slide = ringed ? 0.0 : std::clamp(num("slideAmount"), 0.0, 1.0);
    const double ringPos = ringed ? std::clamp(num("loopPosition"), 0.0, 1.0) : 0.0;
    const QColor light = QColor::fromRgbF(std::clamp(num("lightColorRed"), 0.0, 1.0),
                                          std::clamp(num("lightColorGreen"), 0.0, 1.0),
                                          std::clamp(num("lightColorBlue"), 0.0, 1.0));

    const QImage a = toArgb(from, size);
    const QImage b = toArgb(to, size);
    QImage result(size, QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);

    QPainter p(&result);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (!b.isNull()) {
        p.drawImage(0, 0, b); // what the page uncovers
    }
    if (a.isNull()) {
        p.end();
        return result;
    }

    const QSizeF fs(size.width(), size.height());
    const PageAxis axis = pageAxisFor(angle, fs);
    const double diag = std::hypot(fs.width(), fs.height());
    const double curl = radius * diag * 0.5;
    // The fold runs past the far corner by the curl, or the last sliver of page would
    // still be standing at progress 1.
    const double at = axis.atCorner - progress * (axis.span + curl * 2.0);

    // The part still lying flat.
    p.save();
    {
        QPainterPath flat;
        flat.addPolygon(pageHalfPlane(axis, at, fs));
        p.setClipPath(flat);
    }
    if (slide > 0.001) {
        // Sliding takes the whole sheet with it instead of hinging it at the fold.
        const double travel = slide * progress * diag * 0.35;
        p.translate(axis.dir.x() * travel, axis.dir.y() * travel);
    }
    p.drawImage(0, 0, a);
    p.restore();

    const double lifted = std::max(0.0, axis.atCorner - at);
    const double windable = M_PI * curl;
    const bool winds = ringed || radius < 0.25;
    const double visible = winds ? std::min(lifted, windable) : lifted;

    if (visible > 0.5 && opacity > 0.001) {
        p.save();
        QPainterPath flap;
        flap.addPolygon(pageHalfPlane(axis, at, fs));
        QPainterPath keep;
        keep.addPolygon(pageHalfPlane(axis, at - visible, fs));
        p.setClipPath(flap.subtracted(keep));
        p.setOpacity(0.25 + 0.75 * opacity);
        QTransform m = pageFoldTransform(axis, at);
        if (perspective > 0.001) {
            // The far end of the flap leans away, so it comes back a little smaller.
            const QPointF pivot(fs.width() * 0.5, fs.height() * 0.5);
            m = QTransform().translate(pivot.x(), pivot.y())
                    .scale(1.0 - 0.18 * perspective, 1.0 - 0.18 * perspective)
                    .translate(-pivot.x(), -pivot.y())
                * m;
        }
        p.setTransform(m, true);
        p.drawImage(0, 0, a);
        p.restore();
    }

    // The crease itself, drawn whether or not the flap survived: on a tight roll it is
    // most of what there is to see.
    if (lifted > 0.5 && curl > 0.5) {
        p.save();
        p.setClipRect(QRectF(QPointF(0, 0), fs));
        QPainterPath band;
        band.addPolygon(pageHalfPlane(axis, at + curl, fs));
        QPainterPath inner;
        inner.addPolygon(pageHalfPlane(axis, at - curl, fs));
        p.fillPath(band.subtracted(inner), pageCurlShade(axis, at, std::max(2.0, curl), light));
        p.restore();
    }

    // Loop: the page curls right round, so the curl is a tube lying along the whole fold
    // rather than a crease. Loop position slides that tube along the peel axis — ahead of
    // the fold or behind it — and a preset with radius 0 ("Top-Left, Opaque, No Loop")
    // draws no tube at all, which is exactly what its name promises.
    //
    // Approximate: without a reference render of the real plug-in this is the reading the
    // parameter names support, not a measured match. See MARKDOWN/CHECKLIST.md.
    if (ringed && curl > 1.0 && lifted > 0.5) {
        const QPointF n = axis.dir;
        const double centre = at + (ringPos * 2.0 - 1.0) * curl;
        p.save();
        p.setClipRect(QRectF(QPointF(0, 0), fs));
        QPainterPath band;
        band.addPolygon(pageHalfPlane(axis, centre + curl, fs));
        QPainterPath inner;
        inner.addPolygon(pageHalfPlane(axis, centre - curl, fs));

        // Round shading across the tube: dark where it turns away on both sides, bright
        // along the top. A flat gradient would read as a painted stripe instead.
        QLinearGradient tube(QPointF(n.x() * (centre - curl), n.y() * (centre - curl)),
                             QPointF(n.x() * (centre + curl), n.y() * (centre + curl)));
        QColor lit = light;
        lit.setAlphaF(std::clamp(0.7 * (0.35 + 0.65 * opacity), 0.0, 1.0));
        tube.setColorAt(0.0, QColor(0, 0, 0, 0));
        tube.setColorAt(0.18, QColor(0, 0, 0, 105));
        tube.setColorAt(0.5, lit);
        tube.setColorAt(0.82, QColor(0, 0, 0, 105));
        tube.setColorAt(1.0, QColor(0, 0, 0, 0));
        p.fillPath(band.subtracted(inner), tube);
        p.restore();
    }

    p.end();
    return result;
}

QImage renderPagePeel(const QImage &from, const QImage &to, double progress,
                      const TransitionInstance &t, const QSize &size)
{
    return renderPageFamily(from, to, progress, t, size, "peelAngle", "foldRadius",
                            "peelOpacity", false);
}

QImage renderPageRoll(const QImage &from, const QImage &to, double progress,
                      const TransitionInstance &t, const QSize &size)
{
    return renderPageFamily(from, to, progress, t, size, "rollAngle", "foldRadius",
                            "rollOpacity", false);
}

QImage renderPageLoop(const QImage &from, const QImage &to, double progress,
                      const TransitionInstance &t, const QSize &size)
{
    return renderPageFamily(from, to, progress, t, size, "paperAngle", "loopRadius",
                            "paperOpacity", true);
}

QImage renderTransition(const QImage &from, const QImage &to, double progress,
                        const TransitionInstance &t)
{
    const QSize size = !from.isNull() ? from.size() : to.size();
    if (size.isEmpty()) {
        return QImage();
    }
    const double p = std::clamp(progress, 0.0, 1.0);
    if (t.pluginId == transition3dBlindsId()) {
        return renderBlinds(from, to, p, t, size);
    }
    if (t.pluginId == transitionVenetianBlindsId()) {
        return renderVenetianBlinds(from, to, p, t, size);
    }
    if (t.pluginId == transitionLinearWipeId()) {
        return renderLinearWipe(from, to, p, t, size);
    }
    if (t.pluginId == transitionBarnDoorId()) {
        return renderBarnDoor(from, to, p, t, size);
    }
    if (t.pluginId == transitionIrisId()) {
        return renderIris(from, to, p, t, size);
    }
    if (t.pluginId == transitionClockWipeId()) {
        return renderClockWipe(from, to, p, t, size);
    }
    if (t.pluginId == transitionZoomId()) {
        return renderZoom(from, to, p, t, size);
    }
    if (t.pluginId == transitionPushId()) {
        return renderPush(from, to, p, t, size);
    }
    if (t.pluginId == transitionSlideId()) {
        return renderSlide(from, to, p, t, size);
    }
    if (t.pluginId == transitionSqueezeId()) {
        return renderSqueeze(from, to, p, t, size);
    }
    if (t.pluginId == transitionSplitId()) {
        return renderSplit(from, to, p, t, size);
    }
    if (t.pluginId == transitionFlashId()) {
        return renderFlash(from, to, p, t, size);
    }
    if (t.pluginId == transitionStarWipeId()) {
        return renderStarWipe(from, to, p, t, size);
    }
    if (t.pluginId == transitionSwapId()) {
        return renderSwap(from, to, p, t, size);
    }
    if (t.pluginId == transitionSpiralId()) {
        return renderSpiral(from, to, p, t, size);
    }
    if (t.pluginId == transitionDissolveId()) {
        return renderDissolve(from, to, p, t, size);
    }
    if (t.pluginId == transitionCrossEffectId()) {
        return renderCrossEffect(from, to, p, t, size);
    }
    if (t.pluginId == transitionCascade3dId()) {
        return renderCascade3D(from, to, p, t, size);
    }
    if (t.pluginId == transitionShuffle3dId()) {
        return renderShuffle3D(from, to, p, t, size);
    }
    if (t.pluginId == transitionFlyInOut3dId()) {
        return renderFlyInOut3D(from, to, p, t, size);
    }
    if (t.pluginId == transitionPortalsId()) {
        return renderPortals(from, to, p, t, size);
    }
    if (t.pluginId == transitionPagePeelId()) {
        return renderPagePeel(from, to, p, t, size);
    }
    if (t.pluginId == transitionPageRollId()) {
        return renderPageRoll(from, to, p, t, size);
    }
    if (t.pluginId == transitionPageLoopId()) {
        return renderPageLoop(from, to, p, t, size);
    }
    return crossDissolve(from, to, p, size);
}

QImage renderTransitionPreview(const TransitionInstance &t, const QSize &size, double progress)
{
    if (size.isEmpty()) {
        return QImage();
    }
    const QImage a = testCard(size, QStringLiteral("A"), QColor(0x4a, 0x9a, 0xd0),
                              QColor(0x1c, 0x5a, 0x8a));
    const QImage b = testCard(size, QStringLiteral("B"), QColor(0x9a, 0xd8, 0xf0),
                              QColor(0x4a, 0x9a, 0xd0));
    const QImage blended = renderTransition(a, b, progress, t);

    // Transparent gaps between the blinds are the whole point of the thumbnail — show
    // them over the same checkerboard the Media Generator previews use.
    QImage out(size, QImage::Format_ARGB32_Premultiplied);
    QPainter p(&out);
    constexpr int kTile = 8;
    p.setPen(Qt::NoPen);
    for (int y = 0; y < size.height(); y += kTile) {
        for (int x = 0; x < size.width(); x += kTile) {
            const bool light = ((x / kTile) + (y / kTile)) % 2 == 0;
            p.setBrush(light ? QColor(120, 120, 120) : QColor(85, 85, 85));
            p.drawRect(x, y, kTile, kTile);
        }
    }
    if (!blended.isNull()) {
        p.drawImage(0, 0, blended);
    }
    p.end();
    return out;
}

} // namespace openvegas
