#include "ui/TrimmerWindow.h"
#include "ui/IconFactory.h"

#include <QAction>
#include <QActionGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QCursor>
#include <algorithm>
#include <cmath>
#include <functional>

namespace openvegas {

namespace {

constexpr double kMinSel = 0.05;

QString guessPath(const QString &, EventMediaKind)
{
    return QStringLiteral("D:\\Devs\\C++\\OpenVegas\\SAMPLES\\assets\\");
}

EventMediaKind guessKind(const QString &name)
{
    const QString n = name.toLower();
    if (n.contains(QLatin1String("audio")) || n.endsWith(QLatin1String(".wav"))
        || n.endsWith(QLatin1String(".mp3")) || n.endsWith(QLatin1String(".flac"))
        || n.contains(QLatin1String("midi"))) {
        return EventMediaKind::Audio;
    }
    if (n.contains(QLatin1String("title")) || n.contains(QLatin1String("text"))) {
        return EventMediaKind::Title;
    }
    if (n.endsWith(QLatin1String(".png")) || n.endsWith(QLatin1String(".jpg"))
        || n.contains(QLatin1String("still"))) {
        return EventMediaKind::Still;
    }
    return EventMediaKind::Video;
}

} // namespace

/** Preview / waveform canvas with playhead + In/Out markers. */
class TrimmerCanvas : public QWidget {
public:
    explicit TrimmerCanvas(TrimmerWindow *owner)
        : QWidget(owner)
        , m_owner(owner)
    {
        setMinimumHeight(160);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMouseTracking(true);
        setCursor(Qt::ArrowCursor);
    }

    void setState(EventMediaKind kind, double duration, double current, double inPt, double outPt)
    {
        m_kind = kind;
        m_duration = std::max(0.1, duration);
        m_current = std::clamp(current, 0.0, m_duration);
        m_in = std::clamp(inPt, 0.0, m_duration);
        m_out = std::clamp(outPt, m_in + kMinSel, m_duration);
        update();
    }

    std::function<void(double)> onSeek;
    std::function<void(double, double)> onSelectionChanged;
    QString mediaLabel;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0x1a, 0x1a, 0x1a));

        const QRect body = rect().adjusted(0, isAudio() ? 18 : 0, 0, 0);
        if (isAudio()) {
            paintRuler(p);
            paintWaveform(p, body);
            paintSelection(p, body);
        } else {
            paintVideoFrame(p, body);
            paintSelection(p, body.adjusted(0, body.height() - 10, 0, 0));
        }
        paintPlayhead(p, body);
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton) {
            return;
        }
        const QRect body = contentRect();
        const double t = xToTime(e->pos().x(), body);
        const int inX = timeToX(m_in, body);
        const int outX = timeToX(m_out, body);
        if (std::abs(e->pos().x() - inX) <= 8) {
            m_drag = Drag::In;
        } else if (std::abs(e->pos().x() - outX) <= 8) {
            m_drag = Drag::Out;
        } else {
            m_drag = Drag::Playhead;
            if (onSeek) {
                onSeek(t);
            }
        }
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        const QRect body = contentRect();
        const double t = xToTime(e->pos().x(), body);
        if (m_drag == Drag::None) {
            const int inX = timeToX(m_in, body);
            const int outX = timeToX(m_out, body);
            if (std::abs(e->pos().x() - inX) <= 8 || std::abs(e->pos().x() - outX) <= 8) {
                setCursor(Qt::SizeHorCursor);
            } else {
                setCursor(Qt::ArrowCursor);
            }
            return;
        }
        if (m_drag == Drag::Playhead && onSeek) {
            onSeek(t);
        } else if (m_drag == Drag::In) {
            m_in = std::clamp(t, 0.0, m_out - kMinSel);
            if (onSelectionChanged) {
                onSelectionChanged(m_in, m_out);
            }
            update();
        } else if (m_drag == Drag::Out) {
            m_out = std::clamp(t, m_in + kMinSel, m_duration);
            if (onSelectionChanged) {
                onSelectionChanged(m_in, m_out);
            }
            update();
        }
    }

    void mouseReleaseEvent(QMouseEvent *) override { m_drag = Drag::None; }

