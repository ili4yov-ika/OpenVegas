#include "ui/VideoEventFxDialogExact.h"

#include "audio/BuiltinDsp.h"
#include "io/MediaProbe.h"
#include "io/MediaThumbCache.h"
#include "plugins/AudioPluginTypes.h"
#include "plugins/OfxHost.h"
#include "plugins/VegasVideoPluginCatalog.h"
#include "video/ColorCorrectorApply.h"
#include "video/VideoFrameCache.h"
#include "video/VideoKeyframeEval.h"
#include "ui/AudioEventFxDialog.h"
#include "ui/KeyframeLaneWidgets.h"
#include "ui/PluginChooserDialog.h"

#include <QSlider>
#include <QAbstractSpinBox>
#include <QActionGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineF>
#include <QMenu>
#include <QMouseEvent>
#include <QObject>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QWheelEvent>
#include <QVBoxLayout>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <functional>

namespace openvegas {

namespace {

QDoubleSpinBox *makeSpin(QWidget *parent, double minV, double maxV, int decimals, double step = 1.0)
{
    auto *s = new QDoubleSpinBox(parent);
    s->setObjectName(QStringLiteral("tmSpin"));
    s->setRange(minV, maxV);
    s->setDecimals(decimals);
    s->setSingleStep(step);
    s->setButtonSymbols(QAbstractSpinBox::NoButtons);
    s->setAlignment(Qt::AlignRight);
    s->setFixedWidth(96);
    s->setFixedHeight(20);
    s->setKeyboardTracking(false);
    return s;
}

QWidget *makePropRow(QWidget *parent, const QString &name, QWidget *editor)
{
    auto *row = new QWidget(parent);
    row->setObjectName(QStringLiteral("pcProp"));
    row->setFixedHeight(20);
    auto *lay = new QHBoxLayout(row);
    lay->setContentsMargins(4, 0, 4, 0);
    lay->setSpacing(4);
    auto *n = new QLabel(name, row);
    n->setObjectName(QStringLiteral("pcPropName"));
    lay->addWidget(n, 1);
    lay->addWidget(editor, 0);
    return row;
}

QToolButton *makeTool(QWidget *parent, const QString &text, const QString &tip, bool checkable = true)
{
    auto *b = new QToolButton(parent);
    b->setObjectName(QStringLiteral("pcTool"));
    b->setText(text);
    b->setToolTip(tip);
    b->setFixedSize(22, 22);
    b->setCheckable(checkable);
    b->setAutoRaise(false);
    return b;
}

QPushButton *makeIcoBtn(QWidget *parent, const QString &text, const QString &tip)
{
    auto *b = new QPushButton(text, parent);
    b->setObjectName(QStringLiteral("pcIcoBtn"));
    b->setToolTip(tip);
    b->setFixedHeight(20);
    b->setMinimumWidth(22);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

double snapValue(double v, double grid, bool on)
{
    if (!on || grid <= 1e-9) {
        return v;
    }
    return std::round(v / grid) * grid;
}

} // namespace

class PanCropCanvas : public QWidget {
public:
    enum class Channel { Position, Mask };
    enum class MaskTool { NormalEdit, AddAnchor, DeleteAnchor, Rectangle, Oval };
    enum class MoveAxis { Free, XOnly, YOnly };
    enum class Hit {
        None,
        Move,
        Rotate,
        TL,
        TR,
        BL,
        BR,
        T,
        B,
        L,
        R
    };

    explicit PanCropCanvas(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("pcCanvas"));
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMinimumSize(240, 180);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
    }

    void setChannel(Channel ch)
    {
        m_channel = ch;
        m_dragging = false;
        m_shapeDrag = false;
        m_hit = Hit::None;
        update();
    }

    void setMaskTool(MaskTool tool)
    {
        m_maskTool = tool;
        m_shapeDrag = false;
        update();
    }

    /** Coordinate space for KF (media pixels when Match Output Aspect stores media-space values). */
    void setSpaces(int spaceW, int spaceH, int projectW, int projectH)
    {
        m_frameW = std::max(1, spaceW);
        m_frameH = std::max(1, spaceH);
        m_projectW = std::max(1, projectW);
        m_projectH = std::max(1, projectH);
        update();
    }

    void setKeyframe(const PanCropKeyframe &kf, int spaceW, int spaceH)
    {
        m_kf = kf;
        m_frameW = std::max(1, spaceW);
        m_frameH = std::max(1, spaceH);
        update();
    }

    void setMaskKeyframe(const MaskKeyframe &mk, bool enabled, int spaceW, int spaceH)
    {
        m_maskKf = mk;
        m_maskEnabled = enabled;
        m_frameW = std::max(1, spaceW);
        m_frameH = std::max(1, spaceH);
        update();
    }

    void setWorkspace(double zoomPct, double ox, double oy, int grid)
    {
        m_zoom = std::clamp(zoomPct, 5.0, 400.0);
        m_wsX = ox;
        m_wsY = oy;
        m_grid = std::max(1, grid);
        update();
    }

    void setOptions(bool lockAspect, bool sizeAboutCenter, bool snapping, MoveAxis axis,
                    bool zoomTool)
    {
        m_lockAspect = lockAspect;
        m_sizeAboutCenter = sizeAboutCenter;
        m_snapping = snapping;
        m_moveAxis = axis;
        m_zoomTool = zoomTool;
    }

    void setMediaPixmap(const QPixmap &pm)
    {
        m_media = pm;
        update();
    }

    void setOnEdited(const std::function<void(const PanCropKeyframe &)> &fn) { m_onEdited = fn; }
    void setOnMaskEdited(const std::function<void(const MaskKeyframe &)> &fn)
    {
        m_onMaskEdited = fn;
    }
    void setOnMaskSelection(const std::function<void(int, int)> &fn)
    {
        m_onMaskSelection = fn;
    }
    void setOnWorkspace(const std::function<void(double, double, double)> &fn)
    {
        m_onWorkspace = fn;
    }

    void setMaskSelection(int pathIndex, int anchorIndex)
    {
        m_selPath = pathIndex;
        m_selAnchor = anchorIndex;
        update();
    }

    void flipHorizontal()
    {
        m_kf.width = -m_kf.width;
        update();
        if (m_onEdited) {
            m_onEdited(m_kf);
        }
    }

    void flipVertical()
    {
        m_kf.height = -m_kf.height;
        update();
        if (m_onEdited) {
            m_onEdited(m_kf);
        }
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0xc8, 0xc8, 0xc8));
        const int step = std::max(4, m_grid / 2);
        p.setPen(QColor(0x9a, 0x9a, 0x9a));
        for (int y = 5; y < height(); y += step) {
            for (int x = 5; x < width(); x += step) {
                p.drawPoint(x, y);
            }
        }

        const QRectF frame = frameRect();
        if (!m_media.isNull()) {
            p.setRenderHint(QPainter::SmoothPixmapTransform, true);
            // Source fills the media-aspect workspace (Vegas Event Pan/Crop).
            p.drawPixmap(frame, m_media, QRectF(m_media.rect()));
        } else {
            p.fillRect(frame, QColor(0x1a, 0x1a, 0x1a));
            p.setPen(QPen(QColor(0x2e, 0x2e, 0x2e), 1));
            for (int i = -int(frame.height()); i < int(frame.width() + frame.height()); i += 6) {
                p.drawLine(frame.left() + i, frame.top(), frame.left() + i + frame.height(),
                           frame.bottom());
            }
        }
        p.setPen(QPen(QColor(0x22, 0x22, 0x22), 1));
        p.drawRect(frame);

        if (m_channel == Channel::Mask) {
            paintMaskLayer(p, frame);
            return;
        }

        const QRectF crop = cropRect(frame);
        p.save();
        p.translate(crop.center());
        p.rotate(m_kf.angleDeg);
        p.translate(-crop.center());

        p.save();
        p.translate(crop.center());
        p.scale(m_kf.width < 0.0 ? -1.0 : 1.0, m_kf.height < 0.0 ? -1.0 : 1.0);
        p.translate(-crop.center());
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(QColor(255, 255, 255, 160), 1.5, Qt::DashLine));
        drawOrientationF(p, crop);
        p.restore();

        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(QPen(QColor(255, 255, 255, 242), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(crop);

        auto handle = [&](QPointF pt) {
            p.fillRect(QRectF(pt.x() - 3.5, pt.y() - 3.5, 7, 7), Qt::white);
            p.setPen(QColor(0x22, 0x22, 0x22));
            p.drawRect(QRectF(pt.x() - 3.5, pt.y() - 3.5, 6, 6));
            p.setPen(QPen(QColor(255, 255, 255, 242), 1));
        };
        handle(crop.topLeft());
        handle(crop.topRight());
        handle(crop.bottomLeft());
        handle(crop.bottomRight());
        handle(QPointF(crop.center().x(), crop.top()));
        handle(QPointF(crop.center().x(), crop.bottom()));
        handle(QPointF(crop.left(), crop.center().y()));
        handle(QPointF(crop.right(), crop.center().y()));

        p.setBrush(Qt::white);
        p.setPen(QPen(Qt::black, 1));
        p.drawEllipse(crop.center(), 4, 4);
        p.drawLine(crop.center() + QPointF(-8, 0), crop.center() + QPointF(8, 0));
        p.drawLine(crop.center() + QPointF(0, -8), crop.center() + QPointF(0, 8));
        p.restore();

        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(QColor(255, 255, 255, 200), 1.5, Qt::DashLine));
        const qreal orbit = rotationOrbit(crop);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(crop.center(), orbit, orbit);
        const QPointF rotHandle = rotationHandlePos(crop);
        p.setBrush(QColor(255, 255, 255, 230));
        p.setPen(QPen(QColor(40, 40, 40), 1));
        p.drawEllipse(rotHandle, 5.5, 5.5);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton) {
            return;
        }
        if (m_zoomTool) {
            const bool out = event->modifiers() & Qt::ControlModifier;
            m_zoom = std::clamp(m_zoom * (out ? 0.85 : 1.15), 5.0, 400.0);
            if (m_onWorkspace) {
                m_onWorkspace(m_zoom, m_wsX, m_wsY);
            }
            update();
            return;
        }
        if (m_channel == Channel::Mask) {
            maskMousePress(event);
            return;
        }
        m_press = event->position();
        m_start = m_kf;
        m_hit = hitTest(m_press);
        m_dragging = (m_hit != Hit::None);
        m_dragMods = event->modifiers();
        if (m_hit == Hit::Move) {
            setCursor(Qt::SizeAllCursor);
        } else if (m_hit == Hit::Rotate) {
            setCursor(Qt::ClosedHandCursor);
        } else if (m_hit == Hit::L || m_hit == Hit::R) {
            setCursor(Qt::SizeHorCursor);
        } else if (m_hit == Hit::T || m_hit == Hit::B) {
            setCursor(Qt::SizeVerCursor);
        } else if (m_hit != Hit::None) {
            setCursor(Qt::SizeFDiagCursor);
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_channel == Channel::Mask) {
            maskMouseMove(event);
            return;
        }
        if (!m_dragging) {
            const Hit h = hitTest(event->position());
            if (h == Hit::Move) {
                setCursor(Qt::SizeAllCursor);
            } else if (h == Hit::Rotate) {
                setCursor(Qt::OpenHandCursor);
            } else if (h == Hit::L || h == Hit::R) {
                setCursor(Qt::SizeHorCursor);
            } else if (h == Hit::T || h == Hit::B) {
                setCursor(Qt::SizeVerCursor);
            } else if (h != Hit::None) {
                setCursor(Qt::SizeFDiagCursor);
            } else {
                setCursor(m_zoomTool ? Qt::CrossCursor : Qt::ArrowCursor);
            }
            return;
        }

        m_dragMods = event->modifiers();
        const QRectF fr = frameRect();
        const double sx = m_frameW / std::max(1.0, fr.width());
        const double sy = m_frameH / std::max(1.0, fr.height());

        if (m_hit == Hit::Move) {
            const QPointF d = event->position() - m_press;
            double ndx = d.x() * sx;
            double ndy = d.y() * sy;
            if (m_moveAxis == MoveAxis::XOnly) {
                ndy = 0;
            } else if (m_moveAxis == MoveAxis::YOnly) {
                ndx = 0;
            }
            const bool snap = useSnapping();
            m_kf.xCenter = snapValue(m_start.xCenter + ndx, m_grid, snap);
            m_kf.yCenter = snapValue(m_start.yCenter + ndy, m_grid, snap);
            m_kf.rotationXCenter = m_kf.xCenter;
            m_kf.rotationYCenter = m_kf.yCenter;
        } else if (m_hit == Hit::Rotate) {
            const QPointF c = cropRect(fr).center();
            const double a0 = std::atan2(m_press.y() - c.y(), m_press.x() - c.x());
            const double a1 =
                std::atan2(event->position().y() - c.y(), event->position().x() - c.x());
            m_kf.angleDeg = m_start.angleDeg + qRadiansToDegrees(a1 - a0);
            if (useSnapping()) {
                m_kf.angleDeg = snapValue(m_kf.angleDeg, 5.0, true);
            }
        } else {
            const QPointF local = rotateVec(event->position() - m_press, -m_start.angleDeg);
            resizeFromHit(local.x() * sx, local.y() * sy);
        }

        update();
        if (m_onEdited) {
            m_onEdited(m_kf);
        }
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (m_channel == Channel::Mask) {
            maskMouseRelease(event);
            return;
        }
        m_dragging = false;
        m_hit = Hit::None;
        setCursor(Qt::ArrowCursor);
    }

    void wheelEvent(QWheelEvent *event) override
    {
        const double factor = event->angleDelta().y() > 0 ? 1.1 : 0.9;
        m_zoom = std::clamp(m_zoom * factor, 5.0, 400.0);
        if (m_onWorkspace) {
            m_onWorkspace(m_zoom, m_wsX, m_wsY);
        }
        update();
        event->accept();
    }

    void contextMenuEvent(QContextMenuEvent *event) override
    {
        if (m_channel == Channel::Mask) {
            event->accept();
            return;
        }
        QMenu menu(this);
        QAction *restoreAct = menu.addAction(tr("Restore"));
        QAction *centerAct = menu.addAction(tr("Center"));
        QAction *flipHAct = menu.addAction(tr("Flip Horizontal"));
        QAction *flipVAct = menu.addAction(tr("Flip Vertical"));
        QAction *matchOutAct = menu.addAction(tr("Match Output Aspect"));
        QAction *matchSrcAct = menu.addAction(tr("Match Source Aspect"));

        QAction *chosen = menu.exec(event->globalPos());
        if (!chosen) {
            return;
        }

        if (chosen == restoreAct) {
            const double t = m_kf.timeSec;
            const double smooth = m_kf.smoothness;
            const VideoKeyframeType type = m_kf.type;
            m_kf = EventPanCropState::identityKeyframe(m_frameW, m_frameH);
            m_kf.timeSec = t;
            m_kf.smoothness = smooth;
            m_kf.type = type;
        } else if (chosen == centerAct) {
            m_kf.xCenter = m_frameW * 0.5;
            m_kf.yCenter = m_frameH * 0.5;
            m_kf.rotationXCenter = m_kf.xCenter;
            m_kf.rotationYCenter = m_kf.yCenter;
        } else if (chosen == flipHAct) {
            flipHorizontal();
            event->accept();
            return;
        } else if (chosen == flipVAct) {
            flipVertical();
            event->accept();
            return;
        } else if (chosen == matchOutAct) {
            // Largest crop with project AR that fits in workspace (media) — Vegas Match Output Aspect.
            const double aspect = double(m_projectW) / double(m_projectH);
            const double signW = m_kf.width < 0 ? -1.0 : 1.0;
            const double signH = m_kf.height < 0 ? -1.0 : 1.0;
            double w = double(m_frameH) * aspect;
            double h = double(m_frameH);
            if (w > double(m_frameW)) {
                w = double(m_frameW);
                h = w / aspect;
            }
            m_kf.width = std::max(8.0, w) * signW;
            m_kf.height = std::max(8.0, h) * signH;
            m_kf.xCenter = m_frameW * 0.5;
            m_kf.yCenter = m_frameH * 0.5;
            m_kf.rotationXCenter = m_kf.xCenter;
            m_kf.rotationYCenter = m_kf.yCenter;
        } else if (chosen == matchSrcAct) {
            const double signW = m_kf.width < 0 ? -1.0 : 1.0;
            const double signH = m_kf.height < 0 ? -1.0 : 1.0;
            m_kf.width = double(m_frameW) * signW;
            m_kf.height = double(m_frameH) * signH;
            m_kf.xCenter = m_frameW * 0.5;
            m_kf.yCenter = m_frameH * 0.5;
            m_kf.rotationXCenter = m_kf.xCenter;
            m_kf.rotationYCenter = m_kf.yCenter;
        }

        update();
        if (m_onEdited) {
            m_onEdited(m_kf);
        }
        event->accept();
    }

