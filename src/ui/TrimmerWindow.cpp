#include "ui/TrimmerWindow.h"
#include "ui/IconFactory.h"
#include "io/MediaWaveformCache.h"

#include <QAction>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QShortcut>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QCursor>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <functional>

namespace openvegas {

namespace {

constexpr double kMinSel = 0.05;
constexpr int kRulerH = 18;
constexpr int kLoopBarH = 14;
constexpr int kMarkerHead = 14;
const QColor kLoopYellow(0xf0, 0xc0, 0x20);
const QColor kLoopBlue(0x3a, 0x78, 0xc8, 90);

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

/** Preview / waveform canvas with playhead, In/Out, and Event Media Markers. */
class TrimmerCanvas : public QWidget {
public:
    explicit TrimmerCanvas(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumHeight(160);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMouseTracking(true);
        setCursor(Qt::ArrowCursor);
        setFocusPolicy(Qt::StrongFocus);
    }

    void setState(EventMediaKind kind, double duration, double current, double inPt, double outPt,
                  const QVector<TimelineMarker> &markers, bool markersVisible, const QString &path,
                  bool reversed, int selectedMarkerId)
    {
        m_kind = kind;
        m_duration = std::max(0.1, duration);
        m_reversed = reversed;
        m_selectedMarkerId = selectedMarkerId;
        m_current = std::clamp(current, 0.0, m_duration);
        m_in = std::clamp(inPt, 0.0, m_duration);
        m_out = std::clamp(outPt, m_in + kMinSel, m_duration);
        m_markers = markers;
        m_markersVisible = markersVisible;
        if (m_path != path) {
            m_path = path;
            m_peaks = WaveformPeaks{};
            if (!m_path.isEmpty() && isAudio()) {
                m_peaks = MediaWaveformCache::instance().peaksFor(m_path);
            }
        }
        if (m_viewEnd <= m_viewStart + 0.05 || m_viewEnd > m_duration + 0.01) {
            m_viewStart = 0.0;
            m_viewEnd = m_duration;
        }
        m_viewStart = std::clamp(m_viewStart, 0.0, m_duration);
        m_viewEnd = std::clamp(m_viewEnd, m_viewStart + 0.05, m_duration);
        update();
    }

    void setPeaks(const WaveformPeaks &peaks)
    {
        m_peaks = peaks;
        update();
    }

    std::function<void(double)> onSeek;
    std::function<void(double, double)> onSelectionChanged;
    std::function<void(int /*markerId*/)> onMarkerSelected;
    std::function<void(int /*markerId*/, double /*mediaSec*/)> onMarkerMoved;
    std::function<void(int /*markerId*/, const QPoint & /*global*/)> onMarkerMenu;
    std::function<void(int /*markerId*/)> onMarkerEdit;
    QString mediaLabel;

    /** Zoom view to current Loop Region (In/Out). */
    void zoomToLoopRegion()
    {
        const double pad = std::max(0.05, (m_out - m_in) * 0.15);
        m_viewStart = std::max(0.0, m_in - pad);
        m_viewEnd = std::min(m_duration, m_out + pad);
        if (m_viewEnd - m_viewStart < 0.2) {
            m_viewStart = std::max(0.0, m_in - 0.1);
            m_viewEnd = std::min(m_duration, m_out + 0.1);
        }
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0x1a, 0x1a, 0x1a));

        const QRect body = contentRect();
        if (isAudio()) {
            paintRuler(p);
            paintWaveform(p, body);
            paintLoopRegion(p, body);
        } else {
            paintVideoFrame(p, body);
            paintLoopRegion(p, body);
        }
        if (m_markersVisible) {
            paintMarkers(p, body);
        }
        paintPlayhead(p, body);
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        const QRect body = contentRect();
        if (e->button() == Qt::RightButton) {
            const int mid = markerAtPos(e->pos(), body);
            if (mid > 0 && onMarkerMenu) {
                onMarkerMenu(mid, e->globalPosition().toPoint());
            }
            return;
        }
        if (e->button() != Qt::LeftButton) {
            return;
        }
        const double tDisp = xToTime(e->pos().x(), body);
        const int mid = markerAtPos(e->pos(), body);
        if (mid > 0 && m_markersVisible) {
            m_drag = Drag::Marker;
            m_dragMarkerId = mid;
            if (onMarkerSelected) {
                onMarkerSelected(mid);
            }
            return;
        }
        const int inX = timeToX(m_in, body);
        const int outX = timeToX(m_out, body);
        const QRect loopBar = loopBarRect(body);
        const bool onBar = loopBar.contains(e->pos())
                           || (e->pos().y() >= body.top() && e->pos().y() <= body.top() + kLoopBarH + 4);