private:
    enum class Drag { None, Playhead, In, Out };

    bool isAudio() const { return isAudioFamily(m_kind); }

    QRect contentRect() const
    {
        return rect().adjusted(0, isAudio() ? 18 : 0, 0, 0);
    }

    double xToTime(int x, const QRect &body) const
    {
        if (body.width() <= 1) {
            return 0.0;
        }
        return std::clamp(double(x - body.left()) / body.width() * m_duration, 0.0, m_duration);
    }

    int timeToX(double sec, const QRect &body) const
    {
        return body.left() + static_cast<int>(std::lround((sec / m_duration) * body.width()));
    }

    void paintRuler(QPainter &p)
    {
        p.fillRect(0, 0, width(), 18, QColor(0x2a, 0x2a, 0x2a));
        p.setPen(QColor(0xa8, 0xa8, 0xa8));
        QFont f = font();
        f.setPointSize(8);
        p.setFont(f);
        const int marks = std::max(2, static_cast<int>(m_duration) + 1);
        for (int i = 0; i <= marks; ++i) {
            const double sec = (double(i) / marks) * m_duration;
            const int x = timeToX(sec, QRect(0, 0, width(), height()));
            p.drawLine(x, 14, x, 18);
            // Vegas-like measure.beat mock
            const int m = i + 1;
            p.drawText(x + 3, 12, QStringLiteral("%1.1").arg(m));
        }
    }

    void paintVideoFrame(QPainter &p, const QRect &r)
    {
        // Procedural “frame” (no real decode yet)
        QLinearGradient g(r.topLeft(), r.bottomRight());
        const int hue = static_cast<int>(m_current * 37) % 360;
        g.setColorAt(0.0, QColor::fromHsv(hue, 40, 55));
        g.setColorAt(1.0, QColor::fromHsv((hue + 40) % 360, 50, 35));
        p.fillRect(r, g);
        p.setPen(QColor(255, 255, 255, 40));
        p.drawRect(r.adjusted(0, 0, -1, -1));
        p.setPen(QColor(255, 255, 255, 180));
        QFont f = font();
        f.setPointSize(11);
        f.setBold(true);
        p.setFont(f);
        p.drawText(r, Qt::AlignCenter,
                   mediaLabel.isEmpty() ? QObject::tr("Video preview") : mediaLabel);
        // Scrub strip
        const QRect strip(r.left(), r.bottom() - 8, r.width(), 8);
        p.fillRect(strip, QColor(0x33, 0x33, 0x33));
    }

    void paintWaveform(QPainter &p, const QRect &r)
    {
        p.fillRect(r, QColor(0x2c, 0x2c, 0x2c));
        const int mid = r.center().y();
        const int chH = r.height() / 2;
        auto drawCh = [&](int top, int h, int seed) {
            QPainterPath path;
            const int cy = top + h / 2;
            path.moveTo(r.left(), cy);
            for (int x = r.left(); x <= r.right(); x += 2) {
                const double t = (x + seed) * 0.09;
                const double env = 0.3 + 0.7 * std::abs(std::sin((x + seed * 3) * 0.012));
                const double amp =
                    env * (h * 0.42)
                    * (0.55 * std::sin(t) + 0.25 * std::sin(t * 2.1) + 0.2 * std::sin(t * 4.7));
                path.lineTo(x, cy - amp);
            }
            for (int x = r.right(); x >= r.left(); x -= 2) {
                const double t = (x + seed) * 0.09;
                const double env = 0.3 + 0.7 * std::abs(std::sin((x + seed * 3) * 0.012));
                const double amp =
                    env * (h * 0.42)
                    * (0.55 * std::sin(t) + 0.25 * std::sin(t * 2.1) + 0.2 * std::sin(t * 4.7));
                path.lineTo(x, cy + amp);
            }
            path.closeSubpath();
            p.setBrush(QColor(0xb8, 0x9a, 0xc8, 200));
            p.setPen(QPen(QColor(0x8a, 0x6a, 0x9a), 1));
            p.drawPath(path);
        };
        drawCh(r.top(), chH, 11);
        drawCh(mid, r.height() - chH, 29);
        p.setPen(QColor(0, 0, 0, 60));
        p.drawLine(r.left(), mid, r.right(), mid);
    }

    void paintSelection(QPainter &p, const QRect &body)
    {
        const int x0 = timeToX(m_in, body);
        const int x1 = timeToX(m_out, body);
        p.fillRect(QRect(x0, body.top(), std::max(1, x1 - x0), body.height()),
                   QColor(255, 255, 255, isAudio() ? 35 : 50));
        // Yellow In / Out triangles
        auto tri = [&](int x, bool inMark) {
            QPainterPath path;
            if (inMark) {
                path.moveTo(x, body.top());
                path.lineTo(x + 10, body.top());
                path.lineTo(x, body.top() + 10);
            } else {
                path.moveTo(x, body.top());
                path.lineTo(x - 10, body.top());
                path.lineTo(x, body.top() + 10);
            }
            path.closeSubpath();
            p.setBrush(QColor(0xf0, 0xc0, 0x20));
            p.setPen(Qt::NoPen);
            p.drawPath(path);
            p.setPen(QPen(QColor(0xf0, 0xc0, 0x20), 1));
            p.drawLine(x, body.top(), x, body.bottom());
        };
        tri(x0, true);
        tri(x1, false);
    }

    void paintPlayhead(QPainter &p, const QRect &body)
    {
        const int x = timeToX(m_current, body);
        p.setPen(QPen(Qt::white, 1));
        p.drawLine(x, body.top(), x, body.bottom());
        p.fillRect(QRect(x - 4, body.top(), 8, 8), QColor(0xe8, 0xe8, 0xe8));
    }

    TrimmerWindow *m_owner = nullptr;
    EventMediaKind m_kind = EventMediaKind::Video;
    double m_duration = 10.0;
    double m_current = 0.0;
    double m_in = 0.0;
    double m_out = 10.0;
    Drag m_drag = Drag::None;
};