private:
    static constexpr double kBezierKappa = 0.5522847498;

    static QPointF rotateVec(const QPointF &v, double deg)
    {
        const double rad = qDegreesToRadians(deg);
        const double c = std::cos(rad);
        const double s = std::sin(rad);
        return QPointF(v.x() * c - v.y() * s, v.x() * s + v.y() * c);
    }

    static QPointF mapAround(const QPointF &pt, const QPointF &center, double deg)
    {
        return center + rotateVec(pt - center, deg);
    }

    bool useSnapping() const
    {
        return m_snapping && !(m_dragMods & Qt::ShiftModifier);
    }

    bool useLockAspect() const
    {
        return m_lockAspect && !(m_dragMods & Qt::ControlModifier);
    }

    QPointF widgetToFrame(const QPointF &pt, const QRectF &frame) const
    {
        const double sx = m_frameW / std::max(1.0, frame.width());
        const double sy = m_frameH / std::max(1.0, frame.height());
        return QPointF((pt.x() - frame.left()) * sx, (pt.y() - frame.top()) * sy);
    }

    QPointF frameToWidget(const QPointF &fp, const QRectF &frame) const
    {
        const double sx = frame.width() / m_frameW;
        const double sy = frame.height() / m_frameH;
        return QPointF(frame.left() + fp.x() * sx, frame.top() + fp.y() * sy);
    }

    MaskPath &ensureActivePath()
    {
        if (m_maskKf.paths.isEmpty()) {
            m_maskKf.paths.push_back(MaskPath{});
        }
        return m_maskKf.paths[0];
    }

    void emitMaskEdited()
    {
        update();
        if (m_onMaskEdited) {
            m_onMaskEdited(m_maskKf);
        }
    }

    void emitMaskSelection()
    {
        if (m_onMaskSelection) {
            m_onMaskSelection(m_selPath, m_selAnchor);
        }
    }

    QPainterPath pathToWidget(const MaskPath &path, const QRectF &frame) const
    {
        QPainterPath out;
        if (path.anchors.isEmpty()) {
            return out;
        }
        const MaskAnchor &a0 = path.anchors[0];
        out.moveTo(frameToWidget(QPointF(a0.x, a0.y), frame));
        const int n = path.anchors.size();
        const int segs = path.closed ? n : n - 1;
        for (int i = 0; i < segs; ++i) {
            const MaskAnchor &a = path.anchors[i];
            const MaskAnchor &b = path.anchors[(i + 1) % n];
            const QPointF p1 = frameToWidget(QPointF(a.x + a.outX, a.y + a.outY), frame);
            const QPointF p2 = frameToWidget(QPointF(b.x + b.inX, b.y + b.inY), frame);
            const QPointF p3 = frameToWidget(QPointF(b.x, b.y), frame);
            out.cubicTo(p1, p2, p3);
        }
        if (path.closed) {
            out.closeSubpath();
        }
        return out;
    }

    void paintMaskLayer(QPainter &p, const QRectF &frame)
    {
        if (m_maskEnabled) {
            QPainterPath dimPath;
            dimPath.addRect(frame);
            for (const MaskPath &path : m_maskKf.paths) {
                if (path.mode == MaskPathMode::Positive && path.closed && path.anchors.size() >= 3) {
                    dimPath.setFillRule(Qt::OddEvenFill);
                    dimPath.addPath(pathToWidget(path, frame));
                }
            }
            p.save();
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, 110));
            p.drawPath(dimPath);
            p.restore();
        }

        p.setRenderHint(QPainter::Antialiasing, true);
        for (int pi = 0; pi < m_maskKf.paths.size(); ++pi) {
            const MaskPath &path = m_maskKf.paths[pi];
            if (path.mode == MaskPathMode::Disabled || path.anchors.isEmpty()) {
                continue;
            }
            const QPainterPath wpath = pathToWidget(path, frame);
            p.setPen(QPen(QColor(255, 255, 255, 220), 1.5, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawPath(wpath);

            for (int ai = 0; ai < path.anchors.size(); ++ai) {
                const MaskAnchor &a = path.anchors[ai];
                const QPointF wpt = frameToWidget(QPointF(a.x, a.y), frame);
                const bool sel = (pi == m_selPath && ai == m_selAnchor);
                const double hs = sel ? 4.5 : 3.5;
                p.setPen(QPen(sel ? QColor(255, 220, 0) : QColor(255, 255, 255), 1));
                p.setBrush(sel ? QColor(255, 220, 0) : Qt::white);
                p.drawRect(QRectF(wpt.x() - hs, wpt.y() - hs, hs * 2, hs * 2));
            }
        }

        if (m_shapeDrag) {
            const QRectF preview = QRectF(m_shapeStart, m_shapeCurrent).normalized();
            p.setPen(QPen(QColor(255, 255, 255, 180), 1, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            if (m_maskTool == MaskTool::Rectangle) {
                p.drawRect(preview);
            } else if (m_maskTool == MaskTool::Oval) {
                p.drawEllipse(preview);
            }
        }
    }

    bool findAnchorAt(const QPointF &pt, int *pathIndex, int *anchorIndex) const
    {
        const QRectF frame = frameRect();
        int bestPi = -1;
        int bestAi = -1;
        double bestD = 1e9;
        for (int pi = 0; pi < m_maskKf.paths.size(); ++pi) {
            const MaskPath &path = m_maskKf.paths[pi];
            for (int ai = 0; ai < path.anchors.size(); ++ai) {
                const MaskAnchor &a = path.anchors[ai];
                const QPointF wpt = frameToWidget(QPointF(a.x, a.y), frame);
                const double d = QLineF(wpt, pt).length();
                if (d < bestD) {
                    bestD = d;
                    bestPi = pi;
                    bestAi = ai;
                }
            }
        }
        if (bestPi >= 0 && bestD < 12.0) {
            if (pathIndex) {
                *pathIndex = bestPi;
            }
            if (anchorIndex) {
                *anchorIndex = bestAi;
            }
            return true;
        }
        return false;
    }

    MaskAnchor makeAnchor(double x, double y) const
    {
        MaskAnchor a;
        a.x = x;
        a.y = y;
        return a;
    }

    void setPathFromRect(const QRectF &frameRectPx, MaskPathMode mode)
    {
        MaskPath path;
        path.mode = mode;
        path.closed = true;
        path.anchors.push_back(makeAnchor(frameRectPx.left(), frameRectPx.top()));
        path.anchors.push_back(makeAnchor(frameRectPx.right(), frameRectPx.top()));
        path.anchors.push_back(makeAnchor(frameRectPx.right(), frameRectPx.bottom()));
        path.anchors.push_back(makeAnchor(frameRectPx.left(), frameRectPx.bottom()));
        m_maskKf.paths.clear();
        m_maskKf.paths.push_back(path);
        m_selPath = 0;
        m_selAnchor = 0;
    }

    void setPathFromOval(const QRectF &frameRectPx, MaskPathMode mode)
    {
        const double cx = frameRectPx.center().x();
        const double cy = frameRectPx.center().y();
        const double rx = std::max(4.0, frameRectPx.width() * 0.5);
        const double ry = std::max(4.0, frameRectPx.height() * 0.5);
        const double kx = kBezierKappa * rx;
        const double ky = kBezierKappa * ry;

        MaskPath path;
        path.mode = mode;
        path.closed = true;

        MaskAnchor top = makeAnchor(cx, cy - ry);
        top.outX = kx;
        top.inX = -kx;

        MaskAnchor right = makeAnchor(cx + rx, cy);
        right.outY = ky;
        right.inY = -ky;

        MaskAnchor bottom = makeAnchor(cx, cy + ry);
        bottom.outX = -kx;
        bottom.inX = kx;

        MaskAnchor left = makeAnchor(cx - rx, cy);
        left.outY = -ky;
        left.inY = ky;

        path.anchors = {top, right, bottom, left};
        m_maskKf.paths.clear();
        m_maskKf.paths.push_back(path);
        m_selPath = 0;
        m_selAnchor = 0;
    }

    MaskPathMode activePathMode() const
    {
        if (!m_maskKf.paths.isEmpty()) {
            return m_maskKf.paths[0].mode;
        }
        return MaskPathMode::Positive;
    }

    void maskMousePress(QMouseEvent *event)
    {
        m_press = event->position();
        m_dragMods = event->modifiers();
        const QRectF frame = frameRect();
        const QPointF fp = widgetToFrame(m_press, frame);

        if (m_maskTool == MaskTool::Rectangle || m_maskTool == MaskTool::Oval) {
            m_shapeDrag = true;
            m_shapeStart = m_press;
            m_shapeCurrent = m_press;
            return;
        }

        if (m_maskTool == MaskTool::DeleteAnchor) {
            int pi = -1;
            int ai = -1;
            if (findAnchorAt(m_press, &pi, &ai)) {
                m_maskKf.paths[pi].anchors.removeAt(ai);
                if (m_maskKf.paths[pi].anchors.isEmpty()) {
                    m_maskKf.paths.removeAt(pi);
                }
                m_selPath = -1;
                m_selAnchor = -1;
                emitMaskSelection();
                emitMaskEdited();
            }
            return;
        }

        if (m_maskTool == MaskTool::AddAnchor) {
            if (m_maskKf.paths.isEmpty() || m_maskKf.paths.last().closed) {
                MaskPath np;
                np.mode = activePathMode();
                np.closed = false;
                m_maskKf.paths.push_back(np);
            }
            MaskPath &path = m_maskKf.paths.last();
            if (!path.anchors.isEmpty() && !path.closed) {
                const MaskAnchor &first = path.anchors[0];
                const QPointF firstW = frameToWidget(QPointF(first.x, first.y), frame);
                if (QLineF(firstW, m_press).length() < 12.0 && path.anchors.size() >= 3) {
                    path.closed = true;
                    m_penOpen = false;
                    emitMaskEdited();
                    return;
                }
            }
            MaskAnchor a = makeAnchor(fp.x(), fp.y());
            if (useSnapping()) {
                a.x = snapValue(a.x, m_grid, true);
                a.y = snapValue(a.y, m_grid, true);
            }
            path.anchors.push_back(a);
            m_selPath = m_maskKf.paths.size() - 1;
            m_selAnchor = path.anchors.size() - 1;
            m_penOpen = !path.closed;
            emitMaskSelection();
            emitMaskEdited();
            return;
        }

        int pi = -1;
        int ai = -1;
        if (findAnchorAt(m_press, &pi, &ai)) {
            m_selPath = pi;
            m_selAnchor = ai;
            m_dragging = true;
            m_maskDragStart = m_maskKf.paths[pi].anchors[ai];
            emitMaskSelection();
            setCursor(Qt::SizeAllCursor);
            return;
        }
        m_selPath = -1;
        m_selAnchor = -1;
        emitMaskSelection();
        update();
    }

    void maskMouseMove(QMouseEvent *event)
    {
        m_dragMods = event->modifiers();
        if (m_shapeDrag) {
            m_shapeCurrent = event->position();
            update();
            return;
        }
        if (m_dragging && m_selPath >= 0 && m_selAnchor >= 0
            && m_selPath < m_maskKf.paths.size()
            && m_selAnchor < m_maskKf.paths[m_selPath].anchors.size()) {
            const QRectF frame = frameRect();
            const QPointF fp = widgetToFrame(event->position(), frame);
            const QPointF fp0 = widgetToFrame(m_press, frame);
            double nx = m_maskDragStart.x + (fp.x() - fp0.x());
            double ny = m_maskDragStart.y + (fp.y() - fp0.y());
            if (useSnapping()) {
                nx = snapValue(nx, m_grid, true);
                ny = snapValue(ny, m_grid, true);
            }
            MaskAnchor &a = m_maskKf.paths[m_selPath].anchors[m_selAnchor];
            a.x = nx;
            a.y = ny;
            emitMaskEdited();
            if (m_onMaskSelection) {
                m_onMaskSelection(m_selPath, m_selAnchor);
            }
            return;
        }

        int pi = -1;
        int ai = -1;
        if (findAnchorAt(event->position(), &pi, &ai)) {
            setCursor(Qt::SizeAllCursor);
        } else if (m_maskTool == MaskTool::AddAnchor) {
            setCursor(Qt::CrossCursor);
        } else if (m_maskTool == MaskTool::DeleteAnchor) {
            setCursor(Qt::PointingHandCursor);
        } else if (m_zoomTool) {
            setCursor(Qt::CrossCursor);
        } else {
            setCursor(Qt::ArrowCursor);
        }
    }

    void maskMouseRelease(QMouseEvent *event)
    {
        if (m_shapeDrag) {
            const QRectF frame = frameRect();
            const QPointF a = widgetToFrame(m_shapeStart, frame);
            const QPointF b = widgetToFrame(event->position(), frame);
            QRectF frPx = QRectF(a, b).normalized();
            if (frPx.width() > 4 && frPx.height() > 4) {
                if (useSnapping()) {
                    frPx.setLeft(snapValue(frPx.left(), m_grid, true));
                    frPx.setTop(snapValue(frPx.top(), m_grid, true));
                    frPx.setRight(snapValue(frPx.right(), m_grid, true));
                    frPx.setBottom(snapValue(frPx.bottom(), m_grid, true));
                }
                if (m_maskTool == MaskTool::Rectangle) {
                    setPathFromRect(frPx, activePathMode());
                } else {
                    setPathFromOval(frPx, activePathMode());
                }
                emitMaskSelection();
                emitMaskEdited();
            }
            m_shapeDrag = false;
            update();
            return;
        }
        m_dragging = false;
        setCursor(Qt::ArrowCursor);
    }

    qreal rotationOrbit(const QRectF &crop) const
    {
        return qMax(crop.width(), crop.height()) * 0.62;
    }

    QPointF rotationHandlePos(const QRectF &crop) const
    {
        return crop.center() + QPointF(rotationOrbit(crop), 0.0);
    }

    void matchAspect(double aspect)
    {
        if (aspect <= 1e-9) {
            return;
        }
        const double signW = m_kf.width < 0 ? -1.0 : 1.0;
        const double signH = m_kf.height < 0 ? -1.0 : 1.0;
        double w = std::abs(m_kf.width);
        double h = std::abs(m_kf.height);
        if (w < 1.0) {
            w = double(m_frameW);
        }
        if (h < 1.0) {
            h = double(m_frameH);
        }
        if (w >= h * aspect) {
            w = h * aspect;
        } else {
            h = w / aspect;
        }
        m_kf.width = std::max(8.0, w) * signW;
        m_kf.height = std::max(8.0, h) * signH;
    }

    void drawOrientationF(QPainter &p, const QRectF &r) const
    {
        const double x0 = r.left() + r.width() * 0.28;
        const double x1 = r.left() + r.width() * 0.72;
        const double y0 = r.top() + r.height() * 0.22;
        const double y1 = r.top() + r.height() * 0.78;
        const double ym = r.top() + r.height() * 0.48;
        p.drawLine(QPointF(x0, y0), QPointF(x0, y1));
        p.drawLine(QPointF(x0, y0), QPointF(x1, y0));
        p.drawLine(QPointF(x0, ym), QPointF(r.left() + r.width() * 0.58, ym));
    }

    QRectF frameRect() const
    {
        const double zoom = m_zoom / 100.0;
        const double aspect = double(m_frameW) / double(m_frameH);
        double fw = width() * 0.72 * zoom;
        double fh = fw / aspect;
        if (fh > height() * 0.78 * zoom) {
            fh = height() * 0.78 * zoom;
            fw = fh * aspect;
        }
        return QRectF((width() - fw) * 0.5 + m_wsX, (height() - fh) * 0.5 + m_wsY, fw, fh);
    }

    QRectF cropRect(const QRectF &frame) const
    {
        const double sx = frame.width() / m_frameW;
        const double sy = frame.height() / m_frameH;
        const double w = std::max(1.0, std::abs(m_kf.width)) * sx;
        const double h = std::max(1.0, std::abs(m_kf.height)) * sy;
        const double cx = frame.left() + m_kf.xCenter * sx;
        const double cy = frame.top() + m_kf.yCenter * sy;
        return QRectF(cx - w * 0.5, cy - h * 0.5, w, h);
    }

    Hit hitTest(const QPointF &pt) const
    {
        const QRectF fr = frameRect();
        const QRectF crop = cropRect(fr);
        const QPointF c = crop.center();
        const qreal orbit = rotationOrbit(crop);

        if (QLineF(rotationHandlePos(crop), pt).length() < 12.0) {
            return Hit::Rotate;
        }
        const qreal dOrbit = QLineF(c, pt).length();
        if (dOrbit > orbit * 0.86 && dOrbit < orbit * 1.16) {
            return Hit::Rotate;
        }

        // Handles / interior in crop-local space (undo Angle).
        const QPointF local = mapAround(pt, c, -m_kf.angleDeg);
        auto nearPt = [&](QPointF h) { return QLineF(h, local).length() < 11.0; };
        if (nearPt(crop.topLeft())) {
            return Hit::TL;
        }
        if (nearPt(crop.topRight())) {
            return Hit::TR;
        }
        if (nearPt(crop.bottomLeft())) {
            return Hit::BL;
        }
        if (nearPt(crop.bottomRight())) {
            return Hit::BR;
        }
        if (nearPt(QPointF(c.x(), crop.top()))) {
            return Hit::T;
        }
        if (nearPt(QPointF(c.x(), crop.bottom()))) {
            return Hit::B;
        }
        if (nearPt(QPointF(crop.left(), c.y()))) {
            return Hit::L;
        }
        if (nearPt(QPointF(crop.right(), c.y()))) {
            return Hit::R;
        }
        if (crop.adjusted(-4, -4, 4, 4).contains(local)) {
            return Hit::Move;
        }
        return Hit::None;
    }

    void resizeFromHit(double dx, double dy)
    {
        double w = std::abs(m_start.width);
        double h = std::abs(m_start.height);
        double cx = m_start.xCenter;
        double cy = m_start.yCenter;
        const double signW = m_start.width < 0 ? -1.0 : 1.0;
        const double signH = m_start.height < 0 ? -1.0 : 1.0;
        const double aspect = (h > 1e-6) ? (w / h) : 1.0;
        const bool lockAspect = useLockAspect();
        const bool snap = useSnapping();

        auto applyCorner = [&](double dw, double dh) {
            if (lockAspect) {
                if (std::abs(dw) >= std::abs(dh)) {
                    dh = dw / aspect;
                } else {
                    dw = dh * aspect;
                }
            }
            if (m_sizeAboutCenter) {
                w = std::max(8.0, w + dw * 2.0);
                h = std::max(8.0, h + dh * 2.0);
            } else {
                w = std::max(8.0, w + dw);
                h = std::max(8.0, h + dh);
                cx += dw * 0.5;
                cy += dh * 0.5;
            }
        };

        switch (m_hit) {
        case Hit::BR:
            applyCorner(dx, dy);
            break;
        case Hit::BL:
            applyCorner(-dx, dy);
            break;
        case Hit::TR:
            applyCorner(dx, -dy);
            break;
        case Hit::TL:
            applyCorner(-dx, -dy);
            break;
        case Hit::R:
            applyCorner(dx, lockAspect ? dx / aspect : 0.0);
            break;
        case Hit::L:
            applyCorner(-dx, lockAspect ? dx / aspect : 0.0);
            break;
        case Hit::B:
            applyCorner(lockAspect ? dy * aspect : 0.0, dy);
            break;
        case Hit::T:
            applyCorner(lockAspect ? dy * aspect : 0.0, -dy);
            break;
        default:
            break;
        }

        m_kf.width = snapValue(w, m_grid, snap) * signW;
        m_kf.height = snapValue(h, m_grid, snap) * signH;
        m_kf.xCenter = snapValue(cx, m_grid, snap);
        m_kf.yCenter = snapValue(cy, m_grid, snap);
        m_kf.rotationXCenter = m_kf.xCenter;
        m_kf.rotationYCenter = m_kf.yCenter;
        m_kf.angleDeg = m_start.angleDeg;
    }

    PanCropKeyframe m_kf;
    MaskKeyframe m_maskKf;
    Channel m_channel = Channel::Position;
    MaskTool m_maskTool = MaskTool::NormalEdit;
    int m_frameW = 1920; // workspace / KF space (media or project)
    int m_frameH = 1080;
    int m_projectW = 1920;
    int m_projectH = 1080;
    double m_zoom = 15.4;
    double m_wsX = 0.0;
    double m_wsY = 0.0;
    int m_grid = 16;
    bool m_lockAspect = true;
    bool m_sizeAboutCenter = true;
    bool m_snapping = false;
    bool m_zoomTool = false;
    bool m_maskEnabled = false;
    bool m_dragging = false;
    bool m_shapeDrag = false;
    bool m_penOpen = false;
    MoveAxis m_moveAxis = MoveAxis::Free;
    Hit m_hit = Hit::None;
    int m_selPath = -1;
    int m_selAnchor = -1;
    Qt::KeyboardModifiers m_dragMods;
    QPointF m_press;
    QPointF m_shapeStart;
    QPointF m_shapeCurrent;
    PanCropKeyframe m_start;
    MaskAnchor m_maskDragStart;
    QPixmap m_media;
    std::function<void(const PanCropKeyframe &)> m_onEdited;
    std::function<void(const MaskKeyframe &)> m_onMaskEdited;
    std::function<void(int, int)> m_onMaskSelection;
    std::function<void(double, double, double)> m_onWorkspace;
};

VideoEventFxDialogExact::VideoEventFxDialogExact(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("pcWindow"));
    setWindowTitle(tr("Video Event FX"));
    setWindowFlags(Qt::Window | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
    setWindowModality(Qt::NonModal);
    setModal(false);
    resize(1100, 740);
    setMinimumSize(720, 520);
    buildUi();
}

void VideoEventFxDialogExact::setEvent(TrackEvent *ev, int frameW, int frameH, double playheadSec)
{
    m_event = ev;
    m_frameW = std::max(1, frameW);
    m_frameH = std::max(1, frameH);
    m_mediaW = 0;
    m_mediaH = 0;
    m_spaceW = m_frameW;
    m_spaceH = m_frameH;
    m_durationSec = ev ? std::max(1.0, ev->lengthSec) : 10.0;
    m_playheadSec = std::clamp(playheadSec, 0.0, m_durationSec);
    ensurePanCropFirst();
    if (m_event) {
        panCrop().ensureDefault(m_frameW, m_frameH);
        m_posIndex = panCrop().positionIndexAt(m_playheadSec);
        if (m_posIndex < 0) {
            m_posIndex = 0;
        }
        m_maskIndex = panCrop().maskIndexAt(m_playheadSec);
        if (m_maskIndex < 0) {
            m_maskIndex = 0;
        }
        if (m_titleExtra) {
            m_titleExtra->setText(m_event->name);
        }
        if (m_subtitle) {
            m_subtitle->setText(tr("Event Pan/Crop: "));
        }
    }
    resolvePanCropSpace();
    loadMediaPreview();
    pushCanvasSpaces();
    rebuildChain();
    selectPlugin(0);
}

void VideoEventFxDialogExact::resolvePanCropSpace()
{
    m_spaceW = m_frameW;
    m_spaceH = m_frameH;
    if (!m_event) {
        return;
    }
    if (m_mediaW < 1 || m_mediaH < 1) {
        if (!m_event->mediaPath.isEmpty()) {
            const MediaProbeInfo info = MediaProbe::probe(m_event->mediaPath);
            if (info.ok && info.width > 0 && info.height > 0) {
                m_mediaW = info.width;
                m_mediaH = info.height;
            }
        }
    }
    // VEG Match Output Aspect: KF in media pixels — infer source size if probe failed.
    bool mediaSpace = false;
    double inferW = 0.0;
    double inferH = 0.0;
    for (const PanCropKeyframe &kf : panCrop().positionKeyframes) {
        if (std::abs(kf.width) > m_frameW * 1.25 || std::abs(kf.height) > m_frameH * 1.25) {
            mediaSpace = true;
        }
        inferW = std::max(inferW, std::max(std::abs(kf.width), kf.xCenter * 2.0));
        inferH = std::max(inferH, std::max(std::abs(kf.height), kf.yCenter * 2.0));
    }
    if (m_mediaW < 1 || m_mediaH < 1) {
        if (mediaSpace && inferW >= 2.0 && inferH >= 2.0) {
            m_mediaW = int(std::lround(inferW));
            m_mediaH = int(std::lround(inferH));
        } else {
            return;
        }
    }
    if (mediaSpace) {
        m_spaceW = m_mediaW;
        m_spaceH = m_mediaH;
    }
}

void VideoEventFxDialogExact::pushCanvasSpaces()
{
    if (!m_canvas) {
        return;
    }
    m_canvas->setSpaces(m_spaceW, m_spaceH, m_frameW, m_frameH);
}

void VideoEventFxDialogExact::loadMediaPreview()
{
    if (!m_canvas || !m_event) {
        return;
    }
    QPixmap pm;
    const QString path = m_event->mediaPath;
    if (!path.isEmpty()) {
        const bool still = m_event->mediaKind == EventMediaKind::Still
                           || m_event->mediaKind == EventMediaKind::Title;
        // Prefer a real decoded frame at the Pan/Crop playhead (Vegas workspace preview).
        const QSize decodeSz = VideoFrameCache::cappedSize(
            QSize(m_spaceW > 1 ? m_spaceW : 960, m_spaceH > 1 ? m_spaceH : 540));
        const QImage frame =
            VideoFrameCache::instance().frameIfReady(path, m_playheadSec, decodeSz, still, true);
        if (!frame.isNull()) {
            pm = QPixmap::fromImage(frame);
        } else {
            if (!m_frameHooked) {
                connect(&VideoFrameCache::instance(), &VideoFrameCache::frameReady, this,
                        &VideoEventFxDialogExact::onPreviewFrameReady);
                m_frameHooked = true;
            }
            // Fallback shell/thumb while ffmpeg seeks.
            pm = MediaThumbCache::instance().pixmapIfReady(path, QSize(960, 540),
                                                           QStringLiteral("video"));
            if (pm.isNull()) {
                MediaThumbCache::instance().iconFor(path, QSize(960, 540), QStringLiteral("video"));
                if (!m_thumbHooked) {
                    connect(&MediaThumbCache::instance(), &MediaThumbCache::thumbnailReady, this,
                            &VideoEventFxDialogExact::onThumbnailReady);
                    m_thumbHooked = true;
                }
            }
        }
    }
    m_canvas->setMediaPixmap(pm);
    pushCanvasSpaces();
}

void VideoEventFxDialogExact::onThumbnailReady(const QString &path)
{
    if (m_event && path == m_event->mediaPath) {
        loadMediaPreview();
    }
}

void VideoEventFxDialogExact::onPreviewFrameReady(const QString &path)
{
    if (m_event && path == m_event->mediaPath) {
        loadMediaPreview();
    }
}

bool VideoEventFxDialogExact::isPanCropSlot(int index) const
{
    if (!m_event || index < 0 || index >= m_event->fxChain.size()) {
        return false;
    }
    return m_event->fxChain[index].displayName.compare(QStringLiteral("Pan/Crop"),
                                                       Qt::CaseInsensitive)
           == 0;
}

bool VideoEventFxDialogExact::isColorCorrectorSlot(int index) const
{
    if (!m_event || index < 0 || index >= m_event->fxChain.size()) {
        return false;
    }
    return isColorCorrectorName(m_event->fxChain[index].displayName);
}

QString VideoEventFxDialogExact::fxMasterAutomationId(const FxSlot &slot) const
{
    return QStringLiteral("fx:%1:_master").arg(slot.hostKey.isEmpty() ? slot.pluginId : slot.hostKey);
}

QString VideoEventFxDialogExact::fxParamAutomationId(const FxSlot &slot, const QString &paramKey) const
{
    return QStringLiteral("fx:%1:%2")
        .arg(slot.hostKey.isEmpty() ? slot.pluginId : slot.hostKey, paramKey);
}

AutomationLane *VideoEventFxDialogExact::findAutomationLane(const QString &targetId)
{
    if (!m_event) {
        return nullptr;
    }
    for (AutomationLane &lane : m_event->automationLanes) {
        if (lane.targetId == targetId) {
            return &lane;
        }
    }
    return nullptr;
}

AutomationLane &VideoEventFxDialogExact::ensureAutomationLane(const QString &targetId)
{
    if (AutomationLane *existing = findAutomationLane(targetId)) {
        return *existing;
    }
    AutomationLane lane;
    lane.targetId = targetId;
    m_event->automationLanes.push_back(lane);
    return m_event->automationLanes.last();
}

QVector<OfxParamInfo> VideoEventFxDialogExact::paramsInfoForSlot(const FxSlot &slot) const
{
    return VegasVideoPluginCatalog::paramsInfoForSlot(slot);
}

QVector<QPair<QString, QString>> VideoEventFxDialogExact::animatableParamsForSlot(
    const FxSlot &slot) const
{
    QVector<QPair<QString, QString>> out;
    for (const OfxParamInfo &info : paramsInfoForSlot(slot)) {
        out.push_back({info.label, info.name});
    }
    return out;
}

double VideoEventFxDialogExact::currentParamValue(const FxSlot &slot, const QString &paramKey) const
{
    if (isColorCorrectorName(slot.displayName)) {
        const ColorCorrectorParams p = colorCorrectorFromSlot(slot);
        if (paramKey == QLatin1String("brightness")) {
            return p.brightness;
        }
        if (paramKey == QLatin1String("contrast")) {
            return p.contrast;
        }
        if (paramKey == QLatin1String("saturation")) {
            return p.saturation;
        }
        if (paramKey == QLatin1String("gamma")) {
            return p.gamma;
        }
    }
    double def = 0.5;
    for (const OfxParamInfo &info : paramsInfoForSlot(slot)) {
        if (info.name == paramKey) {
            def = info.defaultValue;
            break;
        }
    }
    const QVariantMap m = unpackFxParams(slot.state);
    return m.value(paramKey, def).toDouble();
}

EventPanCropState &VideoEventFxDialogExact::panCrop()
{
    return m_event->panCrop;
}

PanCropKeyframe *VideoEventFxDialogExact::selectedKf()
{
    if (!m_event || m_posIndex < 0 || m_posIndex >= panCrop().positionKeyframes.size()) {
        return nullptr;
    }
    return &panCrop().positionKeyframes[m_posIndex];
}

MaskKeyframe *VideoEventFxDialogExact::selectedMaskKf()
{
    if (!m_event || m_maskIndex < 0 || m_maskIndex >= panCrop().maskKeyframes.size()) {
        return nullptr;
    }
    return &panCrop().maskKeyframes[m_maskIndex];
}

void VideoEventFxDialogExact::ensurePanCropFirst()
{
    if (!m_event) {
        return;
    }
    ensureFxFirst(m_event->fxChain, QStringLiteral("Pan/Crop"), PluginFormat::Builtin);
}

void VideoEventFxDialogExact::applyCanvasOptions()
{
    if (!m_canvas) {
        return;
    }
    PanCropCanvas::MoveAxis axis = PanCropCanvas::MoveAxis::Free;
    if (m_moveAxis == MoveAxis::XOnly) {
        axis = PanCropCanvas::MoveAxis::XOnly;
    } else if (m_moveAxis == MoveAxis::YOnly) {
        axis = PanCropCanvas::MoveAxis::YOnly;
    }
    m_canvas->setOptions(m_lockAspect, m_sizeAboutCenter, m_snapping, axis, m_zoomTool);
    m_canvas->setChannel(m_channel == PanCropChannel::Mask ? PanCropCanvas::Channel::Mask
                                                           : PanCropCanvas::Channel::Position);
}

void VideoEventFxDialogExact::flipHorizontal()
{
    if (m_canvas) {
        m_canvas->flipHorizontal();
    } else if (PanCropKeyframe *kf = selectedKf()) {
        kf->width = -kf->width;
        syncUiFromSelected();
    }
}

void VideoEventFxDialogExact::flipVertical()
{
    if (m_canvas) {
        m_canvas->flipVertical();
    } else if (PanCropKeyframe *kf = selectedKf()) {
        kf->height = -kf->height;
        syncUiFromSelected();
    }
}

void VideoEventFxDialogExact::syncFlipButtons()
{
    const PanCropKeyframe *kf = selectedKf();
    const bool flipH = kf && kf->width < 0.0;
    const bool flipV = kf && kf->height < 0.0;
    if (m_btnFlipH) {
        m_btnFlipH->setChecked(flipH);
    }
    if (m_btnFlipV) {
        m_btnFlipV->setChecked(flipV);
    }
}

void VideoEventFxDialogExact::selectPositionIndex(int index)
{
    if (!m_event || panCrop().positionKeyframes.isEmpty()) {
        return;
    }
    m_posIndex = std::clamp(index, 0, int(panCrop().positionKeyframes.size()) - 1);
    m_playheadSec = panCrop().positionKeyframes[m_posIndex].timeSec;
    if (m_channel == PanCropChannel::Position) {
        syncUiFromSelected();
    }
    refreshKeyframeLanes();
}

void VideoEventFxDialogExact::selectMaskIndex(int index)
{
    if (!m_event || panCrop().maskKeyframes.isEmpty()) {
        return;
    }
    m_maskIndex = std::clamp(index, 0, int(panCrop().maskKeyframes.size()) - 1);
    m_playheadSec = panCrop().maskKeyframes[m_maskIndex].timeSec;
    if (m_channel == PanCropChannel::Mask) {
        syncUiFromSelected();
    }
    refreshKeyframeLanes();
}

void VideoEventFxDialogExact::setChannel(PanCropChannel ch)
{
    m_channel = ch;
    refreshChannelUi();
    syncUiFromSelected();
}

void VideoEventFxDialogExact::refreshChannelUi()
{
    const bool mask = (m_channel == PanCropChannel::Mask);
    if (m_posTools) {
        m_posTools->setVisible(!mask);
    }
    if (m_maskTools) {
        m_maskTools->setVisible(mask);
    }
    if (m_posPropsBlock) {
        m_posPropsBlock->setVisible(!mask);
    }
    if (m_rotPropsBlock) {
        m_rotPropsBlock->setVisible(!mask);
    }
    if (m_interpPropsBlock) {
        m_interpPropsBlock->setVisible(!mask);
    }
    if (m_sourcePropsBlock) {
        m_sourcePropsBlock->setVisible(!mask);
    }
    if (m_maskPropsBlock) {
        m_maskPropsBlock->setVisible(mask);
    }
    if (m_posRow) {
        m_posRow->setObjectName(mask ? QStringLiteral("pcKfRow") : QStringLiteral("pcKfRowActive"));
        m_posRow->style()->unpolish(m_posRow);
        m_posRow->style()->polish(m_posRow);
    }
    if (m_maskRow) {
        m_maskRow->setObjectName(mask ? QStringLiteral("pcKfRowActive")
                                      : QStringLiteral("pcKfRow"));
        m_maskRow->style()->unpolish(m_maskRow);
        m_maskRow->style()->polish(m_maskRow);
    }
    applyCanvasOptions();
}

void VideoEventFxDialogExact::setPlayheadSec(double sec, bool selectNearestKf)
{
    m_playheadSec = std::clamp(sec, 0.0, m_durationSec);
    if (selectNearestKf && m_event) {
        m_posIndex = panCrop().positionIndexAt(m_playheadSec);
        m_maskIndex = panCrop().maskIndexAt(m_playheadSec);
        syncUiFromSelected();
    }
    loadMediaPreview();
    refreshKeyframeLanes();
    refreshPluginKeyframeLanes();
    if (m_tc) {
        m_tc->setText(formatTc(m_playheadSec));
    }
    if (m_pluginKfTc) {
        m_pluginKfTc->setText(formatTc(m_playheadSec));
    }
}

void VideoEventFxDialogExact::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *sub = new QWidget(this);
    sub->setObjectName(QStringLiteral("pcSubheader"));
    sub->setFixedHeight(28);
    auto *subLay = new QHBoxLayout(sub);
    subLay->setContentsMargins(8, 0, 8, 0);
    subLay->setSpacing(8);
    m_subtitle = new QLabel(tr("Event Pan/Crop: "), sub);
    m_subtitle->setObjectName(QStringLiteral("pcFxName"));
    m_titleExtra = new QLabel(QString(), sub);
    m_titleExtra->setObjectName(QStringLiteral("pcFxNameEm"));
    subLay->addWidget(m_subtitle, 0);
    subLay->addWidget(m_titleExtra, 1);
    auto *addBtn = makeIcoBtn(sub, QStringLiteral("fx+"), tr("Add Plug-In"));
    auto *remBtn = makeIcoBtn(sub, QStringLiteral("fx×"), tr("Remove Plug-In"));
    subLay->addWidget(addBtn);
    subLay->addWidget(remBtn);
    root->addWidget(sub);

    auto *chainRow = new QWidget(this);
    chainRow->setObjectName(QStringLiteral("aefxChainRow"));
    chainRow->setFixedHeight(34);
    auto *chainRowLay = new QHBoxLayout(chainRow);
    chainRowLay->setContentsMargins(4, 2, 4, 2);
    m_chainScroll = new QScrollArea(chainRow);
    m_chainScroll->setObjectName(QStringLiteral("aefxChainScroll"));
    m_chainScroll->setWidgetResizable(true);
    m_chainScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_chainScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_chainScroll->setFixedHeight(30);
    m_chainScroll->setFrameShape(QFrame::NoFrame);
    m_chainHost = new QWidget(m_chainScroll);
    m_chainHost->setObjectName(QStringLiteral("aefxChainHost"));
    m_chainLay = new QHBoxLayout(m_chainHost);
    m_chainLay->setContentsMargins(6, 2, 6, 2);
    m_chainLay->setSpacing(0);
    m_chainLay->addStretch(1);
    m_chainScroll->setWidget(m_chainHost);
    chainRowLay->addWidget(m_chainScroll, 1);
    root->addWidget(chainRow);

    auto *preset = new QWidget(this);
    preset->setObjectName(QStringLiteral("pcPreset"));
    preset->setFixedHeight(28);
    auto *presetLay = new QHBoxLayout(preset);
    presetLay->setContentsMargins(8, 0, 8, 0);
    presetLay->addWidget(new QLabel(tr("Preset:"), preset));
    auto *sel = new QComboBox(preset);
    sel->setObjectName(QStringLiteral("pcPresetSelect"));
    sel->addItem(tr("(Default)"));
    sel->setFixedHeight(20);
    sel->setMinimumWidth(160);
    presetLay->addWidget(sel);
    presetLay->addWidget(makeIcoBtn(preset, QStringLiteral("💾"), tr("Save Preset")));
    presetLay->addWidget(makeIcoBtn(preset, QStringLiteral("✕"), tr("Delete Preset")));
    presetLay->addStretch(1);
    root->addWidget(preset);

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(buildPanCropView());
    m_stack->addWidget(buildColorCorrectorPage());
    m_stack->addWidget(buildGenericFxPage());
    root->addWidget(m_stack, 1);
    root->addWidget(buildPluginKeyframePanel(), 0);

    connect(addBtn, &QPushButton::clicked, this, &VideoEventFxDialogExact::addPlugins);
    connect(remBtn, &QPushButton::clicked, this, &VideoEventFxDialogExact::removeSelected);
}

QWidget *VideoEventFxDialogExact::buildGenericFxPage()
{
    auto *page = new QWidget(this);
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(16, 16, 16, 16);
    m_genericTitle = new QLabel(tr("Plug-In"), page);
    m_genericTitle->setObjectName(QStringLiteral("pcFxName"));
    QFont f = m_genericTitle->font();
    f.setPointSize(f.pointSize() + 2);
    f.setBold(true);
    m_genericTitle->setFont(f);
    lay->addWidget(m_genericTitle);
    lay->addSpacing(8);

    m_genericHint = new QLabel(
        tr("OpenVegas applies this effect in Video Preview (CPU). "
           "Vegas proprietary OFX GUIs are not loaded — use the controls below."),
        page);
    m_genericHint->setWordWrap(true);
    m_genericHint->setObjectName(QStringLiteral("pcFxHint"));
    lay->addWidget(m_genericHint);

    m_ofxParamsHost = new QWidget(page);
    m_ofxParamsLay = new QVBoxLayout(m_ofxParamsHost);
    m_ofxParamsLay->setContentsMargins(0, 8, 0, 0);
    m_ofxParamsLay->setSpacing(8);
    lay->addWidget(m_ofxParamsHost);
    lay->addStretch(1);
    return page;
}

QWidget *VideoEventFxDialogExact::buildColorCorrectorPage()
{
    auto *page = new QWidget(this);
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(16, 16, 16, 16);
    m_ccTitle = new QLabel(tr("Color Corrector"), page);
    m_ccTitle->setObjectName(QStringLiteral("pcFxName"));
    QFont f = m_ccTitle->font();
    f.setPointSize(f.pointSize() + 2);
    f.setBold(true);
    m_ccTitle->setFont(f);
    lay->addWidget(m_ccTitle);
    lay->addSpacing(12);

    auto addSpin = [&](const QString &label, double minV, double maxV, double step,
                       QDoubleSpinBox **out) {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(label, page));
        auto *spin = new QDoubleSpinBox(page);
        spin->setObjectName(QStringLiteral("aefxSpin"));
        spin->setRange(minV, maxV);
        spin->setSingleStep(step);
        spin->setDecimals(3);
        spin->setMinimumWidth(100);
        row->addWidget(spin);
        row->addStretch(1);
        lay->addLayout(row);
        *out = spin;
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                &VideoEventFxDialogExact::syncColorCorrectorFromUi);
    };

    addSpin(tr("Brightness"), -1.0, 1.0, 0.01, &m_ccBrightness);
    addSpin(tr("Contrast"), 0.0, 2.0, 0.01, &m_ccContrast);
    addSpin(tr("Saturation"), 0.0, 2.0, 0.01, &m_ccSaturation);
    addSpin(tr("Gamma"), 0.1, 3.0, 0.01, &m_ccGamma);
    lay->addStretch(1);
    return page;
}

