#include "ui/TrackMotionDialog.h"

#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QSettings>
#include <QMessageBox>
#include <QInputDialog>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtMath>
#include <QLineF>
#include <algorithm>
#include <cmath>
#include <functional>

namespace openvegas {

namespace {

constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

QString formatTc(double sec)
{
    sec = std::max(0.0, sec);
    const int totalCs = int(std::llround(sec * 100.0));
    const int cs = totalCs % 100;
    const int totalSec = totalCs / 100;
    const int s = totalSec % 60;
    const int totalMin = totalSec / 60;
    const int m = totalMin % 60;
    const int h = totalMin / 60;
    return QStringLiteral("%1:%2:%3,%4")
        .arg(h, 2, 10, QChar(u'0'))
        .arg(m, 2, 10, QChar(u'0'))
        .arg(s, 2, 10, QChar(u'0'))
        .arg(cs, 2, 10, QChar(u'0'));
}

QString kfTypeName(VideoKeyframeType t)
{
    switch (t) {
    case VideoKeyframeType::Fast:
        return QStringLiteral("Fast");
    case VideoKeyframeType::Slow:
        return QStringLiteral("Slow");
    case VideoKeyframeType::Smooth:
        return QStringLiteral("Smooth");
    case VideoKeyframeType::Sharp:
        return QStringLiteral("Sharp");
    case VideoKeyframeType::Linear:
    default:
        return QStringLiteral("Linear");
    }
}

VideoKeyframeType kfTypeFromName(const QString &n)
{
    if (n == QStringLiteral("Fast")) {
        return VideoKeyframeType::Fast;
    }
    if (n == QStringLiteral("Slow")) {
        return VideoKeyframeType::Slow;
    }
    if (n == QStringLiteral("Smooth")) {
        return VideoKeyframeType::Smooth;
    }
    if (n == QStringLiteral("Sharp")) {
        return VideoKeyframeType::Sharp;
    }
    return VideoKeyframeType::Linear;
}

/** Pixel-space view of one motion keyframe + workspace UI. */
struct MotionView {
    double x = 0.0;
    double y = 0.0;
    double width = 1920.0;
    double height = 1080.0;
    double orientationDeg = 0.0;
    double workspaceZoom = 50.0;
    double workspaceX = 0.0;
    double workspaceY = 0.0;
    bool shadowEnabled = false;
    double shadowBlur = 5.0;
    double shadowIntensity = 50.0;
    QColor shadowColor = QColor(0, 0, 0);
};

MotionView viewFromKf(const TrackMotionKeyframe &kf, const TrackMotionState &st, int fw, int fh)
{
    MotionView v;
    v.x = kf.positionX * double(fh);
    v.y = kf.positionY * double(fh);
    v.width = kf.width * double(fh);
    v.height = kf.height * double(fh);
    if (v.width < 1.0) {
        v.width = fw;
    }
    if (v.height < 1.0) {
        v.height = fh;
    }
    v.orientationDeg = kf.orientationZ * kRadToDeg;
    v.workspaceZoom = st.workspaceZoom;
    v.workspaceX = st.workspaceX;
    v.workspaceY = st.workspaceY;
    v.shadowEnabled = st.shadowEnabled;
    if (!st.shadowKeyframes.isEmpty()) {
        const TrackFXKeyframe &sh = st.shadowKeyframes.first();
        v.shadowBlur = sh.blur * 100.0;
        v.shadowIntensity = sh.intensity;
        v.shadowColor = sh.color;
    }
    return v;
}

void applyViewToKf(const MotionView &v, TrackMotionKeyframe *kf, int fh)
{
    if (!kf || fh <= 0) {
        return;
    }
    kf->positionX = v.x / double(fh);
    kf->positionY = v.y / double(fh);
    kf->width = v.width / double(fh);
    kf->height = v.height / double(fh);
    kf->orientationZ = v.orientationDeg * kDegToRad;
}

QDoubleSpinBox *makeSpin(QWidget *parent, double minV, double maxV, int decimals, double step = 1.0)
{
    auto *s = new QDoubleSpinBox(parent);
    s->setObjectName(QStringLiteral("tmSpin"));
    s->setRange(minV, maxV);
    s->setDecimals(decimals);
    s->setSingleStep(step);
    s->setButtonSymbols(QAbstractSpinBox::NoButtons);
    s->setAlignment(Qt::AlignRight);
    s->setFixedWidth(88);
    s->setFixedHeight(18);
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

QToolButton *makeTool(QWidget *parent, const QString &text, const QString &tip, bool active = false)
{
    auto *b = new QToolButton(parent);
    b->setObjectName(active ? QStringLiteral("pcToolActive") : QStringLiteral("pcTool"));
    b->setText(text);
    b->setToolTip(tip);
    b->setFixedSize(22, 22);
    b->setCheckable(true);
    b->setChecked(active);
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

/** Keyframe diamond lane. */
class KeyframeLane : public QWidget {
public:
    explicit KeyframeLane(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("pcKfLane"));
        setMinimumHeight(28);
        setMouseTracking(true);
    }

    void setTimes(const QVector<double> &times, double duration, int selected, double playhead)
    {
        m_times = times;
        m_duration = std::max(0.001, duration);
        m_selected = selected;
        m_playhead = playhead;
        update();
    }

    void setOnSelect(const std::function<void(int)> &fn) { m_onSelect = fn; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0x2a, 0x2a, 0x2e));
        const int midY = height() / 2;
        const double phX = (m_playhead / m_duration) * width();
        p.setPen(QPen(QColor(0xe0, 0x40, 0x40), 1));
        p.drawLine(QPointF(phX, 0), QPointF(phX, height()));

        for (int i = 0; i < m_times.size(); ++i) {
            const double x = (m_times[i] / m_duration) * width();
            const bool sel = (i == m_selected);
            QPolygonF dia;
            const double s = sel ? 6.0 : 5.0;
            dia << QPointF(x, midY - s) << QPointF(x + s, midY) << QPointF(x, midY + s)
                << QPointF(x - s, midY);
            p.setBrush(sel ? QColor(0x40, 0xa0, 0xff) : QColor(0xd0, 0xd0, 0xd0));
            p.setPen(QPen(Qt::black, 1));
            p.drawPolygon(dia);
        }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton || m_times.isEmpty()) {
            return;
        }
        const double mx = event->position().x();
        int best = 0;
        double bestD = 1e9;
        for (int i = 0; i < m_times.size(); ++i) {
            const double x = (m_times[i] / m_duration) * width();
            const double d = std::abs(x - mx);
            if (d < bestD) {
                bestD = d;
                best = i;
            }
        }
        if (bestD < 12.0 && m_onSelect) {
            m_onSelect(best);
        }
    }

private:
    QVector<double> m_times;
    double m_duration = 10.0;
    int m_selected = 0;
    double m_playhead = 0.0;
    std::function<void(int)> m_onSelect;
};

} // namespace