        if (std::abs(e->pos().x() - inX) <= 8 && (onBar || isAudio())) {
            m_drag = Drag::In;
        } else if (std::abs(e->pos().x() - outX) <= 8 && (onBar || isAudio())) {
            m_drag = Drag::Out;
        } else if (onBar && e->pos().x() > inX && e->pos().x() < outX) {
            m_drag = Drag::Move;
            m_dragAnchorSec = tDisp;
            m_dragIn0 = m_in;
            m_dragOut0 = m_out;
        } else {
            m_drag = Drag::Playhead;
            if (onSeek) {
                onSeek(tDisp);
            }
        }
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        const QRect body = contentRect();
        const double tDisp = xToTime(e->pos().x(), body);
        if (m_drag == Drag::None) {
            const int inX = timeToX(m_in, body);
            const int outX = timeToX(m_out, body);
            const QRect loopBar = loopBarRect(body);
            if (markerAtPos(e->pos(), body) > 0) {
                setCursor(Qt::SizeHorCursor);
            } else if (std::abs(e->pos().x() - inX) <= 8 || std::abs(e->pos().x() - outX) <= 8) {
                setCursor(Qt::SizeHorCursor);
            } else if (loopBar.contains(e->pos()) && e->pos().x() > inX && e->pos().x() < outX) {
                setCursor(Qt::SizeAllCursor);
            } else {
                setCursor(Qt::ArrowCursor);
            }
            return;
        }
        if (m_drag == Drag::Marker && onMarkerMoved) {
            onMarkerMoved(m_dragMarkerId, displayToMedia(tDisp));
            return;
        }
        if (m_drag == Drag::Playhead && onSeek) {
            onSeek(tDisp);
        } else if (m_drag == Drag::In) {
            m_in = std::clamp(tDisp, 0.0, m_out - kMinSel);
            emitSelection();
            update();
        } else if (m_drag == Drag::Out) {
            m_out = std::clamp(tDisp, m_in + kMinSel, m_duration);
            emitSelection();
            update();
        } else if (m_drag == Drag::Move) {
            const double delta = tDisp - m_dragAnchorSec;
            const double len = m_dragOut0 - m_dragIn0;
            double ni = m_dragIn0 + delta;
            double no = m_dragOut0 + delta;
            if (ni < 0.0) {
                ni = 0.0;
                no = len;
            }
            if (no > m_duration) {
                no = m_duration;
                ni = m_duration - len;
            }
            m_in = ni;
            m_out = no;
            emitSelection();
            update();
        }
    }

    void mouseDoubleClickEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton) {
            return;
        }
        const QRect body = contentRect();
        const int mid = markerAtPos(e->pos(), body);
        if (mid > 0 && onMarkerEdit) {
            onMarkerEdit(mid);
            return;
        }
        const double t = xToTime(e->pos().x(), body);
        const double half = std::clamp(m_duration * 0.05, 0.5, 2.0);
        m_in = std::max(0.0, t - half);
        m_out = std::min(m_duration, t + half);
        if (m_out - m_in < kMinSel) {
            m_out = std::min(m_duration, m_in + kMinSel);
        }
        emitSelection();
        if (onSeek) {
            onSeek(m_in);
        }
        update();
    }

    void mouseReleaseEvent(QMouseEvent *) override
    {
        m_drag = Drag::None;
        m_dragMarkerId = 0;
    }

    void wheelEvent(QWheelEvent *e) override
    {
        const QRect body = contentRect();
        const double span = m_viewEnd - m_viewStart;
        if (span <= 0.05) {
            return;
        }
        const double pivot = xToTime(e->position().toPoint().x(), body);
        const double factor = e->angleDelta().y() > 0 ? 0.8 : 1.25;
        double newSpan = std::clamp(span * factor, 0.2, m_duration);
        double start = pivot - (pivot - m_viewStart) * (newSpan / span);
        double end = start + newSpan;
        if (start < 0.0) {
            end -= start;
            start = 0.0;
        }
        if (end > m_duration) {
            start -= (end - m_duration);
            end = m_duration;
            start = std::max(0.0, start);
        }
        m_viewStart = start;
        m_viewEnd = end;
        update();
    }