QWidget *VideoEventFxDialogExact::buildToolsColumn()
{
    auto *tools = new QWidget;
    tools->setObjectName(QStringLiteral("pcTools"));
    tools->setFixedWidth(28);
    auto *lay = new QVBoxLayout(tools);
    lay->setContentsMargins(2, 4, 2, 4);
    lay->setSpacing(2);

    m_btnProps = makeTool(tools, QStringLiteral("☰"), tr("Show Properties"));
    m_btnProps->setChecked(true);
    lay->addWidget(m_btnProps);

    m_posTools = new QWidget(tools);
    auto *posLay = new QVBoxLayout(m_posTools);
    posLay->setContentsMargins(0, 0, 0, 0);
    posLay->setSpacing(2);

    m_btnEdit = makeTool(m_posTools, QStringLiteral("↖"), tr("Normal Edit Tool"));
    m_btnEdit->setChecked(true);
    m_btnEdit->setObjectName(QStringLiteral("pcToolActive"));
    m_btnLockAspect =
        makeTool(m_posTools, QStringLiteral("⬚"), tr("Lock Aspect Ratio (Hold Ctrl to Override)"));
    m_btnLockAspect->setChecked(true);
    m_btnSizeCenter = makeTool(m_posTools, QStringLiteral("◎"), tr("Size About Center"));
    m_btnSizeCenter->setChecked(true);
    m_btnFlipH = makeTool(m_posTools, QStringLiteral("⇔"), tr("Flip Horizontal"));
    m_btnFlipV = makeTool(m_posTools, QStringLiteral("⇕"), tr("Flip Vertical"));

    auto *btnFree = makeTool(m_posTools, QStringLiteral("↔↕"), tr("Move Freely (X or Y)"));
    auto *btnX = makeTool(m_posTools, QStringLiteral("↔"), tr("Move in X only"));
    auto *btnY = makeTool(m_posTools, QStringLiteral("↕"), tr("Move in Y only"));
    btnFree->setChecked(true);
    m_moveAxisBtns = {btnFree, btnX, btnY};

    for (QToolButton *b : {m_btnEdit, m_btnLockAspect, m_btnSizeCenter, m_btnFlipH, m_btnFlipV,
                           btnFree, btnX, btnY}) {
        posLay->addWidget(b);
    }
    lay->addWidget(m_posTools);

    m_maskTools = new QWidget(tools);
    auto *maskLay = new QVBoxLayout(m_maskTools);
    maskLay->setContentsMargins(0, 0, 0, 0);
    maskLay->setSpacing(2);
    m_btnMaskEdit = makeTool(m_maskTools, QStringLiteral("↖"), tr("Normal Edit Tool"));
    m_btnMaskEdit->setChecked(true);
    m_btnMaskEdit->setObjectName(QStringLiteral("pcToolActive"));
    m_btnMaskPen = makeTool(m_maskTools, QStringLiteral("✎"), tr("Anchor Creation Tool"));
    m_btnMaskDel = makeTool(m_maskTools, QStringLiteral("✕"), tr("Anchor Deletion Tool"), false);
    m_btnMaskRect = makeTool(m_maskTools, QStringLiteral("▭"), tr("Rectangle Tool"), false);
    m_btnMaskOval = makeTool(m_maskTools, QStringLiteral("○"), tr("Oval Tool"), false);
    for (QToolButton *b :
         {m_btnMaskEdit, m_btnMaskPen, m_btnMaskDel, m_btnMaskRect, m_btnMaskOval}) {
        maskLay->addWidget(b);
    }
    m_maskTools->setVisible(false);
    lay->addWidget(m_maskTools);

    m_btnZoom = makeTool(tools, QStringLiteral("⌕"), tr("Zoom Edit Tool"));
    m_btnSnap = makeTool(tools, QStringLiteral("#"),
                         tr("Enable Snapping (Hold Shift to Override)"));
    lay->addWidget(m_btnZoom);
    lay->addWidget(m_btnSnap);
    lay->addStretch(1);

    connect(m_btnProps, &QToolButton::toggled, this, [this](bool on) {
        m_propsVisible = on;
        if (m_propsScroll) {
            m_propsScroll->setVisible(on);
        }
    });
    connect(m_btnEdit, &QToolButton::clicked, this, [this]() {
        m_zoomTool = false;
        m_btnZoom->setChecked(false);
        m_btnEdit->setChecked(true);
        applyCanvasOptions();
    });
    connect(m_btnZoom, &QToolButton::clicked, this, [this]() {
        m_zoomTool = true;
        m_btnZoom->setChecked(true);
        if (m_channel == PanCropChannel::Position) {
            m_btnEdit->setChecked(false);
        } else if (m_btnMaskEdit) {
            m_btnMaskEdit->setChecked(false);
        }
        applyCanvasOptions();
    });
    connect(m_btnSnap, &QToolButton::toggled, this, [this](bool on) {
        m_snapping = on;
        applyCanvasOptions();
    });
    connect(m_btnLockAspect, &QToolButton::toggled, this, [this](bool on) {
        m_lockAspect = on;
        applyCanvasOptions();
    });
    connect(m_btnSizeCenter, &QToolButton::toggled, this, [this](bool on) {
        m_sizeAboutCenter = on;
        applyCanvasOptions();
    });
    connect(m_btnFlipH, &QToolButton::clicked, this, &VideoEventFxDialogExact::flipHorizontal);
    connect(m_btnFlipV, &QToolButton::clicked, this, &VideoEventFxDialogExact::flipVertical);
    auto setAxis = [this, btnFree, btnX, btnY](MoveAxis a) {
        m_moveAxis = a;
        btnFree->setChecked(a == MoveAxis::Free);
        btnX->setChecked(a == MoveAxis::XOnly);
        btnY->setChecked(a == MoveAxis::YOnly);
        applyCanvasOptions();
    };
    connect(btnFree, &QToolButton::clicked, this, [setAxis]() { setAxis(MoveAxis::Free); });
    connect(btnX, &QToolButton::clicked, this, [setAxis]() { setAxis(MoveAxis::XOnly); });
    connect(btnY, &QToolButton::clicked, this, [setAxis]() { setAxis(MoveAxis::YOnly); });

    auto setMaskTool = [this](PanCropCanvas::MaskTool tool) {
        if (!m_canvas) {
            return;
        }
        m_zoomTool = false;
        m_btnZoom->setChecked(false);
        m_btnMaskEdit->setChecked(tool == PanCropCanvas::MaskTool::NormalEdit);
        m_btnMaskPen->setChecked(tool == PanCropCanvas::MaskTool::AddAnchor);
        m_btnMaskDel->setChecked(tool == PanCropCanvas::MaskTool::DeleteAnchor);
        m_btnMaskRect->setChecked(tool == PanCropCanvas::MaskTool::Rectangle);
        m_btnMaskOval->setChecked(tool == PanCropCanvas::MaskTool::Oval);
        m_canvas->setMaskTool(tool);
        applyCanvasOptions();
    };
    connect(m_btnMaskEdit, &QToolButton::clicked, this, [setMaskTool]() {
        setMaskTool(PanCropCanvas::MaskTool::NormalEdit);
    });
    connect(m_btnMaskPen, &QToolButton::clicked, this, [setMaskTool]() {
        setMaskTool(PanCropCanvas::MaskTool::AddAnchor);
    });
    connect(m_btnMaskDel, &QToolButton::clicked, this, [setMaskTool]() {
        setMaskTool(PanCropCanvas::MaskTool::DeleteAnchor);
    });
    connect(m_btnMaskRect, &QToolButton::clicked, this, [setMaskTool]() {
        setMaskTool(PanCropCanvas::MaskTool::Rectangle);
    });
    connect(m_btnMaskOval, &QToolButton::clicked, this, [setMaskTool]() {
        setMaskTool(PanCropCanvas::MaskTool::Oval);
    });
    return tools;
}

