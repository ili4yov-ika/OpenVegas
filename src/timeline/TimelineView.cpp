#include "timeline/TimelineView.h"
#include "ui/FadeCurvePopup.h"
#include "io/MediaMime.h"
#include "io/MediaProbe.h"
#include "io/MediaThumbCache.h"
#include "io/MediaWaveformCache.h"
#include "io/MediaFilmstripCache.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QContextMenuEvent>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QKeyEvent>
#include <QFontMetrics>
#include <QEvent>
#include <QSizePolicy>
#include <QVector>
#include <QLineEdit>
#include <QToolTip>
#include <QTimer>
#include <QElapsedTimer>
#include <QHash>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileInfo>
#include <algorithm>
#include <cmath>

namespace openvegas {

namespace {

QColor videoEvent() { return QColor(0x5a, 0x4a, 0x68); }
QColor videoRail() { return QColor(0x6a, 0x50, 0x80); }
QColor audioEvent() { return QColor(0xe0, 0xb8, 0xa8); }
QColor audioTitle() { return QColor(0xd0, 0xa4, 0x90); }
QColor audioRail() { return QColor(0xc4, 0x88, 0x70); }
QColor audioWave() { return QColor(0x3a, 0x2a, 0x24); }
QColor audioWaveStroke() { return QColor(0x2a, 0x1c, 0x18, 220); }
QColor eventSel() { return QColor(0xf0, 0xd0, 0x40); }
QColor crossfadeStroke() { return QColor(110, 176, 255, 250); }
QColor fadeLineVideo() { return QColor(255, 255, 255, 230); }
QColor fadeLineAudio() { return QColor(0x40, 0x30, 0x28, 220); }
QColor levelLine() { return QColor(220, 230, 245, 200); }
QColor levelHandle() { return QColor(70, 150, 255); }
QColor levelHandleBorder() { return QColor(30, 90, 200); }
QColor markerHead() { return QColor(0xc8, 0x88, 0x14); }
QColor markerGuide() { return QColor(0xe0, 0x90, 0x18); }
QColor loopBar() { return QColor(0x12, 0x4a, 0x96); }
QColor loopBarTop() { return QColor(0xf0, 0xc0, 0x20); }
QColor loopHandle() { return QColor(0xf0, 0xc0, 0x20); }
QColor loopBand() { return QColor(40, 120, 210, 72); }
QColor loopBandEdge() { return QColor(90, 170, 255, 200); }
QColor trackGridMajor() { return QColor(0x2a, 0x2a, 0x2a); }
QColor trackGridMinor() { return QColor(0x1e, 0x1e, 0x1e); }

constexpr int kEventTitleH = 13;
/** Video: name is overlaid on filmstrip (Vegas); no reserved title chrome. */
constexpr int kVideoNameOverlayH = 14;
constexpr int kEventBtn = 16;
constexpr int kEventBtnGap = 2;
/** Vegas-style event gain: top = 0 dB (amp 1), bottom = −Inf.
 *  Vertical position follows linear amplitude (like EDL SustainGain), not linear dB.
 *  −Inf is stored as kGainDbMin (≈ −40 dB UI floor). */
constexpr double kGainDbMax = 0.0;
constexpr double kGainDbMin = -40.0;
constexpr double kGainDbInfThreshold = -39.5;
constexpr int kLevelHandleHalfH = 5;
constexpr int kLevelHandleW = 14;

/** Smooth fade paths — superseded by fadeCurveLinePath / fadeCurveFillPath (FadeCurvePopup). */

void paintFadeRegion(QPainter &p, const QRectF &fadeRect, bool fadeIn, bool isAudio,
                     FadeCurveType curve)
{
    if (fadeRect.width() < 2.0 || fadeRect.height() < 2.0) {
        return;
    }
    const QPainterPath fill = fadeCurveFillPath(fadeRect, curve, fadeIn);
    const QPainterPath line = fadeCurveLinePath(fadeRect, curve, fadeIn);

    if (isAudio) {
        // Darken peach under the fade curve (Vegas-like)
        p.fillPath(fill, QColor(0x40, 0x28, 0x20, 90));
        p.setBrush(QBrush(QColor(0x30, 0x20, 0x18, 160), Qt::Dense5Pattern));
        p.setPen(Qt::NoPen);
        p.drawPath(fill);
    } else {
        p.fillPath(fill, QColor(0, 0, 0, 55));
    }

    p.setBrush(Qt::NoBrush);
    QPen pen(isAudio ? fadeLineAudio() : fadeLineVideo(), 1.5);
    pen.setCosmetic(true);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.drawPath(line);
    p.setRenderHint(QPainter::Antialiasing, false);

    // Fade handle (top edge)
    const QRectF handle = fadeIn ? QRectF(fadeRect.right() - 5, fadeRect.top() + 1, 6, 5)
                                 : QRectF(fadeRect.left() - 1, fadeRect.top() + 1, 6, 5);
    p.setPen(QPen(QColor(80, 140, 220), 1));
    p.setBrush(QColor(120, 170, 240));
    p.drawRoundedRect(handle, 1, 1);
}

/** Interactive controls on a track header — drag reorder uses empty space outside these. */
struct HeaderControls {
    QRect mute;
    QRect solo;
    QRect chipMenu;
    QRect chipFx;
    QRect chipAuto;
    QRect sliderA;
    QRect sliderB;
    QVector<QRect> all() const
    {
        QVector<QRect> out{mute, solo};
        if (chipMenu.isValid()) {
            out.push_back(chipMenu);
        }
        if (chipFx.isValid()) {
            out.push_back(chipFx);
        }
        if (chipAuto.isValid()) {
            out.push_back(chipAuto);
        }
        if (sliderA.isValid()) {
            out.push_back(sliderA);
        }
        if (sliderB.isValid()) {
            out.push_back(sliderB);
        }
        return out;
    }
};

HeaderControls headerControls(const Track &track, int headerW, int railW, int y)
{
    HeaderControls c;
    const int bodyL = railW + 6;
    const int bodyR = headerW - 24;
    c.mute = QRect(headerW - 20, y + 3, 17, 15);
    c.solo = QRect(headerW - 20, y + 20, 17, 15);
    // Vegas-style: blue "…" More + "fx" on the top row (audio and video)
    c.chipMenu = QRect(bodyL, y + 3, 20, 16);
    c.chipFx = QRect(bodyL + 22, y + 3, 20, 16);
    if (track.kind == TrackKind::Audio) {
        c.sliderA = QRect(bodyL, y + track.height - 36, bodyR - bodyL, 15);
        c.sliderB = QRect(bodyL, y + track.height - 20, bodyR - bodyL, 15);
    } else {
        c.sliderA = QRect(bodyL, y + track.height - 22, bodyR - bodyL, 16);
    }
    return c;
}

} // namespace

TimelineView::TimelineView(ProjectModel *model, QWidget *parent)
    : QWidget(parent)
    , m_model(model)
{
    setMouseTracking(true);
    setMinimumHeight(120);
    setMinimumWidth(200);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);
    ensureContentWidth();

    connect(&MediaThumbCache::instance(), &MediaThumbCache::thumbnailReady, this,
            [this](const QString &) { update(); });
    connect(&MediaWaveformCache::instance(), &MediaWaveformCache::waveformReady, this,
            [this](const QString &) { update(); });
    connect(&MediaFilmstripCache::instance(), &MediaFilmstripCache::frameReady, this,
            [this](const QString &) { update(); });

    // Idle playhead: black ↔ white every second (Vegas-style caret)
    m_blinkTimer = new QTimer(this);
    m_blinkTimer->setInterval(1000);
    connect(m_blinkTimer, &QTimer::timeout, this, [this]() {
        if (m_playing || m_draggingPlayhead || std::abs(m_shuttleRate) > 1e-6) {
            return;
        }
        m_playheadBlinkLight = !m_playheadBlinkLight;
        update();
    });
    m_blinkTimer->start();

    m_playTimer = new QTimer(this);
    m_playTimer->setInterval(16);
    connect(m_playTimer, &QTimer::timeout, this, [this]() {
        if (!m_model) {
            return;
        }
        const bool shuttling = std::abs(m_shuttleRate) > 1e-6;
        if (!m_playing && !shuttling) {
            return;
        }
        // AudioEngine owns the clock while playing — only refresh UI from model.
        if (m_playing && m_externalTransportClock && !shuttling) {
            emit playheadChanged(m_model->playheadSec());
            update();
            return;
        }
        const double rate = shuttling ? m_shuttleRate : 1.0;
        const qint64 ms = m_playClock.restart();
        const double dt = std::max(0.0, ms / 1000.0);
        double ph = m_model->playheadSec() + dt * rate;
        ph = std::max(0.0, ph);
        if (m_model->loopPlaybackEnabled() && m_model->hasLoopRegion()) {
            const double a = m_model->loopRegion().startSec;
            const double b = m_model->loopRegion().endSec;
            if (b > a) {
                if (rate >= 0.0 && ph >= b) {
                    ph = a;
                } else if (rate < 0.0 && ph <= a) {
                    ph = b;
                }
            }
        }
        m_model->setPlayheadSec(ph);
        emit playheadChanged(m_model->playheadSec());
        update();
    });
}

void TimelineView::setExternalTransportClock(bool on)
{
    m_externalTransportClock = on;
}

void TimelineView::setShuttleRate(double rate)
{
    const double clamped = std::clamp(rate, -20.0, 20.0);
    if (std::abs(clamped - m_shuttleRate) < 1e-9) {
        return;
    }
    m_shuttleRate = clamped;
    const bool shuttling = std::abs(m_shuttleRate) > 1e-6;
    if (shuttling || m_playing) {
        if (!m_playTimer->isActive()) {
            m_playClock.restart();
            m_playTimer->start();
        }
        m_playheadBlinkLight = true;
    } else {
        m_playTimer->stop();
        m_playheadBlinkLight = true;
        m_blinkTimer->start();
    }
    update();
}

void TimelineView::setPlaying(bool playing)
{
    if (m_playing == playing) {
        return;
    }
    m_playing = playing;
    if (m_playing || std::abs(m_shuttleRate) > 1e-6) {
        m_playheadBlinkLight = true;
        m_playClock.restart();
        m_playTimer->start();
    } else {
        m_playTimer->stop();
        // Resume blink from white so the next second flips to black cleanly
        m_playheadBlinkLight = true;
        m_blinkTimer->start();
    }
    emit playingChanged(m_playing);
    update();
}

void TimelineView::stopPlayback()
{
    setPlaying(false);
}

void TimelineView::togglePlaying()
{
    setPlaying(!m_playing);
}

void TimelineView::seekPlayhead(double sec, bool stop)
{
    if (!m_model) {
        return;
    }
    if (stop) {
        setPlaying(false);
    }
    m_model->setPlayheadSec(sec);
    emit playheadChanged(m_model->playheadSec());
    update();
}

void TimelineView::stepFrames(int deltaFrames)
{
    if (!m_model || deltaFrames == 0) {
        return;
    }
    setPlaying(false);
    const double fps = std::max(1.0, m_model->frameRate());
    const double next = m_model->playheadSec() + double(deltaFrames) / fps;
    m_model->setPlayheadSec(std::clamp(next, 0.0, m_model->timelineEndSec()));
    emit playheadChanged(m_model->playheadSec());
    update();
}

void TimelineView::refreshLayout()
{
    ensureContentWidth();
    update();
}

void TimelineView::setHeaderWidth(int width)
{
    const int clamped = std::clamp(width, 140, 480);
    if (m_headerWidth == clamped) {
        return;
    }
    m_headerWidth = clamped;
    ensureContentWidth();
    positionTrackNameEdit();
    update();
    emit headerWidthChanged(m_headerWidth);
}

bool TimelineView::nearHeaderSplitter(const QPoint &pos) const
{
    return std::abs(pos.x() - m_headerWidth) <= splitterHitPad();
}

int TimelineView::defaultTrackHeight(TrackKind kind) const
{
    return kind == TrackKind::Audio ? 100 : 96;
}

int TimelineView::trackResizeIndexAt(const QPoint &pos) const
{
    if (!m_model || pos.y() < rulerHeight()) {
        return -1;
    }
    int y = rulerHeight() - m_scrollY;
    for (int i = 0; i < m_model->tracks().size(); ++i) {
        const Track &track = m_model->tracks()[i];
        const int bottom = y + track.height;
        if (std::abs(pos.y() - bottom) <= trackResizeHitPad() && pos.y() >= y) {
            return i;
        }
        y = bottom;
    }
    return -1;
}

void TimelineView::ensureContentWidth()
{
    clampScroll();
    emit scrollMetricsChanged();
    updateGeometry();
}

int TimelineView::contentWidthPx() const
{
    double maxEnd = 60.0;
    if (m_model) {
        for (const Track &t : m_model->tracks()) {
            for (const TrackEvent &ev : t.events) {
                maxEnd = std::max(maxEnd, ev.startSec + ev.lengthSec + 8.0);
            }
        }
        for (const TimelineMarker &m : m_model->markers()) {
            maxEnd = std::max(maxEnd, m.timeSec + 4.0);
        }
        if (m_model->hasLoopRegion()) {
            maxEnd = std::max(maxEnd, m_model->loopRegion().endSec + 4.0);
        }
    }
    const double pps = m_model ? m_model->pixelsPerSecond() : 40.0;
    return headerWidth() + static_cast<int>(std::lround(maxEnd * pps)) + 80;
}

int TimelineView::tracksHeightPx() const
{
    int h = 24;
    if (m_model) {
        for (const Track &t : m_model->tracks()) {
            h += t.height;
        }
    }
    return std::max(48, h);
}

int TimelineView::maxScrollX() const
{
    return std::max(0, contentWidthPx() - width());
}

int TimelineView::maxScrollY() const
{
    return std::max(0, tracksHeightPx() - std::max(0, height() - rulerHeight()));
}

void TimelineView::clampScroll()
{
    m_scrollX = std::clamp(m_scrollX, 0, maxScrollX());
    m_scrollY = std::clamp(m_scrollY, 0, maxScrollY());
}

void TimelineView::setScrollX(int x)
{
    const int nx = std::clamp(x, 0, maxScrollX());
    if (nx == m_scrollX) {
        return;
    }
    m_scrollX = nx;
    positionTrackNameEdit();
    update();
    emit scrollOffsetChanged();
}

void TimelineView::setScrollY(int y)
{
    const int ny = std::clamp(y, 0, maxScrollY());
    if (ny == m_scrollY) {
        return;
    }
    m_scrollY = ny;
    positionTrackNameEdit();
    update();
    emit scrollOffsetChanged();
}

void TimelineView::updateHoverCursor(const QPoint &pos)
{
    if (m_resizingHeader || m_resizingTrackIndex >= 0 || m_draggingPlayhead || m_dragging
        || m_reorderingTrack >= 0 || m_eventEditMode != EventEditMode::None
        || m_rulerDrag != RulerDragMode::None) {
        return;
    }
    if (nearHeaderSplitter(pos)) {
        setCursor(Qt::SizeHorCursor);
        if (m_hoverResizeTrack != -1 || m_hoverReorderTrack != -1) {
            m_hoverResizeTrack = -1;
            m_hoverReorderTrack = -1;
            update();
        }
        return;
    }
    if (pos.y() < rulerHeight() && pos.x() >= headerWidth()) {
        if (markerAtPos(pos) >= 0) {
            setCursor(Qt::SizeHorCursor);
            return;
        }
        switch (loopHitAt(pos)) {
        case RulerDragMode::LoopStart:
        case RulerDragMode::LoopEnd:
        case RulerDragMode::LoopCreate:
            setCursor(Qt::SizeHorCursor);
            return;
        case RulerDragMode::LoopMove:
            setCursor(Qt::SizeAllCursor);
            return;
        default:
            setCursor(Qt::ArrowCursor);
            return;
        }
    }
    const int ti = trackResizeIndexAt(pos);
    if (ti != m_hoverResizeTrack) {
        m_hoverResizeTrack = ti;
        update();
    }
    if (ti >= 0) {
        setCursor(Qt::SizeVerCursor);
        m_hoverReorderTrack = -1;
        return;
    }
    int reorderTi = -1;
    if (isHeaderEmptyDragSpace(pos, &reorderTi)) {
        setCursor(Qt::SizeAllCursor);
        if (m_hoverReorderTrack != reorderTi) {
            m_hoverReorderTrack = reorderTi;
            update();
        }
        return;
    }
    if (m_hoverReorderTrack != -1) {
        m_hoverReorderTrack = -1;
        update();
    }

    const EventEditMode mode = eventEditModeAt(pos);
    switch (mode) {
    case EventEditMode::TrimStart:
    case EventEditMode::TrimEnd:
        setCursor(Qt::SizeHorCursor);
        return;
    case EventEditMode::FadeIn:
        setCursor(Qt::SizeBDiagCursor);
        return;
    case EventEditMode::FadeOut:
        setCursor(Qt::SizeFDiagCursor);
        return;
    case EventEditMode::Level:
        setCursor(Qt::SizeVerCursor);
        return;
    default:
        break;
    }
    if (eventButtonAt(pos) != EventChromeButton::None) {
        setCursor(Qt::PointingHandCursor);
        return;
    }
    unsetCursor();
}