private:
    enum class Drag { None, Playhead, In, Out, Move, Marker };

    bool isAudio() const { return isAudioFamily(m_kind); }

    double displayToMedia(double displaySec) const
    {
        if (!m_reversed) {
            return displaySec;
        }
        return m_duration - displaySec;
    }

    double mediaToDisplay(double mediaSec) const
    {
        if (!m_reversed) {
            return mediaSec;
        }
        return m_duration - mediaSec;
    }

    int markerAtPos(const QPoint &pos, const QRect &body) const
    {
        if (!m_markersVisible) {
            return 0;
        }
        int best = 0;
        int bestDist = 10;
        for (const TimelineMarker &m : m_markers) {
            const int x = timeToX(mediaToDisplay(m.timeSec), body);
            const int headY = body.top() + (isAudio() ? kLoopBarH + 1 : 1);
            const QRect headR(x - 1, headY, kMarkerHead + 2, kMarkerHead + 2);
            if (headR.contains(pos) || (std::abs(pos.x() - x) <= 4 && pos.y() >= body.top())) {
                const int d = std::abs(pos.x() - x);
                if (d < bestDist) {
                    bestDist = d;
                    best = m.id;
                }
            }
        }
        return best;
    }

    void emitSelection()
    {
        if (onSelectionChanged) {
            onSelectionChanged(m_in, m_out);
        }
    }

    QRect contentRect() const
    {
        return rect().adjusted(0, isAudio() ? kRulerH : 0, 0, 0);
    }

    /** Yellow Loop Region bar: top of audio body, or bottom of video frame. */
    QRect loopBarRect(const QRect &body) const
    {
        if (isAudio()) {
            return QRect(body.left(), body.top(), body.width(), kLoopBarH);
        }
        return QRect(body.left(), body.bottom() - kLoopBarH - 8, body.width(), kLoopBarH);
    }

    double viewSpan() const { return std::max(0.05, m_viewEnd - m_viewStart); }

    double xToTime(int x, const QRect &body) const
    {
        if (body.width() <= 1) {
            return m_viewStart;
        }
        return std::clamp(m_viewStart
                              + double(x - body.left()) / body.width() * viewSpan(),
                          0.0, m_duration);
    }

    int timeToX(double sec, const QRect &body) const
    {
        return body.left()
               + static_cast<int>(
                     std::lround(((sec - m_viewStart) / viewSpan()) * body.width()));
    }

    void paintRuler(QPainter &p)
    {
        p.fillRect(0, 0, width(), kRulerH, QColor(0x2a, 0x2a, 0x2a));
        p.setPen(QColor(0xa8, 0xa8, 0xa8));
        QFont f = font();
        f.setPointSize(8);
        p.setFont(f);
        const int marks = std::max(2, static_cast<int>(viewSpan()) + 1);
        for (int i = 0; i <= marks; ++i) {
            const double sec = m_viewStart + (double(i) / marks) * viewSpan();
            const int x = timeToX(sec, QRect(0, 0, width(), height()));
            p.drawLine(x, kRulerH - 4, x, kRulerH);
            const int m = static_cast<int>(std::floor(sec)) + 1;
            p.drawText(x + 3, 12, QStringLiteral("%1.1").arg(m));
        }
    }

    void paintVideoFrame(QPainter &p, const QRect &r)
    {
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
        const QRect strip(r.left(), r.bottom() - 8, r.width(), 8);
        p.fillRect(strip, QColor(0x33, 0x33, 0x33));
    }

    void paintWaveform(QPainter &p, const QRect &r)
    {
        p.fillRect(r, QColor(0x2c, 0x2c, 0x2c));
        const int mid = r.center().y();
        const int chH = r.height() / 2;

        if (m_peaks.isValid() && m_peaks.durationSec > 0.05) {
            const int chans = std::max(1, std::min(2, m_peaks.channels));
            for (int ch = 0; ch < chans; ++ch) {
                const int top = r.top() + ch * chH;
                const int h = (ch == chans - 1) ? (r.bottom() - top) : chH;
                const int cy = top + h / 2;
                QPainterPath path;
                path.moveTo(r.left(), cy);
                for (int x = r.left(); x <= r.right(); x += 2) {
                    const double t = xToTime(x, r);
                    const int bin = std::clamp(
                        int(std::floor((t / m_peaks.durationSec) * m_peaks.bins)), 0,
                        m_peaks.bins - 1);
                    const int idx = (bin * m_peaks.channels + std::min(ch, m_peaks.channels - 1)) * 2;
                    double amp = 0.0;
                    if (idx + 1 < m_peaks.minMax.size()) {
                        const double mn = m_peaks.minMax[idx] / 32768.0;
                        const double mx = m_peaks.minMax[idx + 1] / 32768.0;
                        amp = std::max(std::abs(mn), std::abs(mx)) * (h * 0.42);
                    }
                    path.lineTo(x, cy - amp);
                }
                for (int x = r.right(); x >= r.left(); x -= 2) {
                    const double t = xToTime(x, r);
                    const int bin = std::clamp(
                        int(std::floor((t / m_peaks.durationSec) * m_peaks.bins)), 0,
                        m_peaks.bins - 1);
                    const int idx = (bin * m_peaks.channels + std::min(ch, m_peaks.channels - 1)) * 2;
                    double amp = 0.0;
                    if (idx + 1 < m_peaks.minMax.size()) {
                        const double mn = m_peaks.minMax[idx] / 32768.0;
                        const double mx = m_peaks.minMax[idx + 1] / 32768.0;
                        amp = std::max(std::abs(mn), std::abs(mx)) * (h * 0.42);
                    }
                    path.lineTo(x, cy + amp);
                }
                path.closeSubpath();
                p.setBrush(QColor(0xb8, 0x9a, 0xc8, 200));
                p.setPen(QPen(QColor(0x8a, 0x6a, 0x9a), 1));
                p.drawPath(path);
            }
        } else {
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
        }
        p.setPen(QColor(0, 0, 0, 60));
        p.drawLine(r.left(), mid, r.right(), mid);
    }

    void paintLoopRegion(QPainter &p, const QRect &body)
    {
        const int x0 = timeToX(m_in, body);
        const int x1 = timeToX(m_out, body);
        if (x1 < body.left() - 2 || x0 > body.right() + 2) {
            return;
        }
        const int left = std::max(body.left(), x0);
        const int right = std::min(body.right(), x1);
        const int w = std::max(1, right - left);

        // Audio: translucent blue fill over waveform (Vegas selection)
        if (isAudio()) {
            p.fillRect(QRect(left, body.top(), w, body.height()), kLoopBlue);
        }

        // Yellow Loop Region bar with inward brackets [  ]
        const QRect bar = loopBarRect(body);
        const int barY = bar.top();
        const int barH = bar.height();
        const QRect yellow(left, barY + 2, w, barH - 4);
        p.fillRect(yellow, kLoopYellow);
        p.setPen(QPen(QColor(0x90, 0x70, 0x10), 1));
        p.drawRect(yellow.adjusted(0, 0, -1, -1));

        // Inward bracket tips
        auto bracket = [&](int x, bool isIn) {
            QPainterPath path;
            const int tip = 7;
            if (isIn) {
                path.moveTo(x, barY + 1);
                path.lineTo(x + tip, barY + 1);
                path.lineTo(x + tip, barY + barH - 2);
                path.lineTo(x, barY + barH - 2);
                path.lineTo(x + 3, barY + barH / 2);
                path.closeSubpath();
            } else {
                path.moveTo(x, barY + 1);
                path.lineTo(x - tip, barY + 1);
                path.lineTo(x - tip, barY + barH - 2);
                path.lineTo(x, barY + barH - 2);
                path.lineTo(x - 3, barY + barH / 2);
                path.closeSubpath();
            }
            p.setBrush(kLoopYellow);
            p.setPen(QPen(QColor(0x60, 0x48, 0x08), 1));
            p.drawPath(path);
            // Edge line through media body
            p.setPen(QPen(kLoopYellow, 1));
            p.drawLine(x, bar.bottom(), x, body.bottom());
        };
        bracket(x0, true);
        bracket(x1, false);
    }

    void paintMarkers(QPainter &p, const QRect &body)
    {
        QFont numFont = font();
        numFont.setFamily(QStringLiteral("Segoe UI"));
        numFont.setPointSize(8);
        numFont.setBold(true);
        QFont labelFont = numFont;
        labelFont.setBold(false);
        labelFont.setPointSize(9);

        const QColor head(0xe0, 0xa0, 0x20);
        const QColor guide(0xe0, 0xa0, 0x20, 200);

        for (const TimelineMarker &m : m_markers) {
            const double disp = mediaToDisplay(m.timeSec);
            if (disp < m_viewStart - 0.01 || disp > m_viewEnd + 0.01) {
                continue;
            }
            const int x = timeToX(disp, body);
            if (x < body.left() - 40 || x > body.right() + 120) {
                continue;
            }

            p.setPen(QPen(guide, 1));
            p.drawLine(x, body.top() + (isAudio() ? kLoopBarH : 0), x, body.bottom());

            const int headY = body.top() + (isAudio() ? kLoopBarH + 1 : 1);
            const QRect headR(x, headY, kMarkerHead, kMarkerHead);
            p.fillRect(headR, head);
            p.setPen(QColor(40, 24, 0, 200));
            p.drawRect(headR.adjusted(0, 0, -1, -1));
            if (m.selected || m.id == m_selectedMarkerId) {
                p.setPen(QPen(QColor(255, 230, 140), 1));
                p.drawRect(headR.adjusted(1, 1, -2, -2));
            }
            p.setFont(numFont);
            p.setPen(Qt::white);
            p.drawText(headR, Qt::AlignCenter, QString::number(m.number));

            if (!m.label.isEmpty()) {
                p.setFont(labelFont);
                const QRect labelR(headR.right() + 4, headR.top() - 1, 160, headR.height() + 2);
                const QString text =
                    QFontMetrics(labelFont).elidedText(m.label, Qt::ElideRight, labelR.width());
                p.setPen(QColor(0, 0, 0, 180));
                p.drawText(labelR.translated(1, 1), Qt::AlignVCenter | Qt::AlignLeft, text);
                p.setPen(QColor(0xe8, 0xe8, 0xe8));
                p.drawText(labelR, Qt::AlignVCenter | Qt::AlignLeft, text);
            }
        }
    }

    void paintPlayhead(QPainter &p, const QRect &body)
    {
        const int x = timeToX(m_current, body);
        p.setPen(QPen(Qt::white, 1));
        p.drawLine(x, body.top(), x, body.bottom());
        p.fillRect(QRect(x - 4, body.top(), 8, 8), QColor(0xe8, 0xe8, 0xe8));
    }

    EventMediaKind m_kind = EventMediaKind::Video;
    double m_duration = 10.0;
    double m_current = 0.0;
    double m_in = 0.0;
    double m_out = 10.0;
    double m_viewStart = 0.0;
    double m_viewEnd = 10.0;
    Drag m_drag = Drag::None;
    double m_dragAnchorSec = 0.0;
    double m_dragIn0 = 0.0;
    double m_dragOut0 = 0.0;
    int m_dragMarkerId = 0;
    QVector<TimelineMarker> m_markers;
    bool m_markersVisible = true;
    bool m_reversed = false;
    int m_selectedMarkerId = 0;
    QString m_path;
    WaveformPeaks m_peaks;
};

