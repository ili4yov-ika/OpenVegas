#include "video/TransitionApply.h"

#include "video/TransitionPresetData.h"

#include <QHash>
#include <QSet>

#include <QFont>
#include <QPainter>

#include <algorithm>
#include <cmath>

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
    QVector<TransitionParamInfo> params = {
        {QStringLiteral("divisions"), QStringLiteral("Divisions"), 1.0, 16.0, 0, {}},
        {QStringLiteral("direction"), QStringLiteral("Direction"), 0.0, 3.0, 0,
         {QStringLiteral("Left to Right"), QStringLiteral("Right to Left"),
          QStringLiteral("Top to Bottom"), QStringLiteral("Bottom to Top")}},
        {QStringLiteral("stagger"), QStringLiteral("Stagger"), 0.0, 1.0, 4, {}},
        {QStringLiteral("specularLight"), QStringLiteral("Specular light"), 0.0, 1.0, 4, {}},
    };
    auto p = [](const QString &n, int div, int dir, double stagger, double light) {
        TransitionPresetInfo i;
        i.name = n;
        i.params = QVariantMap{{QStringLiteral("divisions"), div},
                               {QStringLiteral("direction"), dir},
                               {QStringLiteral("stagger"), stagger},
                               {QStringLiteral("specularLight"), light}};
        return i;
    };
    return makeStubGroup(transitionCascade3dId(), QStringLiteral("3D Cascade"),
                         QStringLiteral("DXT, 32-bit floating point"), params,
                         {p(QStringLiteral("Curtain"), 10, 2, 0.4, 1.0),
                          p(QStringLiteral("Left to Right"), 10, 0, 0.0, 1.0),
                          p(QStringLiteral("Top to Bottom"), 5, 2, 0.0, 1.0)});
}

TransitionPluginInfo makeShuffle3D()
{
    // One control, exactly as the plug-in's own window shows: Specular light.
    QVector<TransitionParamInfo> params = {
        {QStringLiteral("specularLight"), QStringLiteral("Specular light"), 0.0, 1.0, 4, {}},
    };
    auto p = [](const QString &n, double light) {
        TransitionPresetInfo i;
        i.name = n;
        i.params = QVariantMap{{QStringLiteral("specularLight"), light}};
        return i;
    };
    return makeStubGroup(transitionShuffle3dId(), QStringLiteral("3D Shuffle"),
                         QStringLiteral("DXT, 32-bit floating point"), params,
                         {p(QStringLiteral("Bright Light"), 1.0),
                          p(QStringLiteral("Low Light"), 0.2)});
}

TransitionPluginInfo makeFlyInOut3D()
{
    // Four doubles whose meaning the sample does not pin down, so no sliders are offered
    // rather than five made-up labels.
    auto p = [](const QString &n) {
        TransitionPresetInfo i;
        i.name = n;
        return i;
    };
    return makeStubGroup(transitionFlyInOut3dId(), QStringLiteral("3D Fly In/Out"),
                         QStringLiteral("DXT, 32-bit floating point"), {},
                         {p(QStringLiteral("Default")), p(QStringLiteral("Spin Away")),
                          p(QStringLiteral("Tumble In"))});
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

TransitionPluginInfo makePortals()
{
    // Same story: the preset names a height map, not a set of numbers.
    auto p = [](const QString &n) {
        TransitionPresetInfo i;
        i.name = n;
        return i;
    };
    return makeStubGroup(transitionPortalsId(), QStringLiteral("Portals"),
                         QStringLiteral("DXT, 32-bit floating point"), {},
                         {p(QStringLiteral("Jigsaw Puzzle")), p(QStringLiteral("Mondrian")),
                          p(QStringLiteral("Plaid")), p(QStringLiteral("White Wash")),
                          p(QStringLiteral("Windowed Fade"))});
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
            makePush(),       makeSlide(),          makeSqueeze(),    makeSplit(),
            makeFlash()};
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