void TimelineView::updateEventChromeTooltip(const QPoint &pos)
{
    Hit hit;
    const EventChromeButton btn = eventButtonAt(pos, &hit);
    EventChromeButton newBtn = btn;
    int newLevelId = -1;

    if (btn != EventChromeButton::None && hit.eventId >= 0) {
        QString tip;
        switch (btn) {
        case EventChromeButton::PanCrop:
            tip = tr("Event Pan/Crop…\nClick to crop a video event or add animated pan/zoom effects.");
            break;
        case EventChromeButton::Fx:
            tip = tr("Event FX…\nClick to add effects to an event or edit event effects.");
            break;
        case EventChromeButton::More:
            tip = tr("More");
            break;
        default:
            break;
        }
        if (!tip.isEmpty()) {
            QToolTip::showText(mapToGlobal(pos), tip, this);
        }
        newLevelId = hit.eventId;
    } else {
        const EventEditMode mode = eventEditModeAt(pos, &hit);
        if (mode == EventEditMode::Level && hit.eventId >= 0) {
            if (const TrackEvent *ev = m_model->findEvent(hit.eventId)) {
                QToolTip::showText(mapToGlobal(pos), eventLevelTooltip(*ev), this);
                newLevelId = hit.eventId;
            }
        } else if (m_hoverButton != EventChromeButton::None || m_hoverLevelEventId >= 0) {
            QToolTip::hideText();
        }
    }

    if (newBtn != m_hoverButton || newLevelId != m_hoverLevelEventId) {
        m_hoverButton = newBtn;
        m_hoverLevelEventId = newLevelId;
        update();
    }
}

int TimelineView::trackIndexAtY(int y) const
{
    if (!m_model || y < rulerHeight()) {
        return -1;
    }
    int ty = rulerHeight() - m_scrollY;
    for (int i = 0; i < m_model->tracks().size(); ++i) {
        const int h = m_model->tracks()[i].height;
        if (y >= ty && y < ty + h) {
            return i;
        }
        ty += h;
    }
    return -1;
}

int TimelineView::tracksBottomY() const
{
    if (!m_model) {
        return rulerHeight();
    }
    return trackY(m_model->tracks().size());
}

bool TimelineView::isBelowTracksDropZone(const QPoint &pos) const
{
    return pos.x() >= headerWidth() && pos.y() >= tracksBottomY();
}

bool TimelineView::isTrackHeaderDropZone(const QPoint &pos) const
{
    return pos.x() >= 0 && pos.x() < headerWidth() && pos.y() >= rulerHeight();
}

int TimelineView::dropTargetTrackIndex(const QPoint &pos) const
{
    if (isTrackHeaderDropZone(pos)) {
        const int ti = trackIndexAtY(pos.y());
        if (ti < 0) {
            return kDropCreateNewTracks;
        }
        return ti;
    }
    // Empty timeline body below last track → create new tracks (Vegas-like)
    if (isBelowTracksDropZone(pos)) {
        return kDropCreateNewTracks;
    }
    return trackIndexAtY(pos.y());
}

double TimelineView::dropTargetTimeSec(const QPoint &pos) const
{
    if (isTrackHeaderDropZone(pos)) {
        return 0.0;
    }
    return std::max(0.0, xToTime(pos.x()));
}

int TimelineView::reorderInsertIndexAtY(int y) const
{
    if (!m_model || m_model->tracks().isEmpty()) {
        return 0;
    }
    int ty = rulerHeight() - m_scrollY;
    for (int i = 0; i < m_model->tracks().size(); ++i) {
        const int h = m_model->tracks()[i].height;
        if (y < ty + h / 2) {
            return i;
        }
        ty += h;
    }
    return m_model->tracks().size();
}

bool TimelineView::isHeaderEmptyDragSpace(const QPoint &pos, int *outTrackIndex) const
{
    if (!m_model || pos.x() < 0 || pos.x() >= headerWidth() || pos.y() < rulerHeight()) {
        return false;
    }
    // Don't steal the bottom resize edge
    if (trackResizeIndexAt(pos) >= 0) {
        return false;
    }
    const int ti = trackIndexAtY(pos.y());
    if (ti < 0 || ti >= m_model->tracks().size()) {
        return false;
    }
    const Track &track = m_model->tracks()[ti];
    const int y = trackY(ti);
    const HeaderControls ctrls = headerControls(track, headerWidth(), railWidth(), y);
    for (const QRect &r : ctrls.all()) {
        if (r.contains(pos)) {
            return false;
        }
    }
    if (outTrackIndex) {
        *outTrackIndex = ti;
    }
    return true;
}

void TimelineView::moveTrack(int fromIndex, int insertBefore)
{
    if (!m_model) {
        return;
    }
    auto &tracks = m_model->tracks();
    if (fromIndex < 0 || fromIndex >= tracks.size()) {
        return;
    }
    int dest = insertBefore;
    if (dest > fromIndex) {
        --dest;
    }
    dest = std::clamp(dest, 0, int(tracks.size()) - 1);
    if (dest == fromIndex) {
        return;
    }
    if (m_renamingTrackIndex >= 0) {
        finishTrackRename(true);
    }
    tracks.move(fromIndex, dest);
    ensureContentWidth();
    update();
}

QRect TimelineView::trackNameRect(int trackIndex) const
{
    if (!m_model || trackIndex < 0 || trackIndex >= m_model->tracks().size()) {
        return {};
    }
    const int y = trackY(trackIndex);
    const int bodyL = railWidth() + 6;
    const int bodyR = headerWidth() - 24;
    return QRect(bodyL, y + 3, std::max(20, bodyR - bodyL - 2), 16);
}

void TimelineView::positionTrackNameEdit()
{
    if (!m_trackNameEdit || m_renamingTrackIndex < 0) {
        return;
    }
    const QRect r = trackNameRect(m_renamingTrackIndex);
    if (!r.isValid() || r.bottom() < rulerHeight() || r.top() > height()) {
        m_trackNameEdit->hide();
        return;
    }
    m_trackNameEdit->setGeometry(r.adjusted(-1, -1, 2, 1));
    if (!m_trackNameEdit->isVisible()) {
        m_trackNameEdit->show();
    }
}

void TimelineView::finishTrackRename(bool accept)
{
    if (m_renamingTrackIndex < 0) {
        return;
    }
    const int idx = m_renamingTrackIndex;
    QString text;
    if (m_trackNameEdit) {
        text = m_trackNameEdit->text().trimmed();
        m_trackNameEdit->blockSignals(true);
        m_trackNameEdit->hide();
        m_trackNameEdit->clearFocus();
        m_trackNameEdit->blockSignals(false);
    }
    m_renamingTrackIndex = -1;
    if (accept && m_model && idx >= 0 && idx < m_model->tracks().size() && !text.isEmpty()) {
        if (m_model->tracks()[idx].name != text) {
            emitDocumentEditBegan();
            m_model->tracks()[idx].name = text;
            emitDocumentEditCommitted(tr("Rename Track"));
        }
    }
    update();
}

void TimelineView::beginTrackRename(int trackIndex)
{
    if (!m_model || trackIndex < 0 || trackIndex >= m_model->tracks().size()) {
        return;
    }
    if (m_renamingTrackIndex >= 0 && m_renamingTrackIndex != trackIndex) {
        finishTrackRename(true);
    }
    m_renamingTrackIndex = trackIndex;
    if (!m_trackNameEdit) {
        m_trackNameEdit = new QLineEdit(this);
        m_trackNameEdit->setObjectName(QStringLiteral("trackNameEdit"));
        m_trackNameEdit->setStyleSheet(QStringLiteral(
            "QLineEdit#trackNameEdit {"
            "  background: #1a1a1a;"
            "  color: #f0f0f0;"
            "  border: 1px solid #0078d7;"
            "  selection-background-color: #0078d7;"
            "  selection-color: #ffffff;"
            "  padding: 0 2px;"
            "  font-size: 8pt;"
            "  font-weight: bold;"
            "}"));
        m_trackNameEdit->installEventFilter(this);
        connect(m_trackNameEdit, &QLineEdit::editingFinished, this, [this]() {
            finishTrackRename(true);
        });
        connect(m_trackNameEdit, &QLineEdit::returnPressed, this, [this]() {
            finishTrackRename(true);
        });
    }
    m_trackNameEdit->setText(m_model->tracks()[trackIndex].name);
    positionTrackNameEdit();
    m_trackNameEdit->show();
    m_trackNameEdit->raise();
    m_trackNameEdit->setFocus(Qt::OtherFocusReason);
    m_trackNameEdit->selectAll();
    update();
}