TrimmerWindow::TrimmerWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::Window);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setFocusPolicy(Qt::StrongFocus);
    resize(720, 420);
    buildUi();
    setupShortcuts();
    m_timer = new QTimer(this);
    m_timer->setInterval(33);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        if (!m_playing) {
            return;
        }
        m_current += 0.033;
        const double endLimit = m_loop ? m_outPoint : m_duration;
        if (m_current >= endLimit) {
            if (m_loop) {
                m_current = m_inPoint;
            } else {
                m_current = endLimit;
                stopPlayback();
            }
        }
        updateChrome();
    });
    connect(&MediaWaveformCache::instance(), &MediaWaveformCache::waveformReady, this,
            [this](const QString &path) {
                if (m_canvas && !m_path.isEmpty()
                    && QString::compare(path, m_path, Qt::CaseInsensitive) == 0) {
                    m_canvas->setPeaks(MediaWaveformCache::instance().peaksFor(path));
                }
            });
    setMedia(QStringLiteral("(none)"), EventMediaKind::Video, 10.0);
}

TrimmerWindow::~TrimmerWindow() = default;

void TrimmerWindow::setupShortcuts()
{
    auto *ins = new QShortcut(QKeySequence(QStringLiteral("M")), this);
    connect(ins, &QShortcut::activated, this, &TrimmerWindow::insertMarker);
    auto *beat = new QShortcut(QKeySequence(QStringLiteral("B")), this);
    connect(beat, &QShortcut::activated, this, &TrimmerWindow::runBeatDetection);
    auto *reg = new QShortcut(QKeySequence(QStringLiteral("R")), this);
    connect(reg, &QShortcut::activated, this, &TrimmerWindow::insertRegion);
    auto *inPt = new QShortcut(QKeySequence(QStringLiteral("I")), this);
    connect(inPt, &QShortcut::activated, this, &TrimmerWindow::setInPoint);
    auto *outPt = new QShortcut(QKeySequence(QStringLiteral("O")), this);
    connect(outPt, &QShortcut::activated, this, &TrimmerWindow::setOutPoint);
    auto *delMk = new QShortcut(QKeySequence::Delete, this);
    connect(delMk, &QShortcut::activated, this, [this]() {
        if (m_selectedMarkerId > 0) {
            deleteMarker(m_selectedMarkerId);
        }
    });
    auto *toggleMk = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+K")), this);
    connect(toggleMk, &QShortcut::activated, this, [this]() {
        setMarkersVisible(!m_markersVisible);
    });
}