TrimmerWindow::TrimmerWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::Window);
    setAttribute(Qt::WA_DeleteOnClose, false);
    resize(720, 420);
    buildUi();
    m_timer = new QTimer(this);
    m_timer->setInterval(33);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        if (!m_playing) {
            return;
        }
        m_current += 0.033;
        if (m_current >= m_outPoint) {
            if (m_loop) {
                m_current = m_inPoint;
            } else {
                m_current = m_outPoint;
                stopPlayback();
            }
        }
        updateChrome();
    });
    setMedia(QStringLiteral("(none)"), EventMediaKind::Video, 10.0);
}

TrimmerWindow::~TrimmerWindow() = default;

void TrimmerWindow::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Header
    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("trimmerHeader"));
    header->setFixedHeight(28);
    header->setStyleSheet(
        QStringLiteral("#trimmerHeader { background:#2a2a2a; border-bottom:1px solid #111; }"));
    auto *hLay = new QHBoxLayout(header);
    hLay->setContentsMargins(8, 2, 6, 2);
    hLay->setSpacing(6);
    m_mediaTitle = new QLabel(header);
    m_mediaTitle->setStyleSheet(QStringLiteral("color:#e0e0e0; font-size:11px;"));
    hLay->addWidget(m_mediaTitle, 1);
    auto addHdrBtn = [&](const QString &svg, const QString &tip) {
        auto *b = IconFactory::toolButton(header, tip, svg);
        b->setFixedSize(22, 22);
        hLay->addWidget(b);
        return b;
    };
    addHdrBtn(IconFactory::svgFilter(), tr("Sort"));
    addHdrBtn(IconFactory::svgViews(), tr("Views"));
    connect(addHdrBtn(IconFactory::svgRemove(), tr("Clear")), &QToolButton::clicked, this, [this]() {
        setMedia(QStringLiteral("(none)"), EventMediaKind::Video, 10.0);
    });
    root->addWidget(header);

    m_canvas = new TrimmerCanvas(this);
    m_canvas->onSeek = [this](double sec) { seekTo(sec); };
    m_canvas->onSelectionChanged = [this](double inPt, double outPt) {
        m_inPoint = inPt;
        m_outPoint = outPt;
        updateStatusLabels();
    };
    root->addWidget(m_canvas, 1);

    // Transport
    m_transportHost = new QWidget(this);
    m_transportHost->setFixedHeight(36);
    m_transportHost->setStyleSheet(QStringLiteral("background:#222;"));
    m_transportLay = new QHBoxLayout(m_transportHost);
    m_transportLay->setContentsMargins(8, 4, 8, 4);
    m_transportLay->setSpacing(4);
    m_transportLay->addStretch(1);
    rebuildTransport();
    m_transportLay->addStretch(1);
    root->addWidget(m_transportHost);

    // Status
    auto *status = new QWidget(this);
    status->setFixedHeight(26);
    status->setStyleSheet(QStringLiteral("background:#1e1e1e; border-top:1px solid #111;"));
    auto *sLay = new QHBoxLayout(status);
    sLay->setContentsMargins(8, 2, 8, 2);
    sLay->setSpacing(10);
    m_posLabel = new QLabel(status);
    m_inLabel = new QLabel(status);
    m_outLabel = new QLabel(status);
    m_lenLabel = new QLabel(status);
    for (QLabel *l : {m_posLabel, m_inLabel, m_outLabel, m_lenLabel}) {
        l->setStyleSheet(QStringLiteral("color:#c8c8c8; font-size:11px; font-family:Consolas,monospace;"));
        sLay->addWidget(l);
    }
    sLay->addStretch(1);
    root->addWidget(status);
}