bool TimelineView::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_trackNameEdit && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Escape) {
            finishTrackRename(false);
            return true;
        }
    }
    if (watched == m_markerLabelEdit && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Escape) {
            finishMarkerRename(false);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

QRect TimelineView::markerHeadRect(const TimelineMarker &marker) const
{
    const int x = timeToX(marker.timeSec);
    // Keep head in the upper part of the lane so the loop strip stays visible below
    return QRect(x, 1, 14, 12);
}

QRect TimelineView::markerLabelRect(const TimelineMarker &marker) const
{
    const QRect head = markerHeadRect(marker);
    return QRect(head.right() + 3, head.top(), 160, head.height());
}

int TimelineView::markerAtPos(const QPoint &pos) const
{
    if (!m_model || pos.y() >= markerLaneHeight() || pos.x() < headerWidth()) {
        return -1;
    }
    // Prefer topmost / rightmost in overlap: scan reverse
    for (int i = m_model->markers().size() - 1; i >= 0; --i) {
        if (markerHeadRect(m_model->markers()[i]).adjusted(-2, -1, 2, 1).contains(pos)) {
            return m_model->markers()[i].id;
        }
    }
    return -1;
}

QRect TimelineView::loopBarRect() const
{
    if (!m_model || !m_model->hasLoopRegion()) {
        return {};
    }
    const LoopRegion &lr = m_model->loopRegion();
    const int x0 = timeToX(lr.startSec);
    const int x1 = timeToX(lr.endSec);
    const int left = std::min(x0, x1);
    const int w = std::max(1, std::abs(x1 - x0));
    // 6px blue strip along bottom of marker lane
    return QRect(left, markerLaneHeight() - 7, w, 6);
}

QRect TimelineView::loopSeedRect() const
{
    if (!m_model) {
        return {};
    }
    const int x = timeToX(m_model->loopRegion().startSec);
    return QRect(x, markerLaneHeight() - 12, 12, 12);
}

TimelineView::RulerDragMode TimelineView::loopHitAt(const QPoint &pos) const
{
    if (!m_model || pos.y() >= markerLaneHeight() || pos.x() < headerWidth()) {
        return RulerDragMode::None;
    }
    if (m_model->hasLoopRegion()) {
        const LoopRegion &lr = m_model->loopRegion();
        const int x0 = timeToX(lr.startSec);
        const int x1 = timeToX(lr.endSec);
        const QRect bar = loopBarRect().adjusted(0, -4, 0, 4);
        if (std::abs(pos.x() - x0) <= 8 && pos.y() >= bar.top() - 2 && pos.y() <= bar.bottom() + 4) {
            return RulerDragMode::LoopStart;
        }
        if (std::abs(pos.x() - x1) <= 8 && pos.y() >= bar.top() - 2 && pos.y() <= bar.bottom() + 4) {
            return RulerDragMode::LoopEnd;
        }
        if (bar.contains(pos)) {
            return RulerDragMode::LoopMove;
        }
    } else if (loopSeedRect().adjusted(-2, -2, 4, 2).contains(pos)) {
        return RulerDragMode::LoopCreate;
    }
    return RulerDragMode::None;
}

void TimelineView::insertMarkerAtPlayhead()
{
    if (!m_model) {
        return;
    }
    if (m_renamingMarkerId >= 0) {
        finishMarkerRename(true);
    }
    emitDocumentEditBegan();
    m_model->clearSelection();
    const int id = m_model->addMarkerAt(m_model->playheadSec());
    emitDocumentEditCommitted(tr("Insert Marker"));
    update();
    beginMarkerRename(id);
}

void TimelineView::insertLoopRegionAtPlayhead()
{
    if (!m_model) {
        return;
    }
    emitDocumentEditBegan();
    const double pps = std::max(1.0, m_model->pixelsPerSecond());
    double widthSec = 160.0 / pps;
    if (m_model->hasLoopRegion()) {
        widthSec = std::max(0.05, m_model->loopRegion().endSec - m_model->loopRegion().startSec);
    }
    const double start = std::max(0.0, m_model->playheadSec());
    m_model->setLoopRegion(start, start + widthSec);
    emitDocumentEditCommitted(tr("Loop Region"));
    ensureContentWidth();
    update();
}

void TimelineView::positionMarkerLabelEdit()
{
    if (!m_markerLabelEdit || m_renamingMarkerId < 0 || !m_model) {
        return;
    }
    const TimelineMarker *m = m_model->findMarker(m_renamingMarkerId);
    if (!m) {
        m_markerLabelEdit->hide();
        return;
    }
    const QRect r = markerLabelRect(*m);
    m_markerLabelEdit->setGeometry(r.left(), r.top(), std::max(72, r.width()), r.height());
    if (!m_markerLabelEdit->isVisible()) {
        m_markerLabelEdit->show();
    }
}

void TimelineView::finishMarkerRename(bool accept)
{
    if (m_renamingMarkerId < 0) {
        return;
    }
    const int id = m_renamingMarkerId;
    QString text;
    if (m_markerLabelEdit) {
        text = m_markerLabelEdit->text().trimmed();
        m_markerLabelEdit->blockSignals(true);
        m_markerLabelEdit->hide();
        m_markerLabelEdit->clearFocus();
        m_markerLabelEdit->blockSignals(false);
    }
    m_renamingMarkerId = -1;
    if (accept) {
        if (TimelineMarker *m = m_model ? m_model->findMarker(id) : nullptr) {
            if (m->label != text) {
                emitDocumentEditBegan();
                m->label = text;
                emitDocumentEditCommitted(tr("Rename Marker"));
            }
        }
    }
    setFocus(Qt::OtherFocusReason);
    update();
}

void TimelineView::beginMarkerRename(int markerId)
{
    if (!m_model || !m_model->findMarker(markerId)) {
        return;
    }
    if (m_renamingMarkerId >= 0 && m_renamingMarkerId != markerId) {
        finishMarkerRename(true);
    }
    if (m_renamingTrackIndex >= 0) {
        finishTrackRename(true);
    }
    m_renamingMarkerId = markerId;
    if (!m_markerLabelEdit) {
        m_markerLabelEdit = new QLineEdit(this);
        m_markerLabelEdit->setObjectName(QStringLiteral("markerLabelEdit"));
        m_markerLabelEdit->setStyleSheet(QStringLiteral(
            "QLineEdit#markerLabelEdit {"
            "  background: #ffffff;"
            "  color: #111111;"
            "  border: 1px solid #1a1a1a;"
            "  padding: 0 4px;"
            "  font-size: 9pt;"
            "}"));
        m_markerLabelEdit->installEventFilter(this);
        connect(m_markerLabelEdit, &QLineEdit::editingFinished, this, [this]() {
            finishMarkerRename(true);
        });
        connect(m_markerLabelEdit, &QLineEdit::returnPressed, this, [this]() {
            finishMarkerRename(true);
        });
    }
    const TimelineMarker *m = m_model->findMarker(markerId);
    m_markerLabelEdit->setText(m ? m->label : QString());
    positionMarkerLabelEdit();
    m_markerLabelEdit->show();
    m_markerLabelEdit->raise();
    m_markerLabelEdit->setFocus(Qt::OtherFocusReason);
    m_markerLabelEdit->selectAll();
    update();
}

double TimelineView::xToTime(int x) const
{
    const double pps = m_model ? m_model->pixelsPerSecond() : 40.0;
    return (x - headerWidth() + m_scrollX) / pps;
}

int TimelineView::timeToX(double sec) const
{
    const double pps = m_model ? m_model->pixelsPerSecond() : 40.0;
    return headerWidth() + static_cast<int>(sec * pps) - m_scrollX;
}

int TimelineView::trackY(int trackIndex) const
{
    int y = rulerHeight() - m_scrollY;
    if (!m_model) {
        return y;
    }
    for (int i = 0; i < trackIndex && i < m_model->tracks().size(); ++i) {
        y += m_model->tracks()[i].height;
    }
    return y;
}

QRect TimelineView::eventRect(const Track &track, const TrackEvent &ev, int trackTop) const
{
    const int x0 = timeToX(ev.startSec);
    const int x1 = timeToX(ev.startSec + ev.lengthSec);
    return QRect(x0, trackTop + 2, std::max(4, x1 - x0), track.height - 4);
}

std::optional<TimelineView::Hit> TimelineView::hitTest(const QPoint &pos) const
{
    if (!m_model || pos.y() < rulerHeight() || pos.x() < headerWidth()) {
        return std::nullopt;
    }
    int y = rulerHeight() - m_scrollY;
    for (int ti = 0; ti < m_model->tracks().size(); ++ti) {
        const Track &track = m_model->tracks()[ti];
        if (pos.y() >= y && pos.y() < y + track.height) {
            for (const TrackEvent &ev : track.events) {
                const QRect r = eventRect(track, ev, y);
                if (r.contains(pos)) {
                    return Hit{ti, ev.id, r};
                }
            }
            return Hit{ti, -1, {}};
        }
        y += track.height;
    }
    return std::nullopt;
}

void TimelineView::clampEventFades(TrackEvent &ev) const
{
    const double maxFade = std::max(0.0, ev.lengthSec - minEventLengthSec());
    ev.fadeInSec = std::clamp(ev.fadeInSec, 0.0, maxFade);
    ev.fadeOutSec = std::clamp(ev.fadeOutSec, 0.0, maxFade);
    if (ev.fadeInSec + ev.fadeOutSec > ev.lengthSec) {
        const double scale = ev.lengthSec / (ev.fadeInSec + ev.fadeOutSec);
        ev.fadeInSec *= scale;
        ev.fadeOutSec *= scale;
    }
}

int TimelineView::eventChromeLeftInset(const Track &track, const TrackEvent &ev) const
{
    if (!m_model) {
        return 0;
    }
    const double cfIn = incomingCrossfadeSec(track, ev);
    if (cfIn < 0.05) {
        return 0;
    }
    const double pps = m_model->pixelsPerSecond();
    return std::max(0, static_cast<int>(std::lround(cfIn * pps)));
}

int TimelineView::eventChromeRightInset(const Track &track, const TrackEvent &ev) const
{
    if (!m_model) {
        return 0;
    }
    const double cfOut = outgoingCrossfadeSec(track, ev);
    if (cfOut < 0.05) {
        return 0;
    }
    const double pps = m_model->pixelsPerSecond();
    return std::max(0, static_cast<int>(std::lround(cfOut * pps)));
}

QRect TimelineView::eventContentRect(const Track &track, const TrackEvent &ev, const QRect &r) const
{
    // Keep title / level / buttons out of crossfade zones:
    // left CF → chrome starts to the right of it; right CF → buttons sit just left of it.
    int left = eventChromeLeftInset(track, ev);
    int right = eventChromeRightInset(track, ev);
    const int minChrome = 28;
    if (left + right > std::max(0, r.width() - minChrome)) {
        // Prefer clearing the larger CF side when the event is too short.
        if (left >= right) {
            left = std::min(left, std::max(0, r.width() - minChrome));
            right = std::min(right, std::max(0, r.width() - minChrome - left));
        } else {
            right = std::min(right, std::max(0, r.width() - minChrome));
            left = std::min(left, std::max(0, r.width() - minChrome - right));
        }
    }
    return QRect(r.left() + left, r.top(), std::max(minChrome / 2, r.width() - left - right),
                 r.height());
}

QRect TimelineView::eventLevelBodyRect(const Track &track, const TrackEvent &ev,
                                       const QRect &r) const
{
    const QRect content = eventContentRect(track, ev, r);
    // Vegas-style travel: 100%/0 dB flush with clip top, 0%/−Inf on the bottom edge.
    const int top = content.top() + 1;
    const int bottom = content.bottom();
    if (bottom <= top + 2) {
        return QRect(content.left(), content.top(), content.width(),
                     std::max(4, content.height()));
    }
    return QRect(content.left(), top, content.width(), bottom - top + 1);
}

int TimelineView::levelYFromNormalized(const QRect &body, double n) const
{
    n = std::clamp(n, 0.0, 1.0);
    if (body.height() <= 1) {
        return body.top();
    }
    // n=1 → top (opacity 100% / gain 0 dB), n=0 → bottom (0% / −Inf)
    return body.top() + static_cast<int>(std::lround((1.0 - n) * (body.height() - 1)));
}

double TimelineView::normalizedFromLevelY(const QRect &body, int y) const
{
    if (body.height() <= 1) {
        return 1.0;
    }
    const double t =
        std::clamp(double(y - body.top()) / double(body.height() - 1), 0.0, 1.0);
    return 1.0 - t;
}

double TimelineView::eventLevelNormalized(const TrackEvent &ev) const
{
    if (isAudioFamily(ev.mediaKind)) {
        if (ev.gainDb <= kGainDbInfThreshold) {
            return 0.0;
        }
        // Linear amplitude 0…1 (Vegas SustainGain / envelope), not linear dB.
        // e.g. −12.6 dB → ~0.23 (lower third), −2 dB → ~0.80 (near top).
        const double amp = std::pow(10.0, ev.gainDb / 20.0);
        return std::clamp(amp, 0.0, 1.0);
    }
    return std::clamp(ev.opacity, 0.0, 1.0);
}

void TimelineView::setEventLevelFromNormalized(TrackEvent &ev, double n) const
{
    n = std::clamp(n, 0.0, 1.0);
    if (isAudioFamily(ev.mediaKind)) {
        if (n <= 0.001) {
            ev.gainDb = kGainDbMin; // treated as −Inf in UI
        } else if (n >= 0.999) {
            ev.gainDb = kGainDbMax; // 0 dB
        } else {
            ev.gainDb = 20.0 * std::log10(n);
            // Keep above the −Inf sentinel so tooltip shows a real dB value
            if (ev.gainDb <= kGainDbInfThreshold) {
                ev.gainDb = kGainDbInfThreshold + 0.1;
            }
        }
    } else {
        if (n <= 0.001) {
            ev.opacity = 0.0;
        } else if (n >= 0.999) {
            ev.opacity = 1.0;
        } else {
            ev.opacity = n;
        }
    }
}

QString TimelineView::eventLevelTooltip(const TrackEvent &ev) const
{
    if (isAudioFamily(ev.mediaKind)) {
        if (ev.gainDb <= kGainDbInfThreshold) {
            return tr("Gain is -Inf dB");
        }
        QString s = QString::number(ev.gainDb, 'f', 1);
        s.replace(QLatin1Char('.'), QLatin1Char(','));
        return tr("Gain is %1 dB").arg(s);
    }
    const int pct = static_cast<int>(std::lround(std::clamp(ev.opacity, 0.0, 1.0) * 100.0));
    return tr("Opacity is %1 %").arg(pct);
}

QRect TimelineView::eventLevelHandleRect(const Track &track, const TrackEvent &ev,
                                         const QRect &r) const
{
    const QRect body = eventLevelBodyRect(track, ev, r);
    if (body.width() < 20 || body.height() < 4) {
        return {};
    }
    const int y = levelYFromNormalized(body, eventLevelNormalized(ev));
    const int cx = body.center().x();
    return QRect(cx - kLevelHandleW / 2, y - kLevelHandleHalfH, kLevelHandleW,
                 kLevelHandleHalfH * 2);
}

QRect TimelineView::eventButtonRect(const Track &track, const TrackEvent &ev, const QRect &r,
                                    EventChromeButton button) const
{
    if (button == EventChromeButton::None) {
        return {};
    }
    const bool isVideo = isVideoFamily(ev.mediaKind);
    const QRect content = eventContentRect(track, ev, r);
    if (content.width() < 28 || content.height() < kEventTitleH + kEventBtn + 4) {
        return {};
    }
    const int y = content.bottom() - kEventBtn - 2;
    // Layout right→left: More, FX, [Pan/Crop for video]
    int xr = content.right() - 2;
    const QRect moreR(xr - kEventBtn, y, kEventBtn, kEventBtn);
    xr = moreR.left() - kEventBtnGap;
    const QRect fxR(xr - kEventBtn, y, kEventBtn, kEventBtn);
    xr = fxR.left() - kEventBtnGap;
    const QRect panR = isVideo ? QRect(xr - kEventBtn, y, kEventBtn, kEventBtn) : QRect();

    switch (button) {
    case EventChromeButton::More:
        return moreR;
    case EventChromeButton::Fx:
        return fxR;
    case EventChromeButton::PanCrop:
        return panR;
    default:
        return {};
    }
}

TimelineView::EventChromeButton TimelineView::eventButtonAt(const QPoint &pos, Hit *outHit) const
{
    auto hit = hitTest(pos);
    if (!hit || hit->eventId < 0 || !m_model) {
        return EventChromeButton::None;
    }
    const TrackEvent *ev = m_model->findEvent(hit->eventId);
    if (!ev || hit->trackIndex < 0 || hit->trackIndex >= m_model->tracks().size()) {
        return EventChromeButton::None;
    }
    const Track &track = m_model->tracks()[hit->trackIndex];
    const EventChromeButton order[] = {EventChromeButton::More, EventChromeButton::Fx,
                                       EventChromeButton::PanCrop};
    for (EventChromeButton b : order) {
        const QRect br = eventButtonRect(track, *ev, hit->eventRect, b);
        if (!br.isEmpty() && br.adjusted(-1, -1, 1, 1).contains(pos)) {
            if (outHit) {
                *outHit = *hit;
            }
            return b;
        }
    }
    return EventChromeButton::None;
}

void TimelineView::paintCropGlyph(QPainter &p, const QRect &r) const
{
    p.setPen(QPen(QColor(235, 235, 240), 1.25));
    const int m = 3;
    const int len = 4;
    // Top-left
    p.drawLine(r.left() + m, r.top() + m, r.left() + m + len, r.top() + m);
    p.drawLine(r.left() + m, r.top() + m, r.left() + m, r.top() + m + len);
    // Top-right
    p.drawLine(r.right() - m, r.top() + m, r.right() - m - len, r.top() + m);
    p.drawLine(r.right() - m, r.top() + m, r.right() - m, r.top() + m + len);
    // Bottom-left
    p.drawLine(r.left() + m, r.bottom() - m, r.left() + m + len, r.bottom() - m);
    p.drawLine(r.left() + m, r.bottom() - m, r.left() + m, r.bottom() - m - len);
    // Bottom-right
    p.drawLine(r.right() - m, r.bottom() - m, r.right() - m - len, r.bottom() - m);
    p.drawLine(r.right() - m, r.bottom() - m, r.right() - m, r.bottom() - m - len);
}

TimelineView::EventEditMode TimelineView::eventEditModeAt(const QPoint &pos, Hit *outHit) const
{
    auto hit = hitTest(pos);
    if (!hit || hit->eventId < 0 || !m_model) {
        return EventEditMode::None;
    }
    const TrackEvent *ev = m_model->findEvent(hit->eventId);
    if (!ev) {
        return EventEditMode::None;
    }
    if (outHit) {
        *outHit = *hit;
    }

    const QRect &r = hit->eventRect;
    if (hit->trackIndex >= 0 && hit->trackIndex < m_model->tracks().size()) {
        const Track &track = m_model->tracks()[hit->trackIndex];
        // Buttons win over level / fade / trim
        if (eventButtonAt(pos) != EventChromeButton::None) {
            return EventEditMode::None;
        }
        const QRect levelR = eventLevelHandleRect(track, *ev, r);
        if (!levelR.isEmpty() && levelR.adjusted(-3, -4, 3, 4).contains(pos)) {
            return EventEditMode::Level;
        }
        // Also allow grabbing the level line near the handle's Y across content
        const QRect body = eventLevelBodyRect(track, *ev, r);
        if (body.width() > 24 && body.adjusted(0, -3, 0, 3).contains(pos)) {
            const int levelY = levelYFromNormalized(body, eventLevelNormalized(*ev));
            if (std::abs(pos.y() - levelY) <= 4
                && pos.x() >= body.left() + 8 && pos.x() <= body.right() - 8) {
                return EventEditMode::Level;
            }
        }
    }

    const int titleH = kEventTitleH;
    const int topBand = titleH + 10; // title + top of body — fade zone
    const bool inTopBand = pos.y() <= r.top() + topBand;
    const double pps = m_model->pixelsPerSecond();
    const int fadeInPx = static_cast<int>(std::lround(ev->fadeInSec * pps));
    const int fadeOutPx = static_cast<int>(std::lround(ev->fadeOutSec * pps));

    // Fade handles / create from top corners (priority over trim)
    if (inTopBand) {
        // Existing fade-in handle near inner edge of fade
        if (ev->fadeInSec > 0.02) {
            const int hx = r.left() + std::min(fadeInPx, r.width() - 1);
            if (std::abs(pos.x() - hx) <= fadeCornerHitPad()) {
                return EventEditMode::FadeIn;
            }
        }
        // Existing fade-out handle
        if (ev->fadeOutSec > 0.02) {
            const int hx = r.right() - std::min(fadeOutPx, r.width() - 1);
            if (std::abs(pos.x() - hx) <= fadeCornerHitPad()) {
                return EventEditMode::FadeOut;
            }
        }
        // Create fade from top-left / top-right corners
        if (pos.x() <= r.left() + fadeCornerHitPad()) {
            return EventEditMode::FadeIn;
        }
        if (pos.x() >= r.right() - fadeCornerHitPad()) {
            return EventEditMode::FadeOut;
        }
        // Dragging inside an existing fade region also edits that fade
        if (ev->fadeInSec > 0.02 && pos.x() <= r.left() + fadeInPx + 2) {
            return EventEditMode::FadeIn;
        }
        if (ev->fadeOutSec > 0.02 && pos.x() >= r.right() - fadeOutPx - 2) {
            return EventEditMode::FadeOut;
        }
    }

    // Trim left / right edges
    if (pos.x() <= r.left() + eventEdgeHitPad()) {
        return EventEditMode::TrimStart;
    }
    if (pos.x() >= r.right() - eventEdgeHitPad()) {
        return EventEditMode::TrimEnd;
    }
    return EventEditMode::None;
}

void TimelineView::paintMiniButton(QPainter &p, const QRect &r, const QString &text, bool active,
                                   const QColor &activeBg)
{
    p.setPen(QColor(0x3a, 0x3a, 0x3a));
    p.setBrush(active ? activeBg : QColor(0x1a, 0x1a, 0x1a));
    p.drawRect(r);
    p.setPen(active ? QColor(0xff, 0xff, 0xff) : QColor(0xc8, 0xc8, 0xc8));
    QFont f = p.font();
    f.setPointSize(7);
    f.setBold(true);
    p.setFont(f);
    p.drawText(r, Qt::AlignCenter, text);
}

void TimelineView::paintSlider(QPainter &p, const QRect &r, const QString &label, const QString &value,
                               double normalized)
{
    const int labelW = 28;
    p.setPen(QColor(0xa8, 0xa8, 0xa8));
    QFont f = p.font();
    f.setPointSize(7);
    f.setBold(false);
    p.setFont(f);
    p.drawText(QRect(r.left(), r.top(), labelW, r.height()), Qt::AlignVCenter | Qt::AlignLeft, label);

    const QRect track(r.left() + labelW, r.center().y() - 2, r.width() - labelW - 52, 4);
    p.fillRect(track, QColor(0x14, 0x14, 0x14));
    p.setPen(QColor(0x40, 0x40, 0x40));
    p.drawRect(track);

    const double n = std::clamp(normalized, 0.0, 1.0);
    const int knobX = track.left() + static_cast<int>(n * track.width());
    p.fillRect(QRect(track.left(), track.top(), knobX - track.left(), track.height()),
               QColor(0x5a, 0x5a, 0x5a));
    p.setBrush(QColor(0xd0, 0xd0, 0xd0));
    p.setPen(QColor(0x88, 0x88, 0x88));
    p.drawRect(QRect(knobX - 3, track.center().y() - 5, 6, 10));

    p.setPen(QColor(0xc0, 0xc0, 0xc0));
    p.drawText(QRect(r.right() - 50, r.top(), 50, r.height()), Qt::AlignVCenter | Qt::AlignRight, value);
}

void TimelineView::paintTrackHeader(QPainter &p, const Track &track, int index, int y)
{
    const QRect header(0, y, headerWidth(), track.height);
    const bool audible = m_model && m_model->isTrackAudible(index);
    QLinearGradient bg(0, y, 0, y + track.height);
    if (track.muted) {
        bg.setColorAt(0.0, QColor(0x26, 0x26, 0x26));
        bg.setColorAt(1.0, QColor(0x1c, 0x1c, 0x1c));
    } else {
        bg.setColorAt(0.0, QColor(0x30, 0x30, 0x30));
        bg.setColorAt(1.0, QColor(0x28, 0x28, 0x28));
    }
    p.fillRect(header, bg);
    if (index == m_reorderingTrack || index == m_hoverReorderTrack) {
        p.fillRect(header, QColor(0x00, 0x78, 0xd7, index == m_reorderingTrack ? 70 : 35));
    }
    p.setPen(QColor(0x0a, 0x0a, 0x0a));
    p.drawLine(0, y + track.height - 1, headerWidth(), y + track.height - 1);

    const bool isVideo = track.kind == TrackKind::Video;
    const QRect rail(0, y, railWidth(), track.height);
    QLinearGradient railGrad(0, y, 0, y + track.height);
    if (isVideo) {
        railGrad.setColorAt(0.0, QColor(0x7a, 0x5a, 0xa0));
        railGrad.setColorAt(1.0, videoRail());
    } else {
        railGrad.setColorAt(0.0, QColor(0xb4, 0x5a, 0x88));
        railGrad.setColorAt(1.0, audioRail());
    }
    p.fillRect(rail, railGrad);
    p.setPen(QColor(0, 0, 0, 115));
    p.drawLine(rail.right(), y, rail.right(), y + track.height);

    // Track number badge
    const QRect num(rail.center().x() - 8, y + 6, 16, 16);
    p.setBrush(QColor(0, 0, 0, 90));
    p.setPen(QColor(255, 255, 255, 50));
    p.drawRect(num);
    p.setPen(Qt::white);
    QFont nf = p.font();
    nf.setPointSize(8);
    nf.setBold(true);
    p.setFont(nf);
    p.drawText(num, Qt::AlignCenter, QString::number(index + 1));

    // Type glyph
    p.setPen(QColor(255, 255, 255, 210));
    if (isVideo) {
        p.drawRect(QRect(rail.center().x() - 5, y + 28, 10, 7));
        p.drawLine(rail.center().x() + 5, y + 30, rail.center().x() + 8, y + 28);
        p.drawLine(rail.center().x() + 5, y + 33, rail.center().x() + 8, y + 35);
    } else {
        p.drawEllipse(QRect(rail.center().x() - 5, y + 28, 10, 10));
        p.drawLine(rail.center().x() + 5, y + 33, rail.center().x() + 9, y + 29);
        p.drawLine(rail.center().x() + 5, y + 33, rail.center().x() + 9, y + 37);
    }

    const int bodyL = railWidth() + 6;
    const int bodyR = headerWidth() - 24;
    const HeaderControls ctrls = headerControls(track, headerWidth(), railWidth(), y);

    // Vegas-style blue "…" More button (audio + video)
    {
        const QRect r = ctrls.chipMenu;
        p.setPen(QColor(0x1a, 0x5a, 0xa0));
        p.setBrush(QColor(0x2a, 0x78, 0xc8));
        p.drawRoundedRect(r, 2, 2);
        p.setPen(Qt::white);
        p.setBrush(Qt::white);
        const int cy = r.center().y();
        const int cx = r.center().x();
        for (int dy : {-4, 0, 4}) {
            p.drawEllipse(QPoint(cx, cy + dy), 1, 1);
        }
    }
    // Compact "fx" chip → Track FX
    {
        const QRect r = ctrls.chipFx;
        bool hasFx = false;
        if (isVideo) {
            for (const FxSlot &s : track.fxChain) {
                if (s.displayName.compare(QStringLiteral("Pan/Crop"), Qt::CaseInsensitive) == 0) {
                    continue;
                }
                hasFx = true;
                break;
            }
        } else {
            hasFx = !track.fxChain.isEmpty();
        }
        p.setPen(QColor(0x3a, 0x3a, 0x3a));
        p.setBrush(hasFx ? QColor(0x3a, 0x28, 0x48) : QColor(0x1f, 0x1f, 0x1f));
        p.drawRoundedRect(r, 2, 2);
        p.setPen(hasFx ? QColor(0xd0, 0xb0, 0xff) : QColor(0xb8, 0xb8, 0xb8));
        QFont cf = p.font();
        cf.setPointSize(6);
        cf.setBold(true);
        p.setFont(cf);
        p.drawText(r, Qt::AlignCenter, QStringLiteral("fx"));
    }
    // Name to the right of chips
    if (index != m_renamingTrackIndex) {
        p.setPen(QColor(255, 255, 255, 225));
        QFont nameF = p.font();
        nameF.setPointSize(8);
        nameF.setBold(true);
        p.setFont(nameF);
        p.drawText(QRect(bodyL + 46, y + 3, bodyR - bodyL - 48, 16),
                   Qt::AlignVCenter | Qt::AlignLeft, track.name);
    }

    paintMiniButton(p, ctrls.mute, QStringLiteral("M"), track.muted, QColor(0x2a, 0x78, 0xc8));
    paintMiniButton(p, ctrls.solo, QStringLiteral("S"), track.solo, QColor(0x2a, 0x78, 0xc8));

    // Sliders — MUTED / Off (solo) feedback like Vegas
    if (isVideo) {
        if (track.muted) {
            p.setPen(QColor(0xf0, 0xd0, 0x40));
            QFont mf = p.font();
            mf.setPointSize(8);
            mf.setBold(true);
            p.setFont(mf);
            p.drawText(QRect(bodyL, y + track.height - 22, bodyR - bodyL, 16),
                       Qt::AlignVCenter | Qt::AlignLeft, tr("MUTED"));
        } else if (!audible) {
            paintSlider(p, QRect(bodyL, y + track.height - 22, bodyR - bodyL, 16),
                        QStringLiteral("Level"), tr("Off (solo)"), 0.0);
        } else {
            paintSlider(p, QRect(bodyL, y + track.height - 22, bodyR - bodyL, 16),
                        QStringLiteral("Level"), QStringLiteral("100.0%"), 1.0);
        }
    } else {
        if (track.muted) {
            p.setPen(QColor(0xf0, 0xd0, 0x40));
            QFont mf = p.font();
            mf.setPointSize(8);
            mf.setBold(true);
            p.setFont(mf);
            p.drawText(QRect(bodyL, y + track.height - 36, bodyR - bodyL, 15),
                       Qt::AlignVCenter | Qt::AlignLeft, tr("MUTED"));
            paintSlider(p, QRect(bodyL, y + track.height - 20, bodyR - bodyL, 15),
                        QStringLiteral("Pan"), QStringLiteral("Center"), 0.5);
        } else if (!audible) {
            paintSlider(p, QRect(bodyL, y + track.height - 36, bodyR - bodyL, 15),
                        QStringLiteral("Vol"), tr("Off (solo)"), 0.0);
            paintSlider(p, QRect(bodyL, y + track.height - 20, bodyR - bodyL, 15),
                        QStringLiteral("Pan"), QStringLiteral("Center"), 0.5);
        } else {
            paintSlider(p, QRect(bodyL, y + track.height - 36, bodyR - bodyL, 15),
                        QStringLiteral("Vol"), QStringLiteral("0.0 dB"), 0.75);
            paintSlider(p, QRect(bodyL, y + track.height - 20, bodyR - bodyL, 15),
                        QStringLiteral("Pan"), QStringLiteral("Center"), 0.5);
        }
    }
}

void TimelineView::paintVideoThumbs(QPainter &p, const QRect &body, const TrackEvent &ev)
{
    p.fillRect(body, QColor(0x1c, 0x16, 0x22));
    if (body.width() < 2 || body.height() < 2) {
        return;
    }

    const QString path = eventMediaPath(ev);
    const double ar = (m_model && m_model->frameHeight() > 0)
                          ? double(m_model->frameWidth()) / double(m_model->frameHeight())
                          : 16.0 / 9.0;
    const int cellH = body.height();
    const int cellW = std::clamp(int(std::lround(cellH * ar)), 28, 160);
    const double pps = (m_model && m_model->pixelsPerSecond() > 1.0) ? m_model->pixelsPerSecond()
                                                                    : 40.0;

    QPixmap poster;
    if (!path.isEmpty()) {
        poster = MediaFilmstripCache::instance().posterIfReady(path, QSize(cellW, cellH));
    }

    int x = body.left();
    int i = 0;
    while (x < body.right()) {
        const int w = std::min(cellW, body.right() - x);
        const QRect cell(x, body.top(), w, cellH);

        // Media time at tile center (events currently start at stream 0).
        const double mediaSec =
            std::clamp((x + w * 0.5 - body.left()) / pps, 0.0, std::max(0.0, ev.lengthSec - 1e-3));

        QPixmap tile;
        if (!path.isEmpty()) {
            tile = MediaFilmstripCache::instance().frameIfReady(path, mediaSec, QSize(cellW, cellH));
        }

        if (!tile.isNull()) {
            p.drawPixmap(cell, tile, QRect(0, 0, tile.width(), tile.height()));
        } else if (!poster.isNull()) {
            // Interim: Shell/poster until time-accurate frames land
            p.drawPixmap(cell, poster);
        } else {
            const int seed = ev.id * 17 + static_cast<int>(ev.startSec * 10);
            const int hue = (seed * 37 + i * 47) % 360;
            p.fillRect(cell, QColor::fromHsv(hue, 60, 40));
        }

        // Thin Vegas-style tile separators
        p.setPen(QColor(0, 0, 0, 140));
        p.drawLine(cell.right(), cell.top(), cell.right(), cell.bottom());
        x += cellW;
        ++i;
    }
}

void TimelineView::paintStillImage(QPainter &p, const QRect &body, const TrackEvent &ev)
{
    if (body.width() < 2 || body.height() < 2) {
        return;
    }
    p.fillRect(body, QColor(0x2a, 0x2a, 0x2a));

    const QString path = eventMediaPath(ev);
    if (path.isEmpty()) {
        return;
    }

    // Cache-backed load (sync for images, async refresh on ready)
    QPixmap pm =
        MediaThumbCache::instance().pixmapIfReady(path, body.size(), QStringLiteral("still"));
    if (pm.isNull()) {
        return;
    }

    const QSize fit = pm.size().scaled(body.size(), Qt::KeepAspectRatioByExpanding);
    QPixmap drawn = pm.scaled(fit, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const int ox = (fit.width() - body.width()) / 2;
    const int oy = (fit.height() - body.height()) / 2;
    p.drawPixmap(body, drawn, QRect(ox, oy, body.width(), body.height()));
}

void TimelineView::paintAudioWave(QPainter &p, const QRect &body, const TrackEvent &ev)
{
    // Vegas peach/salmon lane under dark waveform
    p.fillRect(body, audioEvent());
    if (body.width() < 2 || body.height() < 4) {
        return;
    }

    const int mid = body.center().y();
    const int chH = body.height() / 2;
    const int eventCh = ev.channelCount > 0 ? ev.channelCount : 2;
    const int firstCh = std::max(0, ev.firstChannel);
    const bool stereoUi = eventCh >= 2;

    p.setPen(QColor(0x80, 0x58, 0x48, 160));
    if (stereoUi) {
        p.drawLine(body.left(), mid, body.right(), mid);
        p.setPen(QColor(0x90, 0x68, 0x58, 80));
        p.drawLine(body.left(), body.top() + chH / 2, body.right(), body.top() + chH / 2);
        p.drawLine(body.left(), mid + chH / 2, body.right(), mid + chH / 2);
    } else {
        p.setPen(QColor(0x90, 0x68, 0x58, 80));
        p.drawLine(body.left(), mid, body.right(), mid);
    }

    double scale = 0.0;
    if (ev.gainDb > kGainDbInfThreshold) {
        scale = std::clamp(std::pow(10.0, ev.gainDb / 20.0), 0.0, 1.0);
    }
    if (scale < 0.008) {
        return;
    }

    const QString path = eventMediaPath(ev);
    WaveformPeaks peaks;
    if (!path.isEmpty()) {
        peaks = MediaWaveformCache::instance().peaksFor(path);
    }

    if (!peaks.isValid()) {
        // Procedural fallback while peaks load / if unavailable
        const int seed = ev.id * 31 + static_cast<int>(ev.lengthSec * 7) + firstCh * 13;
        auto sampleAmp = [&](int x, int phase) -> double {
            const double t = (x + seed * 3 + phase) * 0.072;
            const double env = 0.28 + 0.72 * std::abs(std::sin((x + seed * 5) * 0.009));
            return scale * env
                   * (0.50 * std::sin(t) + 0.28 * std::sin(t * 2.17) + 0.14 * std::sin(t * 5.3));
        };
        auto drawChannel = [&](int top, int height, int phase) {
            QPainterPath path;
            const int cy = top + height / 2;
            const double ampMax = height * 0.44;
            path.moveTo(body.left(), cy);
            for (int x = body.left(); x <= body.right(); x += 2) {
                path.lineTo(x, cy - sampleAmp(x, phase) * ampMax);
            }
            for (int x = body.right(); x >= body.left(); x -= 2) {
                path.lineTo(x, cy + sampleAmp(x, phase + 17) * ampMax * 0.92);
            }
            path.closeSubpath();
            p.setBrush(audioWave());
            p.setPen(QPen(audioWaveStroke(), 1));
            p.drawPath(path);
        };
        p.setRenderHint(QPainter::Antialiasing, true);
        if (stereoUi) {
            drawChannel(body.top(), chH, 0);
            drawChannel(mid, body.height() - chH, 41);
        } else {
            drawChannel(body.top(), body.height(), 0);
        }
        p.setRenderHint(QPainter::Antialiasing, false);
        return;
    }

    const int drawCh = stereoUi ? 2 : 1;
    const double dur = peaks.durationSec > 0.05 ? peaks.durationSec : std::max(0.05, ev.lengthSec);
    const double t0 = 0.0;
    const double t1 = std::min(ev.lengthSec, dur);

    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QPen(audioWave(), 1));
    for (int uiCh = 0; uiCh < drawCh; ++uiCh) {
        const int srcCh = firstCh + uiCh;
        if (srcCh >= peaks.channels) {
            break;
        }
        const int top = stereoUi ? ((uiCh == 0) ? body.top() : mid) : body.top();
        const int height = stereoUi ? ((uiCh == 0) ? chH : (body.height() - chH)) : body.height();
        const int cy = top + height / 2;
        const double ampMax = height * 0.46 * scale;
        for (int x = body.left(); x <= body.right(); ++x) {
            const double u = (body.width() <= 1)
                                 ? 0.0
                                 : double(x - body.left()) / double(body.width() - 1);
            const double t = t0 + u * (t1 - t0);
            const int bin = std::clamp(int(std::floor((t / dur) * peaks.bins)), 0, peaks.bins - 1);
            const int idx = (bin * peaks.channels + srcCh) * 2;
            if (idx + 1 >= peaks.minMax.size()) {
                continue;
            }
            const double mn = peaks.minMax[idx] / 32768.0;
            const double mx = peaks.minMax[idx + 1] / 32768.0;
            const int y0 = cy - int(std::lround(mx * ampMax));
            const int y1 = cy - int(std::lround(mn * ampMax));
            p.drawLine(x, y0, x, y1);
        }
    }
}

QString TimelineView::eventMediaPath(const TrackEvent &ev) const
{
    if (!m_model) {
        return ev.mediaPath;
    }
    return m_model->mediaPathForEvent(ev);
}

void TimelineView::paintEventBlock(QPainter &p, const Track &track, const TrackEvent &ev, const QRect &r)
{
    const bool isVideo = isVideoFamily(ev.mediaKind);
    const bool isStill = ev.mediaKind == EventMediaKind::Still || ev.mediaKind == EventMediaKind::Title;
    // Video/still: filmstrip fills the block; audio keeps a small title strip.
    const int titleH = isVideo ? 0 : kEventTitleH;
    const int nameOverlayH = isVideo ? kVideoNameOverlayH : 0;

    p.fillRect(r, isVideo ? videoEvent() : audioEvent());

    // Left color rail
    p.fillRect(QRect(r.left(), r.top(), 3, r.height()), isVideo ? videoRail() : audioRail());

    const QRect content = eventContentRect(track, ev, r);

    if (!isVideo) {
        // Audio title strip (slightly darker peach for contrast)
        QLinearGradient tg(0, r.top(), 0, r.top() + titleH);
        tg.setColorAt(0.0, QColor(0xec, 0xc8, 0xb8));
        tg.setColorAt(1.0, audioTitle());
        p.fillRect(QRect(r.left(), r.top(), r.width(), titleH), tg);
        p.setPen(QColor(0x80, 0x58, 0x48, 100));
        p.drawLine(r.left(), r.top() + titleH, r.right(), r.top() + titleH);

        p.setPen(QColor(0x2a, 0x18, 0x20));
        QFont tf = p.font();
        tf.setPointSize(8);
        tf.setBold(true);
        p.setFont(tf);
        QString titleText = ev.name;
        const int n = ev.channelCount > 0 ? ev.channelCount : 2;
        const int a = ev.firstChannel + 1;
        if (n <= 1) {
            titleText += QStringLiteral("  [Channel %1]").arg(a);
        } else if (n == 2) {
            titleText += QStringLiteral("  [Channels %1/%2]").arg(a).arg(a + 1);
        } else {
            titleText += QStringLiteral("  [Channels %1–%2]").arg(a).arg(a + n - 1);
        }
        const QRect title(content.left(), content.top(), content.width(), titleH);
        const QString elided =
            QFontMetrics(tf).elidedText(titleText, Qt::ElideRight, std::max(8, title.width() - 10));
        p.drawText(title.adjusted(5, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, elided);
    }

    // Group badge (Vegas-style linked A/V indicator)
    if (ev.groupId > 0 && m_model && !m_model->ignoreEventGrouping()) {
        const QColor gc = QColor::fromHsv((ev.groupId * 47) % 360, 160, 220);
        p.setBrush(gc);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRect(content.right() - 11, content.top() + 2, 8, 8), 1, 1);
        p.setPen(QColor(255, 255, 255, 220));
        QFont gf = p.font();
        gf.setPointSize(6);
        gf.setBold(true);
        p.setFont(gf);
        p.drawText(QRect(content.right() - 11, content.top() + 1, 8, 9), Qt::AlignCenter,
                   QStringLiteral("G"));
    }

    const QRect body(r.left(), r.top() + titleH, r.width(), r.height() - titleH);
    if (isStill) {
        paintStillImage(p, body, ev);
    } else if (isVideo) {
        paintVideoThumbs(p, body, ev);
        // Filename overlay on filmstrip (Vegas)
        if (body.width() > 8 && nameOverlayH > 0) {
            const QRect nameBand(body.left(), body.top(), body.width(),
                                 std::min(nameOverlayH, body.height()));
            p.fillRect(nameBand, QColor(0x14, 0x10, 0x1a, 170));
            p.setPen(QColor(255, 255, 255, 235));
            QFont tf = p.font();
            tf.setPointSize(8);
            tf.setBold(true);
            p.setFont(tf);
            const QString elided = QFontMetrics(tf).elidedText(
                ev.name, Qt::ElideRight, std::max(8, nameBand.width() - 12));
            p.drawText(nameBand.adjusted(6, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, elided);
        }
    } else {
        paintAudioWave(p, body, ev);
    }

    // Selection / border (single Vegas-like amber outline)
    if (ev.selected) {
        p.setPen(QPen(eventSel(), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(r.adjusted(0, 0, -1, -1));
    } else {
        p.setPen(QColor(0, 0, 0, 150));
        p.setBrush(Qt::NoBrush);
        p.drawRect(r.adjusted(0, 0, -1, -1));
    }

    // Trim handles hint when selected
    if (ev.selected && r.width() > 12) {
        p.fillRect(QRect(r.left(), r.top(), 4, r.height()), QColor(255, 255, 255, 140));
        p.fillRect(QRect(r.right() - 4, r.top(), 4, r.height()), QColor(255, 255, 255, 140));
        const int gripTop = r.top() + (isVideo ? 1 : titleH + 1);
        p.setBrush(QColor(120, 170, 240));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRect(r.left() + 1, gripTop, 7, 5), 1, 1);
        p.drawRoundedRect(QRect(r.right() - 8, gripTop, 7, 5), 1, 1);
    }

    paintEventFades(p, track, ev, r);
    paintEventChrome(p, track, ev, r);
}

void TimelineView::paintEventChrome(QPainter &p, const Track &track, const TrackEvent &ev,
                                    const QRect &r)
{
    const QRect content = eventContentRect(track, ev, r);
    const QRect body = eventLevelBodyRect(track, ev, r);
    if (body.width() < 28 || body.height() < 4) {
        return;
    }

    const double n = eventLevelNormalized(ev);
    const int levelY = levelYFromNormalized(body, n);

    // Level line across usable content
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(levelLine(), 1.0));
    p.drawLine(content.left() + 2, levelY, content.right() - 2, levelY);

    // Blue trapezoid-ish handle (centered on usable region)
    const QRect handle = eventLevelHandleRect(track, ev, r);
    if (!handle.isEmpty()) {
        QPainterPath tip;
        const QRectF hf = handle;
        tip.moveTo(hf.left() + 1, hf.top() + 1);
        tip.lineTo(hf.right() - 1, hf.top() + 1);
        tip.lineTo(hf.right() - 2, hf.bottom() - 1);
        tip.lineTo(hf.left() + 2, hf.bottom() - 1);
        tip.closeSubpath();
        p.setBrush(levelHandle());
        p.setPen(QPen(levelHandleBorder(), 1.0));
        p.drawPath(tip);
    }

    // Button strip (bottom-right of usable content)
    const bool isVideo = isVideoFamily(ev.mediaKind);
    auto paintBtnBg = [&](const QRect &br, bool hot) {
        if (br.isEmpty()) {
            return;
        }
        p.setPen(QColor(0, 0, 0, 120));
        p.setBrush(hot ? QColor(40, 40, 50, 220) : QColor(20, 20, 28, 180));
        p.drawRoundedRect(br, 2, 2);
    };

    const EventChromeButton buttons[] = {EventChromeButton::PanCrop, EventChromeButton::Fx,
                                         EventChromeButton::More};
    for (EventChromeButton b : buttons) {
        if (b == EventChromeButton::PanCrop && !isVideo) {
            continue;
        }
        const QRect br = eventButtonRect(track, ev, r, b);
        if (br.isEmpty()) {
            continue;
        }
        const bool hot = (m_hoverButton == b && m_hoverLevelEventId == ev.id)
                         || (m_hoverButton == b && m_dragEventId == ev.id);
        paintBtnBg(br, hot || (m_hoverButton == b));
        if (b == EventChromeButton::PanCrop) {
            paintCropGlyph(p, br);
        } else if (b == EventChromeButton::Fx) {
            p.setPen(QColor(240, 240, 245));
            QFont f = p.font();
            f.setPointSize(8);
            f.setBold(true);
            f.setFamily(QStringLiteral("Segoe UI"));
            p.setFont(f);
            p.drawText(br, Qt::AlignCenter, QStringLiteral("fx"));

            // Vegas-style blue dot: audio any FX; video any FX besides Pan/Crop
            bool showDot = false;
            for (const FxSlot &s : ev.fxChain) {
                if (isVideo
                    && s.displayName.compare(QStringLiteral("Pan/Crop"), Qt::CaseInsensitive) == 0) {
                    continue;
                }
                showDot = true;
                break;
            }
            if (showDot) {
                const int d = 5;
                const QRect dot(br.right() - d - 1, br.bottom() - d - 1, d, d);
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(0x4a, 0xc0, 0xff));
                p.drawEllipse(dot);
            }
        } else {
            p.setPen(QColor(240, 240, 245));
            QFont f = p.font();
            f.setPointSize(9);
            f.setBold(true);
            p.setFont(f);
            p.drawText(br, Qt::AlignCenter, QStringLiteral("…"));
        }
    }
    p.setRenderHint(QPainter::Antialiasing, false);
}

double TimelineView::overlapSec(const TrackEvent &a, const TrackEvent &b) const
{
    const double a0 = a.startSec;
    const double a1 = a.startSec + a.lengthSec;
    const double b0 = b.startSec;
    const double b1 = b.startSec + b.lengthSec;
    const double o0 = std::max(a0, b0);
    const double o1 = std::min(a1, b1);
    return o1 > o0 ? (o1 - o0) : 0.0;
}

double TimelineView::incomingCrossfadeSec(const Track &track, const TrackEvent &ev) const
{
    double best = 0.0;
    for (const TrackEvent &other : track.events) {
        if (other.id == ev.id) {
            continue;
        }
        // Other starts earlier and overlaps into our start → CF on our fade-in edge
        if (other.startSec < ev.startSec - 1e-6) {
            best = std::max(best, overlapSec(other, ev));
        }
    }
    return best;
}

double TimelineView::outgoingCrossfadeSec(const Track &track, const TrackEvent &ev) const
{
    double best = 0.0;
    for (const TrackEvent &other : track.events) {
        if (other.id == ev.id) {
            continue;
        }
        // Other starts later and overlaps our end → CF on our fade-out edge
        if (other.startSec > ev.startSec + 1e-6) {
            best = std::max(best, overlapSec(ev, other));
        }
    }
    return best;
}

void TimelineView::paintEventFades(QPainter &p, const Track &track, const TrackEvent &ev, const QRect &r)
{
    const bool isAudio = isAudioFamily(ev.mediaKind);
    // Video fades span the full filmstrip (name is only an overlay).
    const int titleH = isVideoFamily(ev.mediaKind) ? 0 : kEventTitleH;
    const QRectF body(r.left(), r.top() + titleH, r.width(), std::max(1, r.height() - titleH));
    const double pps = m_model ? m_model->pixelsPerSecond() : 40.0;
    const double cfIn = incomingCrossfadeSec(track, ev);
    const double cfOut = outgoingCrossfadeSec(track, ev);

    // Solo fade-in (hidden when this edge is part of a crossfade)
    if (ev.fadeInSec > 0.05 && cfIn < 0.05) {
        const double w = std::min(ev.fadeInSec * pps, body.width());
        paintFadeRegion(p, QRectF(body.left(), body.top(), w, body.height()), true, isAudio,
                        ev.fadeInCurve);
    }

    // Solo fade-out
    if (ev.fadeOutSec > 0.05 && cfOut < 0.05) {
        const double w = std::min(ev.fadeOutSec * pps, body.width());
        paintFadeRegion(p, QRectF(body.right() - w, body.top(), w, body.height()), false, isAudio,
                        ev.fadeOutCurve);
    }
}

void TimelineView::paintCrossfades(QPainter &p, const Track &track, int trackTop)
{
    if (track.events.size() < 2) {
        return;
    }

    QVector<const TrackEvent *> sorted;
    sorted.reserve(track.events.size());
    for (const TrackEvent &ev : track.events) {
        sorted.push_back(&ev);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const TrackEvent *a, const TrackEvent *b) { return a->startSec < b->startSec; });

    const bool videoTrack = track.kind == TrackKind::Video;
    const int titleH = videoTrack ? 0 : kEventTitleH;
    const double pps = m_model ? m_model->pixelsPerSecond() : 40.0;

    for (int i = 0; i + 1 < sorted.size(); ++i) {
        const TrackEvent &a = *sorted[i];
        const TrackEvent &b = *sorted[i + 1];
        const double ov = overlapSec(a, b);
        if (ov < 0.05) {
            continue; // hard cut — no X
        }

        const double zoneStart = std::max(a.startSec, b.startSec);
        const int x0 = timeToX(zoneStart);
        const int w = std::max(2, static_cast<int>(std::lround(ov * pps)));
        const QRect zone(x0, trackTop + 2, w, track.height - 4);
        if (zone.right() < headerWidth() || zone.left() > width()) {
            continue;
        }

        const int bodyTopInset = titleH > 0 ? titleH - 2 : 0;
        const QRectF body(zone.left(), zone.top() + bodyTopInset, zone.width(),
                          std::max(2.0, double(zone.height() - bodyTopInset)));

        // Optional stipple behind X on audio
        if (track.kind == TrackKind::Audio) {
            p.fillRect(body.toRect(), QColor(0, 0, 0, 40));
            p.fillRect(body.toRect(), QBrush(QColor(15, 15, 22, 140), Qt::Dense6Pattern));
        }

        p.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(crossfadeStroke(), 1.65);
        pen.setCosmetic(true);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawPath(fadeCurveLinePath(body, a.fadeOutCurve, false));
        p.drawPath(fadeCurveLinePath(body, b.fadeInCurve, true));
        p.setRenderHint(QPainter::Antialiasing, false);

        // Duration badge when either side is selected
        if (a.selected || b.selected) {
            QString dur = QString::number(ov, 'f', 2);
            dur.replace(QLatin1Char('.'), QLatin1Char(','));
            QFont f = p.font();
            f.setPointSize(8);
            f.setBold(true);
            p.setFont(f);
            const QFontMetrics fm(f);
            const int tw = fm.horizontalAdvance(dur) + 8;
            const int th = fm.height() + 2;
            const QRect badge(zone.center().x() - tw / 2, int(body.center().y()) - th / 2, tw, th);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, 230));
            p.drawRoundedRect(badge, 2, 2);
            p.setPen(QColor(20, 20, 20));
            p.drawText(badge, Qt::AlignCenter, dur);
        }
    }
}