void TrimmerWindow::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

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
    m_canvas->onMarkerSelected = [this](int id) {
        m_selectedMarkerId = id;
        updateChrome();
    };
    m_canvas->onMarkerMoved = [this](int id, double mediaSec) { moveMarker(id, mediaSec); };
    m_canvas->onMarkerEdit = [this](int id) { renameMarker(id); };
    m_canvas->onMarkerMenu = [this](int id, const QPoint &gp) {
        m_selectedMarkerId = id;
        QMenu menu(this);
        menu.addAction(tr("Go To"), this, [this, id]() {
            for (const TimelineMarker &m : m_markers) {
                if (m.id == id) {
                    seekTo(m_reversed ? (m_duration - m.timeSec) : m.timeSec);
                    break;
                }
            }
        });
        menu.addAction(tr("Rename…"), this, [this, id]() { renameMarker(id); });
        menu.addAction(tr("Delete"), this, [this, id]() { deleteMarker(id); });
        menu.exec(gp);
        updateChrome();
    };
    root->addWidget(m_canvas, 1);

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

    auto *status = new QWidget(this);
    status->setFixedHeight(28);
    status->setStyleSheet(QStringLiteral("background:#1e1e1e; border-top:1px solid #111;"));
    auto *sLay = new QHBoxLayout(status);
    sLay->setContentsMargins(8, 2, 8, 2);
    sLay->setSpacing(8);
    m_posLabel = new QLabel(status);
    m_inLabel = new QLabel(status);
    m_outLabel = new QLabel(status);
    m_lenLabel = new QLabel(status);
    const QString boxCss = QStringLiteral(
        "color:#e0e0e0; font-size:11px; font-family:Consolas,monospace;"
        "background:#2a2a2a; border:1px solid #444; padding:2px 6px;");
    for (QLabel *l : {m_posLabel, m_inLabel, m_outLabel, m_lenLabel}) {
        l->setStyleSheet(boxCss);
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
    while (QLayoutItem *it = m_transportLay->takeAt(0)) {
        if (QWidget *w = it->widget()) {
            w->deleteLater();
        }
        delete it;
    }
    m_transportLay->addStretch(1);

    m_loopBtn = makeTransportBtn(tr("Loop Region Playback"), IconFactory::svgLoop());
    m_loopBtn->setCheckable(true);
    m_loopBtn->setChecked(m_loop);
    m_loopBtn->setStyleSheet(QStringLiteral(
        "QToolButton:checked { background:#2a5a9a; border:1px solid #4a8ad8; border-radius:3px; }"));
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
        auto *sel = makeTransportBtn(tr("Zoom to Loop Region"), IconFactory::svgSelection());
        connect(sel, &QToolButton::clicked, this, [this]() {
            if (m_canvas) {
                m_canvas->zoomToLoopRegion();
            }
        });
        m_transportLay->addWidget(sel);
    } else {
        auto *start = makeTransportBtn(tr("Go to Start"), IconFactory::svgGoStart());
        connect(start, &QToolButton::clicked, this, [this]() { seekTo(m_inPoint); });
        m_transportLay->addWidget(start);
        auto *end = makeTransportBtn(tr("Go to End"), IconFactory::svgGoEnd());
        connect(end, &QToolButton::clicked, this, [this]() { seekTo(m_outPoint); });
        m_transportLay->addWidget(end);
        auto *sel = makeTransportBtn(tr("Zoom to Loop Region"), IconFactory::svgRegion());
        connect(sel, &QToolButton::clicked, this, [this]() {
            if (m_canvas) {
                m_canvas->zoomToLoopRegion();
            }
        });
        m_transportLay->addWidget(sel);
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
                             const QString &pathHint, const QVector<TimelineMarker> &markers,
                             bool reversed)
{
    stopPlayback();
    m_name = name.isEmpty() ? QStringLiteral("(none)") : name;
    m_kind = kind;
    m_duration = std::max(0.5, durationSec);
    m_path = pathHint.isEmpty() ? guessPath(m_name, m_kind) : pathHint;
    m_reversed = reversed;
    m_current = 0.0;
    m_inPoint = 0.0;
    m_outPoint = m_duration;
    m_selectedMarkerId = 0;
    setMarkers(markers);
    const QString file = m_name.contains(QLatin1Char('.')) || m_name.contains(QLatin1String("subclip"))
                             ? m_name
                             : (isAudioFamily(m_kind) ? m_name + QStringLiteral(".wav")
                                                      : m_name + QStringLiteral(".mp4"));
    setWindowTitle(tr("Trimmer - %1").arg(file));
    m_mediaTitle->setText(file);
    if (m_canvas) {
        m_canvas->mediaLabel = file;
    }
    rebuildTransport();
    updateChrome();
}