QWidget *VideoEventFxDialogExact::buildPanCropView()
{
    auto *view = new QWidget(this);
    view->setObjectName(QStringLiteral("pcViewPanCrop"));
    auto *viewLay = new QVBoxLayout(view);
    viewLay->setContentsMargins(0, 0, 0, 0);
    viewLay->setSpacing(0);

    auto *main = new QWidget(view);
    main->setObjectName(QStringLiteral("pcMain"));
    auto *mainLay = new QHBoxLayout(main);
    mainLay->setContentsMargins(0, 0, 0, 0);
    mainLay->setSpacing(0);
    mainLay->addWidget(buildToolsColumn(), 0);
    m_propsScroll = buildPropsPanel();
    mainLay->addWidget(m_propsScroll, 0);

    auto *canvasWrap = new QWidget(main);
    canvasWrap->setObjectName(QStringLiteral("pcCanvasWrap"));
    auto *cwLay = new QVBoxLayout(canvasWrap);
    cwLay->setContentsMargins(0, 0, 0, 0);
    m_canvas = new PanCropCanvas(canvasWrap);
    m_canvas->setOnEdited([this](const PanCropKeyframe &kf) {
        ensureEditableKeyframeAtPlayhead();
        if (PanCropKeyframe *cur = selectedKf()) {
            const double t = cur->timeSec;
            *cur = kf;
            cur->timeSec = t;
            m_block = true;
            m_w->setValue(kf.width);
            m_h->setValue(kf.height);
            m_x->setValue(kf.xCenter);
            m_y->setValue(kf.yCenter);
            m_angle->setValue(kf.angleDeg);
            m_rotX->setValue(kf.rotationXCenter);
            m_rotY->setValue(kf.rotationYCenter);
            m_block = false;
            syncFlipButtons();
            refreshKeyframeLanes();
        }
    });
    m_canvas->setOnMaskEdited([this](const MaskKeyframe &mk) {
        ensureEditableKeyframeAtPlayhead();
        if (MaskKeyframe *cur = selectedMaskKf()) {
            const double t = cur->timeSec;
            *cur = mk;
            cur->timeSec = t;
            refreshKeyframeLanes();
        }
    });
    m_canvas->setOnMaskSelection([this](int pathIndex, int anchorIndex) {
        m_selMaskPath = pathIndex;
        m_selMaskAnchor = anchorIndex;
        if (pathIndex < 0 || anchorIndex < 0) {
            return;
        }
        if (MaskKeyframe *mk = selectedMaskKf()) {
            if (pathIndex < mk->paths.size() && anchorIndex < mk->paths[pathIndex].anchors.size()) {
                const MaskAnchor &a = mk->paths[pathIndex].anchors[anchorIndex];
                m_block = true;
                m_maskAnchorX->setValue(a.x);
                m_maskAnchorY->setValue(a.y);
                m_block = false;
            }
        }
    });
    m_canvas->setOnWorkspace([this](double z, double ox, double oy) {
        panCrop().workspaceZoom = z;
        panCrop().workspaceX = ox;
        panCrop().workspaceY = oy;
        m_block = true;
        m_wsZoom->setValue(z);
        m_wsX->setValue(ox);
        m_wsY->setValue(oy);
        m_block = false;
    });
    cwLay->addWidget(m_canvas, 1);
    mainLay->addWidget(canvasWrap, 1);
    viewLay->addWidget(main, 1);
    viewLay->addWidget(buildKeyframePanel(), 0);
    applyCanvasOptions();
    refreshChannelUi();
    return view;
}