void TimelineView::paintRuler(QPainter &p)
{
    const int laneH = markerLaneHeight();
    const int ticksTop = laneH;
    const QRect rulerRect(0, 0, width(), rulerHeight());
    p.fillRect(rulerRect, QColor(0x1a, 0x1a, 0x1a));
    // Marker lane
    p.fillRect(headerWidth(), 0, width() - headerWidth(), laneH, QColor(0x1e, 0x1e, 0x1e));
    p.setPen(QColor(0x0a, 0x0a, 0x0a));
    p.drawLine(headerWidth(), laneH - 1, width(), laneH - 1);
    // Ticks band
    p.fillRect(headerWidth(), ticksTop, width() - headerWidth(), ticksHeight(), QColor(0x26, 0x26, 0x26));
    p.fillRect(0, 0, headerWidth(), rulerHeight(), QColor(0x2a, 0x2a, 0x2a));
    p.setPen(QColor(0x11, 0x11, 0x11));
    p.drawLine(headerWidth(), 0, headerWidth(), rulerHeight());
    p.setPen(QColor(0x0a, 0x0a, 0x0a));
    p.drawLine(0, rulerHeight() - 1, width(), rulerHeight() - 1);

    const double ph = m_model ? m_model->playheadSec() : 0.0;
    const QString cornerLabel =
        m_model ? m_model->formatRulerTime(ph) : QStringLiteral("0.0.000");
    p.setPen(QColor(0xe8, 0xe8, 0xe8));
    QFont f = p.font();
    f.setFamily(QStringLiteral("Consolas"));
    f.setPointSize(10);
    f.setBold(true);
    p.setFont(f);
    p.drawText(QRect(6, 0, headerWidth() - 10, rulerHeight()), Qt::AlignVCenter | Qt::AlignLeft,
               cornerLabel);

    p.setPen(QColor(0x88, 0x88, 0x88));
    f.setPointSize(8);
    f.setBold(false);
    p.setFont(f);
    const double pps = m_model ? m_model->pixelsPerSecond() : 40.0;
    const int maxSec = static_cast<int>((width() - headerWidth() + m_scrollX) / pps) + 2;
    for (int sec = 0; sec <= std::max(120, maxSec); ++sec) {
        const int x = timeToX(sec);
        if (x < headerWidth() || x > width()) {
            continue;
        }
        const bool major = (sec % 5) == 0;
        p.setPen(QColor(0x66, 0x66, 0x66));
        p.drawLine(x, rulerHeight() - (major ? 14 : 7), x, rulerHeight() - 1);
        if (major) {
            p.setPen(QColor(0xb0, 0xb0, 0xb0));
            const QString label =
                m_model ? m_model->formatRulerTime(static_cast<double>(sec))
                        : QStringLiteral("%1").arg(sec);
            p.drawText(x + 3, rulerHeight() - 16, label);
        }
    }
}

