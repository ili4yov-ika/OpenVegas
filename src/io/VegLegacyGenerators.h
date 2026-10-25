#pragma once

#include <QByteArray>
#include <QString>
#include <QVariantMap>

namespace openvegas {

/** CLSID VEGAS 22 still writes for its legacy DirectX Color Gradient generator. */
inline QString vegColorGradientClsid()
{
    return QStringLiteral("{8B5DEABC-53BD-49F6-9803-367478307A59}");
}

/** CLSID of the legacy DirectX Credit Roll generator ("DXSCredits"). */
inline QString vegCreditRollClsid()
{
    return QStringLiteral("{02FD3B60-986B-11D4-9F6F-00485481BD31}");
}

/**
 * The stored parameters of a legacy (DirectX) generator, from the bytes that follow its
 * preset name in the media-pool entry. Empty when the CLSID is not one of the two or the
 * block is too short. See MARKDOWN/MEDIA_GENERATORS_VEGAS22_AUDIT.md for the layouts.
 *
 * Color Gradient:
 *   GradientType  int — 0 Linear, 1 Elliptical, 2 Rectangular (sftrans1 strings 1965–1967)
 *   AspectAngle   double, radians — the gradient's direction, or an ellipse's proportions
 *   Points        list of {X, Y, Color}: X/Y in 0..1 of the frame, Color [r, g, b, a] 0..1
 *
 * Credit Roll:
 *   Effect        int — 0 Scrolling Credits, 1 Timed Sequence
 *   Left, Right, Top   double — the text area, 0..1 of the frame
 *   InEffect, OutEffect, Display   int — Timed Sequence's lists (strings 1891–1921, 1887–1890)
 *   Connector     what fills the gap of a two-column line, repeated: "" (spaces), "." (dots),
 *                 ". " (sparse dots)…
 *   Background    [r, g, b, a]
 *   ScrollDirection  int — 0 Up (Forward), 1 Down (Backward)
 *   Styles        list of {Font, Size, Bold, Color, Align, Tracking, SpaceBelow, Scale}
 *   Text          the credits, one line each: a leading space makes a header, a tab
 *                 splits a two-column item (sftrans1 string 6050–6054)
 */
QVariantMap vegLegacyGeneratorParams(const QString &clsid, const QByteArray &body);

} // namespace openvegas