QWidget *VideoEventFxDialogExact::buildPropsPanel()
{
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("pcPropsScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFixedWidth(230);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *props = new QWidget;
    props->setObjectName(QStringLiteral("pcProps"));
    auto *lay = new QVBoxLayout(props);
    lay->setContentsMargins(0, 2, 0, 8);
    lay->setSpacing(2);

    auto addGroupTo = [](QWidget *parent, QVBoxLayout *targetLay, const QString &title) {
        auto *sum = new QLabel(QStringLiteral("−  %1").arg(title), parent);
        sum->setObjectName(QStringLiteral("pcGroupSummary"));
        sum->setFixedHeight(22);
        targetLay->addWidget(sum);
        return sum;
    };

    m_posPropsBlock = new QWidget(props);
    auto *posLay = new QVBoxLayout(m_posPropsBlock);
    posLay->setContentsMargins(0, 0, 0, 0);
    posLay->setSpacing(2);
    addGroupTo(m_posPropsBlock, posLay, tr("Position"));
    m_w = makeSpin(m_posPropsBlock, -100000, 100000, 3, 1);
    m_h = makeSpin(m_posPropsBlock, -100000, 100000, 3, 1);
    m_x = makeSpin(m_posPropsBlock, -100000, 100000, 3, 1);
    m_y = makeSpin(m_posPropsBlock, -100000, 100000, 3, 1);
    posLay->addWidget(makePropRow(m_posPropsBlock, tr("Width"), m_w));
    posLay->addWidget(makePropRow(m_posPropsBlock, tr("Height"), m_h));
    posLay->addWidget(makePropRow(m_posPropsBlock, tr("X Center"), m_x));
    posLay->addWidget(makePropRow(m_posPropsBlock, tr("Y Center"), m_y));
    lay->addWidget(m_posPropsBlock);

    m_rotPropsBlock = new QWidget(props);
    auto *rotLay = new QVBoxLayout(m_rotPropsBlock);
    rotLay->setContentsMargins(0, 0, 0, 0);
    rotLay->setSpacing(2);
    rotLay->addWidget(addGroupTo(m_rotPropsBlock, rotLay, tr("Rotation")));
    m_angle = makeSpin(m_rotPropsBlock, -8640, 8640, 3, 0.1);
    m_rotX = makeSpin(m_rotPropsBlock, -100000, 100000, 3, 1);
    m_rotY = makeSpin(m_rotPropsBlock, -100000, 100000, 3, 1);
    rotLay->addWidget(makePropRow(m_rotPropsBlock, tr("Angle"), m_angle));
    rotLay->addWidget(makePropRow(m_rotPropsBlock, tr("X Center"), m_rotX));
    rotLay->addWidget(makePropRow(m_rotPropsBlock, tr("Y Center"), m_rotY));
    lay->addWidget(m_rotPropsBlock);

    m_interpPropsBlock = new QWidget(props);
    auto *interpLay = new QVBoxLayout(m_interpPropsBlock);
    interpLay->setContentsMargins(0, 0, 0, 0);
    interpLay->setSpacing(2);
    interpLay->addWidget(addGroupTo(m_interpPropsBlock, interpLay, tr("Keyframe interpolation")));
    m_smooth = makeSpin(m_interpPropsBlock, 0, 1, 3, 0.01);
    interpLay->addWidget(makePropRow(m_interpPropsBlock, tr("Smoothness"), m_smooth));
    lay->addWidget(m_interpPropsBlock);

    m_sourcePropsBlock = new QWidget(props);
    auto *srcLay = new QVBoxLayout(m_sourcePropsBlock);
    srcLay->setContentsMargins(0, 0, 0, 0);
    srcLay->setSpacing(2);
    srcLay->addWidget(addGroupTo(m_sourcePropsBlock, srcLay, tr("Source")));
    m_aspect = new QComboBox(m_sourcePropsBlock);
    m_aspect->setObjectName(QStringLiteral("pcPresetSelect"));
    m_aspect->addItems({tr("Yes"), tr("No")});
    m_aspect->setFixedHeight(18);
    m_aspect->setFixedWidth(88);
    m_stretch = new QComboBox(m_sourcePropsBlock);
    m_stretch->setObjectName(QStringLiteral("pcPresetSelect"));
    m_stretch->addItems({tr("Yes"), tr("No")});
    m_stretch->setFixedHeight(18);
    m_stretch->setFixedWidth(88);
    srcLay->addWidget(makePropRow(m_sourcePropsBlock, tr("Maintain aspect ratio"), m_aspect));
    srcLay->addWidget(makePropRow(m_sourcePropsBlock, tr("Stretch to fill frame"), m_stretch));
    lay->addWidget(m_sourcePropsBlock);

    m_maskPropsBlock = new QWidget(props);
    auto *maskLay = new QVBoxLayout(m_maskPropsBlock);
    maskLay->setContentsMargins(0, 0, 0, 0);
    maskLay->setSpacing(2);
    maskLay->addWidget(addGroupTo(m_maskPropsBlock, maskLay, tr("Mask")));
    m_maskEnable = new QCheckBox(tr("Enable mask"), m_maskPropsBlock);
    m_maskEnable->setObjectName(QStringLiteral("pcProp"));
    maskLay->addWidget(m_maskEnable);
    m_maskAnchorX = makeSpin(m_maskPropsBlock, -100000, 100000, 3, 1);
    m_maskAnchorY = makeSpin(m_maskPropsBlock, -100000, 100000, 3, 1);
    maskLay->addWidget(makePropRow(m_maskPropsBlock, tr("Anchor X"), m_maskAnchorX));
    maskLay->addWidget(makePropRow(m_maskPropsBlock, tr("Anchor Y"), m_maskAnchorY));
    m_pathMode = new QComboBox(m_maskPropsBlock);
    m_pathMode->setObjectName(QStringLiteral("pcPresetSelect"));
    m_pathMode->addItems({tr("Positive"), tr("Negative"), tr("Disabled")});
    m_pathMode->setFixedHeight(18);
    m_pathMode->setFixedWidth(88);
    maskLay->addWidget(makePropRow(m_maskPropsBlock, tr("Path mode"), m_pathMode));
    m_maskPropsBlock->setVisible(false);
    lay->addWidget(m_maskPropsBlock);

    addGroupTo(props, lay, tr("Workspace"));
    m_wsZoom = makeSpin(props, 5, 400, 2, 0.1);
    m_wsX = makeSpin(props, -10000, 10000, 2, 1);
    m_wsY = makeSpin(props, -10000, 10000, 2, 1);
    m_grid = makeSpin(props, 1, 200, 0, 1);
    lay->addWidget(makePropRow(props, tr("Zoom (%)"), m_wsZoom));
    lay->addWidget(makePropRow(props, tr("X Offset"), m_wsX));
    lay->addWidget(makePropRow(props, tr("Y Offset"), m_wsY));
    lay->addWidget(makePropRow(props, tr("Grid spacing"), m_grid));
    lay->addStretch(1);
    scroll->setWidget(props);

    auto wire = [this](QDoubleSpinBox *s) {
        connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) {
            if (!m_block) {
                syncSelectedFromUi();
            }
        });
    };
    for (QDoubleSpinBox *s : {m_w, m_h, m_x, m_y, m_angle, m_rotX, m_rotY, m_smooth, m_wsZoom,
                              m_wsX, m_wsY, m_grid, m_maskAnchorX, m_maskAnchorY}) {
        wire(s);
    }
    connect(m_aspect, &QComboBox::currentIndexChanged, this, [this](int) {
        if (!m_block) {
            syncSelectedFromUi();
        }
    });
    connect(m_stretch, &QComboBox::currentIndexChanged, this, [this](int) {
        if (!m_block) {
            syncSelectedFromUi();
        }
    });
    connect(m_maskEnable, &QCheckBox::toggled, this, [this](bool) {
        if (!m_block) {
            syncMaskFromUi();
        }
    });
    connect(m_pathMode, &QComboBox::currentIndexChanged, this, [this](int) {
        if (!m_block) {
            syncMaskFromUi();
        }
    });
    connect(m_maskAnchorX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double) {
                if (!m_block) {
                    syncMaskFromUi();
                }
            });
    connect(m_maskAnchorY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double) {
                if (!m_block) {
                    syncMaskFromUi();
                }
            });
    return scroll;
}

QWidget *VideoEventFxDialogExact::buildKeyframePanel()
{
    auto *kf = new QWidget(this);
    kf->setObjectName(QStringLiteral("pcKf"));
    kf->setFixedHeight(130);
    auto *root = new QVBoxLayout(kf);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *headers = new QWidget(kf);
    headers->setFixedHeight(20);
    auto *hLay = new QHBoxLayout(headers);
    hLay->setContentsMargins(0, 0, 0, 0);
    auto *corner = new QWidget(headers);
    corner->setObjectName(QStringLiteral("pcKfCorner"));
    corner->setFixedWidth(120);
    hLay->addWidget(corner);
    m_ruler = new PanCropKeyframeRuler(headers);
    m_ruler->setOnScrub([this](double t) { setPlayheadSec(t, true); });
    hLay->addWidget(m_ruler, 1);
    root->addWidget(headers);

    auto *row = new QWidget(kf);
    row->setObjectName(QStringLiteral("pcKfRowActive"));
    row->setFixedHeight(28);
    m_posRow = row;
    auto *rowLay = new QHBoxLayout(row);
    rowLay->setContentsMargins(0, 0, 0, 0);
    auto *labHost = new QWidget(row);
    labHost->setObjectName(QStringLiteral("pcKfLabelActive"));
    labHost->setFixedWidth(120);
    auto *labLay = new QHBoxLayout(labHost);
    labLay->setContentsMargins(8, 0, 8, 0);
    auto *posCb = new QCheckBox(tr("Position"), labHost);
    posCb->setChecked(true);
    labLay->addWidget(posCb);
    rowLay->addWidget(labHost);
    auto *lane = new KeyframeLane(row);
    m_posLane = lane;
    lane->setOnSelect([this](int i) {
        setChannel(PanCropChannel::Position);
        selectPositionIndex(i);
    });
    lane->setOnScrub([this](double t) { setPlayheadSec(t, true); });
    lane->setOnMove([this](int i, double t, bool fin) { movePositionKeyframe(i, t, fin); });
    lane->setOnCreateAt([this](double t) {
        setChannel(PanCropChannel::Position);
        addKeyframeAtTime(t);
    });
    lane->setOnDeleteSelected([this]() {
        setChannel(PanCropChannel::Position);
        deleteSelectedKeyframe();
    });
    lane->setOnContextMenu([this](int i, const QPoint &gp) {
        setChannel(PanCropChannel::Position);
        showPositionKeyframeMenu(i, gp);
    });
    rowLay->addWidget(lane, 1);
    root->addWidget(row);
    connect(posCb, &QCheckBox::clicked, this, [this]() { setChannel(PanCropChannel::Position); });
    labHost->installEventFilter(new RowClickFilter([this]() { setChannel(PanCropChannel::Position); },
                                                     labHost));

    auto *maskRow = new QWidget(kf);
    maskRow->setObjectName(QStringLiteral("pcKfRow"));
    maskRow->setFixedHeight(28);
    m_maskRow = maskRow;
    auto *maskLay = new QHBoxLayout(maskRow);
    maskLay->setContentsMargins(0, 0, 0, 0);
    auto *maskLab = new QWidget(maskRow);
    maskLab->setObjectName(QStringLiteral("pcKfLabel"));
    maskLab->setFixedWidth(120);
    auto *maskLabLay = new QHBoxLayout(maskLab);
    maskLabLay->setContentsMargins(8, 0, 8, 0);
    auto *maskCb = new QCheckBox(tr("Mask"), maskLab);
    maskCb->setChecked(false);
    m_maskRowCheck = maskCb;
    maskLabLay->addWidget(maskCb);
    maskLay->addWidget(maskLab);
    auto *maskLane = new KeyframeLane(maskRow);
    m_maskLane = maskLane;
    maskLane->setOnSelect([this](int i) {
        setChannel(PanCropChannel::Mask);
        selectMaskIndex(i);
    });
    maskLane->setOnScrub([this](double t) { setPlayheadSec(t, true); });
    maskLane->setOnMove([this](int i, double t, bool fin) { moveMaskKeyframe(i, t, fin); });
    maskLane->setOnCreateAt([this](double t) {
        setChannel(PanCropChannel::Mask);
        addKeyframeAtTime(t);
    });
    maskLane->setOnDeleteSelected([this]() {
        setChannel(PanCropChannel::Mask);
        deleteSelectedKeyframe();
    });
    maskLane->setOnContextMenu([this](int i, const QPoint &gp) {
        setChannel(PanCropChannel::Mask);
        showMaskKeyframeMenu(i, gp);
    });
    maskLay->addWidget(maskLane, 1);
    root->addWidget(maskRow);
    connect(maskCb, &QCheckBox::toggled, this, [this](bool on) {
        setChannel(PanCropChannel::Mask);
        if (!m_block) {
            panCrop().maskEnabled = on;
            m_block = true;
            if (m_maskEnable) {
                m_maskEnable->setChecked(on);
            }
            m_block = false;
            syncUiFromSelected();
        }
    });
    maskLab->installEventFilter(new RowClickFilter([this]() { setChannel(PanCropChannel::Mask); },
                                                    maskLab));

    auto *tb = new QWidget(kf);
    tb->setObjectName(QStringLiteral("pcKfToolbar"));
    auto *tbLay = new QHBoxLayout(tb);
    tbLay->setContentsMargins(8, 2, 8, 2);
    tbLay->setSpacing(4);
    auto *btnSync = makeIcoBtn(tb, QStringLiteral("⏱"), tr("Sync Cursor"));
    btnSync->setCheckable(true);
    btnSync->setChecked(true);
    auto *btnFirst = makeIcoBtn(tb, QStringLiteral("⏮"), tr("First Keyframe"));
    auto *btnPrev = makeIcoBtn(tb, QStringLiteral("◀"), tr("Previous Keyframe"));
    auto *btnNext = makeIcoBtn(tb, QStringLiteral("▶"), tr("Next Keyframe"));
    auto *btnLast = makeIcoBtn(tb, QStringLiteral("⏭"), tr("Last Keyframe"));
    auto *btnAdd = makeIcoBtn(tb, QStringLiteral("+"), tr("Create Keyframe"));
    auto *btnDel = makeIcoBtn(tb, QStringLiteral("−"), tr("Delete Keyframe"));
    for (auto *b : {btnSync, btnFirst, btnPrev, btnNext, btnLast, btnAdd, btnDel}) {
        tbLay->addWidget(b);
    }
    tbLay->addStretch(1);
    m_tc = new QLabel(QStringLiteral("00:00:00,00"), tb);
    m_tc->setObjectName(QStringLiteral("pcKfTc"));
    tbLay->addWidget(m_tc);
    root->addWidget(tb);

    connect(btnFirst, &QPushButton::clicked, this, &VideoEventFxDialogExact::navigateKeyframeFirst);
    connect(btnPrev, &QPushButton::clicked, this, &VideoEventFxDialogExact::navigateKeyframePrev);
    connect(btnNext, &QPushButton::clicked, this, &VideoEventFxDialogExact::navigateKeyframeNext);
    connect(btnLast, &QPushButton::clicked, this, &VideoEventFxDialogExact::navigateKeyframeLast);
    connect(btnAdd, &QPushButton::clicked, this, &VideoEventFxDialogExact::addKeyframeAtPlayhead);
    connect(btnDel, &QPushButton::clicked, this, &VideoEventFxDialogExact::deleteSelectedKeyframe);
    return kf;
}