void TimelineView::paintLoopRegion(QPainter &p)
{
    if (!m_model) {
        return;
    }
    const int bodyTop = rulerHeight();
    const int bodyH = height() - bodyTop;

    if (m_model->hasLoopRegion()) {
        const LoopRegion &lr = m_model->loopRegion();
        const int x0 = timeToX(lr.startSec);
        const int x1 = timeToX(lr.endSec);
        const int left = std::max(headerWidth(), std::min(x0, x1));
        const int right = std::min(width(), std::max(x0, x1));

        // Soft blue band over tracks (clips stay readable)
        if (right > left && bodyH > 0) {
            p.fillRect(left, bodyTop, right - left, bodyH, loopBand());
        }

        const QRect bar = loopBarRect();
        if (bar.right() >= headerWidth() && bar.left() <= width()) {
            QRect drawBar = bar;
            if (drawBar.left() < headerWidth()) {
                drawBar.setLeft(headerWidth());
            }
            // Dominant blue fill — do NOT paint a full-width yellow stripe
            // (that + end triangles looked like a yellow <——> arrow).
            p.fillRect(drawBar, loopBar());
            // Thin amber accent on top edge only
            p.fillRect(drawBar.left(), drawBar.top(), drawBar.width(), 1, loopBarTop());

            // Yellow corner triangles (Vegas): right-angle at outer top, hypotenuse inward.
            // Start at 0: vertical on LEFT, flat top, diagonal down-right.
            auto drawHandle = [&](int tipX, bool isStart) {
                const int top = drawBar.top() - 2;
                constexpr int w = 8;
                constexpr int h = 9;
                QPainterPath tri;
                if (isStart) {
                    tri.moveTo(tipX, top);
                    tri.lineTo(tipX + w, top);
                    tri.lineTo(tipX, top + h);
                } else {
                    tri.moveTo(tipX, top);
                    tri.lineTo(tipX - w, top);
                    tri.lineTo(tipX, top + h);
                }
                tri.closeSubpath();
                p.setPen(Qt::NoPen);
                p.setBrush(loopHandle());
                p.drawPath(tri);
            };
            if (x0 >= headerWidth() - 2 && x0 <= width() + 2) {
                drawHandle(x0, true);
            }
            if (x1 >= headerWidth() - 2 && x1 <= width() + 2) {
                drawHandle(x1, false);
            }
        }

        // Blue edge guides through tracks (from bar down)
        p.setPen(QPen(loopBandEdge(), 1));
        if (x0 >= headerWidth() && x0 <= width()) {
            p.drawLine(x0, bar.bottom() + 1, x0, height());
        }
        if (x1 >= headerWidth() && x1 <= width()) {
            p.drawLine(x1, bar.bottom() + 1, x1, height());
        }
    } else {
        // Collapsed seed at loop origin (Vegas right-triangle, vertical flush left)
        const QRect seed = loopSeedRect();
        if (seed.right() >= headerWidth() && seed.left() <= width()) {
            const int x = timeToX(m_model->loopRegion().startSec);
            const int top = markerLaneHeight() - 11;
            QPainterPath tri;
            tri.moveTo(x, top);
            tri.lineTo(x + 8, top);
            tri.lineTo(x, top + 9);
            tri.closeSubpath();
            p.setPen(Qt::NoPen);
            p.setBrush(loopHandle());
            p.drawPath(tri);
        }
    }
}