QToolButton *TrimmerWindow::makeTransportBtn(const QString &tip, const QString &svg)
{
    auto *b = IconFactory::toolButton(m_transportHost, tip, svg);
    b->setFixedSize(28, 26);
    b->setAutoRaise(true);
    return b;
}

void TrimmerWindow::rebuildTransport()
{
    // Clear previous buttons except stretches — rebuild middle cluster
    while (QLayoutItem *it = m_transportLay->takeAt(0)) {
        if (QWidget *w = it->widget()) {
            w->deleteLater();
        }
        delete it;
    }
    m_transportLay->addStretch(1);

    m_loopBtn = makeTransportBtn(tr("Loop"), IconFactory::svgLoop());
    m_loopBtn->setCheckable(true);
    m_loopBtn->setChecked(m_loop);
    connect(m_loopBtn, &QToolButton::toggled, this, [this](bool on) { m_loop = on; });
    m_transportLay->addWidget(m_loopBtn);

    auto *play = makeTransportBtn(tr("Play"), IconFactory::svgPlay());
    connect(play, &QToolButton::clicked, this, [this]() { togglePlay(true); });
    m_transportLay->addWidget(play);

    auto *pause = makeTransportBtn(tr("Pause"), IconFactory::svgPause());
    connect(pause, &QToolButton::clicked, this, [this]() { togglePlay(false); });
    m_transportLay->addWidget(pause);

    auto *stop = makeTransportBtn(tr("Stop"), IconFactory::svgStop());
    connect(stop, &QToolButton::clicked, this, [this]() {
        stopPlayback();
        seekTo(m_inPoint);
    });
    m_transportLay->addWidget(stop);

    if (isAudioFamily(m_kind)) {
        auto *toEnd = makeTransportBtn(tr("Go to End"), IconFactory::svgGoEnd());
        connect(toEnd, &QToolButton::clicked, this, [this]() { seekTo(m_outPoint); });
        m_transportLay->addWidget(toEnd);
        auto *sel = makeTransportBtn(tr("Selection"), IconFactory::svgSelection());
        m_transportLay->addWidget(sel);
    } else {
        auto *start = makeTransportBtn(tr("Go to Start"), IconFactory::svgGoStart());
        connect(start, &QToolButton::clicked, this, [this]() { seekTo(m_inPoint); });
        m_transportLay->addWidget(start);
        auto *end = makeTransportBtn(tr("Go to End"), IconFactory::svgGoEnd());
        connect(end, &QToolButton::clicked, this, [this]() { seekTo(m_outPoint); });
        m_transportLay->addWidget(end);
    }

    m_moreBtn = makeTransportBtn(tr("More"), IconFactory::svgMore());
    connect(m_moreBtn, &QToolButton::clicked, this, &TrimmerWindow::showMoreMenu);
    m_transportLay->addWidget(m_moreBtn);

    m_transportLay->addStretch(1);
}