QWidget *VideoEventFxDialogExact::buildPluginKeyframePanel()
{
    auto *kf = new QWidget(this);
    kf->setObjectName(QStringLiteral("pcKf"));
    kf->setMinimumHeight(120);
    m_pluginKfPanel = kf;
    auto *root = new QVBoxLayout(kf);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *headers = new QWidget(kf);
    headers->setFixedHeight(20);
    auto *hLay = new QHBoxLayout(headers);
    hLay->setContentsMargins(0, 0, 0, 0);
    auto *corner = new QWidget(headers);
    corner->setObjectName(QStringLiteral("pcKfCorner"));
    corner->setFixedWidth(140);
    hLay->addWidget(corner);
    m_pluginKfRuler = new PanCropKeyframeRuler(headers);
    m_pluginKfRuler->setOnScrub([this](double t) { setPlayheadSec(t, false); });
    hLay->addWidget(m_pluginKfRuler, 1);
    root->addWidget(headers);

    m_pluginKfLanesHost = new QWidget(kf);
    m_pluginKfLanesLay = new QVBoxLayout(m_pluginKfLanesHost);
    m_pluginKfLanesLay->setContentsMargins(0, 0, 0, 0);
    m_pluginKfLanesLay->setSpacing(0);
    root->addWidget(m_pluginKfLanesHost, 1);

    auto *tb = new QWidget(kf);
    tb->setObjectName(QStringLiteral("pcKfToolbar"));
    auto *tbLay = new QHBoxLayout(tb);
    tbLay->setContentsMargins(8, 2, 8, 2);
    tbLay->setSpacing(4);
    auto *btnSync = makeIcoBtn(tb, QStringLiteral("⏱"), tr("Sync Cursor"));
    btnSync->setCheckable(true);
    btnSync->setChecked(true);
    auto *btnFirst = makeIcoBtn(tb, QStringLiteral("⏮"), tr("First Keyframe"));
    auto *btnPrev = makeIcoBtn(tb, QStringLiteral("◀"), tr("Previous Keyframe"));
    auto *btnNext = makeIcoBtn(tb, QStringLiteral("▶"), tr("Next Keyframe"));
    auto *btnLast = makeIcoBtn(tb, QStringLiteral("⏭"), tr("Last Keyframe"));
    auto *btnAdd = makeIcoBtn(tb, QStringLiteral("◆+"), tr("Create Keyframe"));
    auto *btnDel = makeIcoBtn(tb, QStringLiteral("◆×"), tr("Delete Keyframe"));
    for (auto *b : {btnSync, btnFirst, btnPrev, btnNext, btnLast, btnAdd, btnDel}) {
        tbLay->addWidget(b);
    }
    tbLay->addSpacing(8);
    m_btnPluginLanes = makeIcoBtn(tb, QStringLiteral("Lanes"), tr("Lanes"));
    m_btnPluginCurves = makeIcoBtn(tb, QStringLiteral("Curves"), tr("Curves"));
    m_btnPluginLanes->setCheckable(true);
    m_btnPluginCurves->setCheckable(true);
    m_btnPluginCurves->setChecked(true);
    m_btnPluginLanes->setObjectName(QStringLiteral("pcKfModeBtn"));
    m_btnPluginCurves->setObjectName(QStringLiteral("pcKfModeBtn"));
    tbLay->addWidget(m_btnPluginLanes);
    tbLay->addWidget(m_btnPluginCurves);
    auto *btnZoomIn = makeIcoBtn(tb, QStringLiteral("+"), tr("Zoom In"));
    auto *btnZoomOut = makeIcoBtn(tb, QStringLiteral("−"), tr("Zoom Out"));
    tbLay->addWidget(btnZoomIn);
    tbLay->addWidget(btnZoomOut);
    tbLay->addStretch(1);
    m_pluginKfTc = new QLabel(QStringLiteral("00:00:00,00"), tb);
    m_pluginKfTc->setObjectName(QStringLiteral("pcKfTc"));
    tbLay->addWidget(m_pluginKfTc);
    root->addWidget(tb);

    connect(btnFirst, &QPushButton::clicked, this,
            &VideoEventFxDialogExact::navigatePluginKeyframeFirst);
    connect(btnPrev, &QPushButton::clicked, this,
            &VideoEventFxDialogExact::navigatePluginKeyframePrev);
    connect(btnNext, &QPushButton::clicked, this,
            &VideoEventFxDialogExact::navigatePluginKeyframeNext);
    connect(btnLast, &QPushButton::clicked, this,
            &VideoEventFxDialogExact::navigatePluginKeyframeLast);
    connect(btnAdd, &QPushButton::clicked, this,
            &VideoEventFxDialogExact::addPluginKeyframeAtPlayhead);
    connect(btnDel, &QPushButton::clicked, this,
            &VideoEventFxDialogExact::deleteSelectedPluginKeyframe);
    connect(m_btnPluginLanes, &QPushButton::clicked, this, [this]() { setPluginKfCurvesMode(false); });
    connect(m_btnPluginCurves, &QPushButton::clicked, this, [this]() { setPluginKfCurvesMode(true); });
    // Zoom reserved: lanes already span full event duration (Vegas default).
    Q_UNUSED(btnZoomIn);
    Q_UNUSED(btnZoomOut);

    kf->setVisible(false);
    return kf;
}

void VideoEventFxDialogExact::rebuildChain()
{
    if (!m_chainLay) {
        return;
    }
    while (QLayoutItem *it = m_chainLay->takeAt(0)) {
        if (it->widget()) {
            it->widget()->deleteLater();
        }
        delete it;
    }
    m_nodes.clear();
    if (!m_event || m_event->fxChain.isEmpty()) {
        m_selectedFx = -1;
        auto *hint = new QLabel(tr("(empty chain)"), m_chainHost);
        hint->setObjectName(QStringLiteral("aefxChainEmpty"));
        m_chainLay->addWidget(hint);
        m_chainLay->addStretch(1);
        refreshViewport();
        return;
    }
    auto *startDot = new QLabel(m_chainHost);
    startDot->setObjectName(QStringLiteral("aefxChainDot"));
    startDot->setFixedSize(8, 8);
    m_chainLay->addWidget(startDot, 0, Qt::AlignVCenter);
    auto *startLine = new QFrame(m_chainHost);
    startLine->setObjectName(QStringLiteral("aefxChainLine"));
    startLine->setFixedSize(10, 1);
    m_chainLay->addWidget(startLine, 0, Qt::AlignVCenter);
    for (int i = 0; i < m_event->fxChain.size(); ++i) {
        if (i > 0) {
            auto *line = new QFrame(m_chainHost);
            line->setObjectName(QStringLiteral("aefxChainLine"));
            line->setFixedSize(12, 1);
            m_chainLay->addWidget(line, 0, Qt::AlignVCenter);
        }
        auto *node = new FxChainNodeWidget(i, m_event->fxChain[i], m_chainHost);
        connect(node, &FxChainNodeWidget::selected, this, &VideoEventFxDialogExact::selectPlugin);
        connect(node, &FxChainNodeWidget::bypassToggled, this, &VideoEventFxDialogExact::setBypass);
        connect(node, &FxChainNodeWidget::moveRequested, this, &VideoEventFxDialogExact::movePlugin);
        m_chainLay->addWidget(node, 0, Qt::AlignVCenter);
        m_nodes.push_back(node);
    }
    auto *endLine = new QFrame(m_chainHost);
    endLine->setObjectName(QStringLiteral("aefxChainLine"));
    endLine->setFixedSize(10, 1);
    m_chainLay->addWidget(endLine, 0, Qt::AlignVCenter);
    auto *endDot = new QLabel(m_chainHost);
    endDot->setObjectName(QStringLiteral("aefxChainDot"));
    endDot->setFixedSize(8, 8);
    m_chainLay->addWidget(endDot, 0, Qt::AlignVCenter);
    m_chainLay->addStretch(1);
    if (m_selectedFx < 0 || m_selectedFx >= m_event->fxChain.size()) {
        m_selectedFx = 0;
    }
    for (FxChainNodeWidget *n : m_nodes) {
        n->setSelected(n->index() == m_selectedFx);
    }
    refreshViewport();
}

void VideoEventFxDialogExact::selectPlugin(int index)
{
    if (!m_event || index < 0 || index >= m_event->fxChain.size()) {
        return;
    }
    m_selectedFx = index;
    if (!isPanCropSlot(m_selectedFx)) {
        m_pluginKfFocusFx = m_selectedFx;
        const auto params = animatableParamsForSlot(m_event->fxChain[m_selectedFx]);
        if (m_pluginKfParamKey.isEmpty() && !params.isEmpty()) {
            m_pluginKfParamKey = params.first().second;
        }
    }
    for (FxChainNodeWidget *n : m_nodes) {
        n->setSelected(n->index() == m_selectedFx);
    }
    if (m_subtitle) {
        if (isPanCropSlot(m_selectedFx)) {
            m_subtitle->setText(tr("Event Pan/Crop: "));
        } else {
            m_subtitle->setText(tr("Video Event FX: "));
        }
    }
    refreshViewport();
}

void VideoEventFxDialogExact::refreshViewport()
{
    if (!m_stack) {
        return;
    }
    if (isPanCropSlot(m_selectedFx)) {
        m_stack->setCurrentIndex(0);
        syncUiFromSelected();
        refreshKeyframeLanes();
    } else if (isColorCorrectorSlot(m_selectedFx)) {
        m_stack->setCurrentIndex(1);
        syncColorCorrectorToUi();
    } else {
        m_stack->setCurrentIndex(2);
        if (m_genericTitle && m_event && m_selectedFx >= 0 && m_selectedFx < m_event->fxChain.size()) {
            m_genericTitle->setText(m_event->fxChain[m_selectedFx].displayName);
        }
        rebuildOfxParamsUi();
    }
    updatePluginKeyframePanelVisibility();
}

void VideoEventFxDialogExact::updatePluginKeyframePanelVisibility()
{
    const bool show = m_event && m_selectedFx >= 0 && !isPanCropSlot(m_selectedFx);
    if (m_pluginKfPanel) {
        m_pluginKfPanel->setVisible(show);
    }
    if (show) {
        m_pluginKfFocusFx = m_selectedFx;
        rebuildPluginKeyframeLanes();
        refreshPluginKeyframeLanes();
    }
}

void VideoEventFxDialogExact::rebuildOfxParamsUi()
{
    if (!m_ofxParamsLay || !m_event || m_selectedFx < 0 || m_selectedFx >= m_event->fxChain.size()) {
        return;
    }
    while (QLayoutItem *it = m_ofxParamsLay->takeAt(0)) {
        if (it->widget()) {
            it->widget()->deleteLater();
        }
        delete it;
    }

    FxSlot &slot = m_event->fxChain[m_selectedFx];
    QVariantMap p = unpackFxParams(slot.state);
    const QVector<OfxParamInfo> infos = paramsInfoForSlot(slot);

    auto addSlider = [&](const QString &label, const QString &key, double def, double minV,
                         double maxV, int decimals) {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(label, m_ofxParamsHost));
        auto *sl = new QSlider(Qt::Horizontal, m_ofxParamsHost);
        sl->setRange(0, 1000);
        const double cur = p.value(key, def).toDouble();
        sl->setValue(int(std::lround((cur - minV) / (maxV - minV) * 1000.0)));
        auto *spin = new QDoubleSpinBox(m_ofxParamsHost);
        spin->setRange(minV, maxV);
        spin->setDecimals(decimals);
        spin->setValue(cur);
        spin->setFixedWidth(90);
        FxSlot *slotPtr = &slot;
        QObject::connect(sl, &QSlider::valueChanged, m_ofxParamsHost,
                         [spin, minV, maxV, slotPtr, key](int v) {
                             const double x = minV + (maxV - minV) * (v / 1000.0);
                             spin->blockSignals(true);
                             spin->setValue(x);
                             spin->blockSignals(false);
                             QVariantMap m = unpackFxParams(slotPtr->state);
                             m.insert(key, x);
                             slotPtr->state = packFxParams(m);
                         });
        QObject::connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), m_ofxParamsHost,
                         [sl, minV, maxV, slotPtr, key](double x) {
                             const int v = int(std::lround((x - minV) / (maxV - minV) * 1000.0));
                             sl->blockSignals(true);
                             sl->setValue(std::clamp(v, 0, 1000));
                             sl->blockSignals(false);
                             QVariantMap m = unpackFxParams(slotPtr->state);
                             m.insert(key, x);
                             slotPtr->state = packFxParams(m);
                         });
        row->addWidget(sl, 1);
        row->addWidget(spin);
        m_ofxParamsLay->addLayout(row);
    };

    if (m_genericHint) {
        const bool usingRealParams =
            slot.format == PluginFormat::Ofx && !OfxHost::instance().paramsForSlot(slot).isEmpty();
        m_genericHint->setText(usingRealParams
            ? tr("Parameters from the installed OFX plug-in — applied in Video Preview.")
            : infos.isEmpty()
                ? tr("Effect is kept in the chain. If a matching OFX binary is found it is processed; "
                     "otherwise OpenVegas applies a CPU fallback when available.")
                : tr("Approximate parameters — applied via CPU fallback in Video Preview."));
    }
    for (const OfxParamInfo &info : infos) {
        addSlider(info.label, info.name, info.defaultValue, info.minValue, info.maxValue, 2);
    }
    m_ofxParamsLay->addStretch(1);
}

void VideoEventFxDialogExact::syncColorCorrectorToUi()
{
    if (!m_event || !isColorCorrectorSlot(m_selectedFx)) {
        return;
    }
    const ColorCorrectorParams p = colorCorrectorFromSlot(m_event->fxChain[m_selectedFx]);
    m_block = true;
    if (m_ccTitle) {
        m_ccTitle->setText(m_event->fxChain[m_selectedFx].displayName);
    }
    if (m_ccBrightness) {
        m_ccBrightness->setValue(p.brightness);
    }
    if (m_ccContrast) {
        m_ccContrast->setValue(p.contrast);
    }
    if (m_ccSaturation) {
        m_ccSaturation->setValue(p.saturation);
    }
    if (m_ccGamma) {
        m_ccGamma->setValue(p.gamma);
    }
    m_block = false;
}

void VideoEventFxDialogExact::syncColorCorrectorFromUi()
{
    if (m_block || !m_event || !isColorCorrectorSlot(m_selectedFx)) {
        return;
    }
    ColorCorrectorParams p;
    p.brightness = m_ccBrightness ? m_ccBrightness->value() : 0.0;
    p.contrast = m_ccContrast ? m_ccContrast->value() : 1.0;
    p.saturation = m_ccSaturation ? m_ccSaturation->value() : 1.0;
    p.gamma = m_ccGamma ? m_ccGamma->value() : 1.0;
    colorCorrectorSaveToSlot(&m_event->fxChain[m_selectedFx], p);
}

void VideoEventFxDialogExact::setBypass(int index, bool bypass)
{
    if (!m_event || index < 0 || index >= m_event->fxChain.size()) {
        return;
    }
    m_event->fxChain[index].bypass = bypass;
}

void VideoEventFxDialogExact::movePlugin(int from, int insertBefore)
{
    if (!m_event || from < 0 || from >= m_event->fxChain.size()) {
        return;
    }
    insertBefore = std::clamp(insertBefore, 0, int(m_event->fxChain.size()));
    if (insertBefore == from || insertBefore == from + 1) {
        return;
    }
    const FxSlot slot = m_event->fxChain[from];
    m_event->fxChain.removeAt(from);
    if (insertBefore > from) {
        --insertBefore;
    }
    m_event->fxChain.insert(insertBefore, slot);
    m_selectedFx = insertBefore;
    rebuildChain();
}

void VideoEventFxDialogExact::addPlugins()
{
    if (!m_event) {
        return;
    }
    PluginChooserDialog dlg(m_scanner, this);
    dlg.setAudioMode(false);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    const QString name = dlg.selectedPluginName();
    if (name.isEmpty()) {
        return;
    }
    m_event->fxChain.push_back(VegasVideoPluginCatalog::slotFromDisplayName(name));
    rebuildChain();
    selectPlugin(m_event->fxChain.size() - 1);
}

void VideoEventFxDialogExact::removeSelected()
{
    if (!m_event || m_selectedFx < 0 || m_selectedFx >= m_event->fxChain.size()) {
        return;
    }
    if (isPanCropSlot(m_selectedFx) && m_event->fxChain.size() == 1) {
        return;
    }
    m_event->fxChain.removeAt(m_selectedFx);
    if (m_event->fxChain.isEmpty()) {
        ensurePanCropFirst();
    }
    if (m_selectedFx >= m_event->fxChain.size()) {
        m_selectedFx = m_event->fxChain.size() - 1;
    }
    rebuildChain();
}

void VideoEventFxDialogExact::refreshKeyframeLanes()
{
    if (!m_event) {
        return;
    }
    QVector<double> posTimes;
    QVector<int> posTypes;
    for (const PanCropKeyframe &k : panCrop().positionKeyframes) {
        posTimes.push_back(k.timeSec);
        posTypes.push_back(int(k.type));
    }
    if (auto *lane = static_cast<KeyframeLane *>(m_posLane)) {
        lane->setTimes(posTimes, posTypes, m_durationSec, m_posIndex, m_playheadSec);
    }
    QVector<double> maskTimes;
    QVector<int> maskTypes;
    for (const MaskKeyframe &k : panCrop().maskKeyframes) {
        maskTimes.push_back(k.timeSec);
        maskTypes.push_back(int(k.type));
    }
    if (auto *lane = static_cast<KeyframeLane *>(m_maskLane)) {
        lane->setTimes(maskTimes, maskTypes, m_durationSec, m_maskIndex, m_playheadSec);
    }
    if (m_ruler) {
        m_ruler->setRange(m_durationSec, m_playheadSec);
    }
    if (m_tc) {
        m_tc->setText(formatTc(m_playheadSec));
    }
}

void VideoEventFxDialogExact::setPluginKfCurvesMode(bool curves)
{
    m_pluginKfCurves = curves;
    if (m_btnPluginLanes) {
        m_btnPluginLanes->setChecked(!curves);
    }
    if (m_btnPluginCurves) {
        m_btnPluginCurves->setChecked(curves);
    }
    rebuildPluginKeyframeLanes();
    refreshPluginKeyframeLanes();
}