void TimelineView::paintMarkers(QPainter &p)
{
    if (!m_model) {
        return;
    }
    QFont numFont = p.font();
    numFont.setFamily(QStringLiteral("Segoe UI"));
    numFont.setPointSize(8);
    numFont.setBold(true);
    QFont labelFont = numFont;
    labelFont.setBold(false);
    labelFont.setPointSize(9);

    for (const TimelineMarker &m : m_model->markers()) {
        const int x = timeToX(m.timeSec);
        if (x < headerWidth() - 2 || x > width() + 160) {
            continue;
        }

        const QRect head = markerHeadRect(m);

        // Orange guide from bottom of number box through tracks
        p.setPen(QPen(markerGuide(), 1));
        p.drawLine(x, head.bottom() + 1, x, height());

        // Orange number square
        p.fillRect(head, markerHead());
        p.setPen(QColor(40, 24, 0, 200));
        p.drawRect(head.adjusted(0, 0, -1, -1));
        if (m.selected) {
            p.setPen(QPen(QColor(255, 230, 140), 1));
            p.drawRect(head.adjusted(1, 1, -2, -2));
        }

        p.setFont(numFont);
        p.setPen(Qt::white);
        p.drawText(head, Qt::AlignCenter, QString::number(m.number));

        // Label to the right — text only (not part of the orange box)
        if (!m.label.isEmpty() && m_renamingMarkerId != m.id) {
            p.setFont(labelFont);
            const QRect labelR = markerLabelRect(m);
            const QString text =
                QFontMetrics(labelFont).elidedText(m.label, Qt::ElideRight, labelR.width());
            // Soft shadow for contrast over clips/ruler
            p.setPen(QColor(0, 0, 0, 180));
            p.drawText(labelR.translated(1, 1), Qt::AlignVCenter | Qt::AlignLeft, text);
            p.setPen(QColor(0xe8, 0xe8, 0xe8));
            p.drawText(labelR, Qt::AlignVCenter | Qt::AlignLeft, text);
        }
    }
}

void TimelineView::paintPlayhead(QPainter &p)
{
    if (!m_model) {
        return;
    }
    const int px = timeToX(m_model->playheadSec());
    if (px < headerWidth() || px > width()) {
        return;
    }

    // Playing / scrubbing / shuttle → solid white; idle → blink black ↔ white each second
    const bool solidWhite =
        m_playing || m_draggingPlayhead || std::abs(m_shuttleRate) > 1e-6;
    const QColor lineColor =
        (solidWhite || m_playheadBlinkLight) ? QColor(255, 255, 255) : QColor(0, 0, 0);

    p.setPen(QPen(lineColor, 1));
    p.drawLine(px, 0, px, height());

    // Vegas-style downward caret on the ruler
    QPainterPath caret;
    caret.moveTo(px, 11);
    caret.lineTo(px - 6, 0);
    caret.lineTo(px + 6, 0);
    caret.closeSubpath();
    p.setBrush(lineColor);
    p.setPen(QColor(0, 0, 0, solidWhite || m_playheadBlinkLight ? 120 : 200));
    p.drawPath(caret);
}

void TimelineView::paintTracks(QPainter &p)
{
    const int bodyTop = rulerHeight();
    p.fillRect(0, bodyTop, headerWidth(), height() - bodyTop, QColor(0x22, 0x22, 0x22));
    p.fillRect(headerWidth(), bodyTop, width() - headerWidth(), height() - bodyTop, QColor(0x10, 0x10, 0x10));
    p.setPen(QColor(0x11, 0x11, 0x11));
    p.drawLine(headerWidth(), bodyTop, headerWidth(), height());

    p.save();
    p.setClipRect(0, bodyTop, width(), height() - bodyTop);

    if (!m_model || m_model->tracks().isEmpty()) {
        p.setPen(QColor(0x18, 0x18, 0x18));
        for (int y = bodyTop + 48; y < height(); y += 48) {
            p.drawLine(headerWidth(), y, width(), y);
        }
        p.restore();
        return;
    }

    // Pass 1: lane backgrounds + headers
    {
        int y = bodyTop - m_scrollY;
        int index = 0;
        for (const Track &track : m_model->tracks()) {
            const bool audible = m_model->isTrackAudible(index);
            p.fillRect(headerWidth(), y, width() - headerWidth(), track.height,
                       audible ? QColor(0x12, 0x12, 0x12) : QColor(0x0c, 0x0c, 0x0c));
            p.setPen(QColor(0x0a, 0x0a, 0x0a));
            p.drawLine(headerWidth(), y + track.height - 1, width(), y + track.height - 1);
            if (index == m_hoverResizeTrack || index == m_resizingTrackIndex) {
                p.fillRect(0, y + track.height - 3, width(), 3, QColor(0x00, 0x78, 0xd7, 140));
            }
            paintTrackHeader(p, track, index, y);
            y += track.height;
            ++index;
        }
    }

    // Vegas-style vertical grid through empty lane areas (under events).
    {
        const double pps = m_model->pixelsPerSecond() > 1.0 ? m_model->pixelsPerSecond() : 40.0;
        const int maxSec = static_cast<int>((width() - headerWidth() + m_scrollX) / pps) + 2;
        const bool drawMinor = pps >= 28.0;
        p.setClipRect(headerWidth(), bodyTop, width() - headerWidth(), height() - bodyTop);
        for (int sec = 0; sec <= std::max(120, maxSec); ++sec) {
            const int x = timeToX(sec);
            if (x < headerWidth() || x > width()) {
                continue;
            }
            const bool major = (sec % 5) == 0;
            if (!major && !drawMinor) {
                continue;
            }
            p.setPen(major ? trackGridMajor() : trackGridMinor());
            p.drawLine(x, bodyTop, x, height());
        }
    }

    // Pass 2: events + crossfades + mute dim
    {
        int y = bodyTop - m_scrollY;
        int index = 0;
        for (const Track &track : m_model->tracks()) {
            for (const TrackEvent &ev : track.events) {
                const QRect r = eventRect(track, ev, y);
                if (r.right() < headerWidth() || r.left() > width()) {
                    continue;
                }
                p.save();
                p.setClipRect(QRect(headerWidth(), std::max(y, bodyTop), width() - headerWidth(),
                                    track.height));
                paintEventBlock(p, track, ev, r);
                p.restore();
            }
            p.save();
            p.setClipRect(QRect(headerWidth(), bodyTop, width() - headerWidth(), height() - bodyTop));
            paintCrossfades(p, track, y);
            p.restore();

            if (!m_model->isTrackAudible(index)) {
                p.fillRect(headerWidth(), y, width() - headerWidth(), track.height,
                           QColor(0, 0, 0, 110));
            }

            y += track.height;
            ++index;
        }
    }
    p.restore();
}

void TimelineView::paintHeaderSplitter(QPainter &p)
{
    const int x = m_headerWidth;
    p.setPen(QColor(0x11, 0x11, 0x11));
    p.drawLine(x, 0, x, height());
    if (m_resizingHeader || underMouse()) {
        // accent when dragging / hover handled via cursor; keep thin line
    }
}

void TimelineView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(rect(), QColor(0x14, 0x14, 0x14));
    paintRuler(p);
    paintTracks(p);
    paintLoopRegion(p);
    paintMarkers(p);
    paintReorderCue(p);
    paintPlayhead(p);
    paintDropGhost(p);
    paintHeaderSplitter(p);
}

void TimelineView::paintReorderCue(QPainter &p)
{
    if (m_reorderingTrack < 0 || !m_model || m_reorderInsertBefore < 0) {
        return;
    }
    int y = rulerHeight() - m_scrollY;
    for (int i = 0; i < m_reorderInsertBefore && i < m_model->tracks().size(); ++i) {
        y += m_model->tracks()[i].height;
    }
    if (y < rulerHeight() - 2 || y > height() + 2) {
        return;
    }
    p.fillRect(0, y - 1, width(), 3, QColor(0x00, 0x78, 0xd7));
    p.setPen(QPen(QColor(0x40, 0xb0, 0xff), 1));
    p.drawLine(0, y, width(), y);
    // Small triangles at header edge
    QPainterPath tip;
    tip.moveTo(2, y);
    tip.lineTo(10, y - 5);
    tip.lineTo(10, y + 5);
    tip.closeSubpath();
    p.setBrush(QColor(0x00, 0x78, 0xd7));
    p.setPen(Qt::NoPen);
    p.drawPath(tip);
}

void TimelineView::emitDocumentEditBegan()
{
    if (m_docEditOpen) {
        return;
    }
    m_docEditOpen = true;
    emit documentEditBegan();
}

void TimelineView::emitDocumentEditCommitted(const QString &text)
{
    if (!m_docEditOpen) {
        return;
    }
    m_docEditOpen = false;
    emit documentEditCommitted(text);
}

void TimelineView::mousePressEvent(QMouseEvent *event)
{
    if (!m_model) {
        return;
    }
    if (event->button() == Qt::LeftButton) {
        setFocus(Qt::MouseFocusReason);
        if (m_renamingTrackIndex >= 0) {
            finishTrackRename(true);
        }
        if (nearHeaderSplitter(event->pos())) {
            m_resizingHeader = true;
            setCursor(Qt::SizeHorCursor);
            grabMouse();
            return;
        }
        const int resizeTi = trackResizeIndexAt(event->pos());
        if (resizeTi >= 0) {
            emitDocumentEditBegan();
            m_resizingTrackIndex = resizeTi;
            m_resizeTrackOriginY = event->pos().y();
            m_resizeTrackOriginHeight = m_model->tracks()[resizeTi].height;
            setCursor(Qt::SizeVerCursor);
            grabMouse();
            update();
            return;
        }
        // Mute / Solo toggles on track header
        if (event->pos().x() < headerWidth() && event->pos().y() >= rulerHeight()) {
            const int ti = trackIndexAtY(event->pos().y());
            if (ti >= 0 && ti < m_model->tracks().size()) {
                Track &track = m_model->tracks()[ti];
                const HeaderControls ctrls =
                    headerControls(track, headerWidth(), railWidth(), trackY(ti));
                if (ctrls.chipMenu.isValid() && ctrls.chipMenu.contains(event->pos())) {
                    emit trackMoreMenuRequested(ti, event->globalPos());
                    return;
                }
                if (ctrls.chipFx.isValid() && ctrls.chipFx.contains(event->pos())) {
                    emit trackFxRequested(ti);
                    return;
                }
                if (ctrls.mute.contains(event->pos())) {
                    emitDocumentEditBegan();
                    track.muted = !track.muted;
                    emitDocumentEditCommitted(tr("Mute Track"));
                    update();
                    return;
                }
                if (ctrls.solo.contains(event->pos())) {
                    emitDocumentEditBegan();
                    track.solo = !track.solo;
                    emitDocumentEditCommitted(tr("Solo Track"));
                    update();
                    return;
                }
            }
        }
        // Reorder tracks: drag empty space on track control panel
        int reorderTi = -1;
        if (isHeaderEmptyDragSpace(event->pos(), &reorderTi)) {
            emitDocumentEditBegan();
            m_reorderingTrack = reorderTi;
            m_reorderInsertBefore = reorderTi;
            setCursor(Qt::ClosedHandCursor);
            grabMouse();
            update();
            return;
        }
        if (event->pos().y() < rulerHeight() && event->pos().x() >= headerWidth()) {
            if (m_renamingMarkerId >= 0) {
                finishMarkerRename(true);
            }
            const int mid = markerAtPos(event->pos());
            if (mid >= 0) {
                emitDocumentEditBegan();
                m_model->clearSelection();
                m_model->selectMarker(mid, event->modifiers() & Qt::ControlModifier);
                m_rulerDrag = RulerDragMode::Marker;
                m_dragMarkerId = mid;
                if (const TimelineMarker *mk = m_model->findMarker(mid)) {
                    m_dragMarkerOriginTime = mk->timeSec;
                }
                m_dragOriginX = event->pos().x();
                setCursor(Qt::SizeHorCursor);
                grabMouse();
                update();
                return;
            }
            const RulerDragMode loopMode = loopHitAt(event->pos());
            if (loopMode != RulerDragMode::None) {
                emitDocumentEditBegan();
                m_model->clearMarkerSelection();
                m_rulerDrag = loopMode;
                m_dragOriginX = event->pos().x();
                if (m_model->hasLoopRegion()) {
                    m_dragLoopOriginStart = m_model->loopRegion().startSec;
                    m_dragLoopOriginEnd = m_model->loopRegion().endSec;
                } else {
                    m_dragLoopCreateOrigin = m_model->loopRegion().startSec;
                }
                setCursor(loopMode == RulerDragMode::LoopMove ? Qt::SizeAllCursor
                                                             : Qt::SizeHorCursor);
                grabMouse();
                update();
                return;
            }
            m_model->clearMarkerSelection();
            m_draggingPlayhead = true;
            m_rulerDrag = RulerDragMode::Playhead;
            m_model->setPlayheadSec(std::max(0.0, xToTime(event->pos().x())));
            emit playheadChanged(m_model->playheadSec());
            update();
            return;
        }

        Hit edgeHit;
        // Event chrome buttons (Pan/Crop, FX, More) — click without starting a move
        {
            Hit btnHit;
            const EventChromeButton btn = eventButtonAt(event->pos(), &btnHit);
            if (btn != EventChromeButton::None && btnHit.eventId >= 0) {
                m_model->selectEvent(btnHit.eventId, event->modifiers() & Qt::ControlModifier);
                update();
                switch (btn) {
                case EventChromeButton::PanCrop:
                    emit eventPanCropRequested(btnHit.eventId);
                    break;
                case EventChromeButton::Fx:
                    emit eventFxRequested(btnHit.eventId);
                    break;
                case EventChromeButton::More:
                    emit eventMoreMenuRequested(btnHit.eventId,
                                                event->globalPosition().toPoint());
                    break;
                default:
                    break;
                }
                return;
            }
        }

        const EventEditMode edgeMode = eventEditModeAt(event->pos(), &edgeHit);
        if (edgeMode != EventEditMode::None && edgeHit.eventId >= 0) {
            m_model->selectEvent(edgeHit.eventId, event->modifiers() & Qt::ControlModifier);
            TrackEvent *ev = m_model->findEvent(edgeHit.eventId);
            if (!ev) {
                return;
            }
            emitDocumentEditBegan();
            m_eventEditMode = edgeMode;
            m_dragEventId = edgeHit.eventId;
            m_dragOriginStart = ev->startSec;
            m_dragOriginLength = ev->lengthSec;
            m_dragOriginFadeIn = ev->fadeInSec;
            m_dragOriginFadeOut = ev->fadeOutSec;
            m_dragOriginLevel = eventLevelNormalized(*ev);
            m_dragOriginX = event->pos().x();
            m_dragOriginY = event->pos().y();
            m_dragGroupOrigins.clear();
            m_dragGroupLengths.clear();
            if (!m_model->ignoreEventGrouping() && ev->groupId > 0
                && (edgeMode == EventEditMode::TrimStart || edgeMode == EventEditMode::TrimEnd)) {
                for (int id : m_model->eventIdsInGroup(ev->groupId)) {
                    if (const TrackEvent *m = m_model->findEvent(id)) {
                        m_dragGroupOrigins.insert(id, m->startSec);
                        m_dragGroupLengths.insert(id, m->lengthSec);
                    }
                }
            } else {
                m_dragGroupOrigins.insert(ev->id, ev->startSec);
                m_dragGroupLengths.insert(ev->id, ev->lengthSec);
            }
            m_dragging = false;
            switch (edgeMode) {
            case EventEditMode::TrimStart:
            case EventEditMode::TrimEnd:
                setCursor(Qt::SizeHorCursor);
                break;
            case EventEditMode::FadeIn:
                setCursor(Qt::SizeBDiagCursor);
                break;
            case EventEditMode::FadeOut:
                setCursor(Qt::SizeFDiagCursor);
                break;
            case EventEditMode::Level:
                setCursor(Qt::SizeVerCursor);
                QToolTip::showText(event->globalPosition().toPoint(), eventLevelTooltip(*ev), this);
                break;
            default:
                break;
            }
            grabMouse();
            update();
            return;
        }

        auto hit = hitTest(event->pos());
        if (hit && hit->eventId >= 0) {
            m_model->clearMarkerSelection();
            m_model->selectEvent(hit->eventId, event->modifiers() & Qt::ControlModifier);
            emitDocumentEditBegan();
            m_eventEditMode = EventEditMode::Move;
            m_dragging = true;
            m_dragEventId = hit->eventId;
            m_dragGroupOrigins.clear();
            if (TrackEvent *ev = m_model->findEvent(m_dragEventId)) {
                m_dragOriginStart = ev->startSec;
                m_dragOriginLength = ev->lengthSec;
                m_dragOriginFadeIn = ev->fadeInSec;
                m_dragOriginFadeOut = ev->fadeOutSec;
                if (!m_model->ignoreEventGrouping() && ev->groupId > 0) {
                    for (int id : m_model->eventIdsInGroup(ev->groupId)) {
                        if (const TrackEvent *m = m_model->findEvent(id)) {
                            m_dragGroupOrigins.insert(id, m->startSec);
                        }
                    }
                } else {
                    m_dragGroupOrigins.insert(ev->id, ev->startSec);
                }
            }
            m_dragOriginX = event->pos().x();
            m_dragCreatedTrack = -1;
            grabMouse();
            update();
        } else {
            m_model->clearSelection();
            m_model->clearMarkerSelection();
            update();
        }
    }
}