void TrimmerWindow::setMediaName(const QString &name)
{
    const EventMediaKind kind = guessKind(name);
    const double dur = isAudioFamily(kind) ? 12.0 : 30.0;
    setMedia(name, kind, dur);
}

void TrimmerWindow::setMedia(const QString &name, EventMediaKind kind, double durationSec,
                             const QString &pathHint)
{
    stopPlayback();
    m_name = name.isEmpty() ? QStringLiteral("(none)") : name;
    m_kind = kind;
    m_duration = std::max(0.5, durationSec);
    m_path = pathHint.isEmpty() ? guessPath(m_name, m_kind) : pathHint;
    m_current = 0.0;
    m_inPoint = 0.0;
    m_outPoint = m_duration;
    const QString file = m_name.contains(QLatin1Char('.'))
                             ? m_name
                             : (isAudioFamily(m_kind) ? m_name + QStringLiteral(".wav")
                                                      : m_name + QStringLiteral(".mp4"));
    setWindowTitle(tr("Trimmer - %1%2").arg(m_path, file));
    m_mediaTitle->setText(tr("%1  [%2]").arg(file, m_path));
    if (m_canvas) {
        m_canvas->mediaLabel = file;
    }
    rebuildTransport();
    updateChrome();
}

void TrimmerWindow::updateChrome()
{
    if (m_canvas) {
        m_canvas->setState(m_kind, m_duration, m_current, m_inPoint, m_outPoint);
    }
    updateStatusLabels();
}

void TrimmerWindow::updateStatusLabels()
{
    if (!m_posLabel) {
        return;
    }
    m_posLabel->setText(QStringLiteral("▶ %1").arg(formatTC(m_current)));
    m_inLabel->setText(QStringLiteral("⌜ %1").arg(formatTC(m_inPoint)));
    m_outLabel->setText(QStringLiteral("⌝ %1").arg(formatTC(m_outPoint)));
    m_lenLabel->setText(QStringLiteral("▭ %1").arg(formatTC(m_outPoint - m_inPoint)));
}

QString TrimmerWindow::formatTC(double sec) const
{
    // Vegas-like measures.beats.ticks mock (~120 bpm / 4/4 → 0.5s per beat)
    const double s = std::max(0.0, sec);
    const double totalBeats = s / 0.5;
    const int measures = static_cast<int>(std::floor(totalBeats / 4.0)) + 1;
    const int beats = (static_cast<int>(std::floor(totalBeats)) % 4) + 1;
    const int ticks = static_cast<int>(std::floor(std::fmod(totalBeats, 1.0) * 1000.0));
    return QStringLiteral("%1.%2.%3")
        .arg(measures)
        .arg(beats)
        .arg(ticks, 3, 10, QLatin1Char('0'));
}