/** Interactive workspace: frame + X/Y gizmo. Drag center = move, drag ring = orient. */
class TrackMotionCanvas : public QWidget {
public:
    explicit TrackMotionCanvas(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("pcCanvas"));
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMinimumSize(240, 180);
        setMouseTracking(true);
    }

    void setView(const MotionView &v, int frameW, int frameH)
    {
        m_view = v;
        m_frameW = std::max(1, frameW);
        m_frameH = std::max(1, frameH);
        update();
    }

    void setOnEdited(const std::function<void(const MotionView &)> &fn) { m_onEdited = fn; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0xc8, 0xc8, 0xc8));

        p.setPen(QColor(0x9a, 0x9a, 0x9a));
        for (int y = 5; y < height(); y += 10) {
            for (int x = 5; x < width(); x += 10) {
                p.drawPoint(x, y);
            }
        }

        const QRectF frame = frameRect();
        const QPointF c = frame.center();

        p.save();
        p.translate(c);
        p.rotate(m_view.orientationDeg);
        p.translate(-c);

        if (m_view.shadowEnabled) {
            const double blur = m_view.shadowBlur;
            QColor sc = m_view.shadowColor;
            sc.setAlpha(int(std::clamp(m_view.shadowIntensity / 100.0, 0.0, 1.0) * 180));
            p.setPen(Qt::NoPen);
            p.setBrush(sc);
            p.drawRect(frame.translated(blur * 0.4, blur * 0.4));
        }

        p.setPen(QPen(QColor(0x22, 0x22, 0x22), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(frame);
        p.setPen(QPen(QColor(0x55, 0x88, 0xcc), 1));
        p.drawRect(frame.adjusted(3, 3, -3, -3));

        p.restore();

        p.setRenderHint(QPainter::Antialiasing, true);
        const qreal orbitR = qMax(frame.width(), frame.height()) * 0.22;
        p.setPen(QPen(QColor(0x40, 0xa0, 0xff), 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(c, orbitR, orbitR);

        p.setPen(QPen(QColor(0x40, 0xa0, 0xff), 1.5));
        p.drawLine(c.x() - orbitR * 1.35, c.y(), c.x() + orbitR * 1.35, c.y());
        p.drawLine(c.x(), c.y() - orbitR * 1.35, c.x(), c.y() + orbitR * 1.35);
        p.setPen(QColor(0x20, 0x20, 0x20));
        QFont f = font();
        f.setPointSize(8);
        f.setBold(true);
        p.setFont(f);
        p.drawText(QPointF(c.x() + orbitR * 1.4, c.y() - 2), QStringLiteral("X"));
        p.drawText(QPointF(c.x() + 4, c.y() - orbitR * 1.4), QStringLiteral("Y"));

        p.setBrush(QColor(0x40, 0xa0, 0xff));
        p.setPen(QPen(Qt::white, 1));
        p.drawEllipse(c, 5, 5);
        p.setRenderHint(QPainter::Antialiasing, false);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton) {
            return;
        }
        m_press = event->position();
        m_start = m_view;
        const QPointF c = frameRect().center();
        const qreal d = QLineF(c, m_press).length();
        const qreal orbitR = qMax(frameRect().width(), frameRect().height()) * 0.22;
        m_mode = (d > orbitR * 0.7 && d < orbitR * 1.6) ? DragRotate : DragMove;
        setCursor(m_mode == DragRotate ? Qt::ClosedHandCursor : Qt::SizeAllCursor);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!(event->buttons() & Qt::LeftButton) || m_mode == DragNone) {
            return;
        }
        const QRectF fr = frameRect();
        const double sx = m_frameW / fr.width();
        const double sy = m_frameH / fr.height();
        if (m_mode == DragMove) {
            const QPointF d = event->position() - m_press;
            m_view.x = m_start.x + d.x() * sx;
            m_view.y = m_start.y + d.y() * sy;
        } else {
            const QPointF c = fr.center();
            const double a0 = std::atan2(m_press.y() - c.y(), m_press.x() - c.x());
            const double a1 =
                std::atan2(event->position().y() - c.y(), event->position().x() - c.x());
            m_view.orientationDeg = m_start.orientationDeg + qRadiansToDegrees(a1 - a0);
        }
        update();
        if (m_onEdited) {
            m_onEdited(m_view);
        }
    }

    void mouseReleaseEvent(QMouseEvent *) override
    {
        m_mode = DragNone;
        setCursor(Qt::ArrowCursor);
    }

private:
    enum DragMode { DragNone, DragMove, DragRotate };

    QRectF frameRect() const
    {
        const double zoom = std::clamp(m_view.workspaceZoom, 10.0, 400.0) / 100.0;
        const double aspect = double(m_frameW) / double(m_frameH);
        double fw = width() * 0.55 * zoom;
        double fh = fw / aspect;
        if (fh > height() * 0.7 * zoom) {
            fh = height() * 0.7 * zoom;
            fw = fh * aspect;
        }
        const double cx = width() * 0.5 + m_view.x * (fw / m_frameW) + m_view.workspaceX;
        const double cy = height() * 0.5 + m_view.y * (fh / m_frameH) + m_view.workspaceY;
        const double w = m_view.width * (fw / m_frameW);
        const double h = m_view.height * (fh / m_frameH);
        return QRectF(cx - w * 0.5, cy - h * 0.5, w, h);
    }

    MotionView m_view;
    int m_frameW = 1920;
    int m_frameH = 1080;
    DragMode m_mode = DragNone;
    QPointF m_press;
    MotionView m_start;
    std::function<void(const MotionView &)> m_onEdited;
};