void TimelineView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_resizingHeader) {
        setHeaderWidth(event->pos().x());
        return;
    }
    if (m_resizingTrackIndex >= 0 && m_model
        && m_resizingTrackIndex < m_model->tracks().size()) {
        const int delta = event->pos().y() - m_resizeTrackOriginY;
        const int h = std::clamp(m_resizeTrackOriginHeight + delta, minTrackHeight(), maxTrackHeight());
        m_model->tracks()[m_resizingTrackIndex].height = h;
        ensureContentWidth();
        update();
        return;
    }
    if (m_reorderingTrack >= 0) {
        const int insert = reorderInsertIndexAtY(event->pos().y());
        if (insert != m_reorderInsertBefore) {
            m_reorderInsertBefore = insert;
            update();
        }
        // Auto-scroll vertically near edges while reordering
        if (event->pos().y() < rulerHeight() + 24) {
            setScrollY(m_scrollY - 12);
        } else if (event->pos().y() > height() - 24) {
            setScrollY(m_scrollY + 12);
        }
        return;
    }

    if (!m_model) {
        return;
    }

    if (m_eventEditMode != EventEditMode::None && m_dragEventId >= 0) {
        TrackEvent *ev = m_model->findEvent(m_dragEventId);
        if (!ev) {
            return;
        }
        const double pps = std::max(1.0, m_model->pixelsPerSecond());
        const double deltaSec = (event->pos().x() - m_dragOriginX) / pps;

        switch (m_eventEditMode) {
        case EventEditMode::Move: {
            int fromTrack = -1;
            TrackEvent *cur = m_model->findEvent(m_dragEventId, &fromTrack);
            if (!cur) {
                return;
            }
            // Dragged clip may change track (same media family). Other group members
            // keep their tracks and only follow the time delta.
            int toTrack = trackIndexAtY(event->pos().y());
            if (toTrack < 0 && isBelowTracksDropZone(event->pos())) {
                // Empty space under tracks → create a matching new track once per drag
                if (m_dragCreatedTrack < 0
                    || m_dragCreatedTrack >= m_model->tracks().size()) {
                    const TrackKind kind =
                        isVideoFamily(cur->mediaKind) ? TrackKind::Video : TrackKind::Audio;
                    m_dragCreatedTrack = m_model->addTrack(kind);
                    ensureContentWidth();
                    emit scrollMetricsChanged();
                }
                toTrack = m_dragCreatedTrack;
            }
            if (toTrack >= 0 && toTrack < m_model->tracks().size()) {
                const Track &dst = m_model->tracks()[toTrack];
                if (toTrack != fromTrack) {
                    if (canPlaceEventOnTrack(cur->mediaKind, dst.kind)) {
                        m_model->moveEventToTrack(m_dragEventId, toTrack);
                        setCursor(Qt::ArrowCursor);
                    } else {
                        setCursor(Qt::ForbiddenCursor);
                    }
                } else {
                    setCursor(Qt::ArrowCursor);
                }
            }
            for (auto it = m_dragGroupOrigins.constBegin(); it != m_dragGroupOrigins.constEnd();
                 ++it) {
                if (TrackEvent *m = m_model->findEvent(it.key())) {
                    m->startSec = std::max(0.0, it.value() + deltaSec);
                }
            }
            if (event->pos().y() < rulerHeight() + 24) {
                setScrollY(m_scrollY - 12);
            } else if (event->pos().y() > height() - 24) {
                setScrollY(m_scrollY + 12);
            }
            break;
        }
        case EventEditMode::TrimStart: {
            // Same time delta for every member of the group (Vegas-like).
            for (auto it = m_dragGroupOrigins.constBegin(); it != m_dragGroupOrigins.constEnd();
                 ++it) {
                TrackEvent *m = m_model->findEvent(it.key());
                if (!m) {
                    continue;
                }
                const double originStart = it.value();
                const double originLen = m_dragGroupLengths.value(it.key(), m->lengthSec);
                double newStart = originStart + deltaSec;
                double newLen = originLen - deltaSec;
                if (newLen < minEventLengthSec()) {
                    newLen = minEventLengthSec();
                    newStart = originStart + originLen - newLen;
                }
                if (newStart < 0.0) {
                    newLen += newStart;
                    newStart = 0.0;
                }
                m->startSec = newStart;
                m->lengthSec = std::max(minEventLengthSec(), newLen);
                clampEventFades(*m);
            }
            break;
        }
        case EventEditMode::TrimEnd: {
            for (auto it = m_dragGroupLengths.constBegin(); it != m_dragGroupLengths.constEnd();
                 ++it) {
                TrackEvent *m = m_model->findEvent(it.key());
                if (!m) {
                    continue;
                }
                m->lengthSec = std::max(minEventLengthSec(), it.value() + deltaSec);
                clampEventFades(*m);
            }
            break;
        }
        case EventEditMode::FadeIn: {
            // Absolute: fade length from left edge to cursor
            const double fromLeft = xToTime(event->pos().x()) - ev->startSec;
            const double maxFade = std::max(0.0, ev->lengthSec - ev->fadeOutSec - minEventLengthSec());
            ev->fadeInSec = std::clamp(fromLeft, 0.0, maxFade);
            break;
        }
        case EventEditMode::FadeOut: {
            const double fromRight = (ev->startSec + ev->lengthSec) - xToTime(event->pos().x());
            const double maxFade = std::max(0.0, ev->lengthSec - ev->fadeInSec - minEventLengthSec());
            ev->fadeOutSec = std::clamp(fromRight, 0.0, maxFade);
            break;
        }
        case EventEditMode::Level: {
            int trackIndex = -1;
            m_model->findEvent(m_dragEventId, &trackIndex);
            if (trackIndex < 0 || trackIndex >= m_model->tracks().size()) {
                break;
            }
            const Track &track = m_model->tracks()[trackIndex];
            const QRect r = eventRect(track, *ev, trackY(trackIndex));
            const QRect body = eventLevelBodyRect(track, *ev, r);
            if (body.height() > 1) {
                const double n = normalizedFromLevelY(body, event->pos().y());
                setEventLevelFromNormalized(*ev, n);
                QToolTip::showText(event->globalPosition().toPoint(), eventLevelTooltip(*ev), this);
            }
            break;
        }
        default:
            break;
        }
        ensureContentWidth();
        update();
        return;
    }

    updateHoverCursor(event->pos());
    updateEventChromeTooltip(event->pos());

    if (m_rulerDrag == RulerDragMode::Marker && m_dragMarkerId >= 0) {
        const double pps = std::max(1.0, m_model->pixelsPerSecond());
        const double deltaSec = (event->pos().x() - m_dragOriginX) / pps;
        if (TimelineMarker *mk = m_model->findMarker(m_dragMarkerId)) {
            mk->timeSec = std::max(0.0, m_dragMarkerOriginTime + deltaSec);
            positionMarkerLabelEdit();
        }
        update();
        return;
    }
    if (m_rulerDrag == RulerDragMode::LoopCreate) {
        const double t = std::max(0.0, xToTime(event->pos().x()));
        const double origin = m_dragLoopCreateOrigin;
        if (std::abs(t - origin) >= 0.02) {
            m_model->setLoopRegion(origin, t);
            m_rulerDrag = (t >= origin) ? RulerDragMode::LoopEnd : RulerDragMode::LoopStart;
            m_dragLoopOriginStart = m_model->loopRegion().startSec;
            m_dragLoopOriginEnd = m_model->loopRegion().endSec;
            m_dragOriginX = event->pos().x();
        }
        update();
        return;
    }
    if (m_rulerDrag == RulerDragMode::LoopStart || m_rulerDrag == RulerDragMode::LoopEnd
        || m_rulerDrag == RulerDragMode::LoopMove) {
        const double pps = std::max(1.0, m_model->pixelsPerSecond());
        const double deltaSec = (event->pos().x() - m_dragOriginX) / pps;
        if (m_rulerDrag == RulerDragMode::LoopStart) {
            m_model->setLoopRegion(m_dragLoopOriginStart + deltaSec, m_dragLoopOriginEnd);
        } else if (m_rulerDrag == RulerDragMode::LoopEnd) {
            m_model->setLoopRegion(m_dragLoopOriginStart, m_dragLoopOriginEnd + deltaSec);
        } else {
            const double len = m_dragLoopOriginEnd - m_dragLoopOriginStart;
            double start = std::max(0.0, m_dragLoopOriginStart + deltaSec);
            m_model->setLoopRegion(start, start + len);
        }
        ensureContentWidth();
        update();
        return;
    }

    if (m_draggingPlayhead) {
        m_model->setPlayheadSec(std::max(0.0, xToTime(event->pos().x())));
        emit playheadChanged(m_model->playheadSec());
        update();
        return;
    }
}

void TimelineView::mouseReleaseEvent(QMouseEvent *)
{
    const bool finishedHeader = m_resizingHeader;
    const bool finishedTrack = m_resizingTrackIndex >= 0;
    const bool finishedReorder = m_reorderingTrack >= 0;
    const bool finishedEventEdit = m_eventEditMode != EventEditMode::None;
    const EventEditMode finishedMode = m_eventEditMode;
    const bool finishedMarker = m_rulerDrag == RulerDragMode::Marker;
    const bool finishedLoop = m_rulerDrag == RulerDragMode::LoopCreate
                              || m_rulerDrag == RulerDragMode::LoopMove
                              || m_rulerDrag == RulerDragMode::LoopStart
                              || m_rulerDrag == RulerDragMode::LoopEnd;
    const bool finishedRuler = m_rulerDrag != RulerDragMode::None || m_draggingPlayhead;
    if (finishedHeader || finishedTrack || finishedReorder || finishedEventEdit || finishedRuler) {
        releaseMouse();
    }
    if (finishedReorder) {
        moveTrack(m_reorderingTrack, m_reorderInsertBefore);
    }
    m_dragging = false;
    m_draggingPlayhead = false;
    m_rulerDrag = RulerDragMode::None;
    m_dragMarkerId = -1;
    m_resizingHeader = false;
    m_resizingTrackIndex = -1;
    m_reorderingTrack = -1;
    m_reorderInsertBefore = -1;
    m_dragEventId = -1;
    m_dragGroupOrigins.clear();
    // Drop cancelled onto another track: remove unused track created during this drag
    if (m_dragCreatedTrack >= 0 && m_model) {
        m_model->removeTrackIfEmpty(m_dragCreatedTrack);
        ensureContentWidth();
        emit scrollMetricsChanged();
    }
    m_dragCreatedTrack = -1;
    m_eventEditMode = EventEditMode::None;
    unsetCursor();
    if (finishedHeader) {
        emit headerWidthEditFinished(m_headerWidth);
    }
    if (finishedTrack || finishedReorder || finishedEventEdit || finishedRuler) {
        update();
    }

    if (m_docEditOpen) {
        QString text;
        if (finishedEventEdit) {
            switch (finishedMode) {
            case EventEditMode::Move:
                text = tr("Move Event");
                break;
            case EventEditMode::TrimStart:
            case EventEditMode::TrimEnd:
                text = tr("Trim Event");
                break;
            case EventEditMode::FadeIn:
            case EventEditMode::FadeOut:
                text = tr("Fade Event");
                break;
            case EventEditMode::Level:
                text = tr("Event Level");
                break;
            default:
                text = tr("Edit Event");
                break;
            }
        } else if (finishedTrack) {
            text = tr("Resize Track");
        } else if (finishedReorder) {
            text = tr("Reorder Tracks");
        } else if (finishedMarker) {
            text = tr("Move Marker");
        } else if (finishedLoop) {
            text = tr("Loop Region");
        } else {
            text = tr("Timeline Edit");
        }
        emitDocumentEditCommitted(text);
    }
}

void TimelineView::mouseDoubleClickEvent(QMouseEvent *event)
{
    // Double-click marker → rename label
    if (event->button() == Qt::LeftButton && m_model && event->pos().y() < markerLaneHeight()
        && event->pos().x() >= headerWidth()) {
        const int mid = markerAtPos(event->pos());
        if (mid >= 0) {
            m_model->selectMarker(mid);
            beginMarkerRename(mid);
            return;
        }
    }
    // Double-click track name → inline rename
    if (event->button() == Qt::LeftButton && event->pos().x() < headerWidth()
        && event->pos().y() >= rulerHeight() && m_model) {
        const int ti = trackIndexAtY(event->pos().y());
        if (ti >= 0 && trackNameRect(ti).contains(event->pos())) {
            beginTrackRename(ti);
            return;
        }
    }
    // Double-click bottom edge → reset track height (Vegas-like)
    const int resizeTi = trackResizeIndexAt(event->pos());
    if (resizeTi >= 0 && m_model && resizeTi < m_model->tracks().size()) {
        Track &t = m_model->tracks()[resizeTi];
        const int defH = defaultTrackHeight(t.kind);
        if (t.height != defH) {
            emitDocumentEditBegan();
            t.height = defH;
            emitDocumentEditCommitted(tr("Reset Track Height"));
            ensureContentWidth();
            update();
        }
        return;
    }
    auto hit = hitTest(event->pos());
    if (hit && hit->eventId >= 0) {
        emit eventDoubleClicked(hit->eventId);
    }
}

void TimelineView::keyPressEvent(QKeyEvent *event)
{
    if (!m_model) {
        QWidget::keyPressEvent(event);
        return;
    }
    if (m_renamingMarkerId >= 0 || m_renamingTrackIndex >= 0) {
        QWidget::keyPressEvent(event);
        return;
    }
    const bool noMods =
        !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
    // nativeVirtualKey keeps physical M/L on RU layouts (Windows VK_M / VK_L)
    const quint32 vk = event->nativeVirtualKey();

    if (event->key() == Qt::Key_Space) {
        // Space / Shift+Space handled by KeyboardMap ApplicationShortcuts (Global).
        QWidget::keyPressEvent(event);
        return;
    }
    if (noMods && (event->key() == Qt::Key_Home)) {
        // Home / End / W / E via KeyboardMap Global shortcuts.
        QWidget::keyPressEvent(event);
        return;
    }
    if (noMods && (event->key() == Qt::Key_End)) {
        QWidget::keyPressEvent(event);
        return;
    }
    if (noMods && (event->key() == Qt::Key_Left)) {
        stepFrames(-1);
        event->accept();
        return;
    }
    if (noMods && (event->key() == Qt::Key_Right)) {
        stepFrames(1);
        event->accept();
        return;
    }
    if (noMods && (event->key() == Qt::Key_M || vk == 0x4D)) {
        // Marker via KeyboardMap
        QWidget::keyPressEvent(event);
        return;
    }
    if (noMods && (event->key() == Qt::Key_L || vk == 0x4C)) {
        QWidget::keyPressEvent(event);
        return;
    }
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        int selectedId = -1;
        for (const TimelineMarker &m : m_model->markers()) {
            if (m.selected) {
                selectedId = m.id;
                break;
            }
        }
        if (selectedId >= 0) {
            emitDocumentEditBegan();
            m_model->removeMarker(selectedId);
            emitDocumentEditCommitted(tr("Delete Marker"));
            update();
            event->accept();
            return;
        }
        // Delete selected events (group-aware)
        QVector<int> ids;
        for (const Track &t : m_model->tracks()) {
            for (const TrackEvent &ev : t.events) {
                if (ev.selected) {
                    ids.push_back(ev.id);
                }
            }
        }
        if (!ids.isEmpty()) {
            emitDocumentEditBegan();
            for (int id : ids) {
                m_model->removeEventOrGroup(id);
            }
            emitDocumentEditCommitted(tr("Delete"));
            update();
            event->accept();
            return;
        }
    }
    if (noMods && (event->key() == Qt::Key_S || vk == 0x53)) {
        // Split selected events at playhead
        const double ph = m_model->playheadSec();
        QVector<int> ids;
        for (const Track &t : m_model->tracks()) {
            for (const TrackEvent &ev : t.events) {
                if (ev.selected) {
                    ids.push_back(ev.id);
                }
            }
        }
        bool any = false;
        if (!ids.isEmpty()) {
            emitDocumentEditBegan();
            for (int id : ids) {
                any = m_model->splitEventAt(id, ph) || any;
            }
            emitDocumentEditCommitted(tr("Split"));
        }
        if (any) {
            update();
            event->accept();
            return;
        }
    }
    QWidget::keyPressEvent(event);
}