void TrimmerWindow::setMarkers(const QVector<TimelineMarker> &markers)
{
    m_markers = markers;
    m_nextMarkerId = 1;
    m_nextMarkerNumber = 1;
    for (const TimelineMarker &m : m_markers) {
        m_nextMarkerId = std::max(m_nextMarkerId, m.id + 1);
        m_nextMarkerNumber = std::max(m_nextMarkerNumber, m.number + 1);
    }
    if (m_markers.isEmpty()) {
        m_nextMarkerId = 1;
        m_nextMarkerNumber = 1;
    }
    updateChrome();
}

void TrimmerWindow::setMarkersVisible(bool on)
{
    m_markersVisible = on;
    updateChrome();
}

void TrimmerWindow::notifyMarkersChanged()
{
    if (onMarkersChanged) {
        onMarkersChanged(m_path, m_markers);
    }
}

void TrimmerWindow::renumberMarkers()
{
    std::sort(m_markers.begin(), m_markers.end(),
              [](const TimelineMarker &a, const TimelineMarker &b) {
                  if (a.timeSec != b.timeSec) {
                      return a.timeSec < b.timeSec;
                  }
                  return a.id < b.id;
              });
    int n = 1;
    for (TimelineMarker &m : m_markers) {
        m.number = n++;
    }
    m_nextMarkerNumber = n;
}