void VideoEventFxDialogExact::rebuildPluginKeyframeLanes()
{
    if (!m_pluginKfLanesLay || !m_event) {
        return;
    }
    while (QLayoutItem *it = m_pluginKfLanesLay->takeAt(0)) {
        if (it->widget()) {
            it->widget()->deleteLater();
        }
        delete it;
    }

    if (m_pluginKfFocusFx < 0 || isPanCropSlot(m_pluginKfFocusFx)) {
        m_pluginKfFocusFx = m_selectedFx;
    }
    if (isPanCropSlot(m_pluginKfFocusFx) || m_pluginKfFocusFx < 0
        || m_pluginKfFocusFx >= m_event->fxChain.size()) {
        for (int i = 0; i < m_event->fxChain.size(); ++i) {
            if (!isPanCropSlot(i)) {
                m_pluginKfFocusFx = i;
                break;
            }
        }
    }

    auto addLaneRow = [&](int fxIndex, const QString &label, const QString &paramKey, bool active,
                          bool paramRow) {
        auto *row = new QWidget(m_pluginKfLanesHost);
        row->setObjectName(active ? QStringLiteral("pcKfRowActive") : QStringLiteral("pcKfRow"));
        row->setMinimumHeight(m_pluginKfCurves && paramRow ? 48 : 28);
        auto *rowLay = new QHBoxLayout(row);
        rowLay->setContentsMargins(0, 0, 0, 0);
        auto *labHost = new QWidget(row);
        labHost->setObjectName(active ? QStringLiteral("pcKfLabelActive")
                                       : QStringLiteral("pcKfLabel"));
        labHost->setFixedWidth(140);
        auto *labLay = new QHBoxLayout(labHost);
        labLay->setContentsMargins(paramRow ? 18 : 8, 0, 8, 0);
        auto *name = new QLabel(label, labHost);
        name->setObjectName(QStringLiteral("pcKfLaneName"));
        labLay->addWidget(name, 1);
        rowLay->addWidget(labHost);
        auto *lane = new KeyframeLane(row);
        lane->setProperty("fxIndex", fxIndex);
        lane->setProperty("paramKey", paramKey);
        const int captureFx = fxIndex;
        const QString captureParam = paramKey;
        lane->setOnSelect([this, captureFx, captureParam](int i) {
            const bool focusChanged = (m_pluginKfFocusFx != captureFx)
                                      || (m_pluginKfParamKey != captureParam);
            m_pluginKfFocusFx = captureFx;
            m_pluginKfParamKey = captureParam;
            m_selectedFx = captureFx;
            for (FxChainNodeWidget *n : m_nodes) {
                n->setSelected(n->index() == m_selectedFx);
            }
            if (m_subtitle) {
                m_subtitle->setText(tr("Video Event FX: "));
            }
            if (isColorCorrectorSlot(m_selectedFx)) {
                m_stack->setCurrentIndex(1);
                syncColorCorrectorToUi();
            } else if (!isPanCropSlot(m_selectedFx)) {
                m_stack->setCurrentIndex(2);
                if (m_genericTitle) {
                    m_genericTitle->setText(m_event->fxChain[m_selectedFx].displayName);
                }
                rebuildOfxParamsUi();
            }
            if (focusChanged) {
                rebuildPluginKeyframeLanes();
            }
            selectPluginKeyframeIndex(i);
        });
        lane->setOnScrub([this](double t) { setPlayheadSec(t, false); });
        lane->setOnMove([this, captureFx, captureParam](int i, double t, bool fin) {
            m_pluginKfFocusFx = captureFx;
            m_pluginKfParamKey = captureParam;
            movePluginKeyframe(i, t, fin);
        });
        lane->setOnCreateAt([this, captureFx, captureParam](double t) {
            m_pluginKfFocusFx = captureFx;
            m_pluginKfParamKey = captureParam;
            selectPlugin(captureFx);
            addPluginKeyframeAtTime(t);
        });
        lane->setOnDeleteSelected([this, captureFx, captureParam]() {
            m_pluginKfFocusFx = captureFx;
            m_pluginKfParamKey = captureParam;
            deleteSelectedPluginKeyframe();
        });
        rowLay->addWidget(lane, 1);
        m_pluginKfLanesLay->addWidget(row);
        labHost->installEventFilter(new RowClickFilter(
            [this, captureFx, captureParam, paramRow]() {
                m_pluginKfFocusFx = captureFx;
                if (!paramRow) {
                    const auto params = animatableParamsForSlot(m_event->fxChain[captureFx]);
                    if (!params.isEmpty()
                        && (m_pluginKfParamKey.isEmpty()
                            || m_pluginKfFocusFx != captureFx)) {
                        m_pluginKfParamKey = params.first().second;
                    }
                } else {
                    m_pluginKfParamKey = captureParam;
                }
                selectPlugin(captureFx);
                rebuildPluginKeyframeLanes();
                refreshPluginKeyframeLanes();
            },
            labHost));
    };

    for (int i = 0; i < m_event->fxChain.size(); ++i) {
        if (isPanCropSlot(i)) {
            continue;
        }
        const FxSlot &slot = m_event->fxChain[i];
        const bool active = (i == m_pluginKfFocusFx);
        addLaneRow(i, slot.displayName, QStringLiteral("_master"), active, false);
        if (active) {
            const auto params = animatableParamsForSlot(slot);
            for (const auto &pr : params) {
                const bool paramActive = (m_pluginKfParamKey == pr.second);
                addLaneRow(i, pr.first, pr.second, paramActive, true);
            }
        }
    }

    if (m_pluginKfPanel) {
        const int n = m_pluginKfLanesLay->count();
        m_pluginKfPanel->setFixedHeight(std::clamp(20 + 28 * std::max(1, n) + 28
                                                       + (m_pluginKfCurves ? 24 : 0),
                                                   120, 280));
    }
}

void VideoEventFxDialogExact::refreshPluginKeyframeLanes()
{
    if (!m_event || !m_pluginKfLanesHost || !m_pluginKfPanel || !m_pluginKfPanel->isVisible()) {
        return;
    }
    if (m_pluginKfRuler) {
        m_pluginKfRuler->setRange(m_durationSec, m_playheadSec);
    }
    if (m_pluginKfTc) {
        m_pluginKfTc->setText(formatTc(m_playheadSec));
    }

    const auto widgets = m_pluginKfLanesHost->findChildren<QWidget *>();
    for (QWidget *w : widgets) {
        if (w->objectName() != QLatin1String("pcKfLane")) {
            continue;
        }
        auto *lane = static_cast<KeyframeLane *>(w);
        const int fxIndex = lane->property("fxIndex").toInt();
        const QString paramKey = lane->property("paramKey").toString();
        if (fxIndex < 0 || fxIndex >= m_event->fxChain.size()) {
            continue;
        }
        const FxSlot &slot = m_event->fxChain[fxIndex];
        const QString target = (paramKey == QLatin1String("_master"))
                                   ? fxMasterAutomationId(slot)
                                   : fxParamAutomationId(slot, paramKey);
        const AutomationLane *al = findAutomationLane(target);
        QVector<double> times;
        QVector<double> values;
        QVector<int> types;
        if (al) {
            for (const AutomationPoint &pt : al->points) {
                times.push_back(pt.timeSec);
                values.push_back(pt.value);
                types.push_back(int(VideoKeyframeType::Linear));
            }
        }
        const bool focus = (fxIndex == m_pluginKfFocusFx
                            && (paramKey == m_pluginKfParamKey
                                || (paramKey == QLatin1String("_master")
                                    && m_pluginKfParamKey.isEmpty())));
        const int sel = focus ? m_pluginKfIndex : -1;
        const bool showCurve =
            m_pluginKfCurves && paramKey != QLatin1String("_master") && !times.isEmpty();
        if (showCurve) {
            lane->setCurve(times, values, types, m_durationSec, sel, m_playheadSec, true);
        } else {
            lane->setTimes(times, types, m_durationSec, sel, m_playheadSec);
        }
    }
}

void VideoEventFxDialogExact::selectPluginKeyframeIndex(int pointIndex)
{
    m_pluginKfIndex = std::max(0, pointIndex);
    if (!m_event || m_pluginKfFocusFx < 0 || m_pluginKfFocusFx >= m_event->fxChain.size()) {
        refreshPluginKeyframeLanes();
        return;
    }
    const FxSlot &slot = m_event->fxChain[m_pluginKfFocusFx];
    const QString key =
        m_pluginKfParamKey.isEmpty() ? QStringLiteral("_master") : m_pluginKfParamKey;
    const QString target = (key == QLatin1String("_master")) ? fxMasterAutomationId(slot)
                                                             : fxParamAutomationId(slot, key);
    if (const AutomationLane *al = findAutomationLane(target)) {
        if (m_pluginKfIndex >= 0 && m_pluginKfIndex < al->points.size()) {
            m_playheadSec = al->points[m_pluginKfIndex].timeSec;
            if (m_pluginKfTc) {
                m_pluginKfTc->setText(formatTc(m_playheadSec));
            }
            if (m_pluginKfRuler) {
                m_pluginKfRuler->setRange(m_durationSec, m_playheadSec);
            }
            loadMediaPreview();
        }
    }
    refreshPluginKeyframeLanes();
}

void VideoEventFxDialogExact::navigatePluginKeyframeFirst()
{
    selectPluginKeyframeIndex(0);
}

void VideoEventFxDialogExact::navigatePluginKeyframePrev()
{
    selectPluginKeyframeIndex(std::max(0, m_pluginKfIndex - 1));
}

void VideoEventFxDialogExact::navigatePluginKeyframeNext()
{
    selectPluginKeyframeIndex(m_pluginKfIndex + 1);
}

void VideoEventFxDialogExact::navigatePluginKeyframeLast()
{
    if (!m_event || m_pluginKfFocusFx < 0 || m_pluginKfFocusFx >= m_event->fxChain.size()) {
        return;
    }
    const FxSlot &slot = m_event->fxChain[m_pluginKfFocusFx];
    const QString key =
        m_pluginKfParamKey.isEmpty() ? QStringLiteral("_master") : m_pluginKfParamKey;
    const QString target = (key == QLatin1String("_master")) ? fxMasterAutomationId(slot)
                                                             : fxParamAutomationId(slot, key);
    const AutomationLane *al = findAutomationLane(target);
    if (!al || al->points.isEmpty()) {
        return;
    }
    selectPluginKeyframeIndex(al->points.size() - 1);
}

void VideoEventFxDialogExact::addPluginKeyframeAtPlayhead()
{
    addPluginKeyframeAtTime(m_playheadSec);
}

void VideoEventFxDialogExact::addPluginKeyframeAtTime(double timeSec)
{
    if (!m_event || m_pluginKfFocusFx < 0 || m_pluginKfFocusFx >= m_event->fxChain.size()) {
        return;
    }
    timeSec = std::clamp(timeSec, 0.0, m_durationSec);
    FxSlot &slot = m_event->fxChain[m_pluginKfFocusFx];
    ensureFxHostKey(&slot);
    QString key = m_pluginKfParamKey;
    if (key.isEmpty()) {
        const auto params = animatableParamsForSlot(slot);
        key = params.isEmpty() ? QStringLiteral("_master") : params.first().second;
        m_pluginKfParamKey = key;
    }
    // Always stamp a master marker too (Vegas-style plugin row diamonds).
    {
        AutomationLane &master = ensureAutomationLane(fxMasterAutomationId(slot));
        bool near = false;
        for (const AutomationPoint &pt : master.points) {
            if (std::abs(pt.timeSec - timeSec) < 1e-3) {
                near = true;
                break;
            }
        }
        if (!near) {
            AutomationPoint pt;
            pt.timeSec = timeSec;
            pt.value = 1.0;
            master.points.push_back(pt);
            std::sort(master.points.begin(), master.points.end(),
                      [](const AutomationPoint &a, const AutomationPoint &b) {
                          return a.timeSec < b.timeSec;
                      });
        }
    }
    if (key != QLatin1String("_master")) {
        AutomationLane &lane = ensureAutomationLane(fxParamAutomationId(slot, key));
        const double value = currentParamValue(slot, key);
        int replace = -1;
        for (int i = 0; i < lane.points.size(); ++i) {
            if (std::abs(lane.points[i].timeSec - timeSec) < 1e-3) {
                replace = i;
                break;
            }
        }
        if (replace >= 0) {
            lane.points[replace].value = value;
            m_pluginKfIndex = replace;
        } else {
            AutomationPoint pt;
            pt.timeSec = timeSec;
            pt.value = value;
            lane.points.push_back(pt);
            std::sort(lane.points.begin(), lane.points.end(),
                      [](const AutomationPoint &a, const AutomationPoint &b) {
                          return a.timeSec < b.timeSec;
                      });
            for (int i = 0; i < lane.points.size(); ++i) {
                if (std::abs(lane.points[i].timeSec - timeSec) < 1e-3) {
                    m_pluginKfIndex = i;
                    break;
                }
            }
        }
    }
    m_playheadSec = timeSec;
    rebuildPluginKeyframeLanes();
    refreshPluginKeyframeLanes();
}

void VideoEventFxDialogExact::deleteSelectedPluginKeyframe()
{
    if (!m_event || m_pluginKfFocusFx < 0 || m_pluginKfFocusFx >= m_event->fxChain.size()) {
        return;
    }
    const FxSlot &slot = m_event->fxChain[m_pluginKfFocusFx];
    const QString key =
        m_pluginKfParamKey.isEmpty() ? QStringLiteral("_master") : m_pluginKfParamKey;
    const QString target = (key == QLatin1String("_master")) ? fxMasterAutomationId(slot)
                                                             : fxParamAutomationId(slot, key);
    AutomationLane *al = findAutomationLane(target);
    if (!al || m_pluginKfIndex < 0 || m_pluginKfIndex >= al->points.size()) {
        return;
    }
    const double t = al->points[m_pluginKfIndex].timeSec;
    al->points.removeAt(m_pluginKfIndex);
    if (AutomationLane *master = findAutomationLane(fxMasterAutomationId(slot))) {
        for (int i = master->points.size() - 1; i >= 0; --i) {
            if (std::abs(master->points[i].timeSec - t) < 1e-3) {
                master->points.removeAt(i);
            }
        }
    }
    if (al->points.isEmpty()) {
        m_pluginKfIndex = 0;
    } else {
        m_pluginKfIndex = std::min(m_pluginKfIndex, int(al->points.size()) - 1);
    }
    rebuildPluginKeyframeLanes();
    refreshPluginKeyframeLanes();
}

void VideoEventFxDialogExact::movePluginKeyframe(int pointIndex, double timeSec, bool finalize)
{
    if (!m_event || m_pluginKfFocusFx < 0 || m_pluginKfFocusFx >= m_event->fxChain.size()) {
        return;
    }
    timeSec = std::clamp(timeSec, 0.0, m_durationSec);
    const FxSlot &slot = m_event->fxChain[m_pluginKfFocusFx];
    const QString key =
        m_pluginKfParamKey.isEmpty() ? QStringLiteral("_master") : m_pluginKfParamKey;
    const QString target = (key == QLatin1String("_master")) ? fxMasterAutomationId(slot)
                                                             : fxParamAutomationId(slot, key);
    AutomationLane *al = findAutomationLane(target);
    if (!al || pointIndex < 0 || pointIndex >= al->points.size()) {
        return;
    }
    const double oldT = al->points[pointIndex].timeSec;
    al->points[pointIndex].timeSec = timeSec;
    if (finalize) {
        std::sort(al->points.begin(), al->points.end(),
                  [](const AutomationPoint &a, const AutomationPoint &b) {
                      return a.timeSec < b.timeSec;
                  });
        for (int i = 0; i < al->points.size(); ++i) {
            if (std::abs(al->points[i].timeSec - timeSec) < 1e-6) {
                m_pluginKfIndex = i;
                break;
            }
        }
        if (AutomationLane *master = findAutomationLane(fxMasterAutomationId(slot))) {
            for (AutomationPoint &pt : master->points) {
                if (std::abs(pt.timeSec - oldT) < 1e-3) {
                    pt.timeSec = timeSec;
                }
            }
            std::sort(master->points.begin(), master->points.end(),
                      [](const AutomationPoint &a, const AutomationPoint &b) {
                          return a.timeSec < b.timeSec;
                      });
        }
    }
    m_playheadSec = timeSec;
    if (m_pluginKfTc) {
        m_pluginKfTc->setText(formatTc(m_playheadSec));
    }
    if (m_pluginKfRuler) {
        m_pluginKfRuler->setRange(m_durationSec, m_playheadSec);
    }
    if (finalize) {
        refreshPluginKeyframeLanes();
    }
}

void VideoEventFxDialogExact::navigateKeyframeFirst()
{
    if (m_channel == PanCropChannel::Mask) {
        selectMaskIndex(0);
    } else {
        selectPositionIndex(0);
    }
}

void VideoEventFxDialogExact::navigateKeyframePrev()
{
    if (m_channel == PanCropChannel::Mask) {
        selectMaskIndex(m_maskIndex - 1);
    } else {
        selectPositionIndex(m_posIndex - 1);
    }
}

void VideoEventFxDialogExact::navigateKeyframeNext()
{
    if (m_channel == PanCropChannel::Mask) {
        selectMaskIndex(m_maskIndex + 1);
    } else {
        selectPositionIndex(m_posIndex + 1);
    }
}

void VideoEventFxDialogExact::navigateKeyframeLast()
{
    if (!m_event) {
        return;
    }
    if (m_channel == PanCropChannel::Mask) {
        selectMaskIndex(int(panCrop().maskKeyframes.size()) - 1);
    } else {
        selectPositionIndex(int(panCrop().positionKeyframes.size()) - 1);
    }
}

void VideoEventFxDialogExact::addKeyframeAtPlayhead()
{
    addKeyframeAtTime(m_playheadSec);
}

void VideoEventFxDialogExact::addKeyframeAtTime(double timeSec)
{
    if (!m_event) {
        return;
    }
    timeSec = std::clamp(timeSec, 0.0, m_durationSec);
    m_playheadSec = timeSec;

    if (m_channel == PanCropChannel::Mask) {
        MaskKeyframe k;
        if (MaskKeyframe *cur = selectedMaskKf()) {
            k = *cur;
        }
        k.timeSec = timeSec;
        auto &kfs = panCrop().maskKeyframes;
        for (int i = 0; i < kfs.size(); ++i) {
            if (std::abs(kfs[i].timeSec - timeSec) < 1e-3) {
                kfs[i] = k;
                selectMaskIndex(i);
                return;
            }
        }
        int insertAt = 0;
        while (insertAt < kfs.size() && kfs[insertAt].timeSec <= timeSec + 1e-9) {
            ++insertAt;
        }
        kfs.insert(insertAt, k);
        selectMaskIndex(insertAt);
        return;
    }

    // Capture interpolated state at this time (Vegas: new KF inherits current animated values).
    PanCropKeyframe k = evaluatePanCrop(panCrop(), timeSec, m_spaceW, m_spaceH);
    k.timeSec = timeSec;
    auto &kfs = panCrop().positionKeyframes;
    for (int i = 0; i < kfs.size(); ++i) {
        if (std::abs(kfs[i].timeSec - timeSec) < 1e-3) {
            // Refresh values from current props/canvas if selected was being edited.
            if (PanCropKeyframe *cur = selectedKf()) {
                k = *cur;
                k.timeSec = timeSec;
            }
            kfs[i] = k;
            selectPositionIndex(i);
            return;
        }
    }
    int insertAt = 0;
    while (insertAt < kfs.size() && kfs[insertAt].timeSec <= timeSec + 1e-9) {
        ++insertAt;
    }
    kfs.insert(insertAt, k);
    selectPositionIndex(insertAt);
}