void TimelineView::leaveEvent(QEvent *event)
{
    if (m_hoverResizeTrack != -1 || m_hoverReorderTrack != -1
        || m_hoverButton != EventChromeButton::None || m_hoverLevelEventId >= 0) {
        m_hoverResizeTrack = -1;
        m_hoverReorderTrack = -1;
        m_hoverButton = EventChromeButton::None;
        m_hoverLevelEventId = -1;
        update();
    }
    if (!m_resizingHeader && m_resizingTrackIndex < 0 && m_reorderingTrack < 0) {
        unsetCursor();
    }
    QToolTip::hideText();
    QWidget::leaveEvent(event);
}

void TimelineView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    clampScroll();
    positionTrackNameEdit();
    positionMarkerLabelEdit();
    emit scrollMetricsChanged();
}

void TimelineView::wheelEvent(QWheelEvent *event)
{
    if (!m_model) {
        return;
    }

    const QPoint delta = event->pixelDelta().isNull() ? event->angleDelta() / 8 : event->pixelDelta();
    const int wheelY = event->angleDelta().y();

    // Vegas Pro: plain wheel zooms time (Ctrl is reserved for toggle-select on click).
    // Shift+wheel → horizontal pan; Alt+wheel → vertical track scroll.
    if (event->modifiers() & Qt::ShiftModifier) {
        setScrollX(m_scrollX - delta.y() - delta.x());
        event->accept();
        return;
    }
    if (event->modifiers() & Qt::AltModifier) {
        if (maxScrollY() > 0) {
            setScrollY(m_scrollY - delta.y());
        }
        event->accept();
        return;
    }
    if (std::abs(delta.x()) > std::abs(delta.y()) && delta.x() != 0) {
        // Trackpad / tilt wheel horizontal gesture → pan time
        setScrollX(m_scrollX - delta.x());
        event->accept();
        return;
    }

    if (wheelY == 0) {
        event->ignore();
        return;
    }

    const double oldPps = m_model->pixelsPerSecond();
    const double factor = wheelY > 0 ? 1.15 : 1.0 / 1.15;
    // Keep timeline time under the cursor stable while zooming
    const QPoint pos = event->position().toPoint();
    const double anchorSec = xToTime(pos.x());
    m_model->setPixelsPerSecond(oldPps * factor);
    ensureContentWidth();
    const int newX = timeToX(anchorSec);
    setScrollX(m_scrollX + (newX - pos.x()));
    update();
    emit scrollOffsetChanged();
    event->accept();
}

void TimelineView::dragEnterEvent(QDragEnterEvent *event)
{
    if (!MediaMime::hasMediaPayload(event->mimeData())) {
        return;
    }
    event->acceptProposedAction();
    updateDropGhost(event->position().toPoint(), event->mimeData());
}

void TimelineView::dragMoveEvent(QDragMoveEvent *event)
{
    if (!MediaMime::hasMediaPayload(event->mimeData())) {
        event->ignore();
        return;
    }
    const QPoint pos = event->position().toPoint();
    // Accept timeline body and track header / empty controller area
    if (pos.y() < rulerHeight() && !isTrackHeaderDropZone(pos)) {
        clearDropGhost();
        event->ignore();
        return;
    }
    event->acceptProposedAction();
    updateDropGhost(pos, event->mimeData());
}

void TimelineView::dragLeaveEvent(QDragLeaveEvent *event)
{
    clearDropGhost();
    QWidget::dragLeaveEvent(event);
}

void TimelineView::dropEvent(QDropEvent *event)
{
    clearDropGhost();
    const QPoint pos = event->position().toPoint();
    if (pos.y() < rulerHeight() && !isTrackHeaderDropZone(pos)) {
        event->ignore();
        return;
    }

    QStringList names;
    QStringList kinds;
    QStringList paths;
    QVector<double> lengths;
    MediaMime::parse(event->mimeData(), &names, &kinds, &paths, &lengths);
    if (names.isEmpty()) {
        event->ignore();
        return;
    }

    const double t = dropTargetTimeSec(pos);
    const int trackIndex = dropTargetTrackIndex(pos);

    for (int i = 0; i < names.size(); ++i) {
        const QString kind = i < kinds.size() ? kinds[i] : QString();
        const QString path = i < paths.size() ? paths[i] : QString();
        double len = (i < lengths.size()) ? lengths[i] : 0.0;
        len = MediaProbe::lengthForInsert(path, kind, len);
        emit mediaDropRequested(names[i], kind, t, trackIndex, len, path);
    }
    event->acceptProposedAction();
}

bool TimelineView::mimeHasMedia(const QMimeData *md)
{
    return MediaMime::hasMediaPayload(md);
}

void TimelineView::parseMediaMime(const QMimeData *md, QStringList *names, QStringList *kinds,
                                  QStringList *paths, QVector<double> *lengths)
{
    MediaMime::parse(md, names, kinds, paths, lengths);
}

void TimelineView::clearDropGhost()
{
    if (!m_dropGhostActive) {
        return;
    }
    m_dropGhostActive = false;
    update();
}

void TimelineView::updateDropGhost(const QPoint &pos, const QMimeData *md)
{
    QStringList names;
    QStringList kinds;
    QStringList paths;
    QVector<double> lengths;
    MediaMime::parse(md, &names, &kinds, &paths, &lengths);
    if (names.isEmpty() || (pos.y() < rulerHeight() && !isTrackHeaderDropZone(pos))) {
        clearDropGhost();
        return;
    }

    QString kind = kinds.isEmpty() ? QString() : kinds.first();
    const QString path = paths.isEmpty() ? QString() : paths.first();
    if (kind.isEmpty()) {
        kind = MediaMime::guessKind(path.isEmpty() ? names.first() : path);
    }

    double len = lengths.isEmpty() ? 0.0 : lengths.first();
    len = MediaProbe::lengthForInsert(path, kind, len);

    m_dropGhostActive = true;
    m_dropGhostStartSec = dropTargetTimeSec(pos);
    m_dropGhostLengthSec = len;
    m_dropGhostKind = kind;
    m_dropGhostTrack = dropTargetTrackIndex(pos);
    update();
}

void TimelineView::paintDropGhost(QPainter &p)
{
    if (!m_dropGhostActive || !m_model) {
        return;
    }

    const int x0 = timeToX(m_dropGhostStartSec);
    const int x1 = timeToX(m_dropGhostStartSec + m_dropGhostLengthSec);

    const QString kind = m_dropGhostKind.toLower();
    const bool isAudio = (kind == QLatin1String("audio"));
    const bool isStill = (kind == QLatin1String("still") || kind == QLatin1String("image"));
    const bool isVideo = !isAudio && !isStill;

    auto paintGhostOnTrack = [&](int ti, const QColor &fill, const QColor &stroke) {
        if (ti < 0 || ti >= m_model->tracks().size()) {
            return;
        }
        const int y = trackY(ti);
        const int h = m_model->tracks()[ti].height;
        if (m_dropGhostStartSec <= 0.001) {
            p.fillRect(0, y, headerWidth(), h, QColor(0, 120, 215, 50));
        }
        if (x1 <= m_headerWidth) {
            return;
        }
        QRect r(x0, y, std::max(8, x1 - x0), h);
        r = r.intersected(QRect(m_headerWidth, rulerHeight(), width() - m_headerWidth,
                                height() - rulerHeight()));
        if (r.width() < 2) {
            return;
        }
        p.fillRect(r, fill);
        p.setPen(QPen(stroke, 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(r.adjusted(0, 0, -1, -1));
    };

    if (m_dropGhostTrack == kDropCreateNewTracks) {
        const int y = std::max(tracksBottomY(), rulerHeight());
        if (isAudio) {
            const int ah = defaultTrackHeight(TrackKind::Audio);
            p.fillRect(0, y, headerWidth(), ah, QColor(0, 120, 215, 45));
            if (x1 > m_headerWidth) {
                QRect ar(x0, y, std::max(8, x1 - x0), ah);
                p.fillRect(ar, QColor(160, 120, 100, 80));
                p.setPen(QPen(QColor(255, 200, 160, 220), 1, Qt::DashLine));
                p.drawRect(ar.adjusted(0, 0, -1, -1));
            }
        } else if (isStill) {
            const int vh = defaultTrackHeight(TrackKind::Video);
            p.fillRect(0, y, headerWidth(), vh, QColor(0, 120, 215, 45));
            if (x1 > m_headerWidth) {
                QRect vr(x0, y, std::max(8, x1 - x0), vh);
                p.fillRect(vr, QColor(100, 80, 160, 80));
                p.setPen(QPen(QColor(180, 160, 255, 220), 1, Qt::DashLine));
                p.drawRect(vr.adjusted(0, 0, -1, -1));
            }
        } else {
            const int vh = defaultTrackHeight(TrackKind::Video);
            const int ah = defaultTrackHeight(TrackKind::Audio);
            p.fillRect(0, y, headerWidth(), vh + ah, QColor(0, 120, 215, 45));
            if (x1 > m_headerWidth) {
                QRect vr(x0, y, std::max(8, x1 - x0), vh);
                QRect ar(x0, y + vh, std::max(8, x1 - x0), ah);
                p.fillRect(vr, QColor(100, 80, 160, 70));
                p.fillRect(ar, QColor(160, 120, 100, 70));
                p.setPen(QPen(QColor(180, 160, 255, 220), 1, Qt::DashLine));
                p.drawRect(vr.adjusted(0, 0, -1, -1));
                p.setPen(QPen(QColor(255, 200, 160, 220), 1, Qt::DashLine));
                p.drawRect(ar.adjusted(0, 0, -1, -1));
            }
        }
        p.setPen(QPen(QColor(90, 170, 255, 200), 1, Qt::DashLine));
        p.drawLine(x0, rulerHeight(), x0, height());
        return;
    }

    if (isVideo) {
        // Ghost A/V pair like Vegas
        int vi = m_dropGhostTrack;
        int ai = -1;
        if (vi < 0 || vi >= m_model->tracks().size() || m_model->tracks()[vi].kind != TrackKind::Video) {
            for (int i = 0; i < m_model->tracks().size(); ++i) {
                if (m_model->tracks()[i].kind == TrackKind::Video) {
                    vi = i;
                    break;
                }
            }
        }
        if (vi >= 0 && vi + 1 < m_model->tracks().size()
            && m_model->tracks()[vi + 1].kind == TrackKind::Audio) {
            ai = vi + 1;
        } else {
            for (int i = 0; i < m_model->tracks().size(); ++i) {
                if (m_model->tracks()[i].kind == TrackKind::Audio) {
                    ai = i;
                    break;
                }
            }
        }
        paintGhostOnTrack(vi, QColor(100, 80, 160, 70), QColor(180, 160, 255, 220));
        paintGhostOnTrack(ai, QColor(160, 120, 100, 70), QColor(255, 200, 160, 220));
        // Snap guide at drop time
        p.setPen(QPen(QColor(90, 170, 255, 200), 1, Qt::DashLine));
        p.drawLine(x0, rulerHeight(), x0, height());
        return;
    }

    int ti = m_dropGhostTrack;
    if (isAudio) {
        if (ti < 0 || ti >= m_model->tracks().size() || m_model->tracks()[ti].kind != TrackKind::Audio) {
            for (int i = 0; i < m_model->tracks().size(); ++i) {
                if (m_model->tracks()[i].kind == TrackKind::Audio) {
                    ti = i;
                    break;
                }
            }
        }
        paintGhostOnTrack(ti, QColor(160, 120, 100, 80), QColor(255, 200, 160, 220));
    } else {
        if (ti < 0 || ti >= m_model->tracks().size() || m_model->tracks()[ti].kind != TrackKind::Video) {
            for (int i = 0; i < m_model->tracks().size(); ++i) {
                if (m_model->tracks()[i].kind == TrackKind::Video) {
                    ti = i;
                    break;
                }
            }
        }
        paintGhostOnTrack(ti, QColor(100, 80, 160, 80), QColor(180, 160, 255, 220));
    }
    p.setPen(QPen(QColor(90, 170, 255, 200), 1, Qt::DashLine));
    p.drawLine(x0, rulerHeight(), x0, height());
}

void TimelineView::contextMenuEvent(QContextMenuEvent *event)
{
    if (event->pos().y() < rulerHeight() && event->pos().x() >= headerWidth()) {
        emit rulerContextMenuRequested(event->globalPos());
        return;
    }
    if (event->pos().x() < headerWidth() && event->pos().y() >= rulerHeight() && m_model) {
        int y = rulerHeight() - m_scrollY;
        for (int i = 0; i < m_model->tracks().size(); ++i) {
            const Track &track = m_model->tracks()[i];
            if (event->pos().y() >= y && event->pos().y() < y + track.height) {
                emit trackHeaderContextMenuRequested(i, event->globalPos());
                return;
            }
            y += track.height;
        }
    }

    // Right-click on event chrome FX button → Bypass / Enable / Delete All menu
    {
        Hit btnHit;
        if (eventButtonAt(event->pos(), &btnHit) == EventChromeButton::Fx && btnHit.eventId >= 0) {
            m_model->selectEvent(btnHit.eventId, false);
            update();
            emit eventFxMenuRequested(btnHit.eventId, event->globalPos());
            return;
        }
    }

    // Right-click on fade handle / create corner → curve shape popup
    Hit fadeHit;
    const EventEditMode fadeMode = eventEditModeAt(event->pos(), &fadeHit);
    if (fadeHit.eventId >= 0
        && (fadeMode == EventEditMode::FadeIn || fadeMode == EventEditMode::FadeOut)) {
        m_model->selectEvent(fadeHit.eventId, false);
        update();
        showFadeCurvePopup(fadeHit.eventId, fadeMode == EventEditMode::FadeIn, event->globalPos());
        return;
    }

    auto hit = hitTest(event->pos());
    if (hit && hit->eventId >= 0) {
        m_model->selectEvent(hit->eventId, false);
        update();
        emit eventContextMenuRequested(hit->eventId, event->globalPos());
        return;
    }
    if (m_model && event->pos().y() >= rulerHeight()) {
        int y = rulerHeight() - m_scrollY;
        for (int i = 0; i < m_model->tracks().size(); ++i) {
            const Track &track = m_model->tracks()[i];
            if (event->pos().y() >= y && event->pos().y() < y + track.height) {
                emit trackEmptyContextMenuRequested(i, event->globalPos());
                return;
            }
            y += track.height;
        }
    }
    emit emptyAreaContextMenuRequested(event->globalPos());
}

void TimelineView::showFadeCurvePopup(int eventId, bool fadeIn, const QPoint &globalPos)
{
    if (!m_model) {
        return;
    }
    TrackEvent *ev = m_model->findEvent(eventId);
    if (!ev) {
        return;
    }

    if (!m_fadeCurvePopup) {
        m_fadeCurvePopup = new FadeCurvePopup(this);
        connect(m_fadeCurvePopup, &FadeCurvePopup::curveChosen, this, [this](FadeCurveType type) {
            if (!m_model || m_fadeCurveEventId < 0) {
                return;
            }
            TrackEvent *e = m_model->findEvent(m_fadeCurveEventId);
            if (!e) {
                return;
            }
            emitDocumentEditBegan();
            if (m_fadeCurveIsIn) {
                e->fadeInCurve = type;
                if (e->fadeInSec < 0.05) {
                    const double maxFade =
                        std::max(0.05, e->lengthSec - e->fadeOutSec - minEventLengthSec());
                    e->fadeInSec = std::clamp(std::min(0.75, e->lengthSec * 0.2), 0.05, maxFade);
                }
            } else {
                e->fadeOutCurve = type;
                if (e->fadeOutSec < 0.05) {
                    const double maxFade =
                        std::max(0.05, e->lengthSec - e->fadeInSec - minEventLengthSec());
                    e->fadeOutSec = std::clamp(std::min(0.75, e->lengthSec * 0.2), 0.05, maxFade);
                }
            }
            clampEventFades(*e);
            // Mirror curve onto grouped partner when grouping is honored
            if (!m_model->ignoreEventGrouping() && e->groupId > 0) {
                for (int id : m_model->eventIdsInGroup(e->groupId)) {
                    if (id == e->id) {
                        continue;
                    }
                    if (TrackEvent *peer = m_model->findEvent(id)) {
                        if (m_fadeCurveIsIn) {
                            peer->fadeInCurve = type;
                            peer->fadeInSec = e->fadeInSec;
                        } else {
                            peer->fadeOutCurve = type;
                            peer->fadeOutSec = e->fadeOutSec;
                        }
                        clampEventFades(*peer);
                    }
                }
            }
            emitDocumentEditCommitted(tr("Fade Curve"));
            update();
        });
    }

    m_fadeCurveEventId = eventId;
    m_fadeCurveIsIn = fadeIn;
    m_fadeCurvePopup->setFadeIn(fadeIn);
    m_fadeCurvePopup->setCurrent(fadeIn ? ev->fadeInCurve : ev->fadeOutCurve);
    m_fadeCurvePopup->popupAt(globalPos);
}

} // namespace openvegas