TrackMotionDialog::TrackMotionDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("pcWindow"));
    setWindowTitle(tr("Track Motion"));
    // A real top-level window, not a modal sheet: VEGAS leaves this open while you scrub
    // and edit behind it, and a modal one froze the preview so a change could not be seen.
    setWindowFlags(Qt::Window | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
    setModal(false);
    resize(1000, 700);
    setMinimumSize(640, 480);
    buildUi();
}

void TrackMotionDialog::setTrack(Track *track, int frameW, int frameH, double durationSec,
                                 double playheadSec)
{
    m_track = track;
    m_frameW = std::max(1, frameW);
    m_frameH = std::max(1, frameH);
    m_durationSec = std::max(1.0, durationSec);
    m_playheadSec = std::clamp(playheadSec, 0.0, m_durationSec);
    if (m_track) {
        const double aspect = double(m_frameW) / double(m_frameH);
        m_track->motion.ensureDefault(aspect);
        m_motionIndex = m_track->motion.motionIndexAt(m_playheadSec);
        if (m_motionIndex < 0) {
            m_motionIndex = 0;
        }
    }
    refreshTitle();
    syncUiFromSelected();
    refreshKeyframeLanes();
}

TrackMotionState &TrackMotionDialog::motion()
{
    return m_track->motion;
}

TrackMotionKeyframe *TrackMotionDialog::selectedMotion()
{
    if (!m_track || m_motionIndex < 0 || m_motionIndex >= motion().motionKeyframes.size()) {
        return nullptr;
    }
    return &motion().motionKeyframes[m_motionIndex];
}

void TrackMotionDialog::refreshTitle()
{
    if (!m_track) {
        setWindowTitle(tr("Track Motion"));
        return;
    }
    setWindowTitle(tr("Track Motion - %1").arg(m_track->name));
}

void TrackMotionDialog::setPlayheadSec(double sec)
{
    m_playheadSec = std::clamp(sec, 0.0, m_durationSec);
    if (m_tc) {
        m_tc->setText(formatTc(m_playheadSec));
    }
    refreshKeyframeLanes();
}

void TrackMotionDialog::selectMotionIndex(int index)
{
    if (!m_track || motion().motionKeyframes.isEmpty()) {
        return;
    }
    m_motionIndex = std::clamp(index, 0, int(motion().motionKeyframes.size()) - 1);
    m_playheadSec = motion().motionKeyframes[m_motionIndex].timeSec;
    syncUiFromSelected();
    refreshKeyframeLanes();
}

void TrackMotionDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *preset = new QWidget(this);
    preset->setObjectName(QStringLiteral("pcPreset"));
    preset->setFixedHeight(28);
    auto *presetLay = new QHBoxLayout(preset);
    presetLay->setContentsMargins(8, 0, 8, 0);
    presetLay->setSpacing(6);
    auto *pl = new QLabel(tr("Preset:"), preset);
    pl->setObjectName(QStringLiteral("pcPresetLabel"));
    m_preset = new QComboBox(preset);
    m_preset->setObjectName(QStringLiteral("pcPresetSelect"));
    m_preset->addItem(tr("(Untitled)"));
    m_preset->setFixedHeight(20);
    m_preset->setMinimumWidth(200);
    presetLay->addWidget(pl);
    presetLay->addWidget(m_preset, 1);
    auto *savePreset = makeIcoBtn(preset, QStringLiteral("💾"), tr("Save Preset"));
    auto *delPreset = makeIcoBtn(preset, QStringLiteral("✕"), tr("Delete Preset"));
    presetLay->addWidget(savePreset);
    presetLay->addWidget(delPreset);
    connect(savePreset, &QPushButton::clicked, this, &TrackMotionDialog::saveCurrentPreset);
    connect(delPreset, &QPushButton::clicked, this, &TrackMotionDialog::deleteCurrentPreset);
    connect(m_preset, &QComboBox::currentTextChanged, this,
            [this](const QString &name) { applyPreset(name); });
    reloadPresets();
    root->addWidget(preset);

    root->addWidget(buildToolbar());

    auto *main = new QWidget(this);
    main->setObjectName(QStringLiteral("pcMain"));
    auto *mainLay = new QHBoxLayout(main);
    mainLay->setContentsMargins(0, 0, 0, 0);
    mainLay->setSpacing(0);

    auto *tools = new QWidget(main);
    tools->setObjectName(QStringLiteral("pcTools"));
    tools->setFixedWidth(28);
    auto *toolsLay = new QVBoxLayout(tools);
    toolsLay->setContentsMargins(2, 4, 2, 4);
    toolsLay->setSpacing(2);
    toolsLay->addWidget(makeTool(tools, QStringLiteral("↖"), tr("Normal Edit"), true));
    toolsLay->addWidget(makeTool(tools, QStringLiteral("⌖"), tr("Enable Snapping")));
    toolsLay->addWidget(makeTool(tools, QStringLiteral("⬚"), tr("Lock Aspect Ratio")));
    toolsLay->addWidget(makeTool(tools, QStringLiteral("◎"), tr("Size About Center")));
    toolsLay->addWidget(makeTool(tools, QStringLiteral("X"), tr("Prevent Movement (X)")));
    toolsLay->addWidget(makeTool(tools, QStringLiteral("Y"), tr("Prevent Movement (Y)")));
    toolsLay->addWidget(makeTool(tools, QStringLiteral("↔"), tr("Prevent Scaling (X)")));
    toolsLay->addWidget(makeTool(tools, QStringLiteral("↕"), tr("Prevent Scaling (Y)")));
    toolsLay->addStretch(1);
    mainLay->addWidget(tools);

    mainLay->addWidget(buildPropsPanel(), 0);

    auto *canvasWrap = new QWidget(main);
    canvasWrap->setObjectName(QStringLiteral("pcCanvasWrap"));
    auto *cwLay = new QVBoxLayout(canvasWrap);
    cwLay->setContentsMargins(0, 0, 0, 0);
    m_canvas = new TrackMotionCanvas(canvasWrap);
    m_canvas->setOnEdited([this](const MotionView &v) {
        TrackMotionKeyframe *kf = selectedMotion();
        if (!kf) {
            return;
        }
        applyViewToKf(v, kf, m_frameH);
        motion().workspaceZoom = v.workspaceZoom;
        motion().workspaceX = v.workspaceX;
        motion().workspaceY = v.workspaceY;
        m_block = true;
        m_x->setValue(v.x);
        m_y->setValue(v.y);
        m_w->setValue(v.width);
        m_h->setValue(v.height);
        m_orient->setValue(v.orientationDeg);
        m_block = false;
    });
    cwLay->addWidget(m_canvas, 1);
    mainLay->addWidget(canvasWrap, 1);

    root->addWidget(main, 1);
    root->addWidget(buildKeyframePanel());
}

QWidget *TrackMotionDialog::buildToolbar()
{
    auto *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("tmToolbar"));
    bar->setFixedHeight(28);
    auto *lay = new QHBoxLayout(bar);
    lay->setContentsMargins(8, 2, 8, 2);
    lay->setSpacing(4);

    m_compMode = new QComboBox(bar);
    m_compMode->setObjectName(QStringLiteral("pcPresetSelect"));
    m_compMode->addItems({tr("3D Source Alpha"), tr("Custom…"), tr("Add"), tr("Subtract"),
                          tr("Multiply (Mask)"), tr("Source Alpha"), tr("Cut"), tr("Screen"),
                          tr("Overlay"), tr("Hard Light"), tr("Dodge"), tr("Burn"), tr("Darken"),
                          tr("Lighten"), tr("Difference"), tr("Difference Squared")});
    m_compMode->setCurrentText(tr("Source Alpha"));
    m_compMode->setFixedHeight(20);
    m_compMode->setMinimumWidth(130);
    connect(m_compMode, &QComboBox::currentTextChanged, this, [this](const QString &t) {
        if (m_block || !m_track) {
            return;
        }
        motion().compositingMode = t;
    });
    lay->addWidget(m_compMode);
    lay->addWidget(makeTool(bar, QStringLiteral("⌖"), tr("Enable Snapping")));
    lay->addWidget(makeTool(bar, QStringLiteral("⬚"), tr("Lock Aspect Ratio"), true));
    lay->addWidget(makeTool(bar, QStringLiteral("◎"), tr("Size About Center")));
    lay->addWidget(makeTool(bar, QStringLiteral("X"), tr("Prevent Movement (X)")));
    lay->addWidget(makeTool(bar, QStringLiteral("Y"), tr("Prevent Movement (Y)")));
    lay->addStretch(1);
    return bar;
}