void VideoEventFxDialogExact::deleteSelectedKeyframe()
{
    if (!m_event) {
        return;
    }
    if (m_channel == PanCropChannel::Mask) {
        if (panCrop().maskKeyframes.size() <= 1) {
            return;
        }
        const int removeAt = std::clamp(m_maskIndex, 0, int(panCrop().maskKeyframes.size()) - 1);
        panCrop().maskKeyframes.removeAt(removeAt);
        selectMaskIndex(std::min(removeAt, int(panCrop().maskKeyframes.size()) - 1));
        loadMediaPreview();
        return;
    }
    if (panCrop().positionKeyframes.size() <= 1) {
        return;
    }
    const int removeAt = std::clamp(m_posIndex, 0, int(panCrop().positionKeyframes.size()) - 1);
    panCrop().positionKeyframes.removeAt(removeAt);
    selectPositionIndex(std::min(removeAt, int(panCrop().positionKeyframes.size()) - 1));
    loadMediaPreview();
}

void VideoEventFxDialogExact::movePositionKeyframe(int index, double timeSec, bool finalize)
{
    if (!m_event || index < 0 || index >= panCrop().positionKeyframes.size()) {
        return;
    }
    timeSec = std::clamp(timeSec, 0.0, m_durationSec);
    auto &kfs = panCrop().positionKeyframes;
    if (!finalize) {
        kfs[index].timeSec = timeSec;
        m_posIndex = index;
        m_playheadSec = timeSec;
        if (m_tc) {
            m_tc->setText(formatTc(m_playheadSec));
        }
        if (m_ruler) {
            m_ruler->setRange(m_durationSec, m_playheadSec);
        }
        return;
    }
    PanCropKeyframe moved = kfs[index];
    moved.timeSec = timeSec;
    kfs.removeAt(index);
    int insertAt = 0;
    while (insertAt < kfs.size() && kfs[insertAt].timeSec <= timeSec + 1e-9) {
        ++insertAt;
    }
    kfs.insert(insertAt, moved);
    m_posIndex = insertAt;
    m_playheadSec = moved.timeSec;
    syncUiFromSelected();
    refreshKeyframeLanes();
    loadMediaPreview();
}

void VideoEventFxDialogExact::moveMaskKeyframe(int index, double timeSec, bool finalize)
{
    if (!m_event || index < 0 || index >= panCrop().maskKeyframes.size()) {
        return;
    }
    timeSec = std::clamp(timeSec, 0.0, m_durationSec);
    auto &kfs = panCrop().maskKeyframes;
    if (!finalize) {
        kfs[index].timeSec = timeSec;
        m_maskIndex = index;
        m_playheadSec = timeSec;
        if (m_tc) {
            m_tc->setText(formatTc(m_playheadSec));
        }
        if (m_ruler) {
            m_ruler->setRange(m_durationSec, m_playheadSec);
        }
        return;
    }
    MaskKeyframe moved = kfs[index];
    moved.timeSec = timeSec;
    kfs.removeAt(index);
    int insertAt = 0;
    while (insertAt < kfs.size() && kfs[insertAt].timeSec <= timeSec + 1e-9) {
        ++insertAt;
    }
    kfs.insert(insertAt, moved);
    m_maskIndex = insertAt;
    m_playheadSec = moved.timeSec;
    syncUiFromSelected();
    refreshKeyframeLanes();
}

void VideoEventFxDialogExact::ensureEditableKeyframeAtPlayhead()
{
    if (!m_event) {
        return;
    }
    constexpr double kEps = 1e-3;
    if (m_channel == PanCropChannel::Mask) {
        for (int i = 0; i < panCrop().maskKeyframes.size(); ++i) {
            if (std::abs(panCrop().maskKeyframes[i].timeSec - m_playheadSec) < kEps) {
                m_maskIndex = i;
                return;
            }
        }
        addKeyframeAtTime(m_playheadSec);
        return;
    }
    for (int i = 0; i < panCrop().positionKeyframes.size(); ++i) {
        if (std::abs(panCrop().positionKeyframes[i].timeSec - m_playheadSec) < kEps) {
            m_posIndex = i;
            return;
        }
    }
    addKeyframeAtTime(m_playheadSec);
}

void VideoEventFxDialogExact::cutSelectedKeyframe()
{
    copySelectedKeyframe();
    deleteSelectedKeyframe();
}

void VideoEventFxDialogExact::copySelectedKeyframe()
{
    if (!m_event) {
        return;
    }
    if (m_channel == PanCropChannel::Mask) {
        if (MaskKeyframe *mk = selectedMaskKf()) {
            m_maskClipboard = *mk;
            m_hasMaskClipboard = true;
            refreshKeyframeLanes();
        }
        return;
    }
    if (PanCropKeyframe *kf = selectedKf()) {
        m_posClipboard = *kf;
        m_hasPosClipboard = true;
        refreshKeyframeLanes();
    }
}

void VideoEventFxDialogExact::pasteKeyframeAtPlayhead()
{
    if (!m_event) {
        return;
    }
    if (m_channel == PanCropChannel::Mask) {
        if (!m_hasMaskClipboard) {
            return;
        }
        MaskKeyframe k = m_maskClipboard;
        k.timeSec = m_playheadSec;
        auto &kfs = panCrop().maskKeyframes;
        for (int i = 0; i < kfs.size(); ++i) {
            if (std::abs(kfs[i].timeSec - m_playheadSec) < 1e-3) {
                kfs[i] = k;
                selectMaskIndex(i);
                return;
            }
        }
        int insertAt = 0;
        while (insertAt < kfs.size() && kfs[insertAt].timeSec <= m_playheadSec + 1e-9) {
            ++insertAt;
        }
        kfs.insert(insertAt, k);
        selectMaskIndex(insertAt);
        return;
    }
    if (!m_hasPosClipboard) {
        return;
    }
    PanCropKeyframe k = m_posClipboard;
    k.timeSec = m_playheadSec;
    auto &kfs = panCrop().positionKeyframes;
    for (int i = 0; i < kfs.size(); ++i) {
        if (std::abs(kfs[i].timeSec - m_playheadSec) < 1e-3) {
            kfs[i] = k;
            selectPositionIndex(i);
            return;
        }
    }
    int insertAt = 0;
    while (insertAt < kfs.size() && kfs[insertAt].timeSec <= m_playheadSec + 1e-9) {
        ++insertAt;
    }
    kfs.insert(insertAt, k);
    selectPositionIndex(insertAt);
}

void VideoEventFxDialogExact::setSelectedKeyframeType(VideoKeyframeType type)
{
    if (!m_event) {
        return;
    }
    if (m_channel == PanCropChannel::Mask) {
        if (MaskKeyframe *mk = selectedMaskKf()) {
            mk->type = type;
            refreshKeyframeLanes();
        }
        return;
    }
    if (PanCropKeyframe *kf = selectedKf()) {
        kf->type = type;
        refreshKeyframeLanes();
        syncUiFromSelected();
    }
}

namespace {

void addInterpolationActions(QMenu *menu, VideoKeyframeType current,
                             const std::function<void(VideoKeyframeType)> &onPick)
{
    auto *group = new QActionGroup(menu);
    group->setExclusive(true);
    const VideoKeyframeType order[] = {
        VideoKeyframeType::Linear, VideoKeyframeType::Fast,   VideoKeyframeType::Slow,
        VideoKeyframeType::Smooth, VideoKeyframeType::Sharp,  VideoKeyframeType::Hold,
    };
    for (VideoKeyframeType t : order) {
        QAction *a = menu->addAction(videoKeyframeTypeName(t));
        a->setCheckable(true);
        a->setChecked(t == current);
        group->addAction(a);
        QObject::connect(a, &QAction::triggered, menu, [onPick, t]() { onPick(t); });
    }
}

} // namespace

void VideoEventFxDialogExact::showPositionKeyframeMenu(int index, const QPoint &globalPos)
{
    if (!m_event || index < 0 || index >= panCrop().positionKeyframes.size()) {
        return;
    }
    selectPositionIndex(index);
    const VideoKeyframeType cur = panCrop().positionKeyframes[m_posIndex].type;

    QMenu menu(this);
    QAction *cutAct = menu.addAction(tr("Cut"));
    QAction *copyAct = menu.addAction(tr("Copy"));
    QAction *pasteAct = menu.addAction(tr("Paste"));
    pasteAct->setEnabled(m_hasPosClipboard);
    menu.addSeparator();
    QAction *delAct = menu.addAction(tr("Delete"));
    delAct->setEnabled(panCrop().positionKeyframes.size() > 1);
    menu.addSeparator();
    addInterpolationActions(&menu, cur, [this](VideoKeyframeType t) { setSelectedKeyframeType(t); });

    QAction *chosen = menu.exec(globalPos);
    if (!chosen) {
        return;
    }
    if (chosen == cutAct) {
        cutSelectedKeyframe();
    } else if (chosen == copyAct) {
        copySelectedKeyframe();
    } else if (chosen == pasteAct) {
        pasteKeyframeAtPlayhead();
    } else if (chosen == delAct) {
        deleteSelectedKeyframe();
    }
}

void VideoEventFxDialogExact::showMaskKeyframeMenu(int index, const QPoint &globalPos)
{
    if (!m_event || index < 0 || index >= panCrop().maskKeyframes.size()) {
        return;
    }
    selectMaskIndex(index);
    const VideoKeyframeType cur = panCrop().maskKeyframes[m_maskIndex].type;

    QMenu menu(this);
    QAction *cutAct = menu.addAction(tr("Cut"));
    QAction *copyAct = menu.addAction(tr("Copy"));
    QAction *pasteAct = menu.addAction(tr("Paste"));
    pasteAct->setEnabled(m_hasMaskClipboard);
    menu.addSeparator();
    QAction *delAct = menu.addAction(tr("Delete"));
    delAct->setEnabled(panCrop().maskKeyframes.size() > 1);
    menu.addSeparator();
    addInterpolationActions(&menu, cur, [this](VideoKeyframeType t) { setSelectedKeyframeType(t); });

    QAction *chosen = menu.exec(globalPos);
    if (!chosen) {
        return;
    }
    if (chosen == cutAct) {
        cutSelectedKeyframe();
    } else if (chosen == copyAct) {
        copySelectedKeyframe();
    } else if (chosen == pasteAct) {
        pasteKeyframeAtPlayhead();
    } else if (chosen == delAct) {
        deleteSelectedKeyframe();
    }
}

void VideoEventFxDialogExact::syncMaskFromUi()
{
    if (!m_event || m_block) {
        return;
    }
    panCrop().maskEnabled = m_maskEnable->isChecked();
    if (MaskKeyframe *mk = selectedMaskKf()) {
        if (mk->paths.isEmpty()) {
            mk->paths.push_back(MaskPath{});
        }
        mk->paths[0].mode = static_cast<MaskPathMode>(m_pathMode->currentIndex());
        if (m_selMaskPath >= 0 && m_selMaskAnchor >= 0 && m_selMaskPath < mk->paths.size()
            && m_selMaskAnchor < mk->paths[m_selMaskPath].anchors.size()) {
            mk->paths[m_selMaskPath].anchors[m_selMaskAnchor].x = m_maskAnchorX->value();
            mk->paths[m_selMaskPath].anchors[m_selMaskAnchor].y = m_maskAnchorY->value();
        }
    }
    if (m_maskRowCheck) {
        m_block = true;
        m_maskRowCheck->setChecked(panCrop().maskEnabled);
        m_block = false;
    }
    if (m_canvas) {
        if (MaskKeyframe *mk = selectedMaskKf()) {
            m_canvas->setMaskKeyframe(*mk, panCrop().maskEnabled, m_spaceW, m_spaceH);
            m_canvas->setMaskSelection(m_selMaskPath, m_selMaskAnchor);
        }
        m_canvas->setWorkspace(panCrop().workspaceZoom, panCrop().workspaceX, panCrop().workspaceY,
                               panCrop().gridSpacing);
    }
}

void VideoEventFxDialogExact::syncUiFromSelected()
{
    if (!m_event) {
        return;
    }
    m_block = true;
    m_wsZoom->setValue(panCrop().workspaceZoom);
    m_wsX->setValue(panCrop().workspaceX);
    m_wsY->setValue(panCrop().workspaceY);
    m_grid->setValue(panCrop().gridSpacing);

    if (m_channel == PanCropChannel::Mask) {
        if (MaskKeyframe *mk = selectedMaskKf()) {
            m_maskEnable->setChecked(panCrop().maskEnabled);
            if (m_maskRowCheck) {
                m_maskRowCheck->setChecked(panCrop().maskEnabled);
            }
            if (!mk->paths.isEmpty()) {
                m_pathMode->setCurrentIndex(int(mk->paths[0].mode));
                if (m_selMaskPath >= 0 && m_selMaskAnchor >= 0
                    && m_selMaskPath < mk->paths.size()
                    && m_selMaskAnchor < mk->paths[m_selMaskPath].anchors.size()) {
                    const MaskAnchor &a = mk->paths[m_selMaskPath].anchors[m_selMaskAnchor];
                    m_maskAnchorX->setValue(a.x);
                    m_maskAnchorY->setValue(a.y);
                } else if (!mk->paths[0].anchors.isEmpty()) {
                    m_maskAnchorX->setValue(mk->paths[0].anchors[0].x);
                    m_maskAnchorY->setValue(mk->paths[0].anchors[0].y);
                } else {
                    m_maskAnchorX->setValue(0);
                    m_maskAnchorY->setValue(0);
                }
            } else {
                m_pathMode->setCurrentIndex(0);
                m_maskAnchorX->setValue(0);
                m_maskAnchorY->setValue(0);
            }
            if (m_canvas) {
                pushCanvasSpaces();
                m_canvas->setMaskKeyframe(*mk, panCrop().maskEnabled, m_spaceW, m_spaceH);
                m_canvas->setMaskSelection(m_selMaskPath, m_selMaskAnchor);
                m_canvas->setWorkspace(panCrop().workspaceZoom, panCrop().workspaceX,
                                       panCrop().workspaceY, panCrop().gridSpacing);
                applyCanvasOptions();
            }
        }
        m_block = false;
        refreshKeyframeLanes();
        if (m_tc) {
            m_tc->setText(formatTc(m_playheadSec));
        }
        return;
    }

    PanCropKeyframe *kf = selectedKf();
    if (!kf) {
        m_block = false;
        return;
    }
    m_w->setValue(kf->width);
    m_h->setValue(kf->height);
    m_x->setValue(kf->xCenter);
    m_y->setValue(kf->yCenter);
    m_angle->setValue(kf->angleDeg);
    m_rotX->setValue(kf->rotationXCenter);
    m_rotY->setValue(kf->rotationYCenter);
    m_smooth->setValue(kf->smoothness);
    m_aspect->setCurrentIndex(panCrop().maintainAspectRatio ? 0 : 1);
    m_stretch->setCurrentIndex(panCrop().stretchToFillFrame ? 0 : 1);
    m_block = false;
    syncFlipButtons();
    if (m_canvas) {
        pushCanvasSpaces();
        m_canvas->setKeyframe(*kf, m_spaceW, m_spaceH);
        if (MaskKeyframe *mk = selectedMaskKf()) {
            m_canvas->setMaskKeyframe(*mk, panCrop().maskEnabled, m_spaceW, m_spaceH);
        }
        m_canvas->setWorkspace(panCrop().workspaceZoom, panCrop().workspaceX, panCrop().workspaceY,
                               panCrop().gridSpacing);
        applyCanvasOptions();
    }
    refreshKeyframeLanes();
}

void VideoEventFxDialogExact::syncSelectedFromUi()
{
    if (!m_event || m_block) {
        return;
    }
    if (m_channel == PanCropChannel::Mask) {
        ensureEditableKeyframeAtPlayhead();
        syncMaskFromUi();
        refreshKeyframeLanes();
        return;
    }
    ensureEditableKeyframeAtPlayhead();
    PanCropKeyframe *kf = selectedKf();
    if (!kf) {
        return;
    }
    kf->width = m_w->value();
    kf->height = m_h->value();
    kf->xCenter = m_x->value();
    kf->yCenter = m_y->value();
    kf->angleDeg = m_angle->value();
    kf->rotationXCenter = m_rotX->value();
    kf->rotationYCenter = m_rotY->value();
    kf->smoothness = m_smooth->value();
    panCrop().workspaceZoom = m_wsZoom->value();
    panCrop().workspaceX = m_wsX->value();
    panCrop().workspaceY = m_wsY->value();
    panCrop().gridSpacing = int(m_grid->value());
    panCrop().maintainAspectRatio = (m_aspect->currentIndex() == 0);
    panCrop().stretchToFillFrame = (m_stretch->currentIndex() == 0);
    syncFlipButtons();
    if (m_canvas) {
        m_canvas->setKeyframe(*kf, m_spaceW, m_spaceH);
        m_canvas->setWorkspace(panCrop().workspaceZoom, panCrop().workspaceX, panCrop().workspaceY,
                               panCrop().gridSpacing);
    }
    refreshKeyframeLanes();
}

void VideoEventFxDialogExact::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        // Don't steal Backspace from spinboxes / line edits.
        if (qobject_cast<QAbstractSpinBox *>(focusWidget())
            || qobject_cast<QComboBox *>(focusWidget())) {
            QDialog::keyPressEvent(event);
            return;
        }
        deleteSelectedKeyframe();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::New)
        || ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_K)) {
        addKeyframeAtPlayhead();
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

} // namespace openvegas
