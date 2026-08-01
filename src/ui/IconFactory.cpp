#include "ui/IconFactory.h"

#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>
#include <QByteArray>

namespace openvegas {

QIcon IconFactory::iconFromSvgBody(const QString &svgInner, int size, const QColor &color)
{
    QString body = svgInner;
    body.replace(QStringLiteral("currentColor"), color.name(QColor::HexRgb));
    const QString doc = QStringLiteral(
                            "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 16 16\" "
                            "width=\"%1\" height=\"%1\">%2</svg>")
                            .arg(size)
                            .arg(body);

    QSvgRenderer renderer(doc.toUtf8());
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    renderer.render(&p);
    p.end();
    return QIcon(pm);
}

QToolButton *IconFactory::toolButton(QWidget *parent, const QString &title, const QString &svgInner,
                                     bool checkable, bool checked)
{
    auto *btn = new QToolButton(parent);
    btn->setObjectName(QStringLiteral("iconBtn"));
    btn->setToolTip(title);
    btn->setAutoRaise(true);
    btn->setIconSize(QSize(16, 16));
    btn->setFixedSize(24, 24);
    btn->setIcon(iconFromSvgBody(svgInner, 16));
    btn->setCheckable(checkable);
    btn->setChecked(checked);
    btn->setFocusPolicy(Qt::NoFocus);
    return btn;
}

QString IconFactory::svgNew()
{
    return QStringLiteral("<path d=\"M3 2h7l3 3v9H3V2zm7 1v2h3\" fill=\"none\" stroke=\"currentColor\" "
                          "stroke-width=\"1.2\"/>");
}
QString IconFactory::svgOpen()
{
    return QStringLiteral("<path d=\"M1 4h5l1.5 2H15v8H1V4z\" fill=\"none\" stroke=\"currentColor\" "
                          "stroke-width=\"1.2\"/>");
}
QString IconFactory::svgSave()
{
    return QStringLiteral("<path d=\"M2 2h9l3 3v9H2V2zm2 0v4h7V2M4 10h8v4H4z\" fill=\"none\" "
                          "stroke=\"currentColor\" stroke-width=\"1.2\"/>");
}
QString IconFactory::svgRender()
{
    return QStringLiteral("<path d=\"M2 3h12v8H8.5L7 13v-2H2V3zm2 2v4h8V5H4z\" fill=\"none\" "
                          "stroke=\"currentColor\" stroke-width=\"1.2\"/>");
}
QString IconFactory::svgGear()
{
    return QStringLiteral(
        "<circle cx=\"8\" cy=\"8\" r=\"2.2\" fill=\"currentColor\"/>"
        "<path d=\"M8 1.5v1.6M8 12.9v1.6M1.5 8h1.6M12.9 8h1.6M3.3 3.3l1.1 1.1M11.6 11.6l1.1 1.1M12.7 "
        "3.3l-1.1 1.1M4.4 11.6l-1.1 1.1\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.2\" "
        "stroke-linecap=\"round\"/>");
}
QString IconFactory::svgCut()
{
    return QStringLiteral(
        "<circle cx=\"4\" cy=\"3.5\" r=\"1.8\" fill=\"currentColor\"/><circle cx=\"4\" cy=\"12.5\" "
        "r=\"1.8\" fill=\"currentColor\"/><path d=\"M5.5 4.5l6 7M5.5 11.5l6-7\" fill=\"none\" "
        "stroke=\"currentColor\" stroke-width=\"1.2\"/>");
}
QString IconFactory::svgCopy()
{
    return QStringLiteral("<path d=\"M5 2.5h8v10H5V2.5zm-2.5 2.5H5v9.5H2.5V5z\" fill=\"none\" "
                          "stroke=\"currentColor\" stroke-width=\"1.2\"/>");
}
QString IconFactory::svgPaste()
{
    return QStringLiteral(
        "<path d=\"M5 1.5h6v2.5H5V1.5zM3.5 4h9v10.5h-9V4zm2.5 3h4v1.2H6V7z\" fill=\"none\" "
        "stroke=\"currentColor\" stroke-width=\"1.2\"/>");
}
QString IconFactory::svgUndo()
{
    return QStringLiteral(
        "<path d=\"M2.5 7.5C4 4.5 9.5 4 12 7v2h-1.8V8c-1.4-1.8-4.2-2-5.8-.2L6 9.5H2V5l.5 2.5z\" "
        "fill=\"currentColor\"/>");
}
QString IconFactory::svgRedo()
{
    return QStringLiteral(
        "<path d=\"M13.5 7.5C12 4.5 6.5 4 4 7v2h1.8V8c1.4-1.8 4.2-2 5.8-.2L10 9.5h4V5l-.5 2.5z\" "
        "fill=\"currentColor\"/>");
}
QString IconFactory::svgPlay()
{
    return QStringLiteral("<path d=\"M4 2.8l9.5 5.2L4 13.2V2.8z\" fill=\"currentColor\"/>");
}
QString IconFactory::svgPause()
{
    return QStringLiteral(
        "<path d=\"M3.2 2.8h3.2v10.4H3.2V2.8zm6.4 0h3.2v10.4H9.6V2.8z\" fill=\"currentColor\"/>");
}
QString IconFactory::svgStop()
{
    return QStringLiteral("<path d=\"M3.4 3.4h9.2v9.2H3.4z\" fill=\"currentColor\"/>");
}
QString IconFactory::svgLoop()
{
    return QStringLiteral(
        "<path d=\"M3.2 8a4.8 4.8 0 018.2-3.2M12.2 3.2v3h-3M12.8 8a4.8 4.8 0 01-8.2 3.2M3.8 12.8v-3h3\" "
        "fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.35\" stroke-linecap=\"round\"/>");
}
QString IconFactory::svgPlayFromStart()
{
    return QStringLiteral(
        "<path d=\"M2.2 3.2v9.6h1.4V3.2H2.2zm3.2 0l8.8 4.8-8.8 4.8V3.2z\" fill=\"currentColor\"/>");
}
QString IconFactory::svgGoStart()
{
    return QStringLiteral(
        "<path d=\"M2.2 3.2v9.6h1.4V3.2H2.2zm11.6 0L6.2 8l7.6 4.8V3.2z\" fill=\"currentColor\"/>");
}
QString IconFactory::svgGoEnd()
{
    return QStringLiteral(
        "<path d=\"M12.4 3.2v9.6H14V3.2h-1.6zM2.2 3.2L9.8 8l-7.6 4.8V3.2z\" fill=\"currentColor\"/>");
}
QString IconFactory::svgPrevFrame()
{
    return QStringLiteral(
        "<path d=\"M10.8 3.2L5.2 8l5.6 4.8V3.2z\" fill=\"currentColor\"/><path d=\"M3.6 3.2v9.6H2.2V3.2h1.4zm1.8 "
        "0v9.6H4V3.2h1.4z\" fill=\"currentColor\"/>");
}
QString IconFactory::svgNextFrame()
{
    return QStringLiteral(
        "<path d=\"M5.2 3.2L10.8 8 5.2 12.8V3.2z\" fill=\"currentColor\"/><path d=\"M12.4 3.2v9.6H14V3.2h-1.6zm-1.8 "
        "0v9.6h1.4V3.2h-1.4z\" fill=\"currentColor\"/>");
}
QString IconFactory::svgEditNormal()
{
    return QStringLiteral(
        "<path d=\"M3.2 1.6l9.2 5.6-3.4 1.15 2.35 5.45-2.15.9-2.35-5.45-3.35 2.1z\" fill=\"currentColor\"/>");
}
QString IconFactory::svgEnvelope()
{
    return QStringLiteral(
        "<path d=\"M2 12.5 L5 4.5 L9 10 L14 3.5\" fill=\"none\" stroke=\"currentColor\" "
        "stroke-width=\"1.35\"/><rect x=\"1.2\" y=\"11.5\" width=\"2.2\" height=\"2.2\" rx=\"0.3\" "
        "fill=\"currentColor\"/><rect x=\"4.2\" y=\"3.5\" width=\"2.2\" height=\"2.2\" rx=\"0.3\" "
        "fill=\"currentColor\"/><rect x=\"8.2\" y=\"9\" width=\"2.2\" height=\"2.2\" rx=\"0.3\" "
        "fill=\"currentColor\"/><rect x=\"13\" y=\"2.5\" width=\"2.2\" height=\"2.2\" rx=\"0.3\" "
        "fill=\"currentColor\"/>");
}
QString IconFactory::svgSelection()
{
    return QStringLiteral(
        "<rect x=\"2.5\" y=\"2.5\" width=\"11\" height=\"11\" fill=\"none\" stroke=\"currentColor\" "
        "stroke-width=\"1.25\" stroke-dasharray=\"2 1.5\"/>");
}
QString IconFactory::svgZoom()
{
    return QStringLiteral(
        "<circle cx=\"7\" cy=\"7\" r=\"4.2\" fill=\"none\" stroke=\"currentColor\" "
        "stroke-width=\"1.35\"/><path d=\"M10.2 10.2l3.6 3.6\" stroke=\"currentColor\" stroke-width=\"1.45\" "
        "fill=\"none\" stroke-linecap=\"round\"/>");
}
QString IconFactory::svgSnap()
{
    return QStringLiteral(
        "<path d=\"M8.2 2.2c-1.7 0-3 1.2-3.3 2.8H3.4v2.2h1.1c.15.55.4 1.05.75 1.45L3.4 10.7l1.5 1.5 "
        "1.9-2.05c.55.4 1.2.65 1.9.7V13.8h2.2v-2.9c.7-.1 1.35-.35 1.9-.75l1.95 2.1 1.5-1.5-1.85-2c.35-.4.6-.9.75-"
        "1.45h1.15V5h-1.55C12.9 3.4 11.55 2.2 9.8 2.2H8.2zm.8 2.2c1.1 0 1.9.8 1.9 1.85S10.1 8.1 9 8.1 7.1 7.3 "
        "7.1 6.25 7.9 4.4 9 4.4z\" fill=\"currentColor\"/>");
}
QString IconFactory::svgAutoCf()
{
    return QStringLiteral(
        "<path d=\"M2.5 2.5h11v11h-11z\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.2\"/><path "
        "d=\"M2.5 13.5L13.5 2.5\" stroke=\"currentColor\" stroke-width=\"1.25\"/><path d=\"M2.5 2.5h11L2.5 "
        "13.5z\" fill=\"currentColor\" opacity=\".4\"/>");
}
QString IconFactory::svgImport()
{
    return QStringLiteral(
        "<path d=\"M8 2.5v8M5 8l3 3 3-3M3 13.5h10\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.3\" "
        "stroke-linecap=\"round\" stroke-linejoin=\"round\"/>");
}
QString IconFactory::svgSearch()
{
    return QStringLiteral(
        "<circle cx=\"7\" cy=\"7\" r=\"4\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.3\"/><path "
        "d=\"M10 10l3.5 3.5\" stroke=\"currentColor\" stroke-width=\"1.4\" fill=\"none\" "
        "stroke-linecap=\"round\"/>");
}
QString IconFactory::svgFx()
{
    return QStringLiteral(
        "<path d=\"M2.5 12.5L8 2.8l5.5 9.7H2.5z\" fill=\"none\" stroke=\"currentColor\" "
        "stroke-width=\"1.2\"/><path d=\"M6.2 9.2h3.6M8 7.4v4\" stroke=\"currentColor\" "
        "stroke-width=\"1.2\" stroke-linecap=\"round\"/>");
}
QString IconFactory::svgMute()
{
    return QStringLiteral(
        "<path d=\"M3 6h3l3-2.5v9L6 10H3V6z\" fill=\"currentColor\"/><path d=\"M11 6l3 4M14 6l-3 4\" "
        "stroke=\"currentColor\" stroke-width=\"1.3\" fill=\"none\" stroke-linecap=\"round\"/>");
}
QString IconFactory::svgSolo()
{
    return QStringLiteral(
        "<circle cx=\"8\" cy=\"8\" r=\"5.5\" fill=\"none\" stroke=\"currentColor\" "
        "stroke-width=\"1.2\"/><path d=\"M6.5 5.5v5h1.4c1.3 0 2.1-.7 2.1-1.7S9.2 7 8 7H7.9V5.5H6.5zm1.4 "
        "3.2h.3c.45 0 .8.2.8.55s-.35.55-.8.55h-.3V8.7z\" fill=\"currentColor\"/>");
}
QString IconFactory::svgRecord()
{
    return QStringLiteral(
        "<circle cx=\"8\" cy=\"8\" r=\"5.2\" fill=\"#c42b1c\"/><circle cx=\"8\" cy=\"8\" r=\"5.2\" "
        "fill=\"none\" stroke=\"#e05050\" stroke-width=\"0.8\"/>");
}
QString IconFactory::svgAutoPreview()
{
    return QStringLiteral(
        "<path d=\"M3.5 2.5h2v4h-2zM10.5 2.5h2v4h-2zM5.5 4.5h5M4.5 9.5l3.5 3.5 3.5-3.5\" fill=\"none\" "
        "stroke=\"currentColor\" stroke-width=\"1.25\"/><path d=\"M8 6.5v6.5\" stroke=\"currentColor\" "
        "stroke-width=\"1.25\"/>");
}
QString IconFactory::svgCapture()
{
    return QStringLiteral(
        "<rect x=\"2\" y=\"4.5\" width=\"9.5\" height=\"7\" rx=\"0.8\" fill=\"none\" stroke=\"currentColor\" "
        "stroke-width=\"1.2\"/><path d=\"M11.5 7l3-1.5v5L11.5 9V7z\" fill=\"currentColor\"/><circle cx=\"6.5\" "
        "cy=\"8\" r=\"1.6\" fill=\"#c42b1c\"/>");
}
QString IconFactory::svgCdExtract()
{
    return QStringLiteral(
        "<circle cx=\"7\" cy=\"8\" r=\"4.5\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.2\"/>"
        "<circle cx=\"7\" cy=\"8\" r=\"1.3\" fill=\"currentColor\"/><circle cx=\"12.2\" cy=\"12.2\" r=\"2.4\" "
        "fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.15\"/><path d=\"M13.7 13.7l1.5 1.5\" "
        "stroke=\"currentColor\" stroke-width=\"1.2\"/>");
}
QString IconFactory::svgWeb()
{
    return QStringLiteral(
        "<circle cx=\"8\" cy=\"8\" r=\"5.5\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.2\"/>"
        "<path d=\"M2.5 8h11M8 2.5c1.8 1.8 2.8 3.6 2.8 5.5S9.8 11.7 8 13.5C6.2 11.7 5.2 9.9 5.2 8S6.2 4.3 8 "
        "2.5z\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.1\"/>");
}
QString IconFactory::svgFilter()
{
    return QStringLiteral(
        "<path d=\"M2.5 3.5h11l-4 5v4l-3 1.5v-5.5z\" fill=\"none\" stroke=\"currentColor\" "
        "stroke-width=\"1.25\" stroke-linejoin=\"round\"/>");
}
QString IconFactory::svgViews()
{
    return QStringLiteral(
        "<path d=\"M2 2h5v5H2V2zm7 0h5v5H9V2zM2 9h5v5H2V9zm7 0h5v5H9V9z\" fill=\"currentColor\"/>");
}
QString IconFactory::svgWaveform()
{
    return QStringLiteral(
        "<path d=\"M2 10.5c1.2-3 2.2-4.5 3-4.5s1.5 3 2.5 3 1.8-4.5 2.5-4.5 1.8 3 3 4.5\" fill=\"none\" "
        "stroke=\"currentColor\" stroke-width=\"1.25\"/><path d=\"M2 13h12\" stroke=\"currentColor\" "
        "stroke-width=\"1.1\"/>");
}
QString IconFactory::svgRemove()
{
    return QStringLiteral(
        "<path d=\"M3.2 3.2l9.6 9.6M12.8 3.2L3.2 12.8\" stroke=\"#c42b1c\" fill=\"none\" stroke-width=\"1.7\" "
        "stroke-linecap=\"round\"/>");
}
QString IconFactory::svgAudioDevice()
{
    return QStringLiteral(
        "<path d=\"M2 6.5h2L6.5 4v8L4 9.5H2V6.5zM8.2 6.3a2 2 0 010 3.4M9.7 5.2a3.4 3.4 0 010 5.6\" "
        "fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.2\"/>"
        "<rect x=\"11\" y=\"2\" width=\"4\" height=\"4\" rx=\"0.6\" fill=\"currentColor\"/>");
}
QString IconFactory::svgDownmix()
{
    return QStringLiteral(
        "<path d=\"M2 6.5h2L6.5 4v8L4 9.5H2V6.5zM8.2 6.3a2 2 0 010 3.4M9.7 5.2a3.4 3.4 0 010 5.6\" "
        "fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.2\"/>"
        "<path d=\"M12 4.5v5.5M10.2 8.2L12 10.2l1.8-2\" fill=\"none\" stroke=\"currentColor\" "
        "stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>");
}
QString IconFactory::svgMixingConsole()
{
    return QStringLiteral(
        "<path d=\"M3 3v10M6.5 5v8M10 4v9M13.5 6v7\" stroke=\"currentColor\" stroke-width=\"1.4\" fill=\"none\"/>"
        "<circle cx=\"3\" cy=\"7\" r=\"1.2\" fill=\"currentColor\"/><circle cx=\"6.5\" cy=\"9\" r=\"1.2\" "
        "fill=\"currentColor\"/><circle cx=\"10\" cy=\"6.5\" r=\"1.2\" fill=\"currentColor\"/><circle "
        "cx=\"13.5\" cy=\"10\" r=\"1.2\" fill=\"currentColor\"/>");
}
QString IconFactory::svgExternalMonitor()
{
    return QStringLiteral(
        "<path d=\"M2 3.5h9v7H2v-7zm10 1.5h2v6h-2V5zM4.5 12.5h4\" fill=\"none\" stroke=\"currentColor\" "
        "stroke-width=\"1.2\"/>");
}
QString IconFactory::svgSplitScreen()
{
    return QStringLiteral(
        "<path d=\"M2.5 2.5h11v11h-11z\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.2\"/><path "
        "d=\"M2.5 13.5L13.5 2.5\" stroke=\"currentColor\" stroke-width=\"1.2\"/><path d=\"M2.5 2.5h11L2.5 "
        "13.5z\" fill=\"currentColor\" opacity=\".35\"/>");
}
QString IconFactory::svgOverlays()
{
    return QStringLiteral(
        "<text x=\"8\" y=\"12\" text-anchor=\"middle\" font-size=\"11\" font-weight=\"700\" "
        "fill=\"currentColor\">#</text>");
}
QString IconFactory::svgMore()
{
    return QStringLiteral(
        "<circle cx=\"3\" cy=\"8\" r=\"1.2\" fill=\"currentColor\"/><circle cx=\"8\" cy=\"8\" r=\"1.2\" "
        "fill=\"currentColor\"/><circle cx=\"13\" cy=\"8\" r=\"1.2\" fill=\"currentColor\"/>");
}
QString IconFactory::svgDelete()
{
    return QStringLiteral(
        "<path d=\"M4 4l8 8M12 4L4 12\" stroke=\"currentColor\" stroke-width=\"1.6\" fill=\"none\" "
        "stroke-linecap=\"round\"/>");
}
QString IconFactory::svgTrim()
{
    return QStringLiteral(
        "<path d=\"M8 2.5v11M3.5 5.5h4M8.5 10.5h4\" stroke=\"currentColor\" stroke-width=\"1.35\" fill=\"none\" "
        "stroke-linecap=\"round\"/>");
}
QString IconFactory::svgTrimStart()
{
    return QStringLiteral(
        "<path d=\"M6 3v10M6 3h5M6 13h5M3.5 5.5v5\" stroke=\"currentColor\" stroke-width=\"1.3\" fill=\"none\" "
        "stroke-linecap=\"round\"/>");
}
QString IconFactory::svgTrimEnd()
{
    return QStringLiteral(
        "<path d=\"M10 3v10M10 3H5M10 13H5M12.5 5.5v5\" stroke=\"currentColor\" stroke-width=\"1.3\" "
        "fill=\"none\" stroke-linecap=\"round\"/>");
}
QString IconFactory::svgSplit()
{
    return QStringLiteral(
        "<path d=\"M8 2.5v11M3 6.5h3.5M9.5 9.5H13\" stroke=\"currentColor\" stroke-width=\"1.35\" fill=\"none\" "
        "stroke-linecap=\"round\"/>");
}
QString IconFactory::svgHeal()
{
    return QStringLiteral(
        "<path d=\"M3 8h10M5.5 5.5L3 8l2.5 2.5M10.5 5.5L13 8l-2.5 2.5\" stroke=\"currentColor\" "
        "stroke-width=\"1.3\" fill=\"none\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>");
}
QString IconFactory::svgLock()
{
    return QStringLiteral(
        "<path d=\"M5 7V5.6a3 3 0 016 0V7h1.4v7.2H3.6V7H5zm1.4 0h3.2V5.6a1.6 1.6 0 00-3.2 0V7z\" "
        "fill=\"currentColor\"/>");
}
QString IconFactory::svgMarker()
{
    return QStringLiteral(
        "<path d=\"M4.2 2.2h6.2v7.2L7.3 12.4 4.2 9.4V2.2z\" fill=\"#e0a020\"/><path d=\"M5.6 2.2v12.5\" "
        "stroke=\"#e0a020\" stroke-width=\"1.25\"/>");
}
QString IconFactory::svgRegion()
{
    return QStringLiteral(
        "<path d=\"M2.2 2.2h4.2v6.4L4.3 11 2.2 8.6V2.2z\" fill=\"#e0a020\"/><path "
        "d=\"M13.8 2.2H9.6v6.4L11.7 11l2.1-2.4V2.2z\" fill=\"#e0a020\"/>");
}
QString IconFactory::svgAutoRipple()
{
    return QStringLiteral(
        "<rect x=\"2.2\" y=\"3.2\" width=\"4.2\" height=\"9.6\" rx=\"0.4\" fill=\"#4a9be8\"/><path "
        "d=\"M8 8h5.5M11.2 5.6L14 8l-2.8 2.4\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.3\" "
        "stroke-linecap=\"round\" stroke-linejoin=\"round\"/>");
}
QString IconFactory::svgLockEnvelopes()
{
    return QStringLiteral(
        "<path d=\"M3.2 9.2h4.2v4.2H3.2zM8.6 9.2h4.2v4.2H8.6z\" fill=\"none\" stroke=\"currentColor\" "
        "stroke-width=\"1.15\"/><path d=\"M5.5 5.2V4.3a2.5 2.5 0 015 0v.9h1.2v2.2H4.3V5.2H5.5zm1.3 "
        "0h2.4V4.3a1.2 1.2 0 00-2.4 0v.9z\" fill=\"currentColor\"/><path d=\"M7.4 11.3h1.2\" "
        "stroke=\"currentColor\" stroke-width=\"1.2\"/>");
}
QString IconFactory::svgIgnoreGrouping()
{
    return QStringLiteral(
        "<rect x=\"2.5\" y=\"3.5\" width=\"6.5\" height=\"5.5\" rx=\"0.4\" fill=\"none\" stroke=\"currentColor\" "
        "stroke-width=\"1.15\"/><rect x=\"7\" y=\"7\" width=\"6.5\" height=\"5.5\" rx=\"0.4\" fill=\"none\" "
        "stroke=\"currentColor\" stroke-width=\"1.15\"/><path "
        "d=\"M10.5 2.5l3.2 2-1.15.4.7 1.85-.95.35-.7-1.85-1.2.75z\" fill=\"currentColor\"/>");
}
QString IconFactory::svgColorGrade()
{
    return QStringLiteral(
        "<circle cx=\"6.2\" cy=\"7.2\" r=\"3.1\" fill=\"#c44\"/><circle cx=\"9.8\" cy=\"7.2\" r=\"3.1\" "
        "fill=\"#4a4\"/><circle cx=\"8\" cy=\"10.2\" r=\"3.1\" fill=\"#44a\"/><circle cx=\"6.2\" cy=\"7.2\" "
        "r=\"3.1\" fill=\"none\" stroke=\"#222\" stroke-width=\"0.4\"/><circle cx=\"9.8\" cy=\"7.2\" r=\"3.1\" "
        "fill=\"none\" stroke=\"#222\" stroke-width=\"0.4\"/><circle cx=\"8\" cy=\"10.2\" r=\"3.1\" fill=\"none\" "
        "stroke=\"#222\" stroke-width=\"0.4\"/>");
}
QString IconFactory::svgPasteAttr()
{
    return QStringLiteral(
        "<path d=\"M4 3.5h7.5v9H4z\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.2\"/><path "
        "d=\"M11 7.5h3.2M12.5 6l2 1.5-2 1.5\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.2\" "
        "stroke-linecap=\"round\"/>");
}
QString IconFactory::svgCopyAttr()
{
    return QStringLiteral(
        "<path d=\"M4.5 3.5H12v9H4.5z\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.2\"/><path "
        "d=\"M2.2 7.5H5M3.5 6L1.5 7.5 3.5 9\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.2\" "
        "stroke-linecap=\"round\"/>");
}
QString IconFactory::svgGroup()
{
    return QStringLiteral(
        "<path d=\"M3.5 3.5h4v4h-4zM8.5 8.5h4v4h-4z\" fill=\"none\" stroke=\"currentColor\" "
        "stroke-width=\"1.2\"/><path d=\"M7 7.5h2M8 6.5v2\" stroke=\"currentColor\" stroke-width=\"1.2\"/>");
}
QString IconFactory::svgAutomation()
{
    return QStringLiteral(
        "<circle cx=\"8\" cy=\"8\" r=\"5.4\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.2\"/>"
        "<circle cx=\"8\" cy=\"8\" r=\"1.6\" fill=\"currentColor\"/>"
        "<path d=\"M8 2.6v1.6M8 11.8v1.6M2.6 8h1.6M11.8 8h1.6\" stroke=\"currentColor\" "
        "stroke-width=\"1.1\" stroke-linecap=\"round\"/>");
}
QString IconFactory::svgLockFader()
{
    return QStringLiteral(
        "<path d=\"M4.67 6.93V5.07a3.33 3.33 0 016.66 0v1.86H12.67v7.07H3.33V6.93H4.67zm1.6 0h3.46V5.07a1.73 "
        "1.73 0 00-3.46 0v1.86z\" fill=\"currentColor\"/>");
}
QString IconFactory::svgMasterTitle()
{
    return QStringLiteral(
        "<rect x=\"2\" y=\"2\" width=\"12\" height=\"12\" fill=\"none\" stroke=\"currentColor\" "
        "stroke-width=\"1.2\"/><rect x=\"5.5\" y=\"5.5\" width=\"5\" height=\"5\" fill=\"currentColor\"/>");
}

} // namespace openvegas