QWidget *TrackMotionDialog::buildPropsPanel()
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

    auto addGroup = [&](const QString &title) {
        auto *sum = new QLabel(QStringLiteral("−  %1").arg(title), props);
        sum->setObjectName(QStringLiteral("pcGroupSummary"));
        sum->setFixedHeight(22);
        lay->addWidget(sum);
    };

    m_shadowGroup = new QWidget(props);
    auto *shLay = new QVBoxLayout(m_shadowGroup);
    shLay->setContentsMargins(0, 0, 0, 0);
    shLay->setSpacing(2);
    auto *shSum = new QLabel(QStringLiteral("−  %1").arg(tr("2D Shadow")), m_shadowGroup);
    shSum->setObjectName(QStringLiteral("pcGroupSummary"));
    shSum->setFixedHeight(22);
    shLay->addWidget(shSum);
    m_shadowBlur = makeSpin(m_shadowGroup, 0, 100, 2, 0.5);
    m_shadowInt = makeSpin(m_shadowGroup, 0, 100, 2, 1.0);
    shLay->addWidget(makePropRow(m_shadowGroup, tr("Blur (%)"), m_shadowBlur));
    shLay->addWidget(makePropRow(m_shadowGroup, tr("Intensity (%)"), m_shadowInt));
    auto *colorRow = new QLabel(tr("Color"), m_shadowGroup);
    colorRow->setObjectName(QStringLiteral("pcPropName"));
    auto *colorBox = new QFrame(m_shadowGroup);
    colorBox->setObjectName(QStringLiteral("tmColorSwatch"));
    colorBox->setFixedSize(40, 16);
    colorBox->setStyleSheet(QStringLiteral("background:#000000;border:1px solid #666;"));
    auto *cHost = new QWidget(m_shadowGroup);
    auto *cLay = new QHBoxLayout(cHost);
    cLay->setContentsMargins(4, 0, 4, 0);
    cLay->addWidget(colorRow, 1);
    cLay->addWidget(colorBox, 0);
    shLay->addWidget(cHost);
    m_shadowGroup->setVisible(false);
    lay->addWidget(m_shadowGroup);

    addGroup(tr("Position"));
    m_x = makeSpin(props, -100000, 100000, 2, 1);
    m_y = makeSpin(props, -100000, 100000, 2, 1);
    m_w = makeSpin(props, 1, 100000, 2, 1);
    m_h = makeSpin(props, 1, 100000, 2, 1);
    lay->addWidget(makePropRow(props, tr("X"), m_x));
    lay->addWidget(makePropRow(props, tr("Y"), m_y));
    lay->addWidget(makePropRow(props, tr("Width"), m_w));
    lay->addWidget(makePropRow(props, tr("Height"), m_h));

    addGroup(tr("Orientation"));
    m_orient = makeSpin(props, -3600, 3600, 1, 0.1);
    lay->addWidget(makePropRow(props, tr("Angle"), m_orient));

    addGroup(tr("Rotation"));
    m_rot = makeSpin(props, -3600, 3600, 1, 0.1);
    m_rotX = makeSpin(props, -100000, 100000, 2, 1);
    m_rotY = makeSpin(props, -100000, 100000, 2, 1);
    lay->addWidget(makePropRow(props, tr("Angle"), m_rot));
    lay->addWidget(makePropRow(props, tr("X Offset"), m_rotX));
    lay->addWidget(makePropRow(props, tr("Y Offset"), m_rotY));

    addGroup(tr("Keyframe"));
    m_kfSmooth = makeSpin(props, 0, 1, 2, 0.01);
    m_kfType = new QComboBox(props);
    m_kfType->setObjectName(QStringLiteral("pcPresetSelect"));
    m_kfType->addItems({tr("Linear"), tr("Fast"), tr("Slow"), tr("Smooth"), tr("Sharp")});
    m_kfType->setFixedHeight(18);
    m_kfType->setFixedWidth(88);
    lay->addWidget(makePropRow(props, tr("Smoothness"), m_kfSmooth));
    lay->addWidget(makePropRow(props, tr("Type"), m_kfType));

    addGroup(tr("Workspace"));
    m_wsZoom = makeSpin(props, 5, 400, 2, 1);
    m_wsX = makeSpin(props, -10000, 10000, 2, 1);
    m_wsY = makeSpin(props, -10000, 10000, 2, 1);
    lay->addWidget(makePropRow(props, tr("Zoom (%)"), m_wsZoom));
    lay->addWidget(makePropRow(props, tr("X Offset"), m_wsX));
    lay->addWidget(makePropRow(props, tr("Y Offset"), m_wsY));

    addGroup(tr("Snap Settings"));
    m_snapGrid = makeSpin(props, 1, 200, 0, 1);
    m_snapRot = makeSpin(props, 1, 90, 0, 1);
    lay->addWidget(makePropRow(props, tr("Grid Spacing"), m_snapGrid));
    lay->addWidget(makePropRow(props, tr("Rotation"), m_snapRot));

    lay->addStretch(1);
    scroll->setWidget(props);

    auto wire = [this](QDoubleSpinBox *s) {
        connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) {
            if (!m_block) {
                syncSelectedFromUi();
            }
        });
    };
    for (QDoubleSpinBox *s : {m_x, m_y, m_w, m_h, m_orient, m_rot, m_rotX, m_rotY, m_kfSmooth,
                              m_wsZoom, m_wsX, m_wsY, m_snapGrid, m_snapRot, m_shadowBlur,
                              m_shadowInt}) {
        wire(s);
    }
    connect(m_kfType, &QComboBox::currentTextChanged, this, [this](const QString &) {
        if (!m_block) {
            syncSelectedFromUi();
        }
    });

    return scroll;
}