void TrimmerWindow::insertMarker()
{
    bool ok = false;
    const QString label = QInputDialog::getText(this, tr("Insert Marker"), tr("Label:"),
                                                QLineEdit::Normal, QString(), &ok);
    if (!ok) {
        return;
    }
    TimelineMarker m;
    m.id = m_nextMarkerId++;
    m.number = m_nextMarkerNumber++;
    const double mediaT = m_reversed ? (m_duration - m_current) : m_current;
    m.timeSec = std::clamp(mediaT, 0.0, m_duration);
    m.label = label.trimmed();
    m_markers.push_back(m);
    m_selectedMarkerId = m.id;
    renumberMarkers();
    m_markersVisible = true;
    notifyMarkersChanged();
    updateChrome();
}

void TrimmerWindow::insertRegion()
{
    // Loop Region around playhead (Vegas-style time selection), not marker pair.
    const double half = std::clamp(m_duration * 0.05, 0.5, 2.0);
    m_inPoint = std::max(0.0, m_current - half);
    m_outPoint = std::min(m_duration, m_current + half);
    if (m_outPoint - m_inPoint < kMinSel) {
        m_outPoint = std::min(m_duration, m_inPoint + kMinSel);
    }
    seekTo(m_inPoint);
    updateChrome();
}

void TrimmerWindow::clearMarkers()
{
    if (m_markers.isEmpty()) {
        return;
    }
    m_markers.clear();
    m_nextMarkerId = 1;
    m_nextMarkerNumber = 1;
    notifyMarkersChanged();
    updateChrome();
}

void TrimmerWindow::runBeatDetection()
{
    if (!isAudioFamily(m_kind)) {
        return;
    }

    const double t0 = m_inPoint;
    const double t1 = m_outPoint;
    QVector<double> hits;

    WaveformPeaks peaks = MediaWaveformCache::instance().peaksFor(m_path);
    if (peaks.isValid() && peaks.durationSec > 0.05 && peaks.bins > 8) {
        QVector<double> energy(peaks.bins, 0.0);
        for (int b = 0; b < peaks.bins; ++b) {
            double e = 0.0;
            for (int ch = 0; ch < peaks.channels; ++ch) {
                const int idx = (b * peaks.channels + ch) * 2;
                if (idx + 1 >= peaks.minMax.size()) {
                    continue;
                }
                const double mn = peaks.minMax[idx] / 32768.0;
                const double mx = peaks.minMax[idx + 1] / 32768.0;
                e = std::max(e, std::max(std::abs(mn), std::abs(mx)));
            }
            energy[b] = e;
        }
        QVector<double> sorted = energy;
        std::sort(sorted.begin(), sorted.end());
        const int threshIdx =
            std::clamp(int(sorted.size() * 0.72), 0, std::max(0, int(sorted.size()) - 1));
        const double thresh = sorted[threshIdx];
        const double minGap = 0.18;
        double lastHit = -1e9;
        for (int b = 1; b < peaks.bins - 1; ++b) {
            const double t = (double(b) / peaks.bins) * peaks.durationSec;
            if (t < t0 || t > t1) {
                continue;
            }
            if (energy[b] < thresh) {
                continue;
            }
            if (energy[b] < energy[b - 1] || energy[b] < energy[b + 1]) {
                continue;
            }
            if (t - lastHit < minGap) {
                continue;
            }
            hits.push_back(t);
            lastHit = t;
        }
    }

    if (hits.isEmpty()) {
        // Tempo grid fallback (~120 BPM) across selection
        const double beat = 0.5;
        for (double t = t0; t <= t1 + 1e-6; t += beat) {
            hits.push_back(std::clamp(t, 0.0, m_duration));
        }
    }

    // Replace markers inside selection; keep outside
    QVector<TimelineMarker> kept;
    for (const TimelineMarker &m : m_markers) {
        if (m.timeSec < t0 - 1e-4 || m.timeSec > t1 + 1e-4) {
            kept.push_back(m);
        }
    }
    m_markers = kept;
    for (double t : hits) {
        TimelineMarker m;
        m.id = m_nextMarkerId++;
        m.timeSec = t;
        m.label.clear();
        m_markers.push_back(m);
    }
    renumberMarkers();
    m_markersVisible = true;
    notifyMarkersChanged();
    updateChrome();

    QMessageBox::information(this, tr("Beat Detection"),
                             tr("Added %1 media marker(s) in the selection.").arg(hits.size()));
}

void TrimmerWindow::moveMarker(int markerId, double mediaTimeSec)
{
    for (TimelineMarker &m : m_markers) {
        if (m.id == markerId) {
            m.timeSec = std::clamp(mediaTimeSec, 0.0, m_duration);
            renumberMarkers();
            notifyMarkersChanged();
            updateChrome();
            return;
        }
    }
}