void TrimmerWindow::togglePlay(bool play)
{
    m_playing = play;
    if (m_playing) {
        if (m_current >= m_outPoint - 0.01) {
            m_current = m_inPoint;
        }
        m_timer->start();
    } else {
        m_timer->stop();
    }
    updateChrome();
}

void TrimmerWindow::stopPlayback()
{
    m_playing = false;
    m_timer->stop();
}

void TrimmerWindow::seekTo(double sec)
{
    m_current = std::clamp(sec, 0.0, m_duration);
    updateChrome();
}

void TrimmerWindow::setInPoint()
{
    m_inPoint = std::clamp(m_current, 0.0, m_outPoint - kMinSel);
    updateChrome();
}

void TrimmerWindow::setOutPoint()
{
    m_outPoint = std::clamp(m_current, m_inPoint + kMinSel, m_duration);
    updateChrome();
}

void TrimmerWindow::showMoreMenu()
{
    QMenu menu(this);
    menu.addAction(tr("Play From Start"), QKeySequence(QStringLiteral("Shift+Space")), this, [this]() {
        seekTo(m_inPoint);
        togglePlay(true);
    });
    menu.addAction(tr("Go to Start"), QKeySequence(QStringLiteral("Ctrl+Home")), this,
                   [this]() { seekTo(m_inPoint); });
    menu.addAction(tr("Go to End"), QKeySequence(QStringLiteral("Ctrl+End")), this,
                   [this]() { seekTo(m_outPoint); });
    menu.addAction(tr("Previous Frame"), QKeySequence(QStringLiteral("Alt+Left")), this,
                   [this]() { seekTo(m_current - (1.0 / 30.0)); });
    menu.addAction(tr("Next Frame"), QKeySequence(QStringLiteral("Alt+Right")), this,
                   [this]() { seekTo(m_current + (1.0 / 30.0)); });
    menu.addSeparator();
    {
        auto *ow = menu.addAction(tr("Enable Timeline Overwrite"));
        ow->setCheckable(true);
        ow->setChecked(m_overwrite);
        connect(ow, &QAction::toggled, this, [this](bool on) { m_overwrite = on; });
    }
    {
        auto *a = menu.addAction(tr("Add to Timeline up to Cursor"),
                                 QKeySequence(QStringLiteral("Shift+A")));
        a->setEnabled(false);
    }
    menu.addAction(tr("Create Subclip…"));
    menu.addSeparator();
    menu.addAction(tr("Set In Point"), QKeySequence(QStringLiteral("I")), this,
                   &TrimmerWindow::setInPoint);
    menu.addAction(tr("Set Out Point"), QKeySequence(QStringLiteral("O")), this,
                   &TrimmerWindow::setOutPoint);
    menu.addAction(tr("Insert Marker"), QKeySequence(QStringLiteral("M")));
    menu.addAction(tr("Insert Region"), QKeySequence(QStringLiteral("R")));
    {
        auto *a = menu.addAction(tr("Save Markers/Regions"), QKeySequence(QStringLiteral("S")));
        a->setEnabled(false);
    }
    menu.addSeparator();
    if (isVideoFamily(m_kind)) {
        menu.addAction(tr("Detect Scenes and Add to Timeline from Cursor"));
    }
    if (isAudioFamily(m_kind)) {
        menu.addAction(tr("Beat Detection"), QKeySequence(QStringLiteral("B")));
    }
    menu.addSeparator();
    menu.addAction(tr("Edit Visible Button Set…"));

    if (m_moreBtn) {
        menu.exec(m_moreBtn->mapToGlobal(QPoint(0, m_moreBtn->height())));
    } else {
        menu.exec(QCursor::pos());
    }
}

} // namespace openvegas