QWidget *TrackMotionDialog::buildKeyframePanel()
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
    hLay->setSpacing(0);
    auto *corner = new QWidget(headers);
    corner->setObjectName(QStringLiteral("pcKfCorner"));
    corner->setFixedWidth(120);
    hLay->addWidget(corner);
    auto *ruler = new QWidget(headers);
    ruler->setObjectName(QStringLiteral("pcKfRuler"));
    ruler->setFixedHeight(20);
    hLay->addWidget(ruler, 1);
    root->addWidget(headers);

    auto makeRow = [&](const QString &label, QCheckBox **outCb, bool active, KeyframeLane **outLane) {
        auto *row = new QWidget(kf);
        row->setObjectName(active ? QStringLiteral("pcKfRowActive") : QStringLiteral("pcKfRow"));
        row->setFixedHeight(28);
        auto *lay = new QHBoxLayout(row);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(0);
        auto *labHost = new QWidget(row);
        labHost->setObjectName(active ? QStringLiteral("pcKfLabelActive")
                                       : QStringLiteral("pcKfLabel"));
        labHost->setFixedWidth(120);
        auto *labLay = new QHBoxLayout(labHost);
        labLay->setContentsMargins(8, 0, 8, 0);
        auto *cb = new QCheckBox(label, labHost);
        *outCb = cb;
        labLay->addWidget(cb);
        lay->addWidget(labHost);
        auto *lane = new KeyframeLane(row);
        *outLane = lane;
        lay->addWidget(lane, 1);
        return row;
    };

    QCheckBox *posCb = nullptr;
    KeyframeLane *posLane = nullptr;
    root->addWidget(makeRow(tr("Position"), &posCb, true, &posLane));
    m_motionLane = posLane;
    if (posCb) {
        posCb->setChecked(true);
        posCb->setEnabled(false);
    }
    if (posLane) {
        posLane->setOnSelect([this](int i) { selectMotionIndex(i); });
    }

    KeyframeLane *shadowLane = nullptr;
    KeyframeLane *glowLane = nullptr;
    root->addWidget(makeRow(tr("2D Shadow"), &m_shadowEnable, false, &shadowLane));
    root->addWidget(makeRow(tr("2D Glow"), &m_glowEnable, false, &glowLane));
    m_shadowLane = shadowLane;
    m_glowLane = glowLane;

    connect(m_shadowEnable, &QCheckBox::toggled, this, [this](bool on) {
        if (m_shadowGroup) {
            m_shadowGroup->setVisible(on);
        }
        if (!m_block && m_track) {
            motion().shadowEnabled = on;
            if (on && motion().shadowKeyframes.isEmpty()) {
                TrackFXKeyframe sh;
                sh.timeSec = 0.0;
                motion().shadowKeyframes.push_back(sh);
            }
            syncUiFromSelected();
            refreshKeyframeLanes();
        }
    });
    connect(m_glowEnable, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_block && m_track) {
            motion().glowEnabled = on;
            if (on && motion().glowKeyframes.isEmpty()) {
                TrackFXKeyframe g;
                g.timeSec = 0.0;
                motion().glowKeyframes.push_back(g);
            }
            refreshKeyframeLanes();
        }
    });

    auto *tb = new QWidget(kf);
    tb->setObjectName(QStringLiteral("pcKfToolbar"));
    auto *tbLay = new QHBoxLayout(tb);
    tbLay->setContentsMargins(8, 2, 8, 2);
    tbLay->setSpacing(4);

    auto *btnSync = makeIcoBtn(tb, QStringLiteral("⏱"), tr("Sync Cursor"));
    auto *btnFirst = makeIcoBtn(tb, QStringLiteral("⏮"), tr("First Keyframe"));
    auto *btnPrev = makeIcoBtn(tb, QStringLiteral("◀"), tr("Previous Keyframe"));
    auto *btnNext = makeIcoBtn(tb, QStringLiteral("▶"), tr("Next Keyframe"));
    auto *btnLast = makeIcoBtn(tb, QStringLiteral("⏭"), tr("Last Keyframe"));
    auto *btnAdd = makeIcoBtn(tb, QStringLiteral("+"), tr("Create Keyframe (Insert)"));
    auto *btnDel = makeIcoBtn(tb, QStringLiteral("−"), tr("Delete Keyframe"));
    tbLay->addWidget(btnSync);
    tbLay->addWidget(btnFirst);
    tbLay->addWidget(btnPrev);
    tbLay->addWidget(btnNext);
    tbLay->addWidget(btnLast);
    tbLay->addWidget(btnAdd);
    tbLay->addWidget(btnDel);
    tbLay->addStretch(1);
    m_tc = new QLabel(QStringLiteral("00:00:00,00"), tb);
    m_tc->setObjectName(QStringLiteral("pcKfTc"));
    tbLay->addWidget(m_tc);
    root->addWidget(tb);

    connect(btnSync, &QPushButton::clicked, this, [this]() {
        if (!m_track) {
            return;
        }
        selectMotionIndex(motion().motionIndexAt(m_playheadSec));
    });
    connect(btnFirst, &QPushButton::clicked, this, [this]() { selectMotionIndex(0); });
    connect(btnPrev, &QPushButton::clicked, this, [this]() { selectMotionIndex(m_motionIndex - 1); });
    connect(btnNext, &QPushButton::clicked, this, [this]() { selectMotionIndex(m_motionIndex + 1); });
    connect(btnLast, &QPushButton::clicked, this, [this]() {
        if (m_track) {
            selectMotionIndex(motion().motionKeyframes.size() - 1);
        }
    });
    connect(btnAdd, &QPushButton::clicked, this, [this]() {
        if (!m_track) {
            return;
        }
        const double aspect = double(m_frameW) / double(m_frameH);
        TrackMotionKeyframe k = TrackMotionState::identityKeyframe(aspect);
        if (TrackMotionKeyframe *cur = selectedMotion()) {
            k = *cur;
        }
        k.timeSec = m_playheadSec;
        auto &kfs = motion().motionKeyframes;
        int insertAt = 0;
        while (insertAt < kfs.size() && kfs[insertAt].timeSec <= k.timeSec + 1e-9) {
            ++insertAt;
        }
        // Replace existing at same time
        if (insertAt > 0 && std::abs(kfs[insertAt - 1].timeSec - k.timeSec) < 1e-4) {
            kfs[insertAt - 1] = k;
            selectMotionIndex(insertAt - 1);
            return;
        }
        kfs.insert(insertAt, k);
        selectMotionIndex(insertAt);
    });
    connect(btnDel, &QPushButton::clicked, this, [this]() {
        if (!m_track || motion().motionKeyframes.size() <= 1) {
            return;
        }
        motion().motionKeyframes.removeAt(m_motionIndex);
        selectMotionIndex(std::min(m_motionIndex, int(motion().motionKeyframes.size()) - 1));
    });

    return kf;
}