void TrimmerWindow::renameMarker(int markerId)
{
    TimelineMarker *target = nullptr;
    for (TimelineMarker &m : m_markers) {
        if (m.id == markerId) {
            target = &m;
            break;
        }
    }
    if (!target) {
        return;
    }
    bool ok = false;
    const QString label = QInputDialog::getText(this, tr("Rename Marker"), tr("Label:"),
                                                QLineEdit::Normal, target->label, &ok);
    if (!ok) {
        return;
    }
    target->label = label.trimmed();
    m_selectedMarkerId = markerId;
    notifyMarkersChanged();
    updateChrome();
}

void TrimmerWindow::deleteMarker(int markerId)
{
    const auto it = std::remove_if(m_markers.begin(), m_markers.end(),
                                   [markerId](const TimelineMarker &m) { return m.id == markerId; });
    if (it == m_markers.end()) {
        return;
    }
    m_markers.erase(it, m_markers.end());
    if (m_selectedMarkerId == markerId) {
        m_selectedMarkerId = 0;
    }
    renumberMarkers();
    notifyMarkersChanged();
    updateChrome();
}

void TrimmerWindow::updateChrome()
{
    if (m_canvas) {
        m_canvas->setState(m_kind, m_duration, m_current, m_inPoint, m_outPoint, m_markers,
                           m_markersVisible, m_path, m_reversed, m_selectedMarkerId);
    }
    updateStatusLabels();
}

void TrimmerWindow::updateStatusLabels()
{
    if (!m_posLabel) {
        return;
    }
    // Vegas Trimmer: cursor | Loop In | Loop Out | Loop Length
    m_posLabel->setText(QStringLiteral("▶ %1").arg(formatTC(m_current)));
    m_inLabel->setText(QStringLiteral("[ %1").arg(formatTC(m_inPoint)));
    m_outLabel->setText(QStringLiteral("%1 ]").arg(formatTC(m_outPoint)));
    m_lenLabel->setText(QStringLiteral("▭ %1").arg(formatTC(m_outPoint - m_inPoint)));
    m_inLabel->setToolTip(tr("Loop Region start"));
    m_outLabel->setToolTip(tr("Loop Region end"));
    m_lenLabel->setToolTip(tr("Loop Region length"));
}

QString TrimmerWindow::formatTC(double sec) const
{
    const double s = std::max(0.0, sec);
    const int totalFrames = static_cast<int>(std::lround(s * 30.0));
    const int ff = totalFrames % 30;
    const int totalSec = totalFrames / 30;
    const int ss = totalSec % 60;
    const int totalMin = totalSec / 60;
    const int mm = totalMin % 60;
    const int hh = totalMin / 60;
    return QStringLiteral("%1:%2:%3,%4")
        .arg(hh, 2, 10, QLatin1Char('0'))
        .arg(mm, 2, 10, QLatin1Char('0'))
        .arg(ss, 2, 10, QLatin1Char('0'))
        .arg(ff, 2, 10, QLatin1Char('0'));
}

void TrimmerWindow::togglePlay(bool play)
{
    m_playing = play;
    if (m_playing) {
        if (m_loop) {
            if (m_current < m_inPoint || m_current >= m_outPoint - 0.01) {
                m_current = m_inPoint;
            }
        } else if (m_current >= m_duration - 0.01) {
            m_current = 0.0;
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
    menu.addAction(tr("Play From Start"), QKeySequence(QStringLiteral("Shift+Space")), this,
                   [this]() {
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
    menu.addAction(tr("Insert Marker"), QKeySequence(QStringLiteral("M")), this,
                   &TrimmerWindow::insertMarker);
    menu.addAction(tr("Insert Region / Loop Region"), QKeySequence(QStringLiteral("R")), this,
                   &TrimmerWindow::insertRegion);
    menu.addAction(tr("Zoom to Loop Region"), this, [this]() {
        if (m_canvas) {
            m_canvas->zoomToLoopRegion();
        }
    });
    {
        auto *vis = menu.addAction(tr("Show Event Media Markers"),
                                   QKeySequence(QStringLiteral("Ctrl+Shift+K")));
        vis->setCheckable(true);
        vis->setChecked(m_markersVisible);
        connect(vis, &QAction::toggled, this, &TrimmerWindow::setMarkersVisible);
    }
    menu.addAction(tr("Clear Markers"), this, &TrimmerWindow::clearMarkers);
    menu.addSeparator();
    if (isVideoFamily(m_kind)) {
        menu.addAction(tr("Detect Scenes and Add to Timeline from Cursor"));
    }
    if (isAudioFamily(m_kind)) {
        menu.addAction(tr("Beat Detection"), QKeySequence(QStringLiteral("B")), this,
                       &TrimmerWindow::runBeatDetection);
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