void TrackMotionDialog::refreshKeyframeLanes()
{
    if (!m_track) {
        return;
    }
    QVector<double> motionTimes;
    for (const TrackMotionKeyframe &k : motion().motionKeyframes) {
        motionTimes.push_back(k.timeSec);
    }
    if (auto *lane = static_cast<KeyframeLane *>(m_motionLane)) {
        lane->setTimes(motionTimes, m_durationSec, m_motionIndex, m_playheadSec);
    }
    QVector<double> shadowTimes;
    for (const TrackFXKeyframe &k : motion().shadowKeyframes) {
        shadowTimes.push_back(k.timeSec);
    }
    if (auto *lane = static_cast<KeyframeLane *>(m_shadowLane)) {
        lane->setTimes(shadowTimes, m_durationSec, -1, m_playheadSec);
    }
    QVector<double> glowTimes;
    for (const TrackFXKeyframe &k : motion().glowKeyframes) {
        glowTimes.push_back(k.timeSec);
    }
    if (auto *lane = static_cast<KeyframeLane *>(m_glowLane)) {
        lane->setTimes(glowTimes, m_durationSec, -1, m_playheadSec);
    }
    if (m_tc) {
        m_tc->setText(formatTc(m_playheadSec));
    }
}

void TrackMotionDialog::syncUiFromSelected()
{
    if (!m_track) {
        return;
    }
    TrackMotionKeyframe *kf = selectedMotion();
    if (!kf) {
        return;
    }
    const MotionView v = viewFromKf(*kf, motion(), m_frameW, m_frameH);
    m_block = true;
    m_x->setValue(v.x);
    m_y->setValue(v.y);
    m_w->setValue(v.width);
    m_h->setValue(v.height);
    m_orient->setValue(v.orientationDeg);
    m_rot->setValue(kf->rotationZ * kRadToDeg);
    m_rotX->setValue(0.0);
    m_rotY->setValue(0.0);
    m_kfSmooth->setValue(kf->smoothness);
    {
        const int idx = m_kfType->findText(kfTypeName(kf->type));
        if (idx >= 0) {
            m_kfType->setCurrentIndex(idx);
        }
    }
    m_wsZoom->setValue(motion().workspaceZoom);
    m_wsX->setValue(motion().workspaceX);
    m_wsY->setValue(motion().workspaceY);
    m_snapGrid->setValue(motion().snapGrid);
    m_snapRot->setValue(motion().snapRotation);
    m_shadowBlur->setValue(v.shadowBlur);
    m_shadowInt->setValue(v.shadowIntensity);
    m_shadowEnable->setChecked(motion().shadowEnabled);
    m_glowEnable->setChecked(motion().glowEnabled);
    m_shadowGroup->setVisible(motion().shadowEnabled);
    {
        const int idx = m_compMode->findText(motion().compositingMode);
        if (idx >= 0) {
            m_compMode->setCurrentIndex(idx);
        } else {
            m_compMode->setCurrentText(tr("Source Alpha"));
        }
    }
    m_playheadSec = kf->timeSec;
    m_block = false;
    if (m_canvas) {
        m_canvas->setView(v, m_frameW, m_frameH);
    }
    if (m_tc) {
        m_tc->setText(formatTc(m_playheadSec));
    }
}

namespace {

/** Where named Track Motion presets live between sessions. */
const char kPresetGroup[] = "trackMotion/presets";

/** One keyframe's settings as a flat map — what a preset stores. */
QVariantMap motionToMap(const TrackMotionKeyframe &kf, const TrackMotionState &st)
{
    QVariantMap m;
    m[QStringLiteral("positionX")] = kf.positionX;
    m[QStringLiteral("positionY")] = kf.positionY;
    m[QStringLiteral("width")] = kf.width;
    m[QStringLiteral("height")] = kf.height;
    m[QStringLiteral("rotationZ")] = kf.rotationZ;
    m[QStringLiteral("orientationZ")] = kf.orientationZ;
    m[QStringLiteral("smoothness")] = kf.smoothness;
    m[QStringLiteral("type")] = int(kf.type);
    m[QStringLiteral("shadowEnabled")] = st.shadowEnabled;
    m[QStringLiteral("glowEnabled")] = st.glowEnabled;
    return m;
}

void mapToMotion(const QVariantMap &m, TrackMotionKeyframe *kf, TrackMotionState *st)
{
    if (!kf || !st) {
        return;
    }
    const auto num = [&m](const char *key, double fallback) {
        const QVariant v = m.value(QString::fromLatin1(key));
        return v.isValid() ? v.toDouble() : fallback;
    };
    kf->positionX = num("positionX", kf->positionX);
    kf->positionY = num("positionY", kf->positionY);
    kf->width = num("width", kf->width);
    kf->height = num("height", kf->height);
    kf->rotationZ = num("rotationZ", kf->rotationZ);
    kf->orientationZ = num("orientationZ", kf->orientationZ);
    kf->smoothness = num("smoothness", kf->smoothness);
    if (m.contains(QStringLiteral("type"))) {
        kf->type = VideoKeyframeType(m.value(QStringLiteral("type")).toInt());
    }
    if (m.contains(QStringLiteral("shadowEnabled"))) {
        st->shadowEnabled = m.value(QStringLiteral("shadowEnabled")).toBool();
    }
    if (m.contains(QStringLiteral("glowEnabled"))) {
        st->glowEnabled = m.value(QStringLiteral("glowEnabled")).toBool();
    }
}

} // namespace

void TrackMotionDialog::reloadPresets(const QString &select)
{
    if (!m_preset) {
        return;
    }
    const QSignalBlocker block(m_preset);
    m_preset->clear();
    m_preset->addItem(tr("(Untitled)"));
    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    s.beginGroup(QString::fromLatin1(kPresetGroup));
    QStringList names = s.childKeys();
    s.endGroup();
    names.sort(Qt::CaseInsensitive);
    m_preset->addItems(names);
    const int at = select.isEmpty() ? 0 : m_preset->findText(select);
    m_preset->setCurrentIndex(at >= 0 ? at : 0);
}

void TrackMotionDialog::applyPreset(const QString &name)
{
    TrackMotionKeyframe *kf = selectedMotion();
    if (!kf || name.isEmpty() || name == tr("(Untitled)")) {
        return;
    }
    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    s.beginGroup(QString::fromLatin1(kPresetGroup));
    const QVariantMap m = s.value(name).toMap();
    s.endGroup();
    if (m.isEmpty()) {
        return;
    }
    mapToMotion(m, kf, &motion());
    syncUiFromSelected();
    emit motionChanged();
}

void TrackMotionDialog::saveCurrentPreset()
{
    const TrackMotionKeyframe *kf = selectedMotion();
    if (!kf) {
        return;
    }
    const QString suggested =
        m_preset && m_preset->currentIndex() > 0 ? m_preset->currentText() : QString();
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, tr("Save Preset"), tr("Preset name:"), QLineEdit::Normal,
                              suggested, &ok)
            .trimmed();
    if (!ok || name.isEmpty() || name == tr("(Untitled)")) {
        return;
    }
    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    s.beginGroup(QString::fromLatin1(kPresetGroup));
    s.setValue(name, motionToMap(*kf, motion()));
    s.endGroup();
    reloadPresets(name);
}

void TrackMotionDialog::deleteCurrentPreset()
{
    if (!m_preset || m_preset->currentIndex() <= 0) {
        return; // "(Untitled)" is the live state, not a stored preset
    }
    const QString name = m_preset->currentText();
    if (QMessageBox::question(this, tr("Delete Preset"),
                              tr("Delete the preset \"%1\"?").arg(name))
        != QMessageBox::Yes) {
        return;
    }
    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    s.beginGroup(QString::fromLatin1(kPresetGroup));
    s.remove(name);
    s.endGroup();
    reloadPresets();
}

void TrackMotionDialog::syncSelectedFromUi()
{
    if (!m_track || m_block) {
        return;
    }
    TrackMotionKeyframe *kf = selectedMotion();
    if (!kf) {
        return;
    }
    MotionView v;
    v.x = m_x->value();
    v.y = m_y->value();
    v.width = m_w->value();
    v.height = m_h->value();
    v.orientationDeg = m_orient->value();
    v.workspaceZoom = m_wsZoom->value();
    v.workspaceX = m_wsX->value();
    v.workspaceY = m_wsY->value();
    v.shadowEnabled = m_shadowEnable->isChecked();
    v.shadowBlur = m_shadowBlur->value();
    v.shadowIntensity = m_shadowInt->value();
    applyViewToKf(v, kf, m_frameH);
    kf->rotationZ = m_rot->value() * kDegToRad;
    kf->smoothness = m_kfSmooth->value();
    kf->type = kfTypeFromName(m_kfType->currentText());
    motion().workspaceZoom = v.workspaceZoom;
    motion().workspaceX = v.workspaceX;
    motion().workspaceY = v.workspaceY;
    motion().snapGrid = int(m_snapGrid->value());
    motion().snapRotation = int(m_snapRot->value());
    motion().shadowEnabled = m_shadowEnable->isChecked();
    motion().glowEnabled = m_glowEnable->isChecked();
    if (motion().shadowEnabled) {
        if (motion().shadowKeyframes.isEmpty()) {
            motion().shadowKeyframes.push_back(TrackFXKeyframe{});
        }
        TrackFXKeyframe &sh = motion().shadowKeyframes[0];
        sh.blur = m_shadowBlur->value() / 100.0;
        sh.intensity = m_shadowInt->value();
    }
    if (m_canvas) {
        m_canvas->setView(viewFromKf(*kf, motion(), m_frameW, m_frameH), m_frameW, m_frameH);
    }
    emit motionChanged();
}

} // namespace openvegas
