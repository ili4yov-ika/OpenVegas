#pragma once

#include "model/ProjectModel.h"

#include <QWidget>
#include <QPoint>
#include <QRect>
#include <QElapsedTimer>
#include <QHash>
#include <optional>

class QLineEdit;
class QTimer;
class QKeyEvent;
class QMimeData;

namespace openvegas {

class FadeCurvePopup;

class TimelineView : public QWidget {
    Q_OBJECT
public:
    explicit TimelineView(ProjectModel *model, QWidget *parent = nullptr);

    ProjectModel *model() const { return m_model; }
    void refreshLayout();
    void beginTrackRename(int trackIndex);

    int headerWidth() const { return m_headerWidth; }
    void setHeaderWidth(int width);

    int scrollX() const { return m_scrollX; }
    int scrollY() const { return m_scrollY; }
    void setScrollX(int x);
    void setScrollY(int y);
    int contentWidthPx() const;
    int tracksHeightPx() const;
    int maxScrollX() const;
    int maxScrollY() const;
    int rulerHeight() const { return markerLaneHeight() + ticksHeight(); }
    int markerLaneHeight() const { return 18; }
    int ticksHeight() const { return 22; }

    bool isPlaying() const { return m_playing; }
    void setPlaying(bool playing);
    void stopPlayback();
    void togglePlaying();
    /** Seek playhead; optionally stop playback. */
    void seekPlayhead(double sec, bool stop = false);
    void stepFrames(int deltaFrames);

    /**
     * When true, AudioEngine owns time during Play — the local QTimer only
     * refreshes UI from model playhead (does not advance it).
     */
    void setExternalTransportClock(bool on);
    bool externalTransportClock() const { return m_externalTransportClock; }

    /** Shuttle rate −20…+20 (0 = idle). Non-zero drives the playhead while held. */
    double shuttleRate() const { return m_shuttleRate; }
    void setShuttleRate(double rate);

    /** Insert numbered marker at the playhead and start label edit. */
    void insertMarkerAtPlayhead();
    /** Place / move Loop Region starting at the playhead. */
    void insertLoopRegionAtPlayhead();
    /** Visible timeline span (seconds), excluding the track header. */
    void visibleTimeRange(double *startSec, double *endSec) const;
    void beginMarkerRename(int markerId);

signals:
    void eventDoubleClicked(int eventId);
    void eventContextMenuRequested(int eventId, const QPoint &globalPos);
    void eventPanCropRequested(int eventId);
    void eventFxRequested(int eventId);
    /** Leftmost chrome button on a generated-media event (e.g. Titles & Text). */
    void eventGeneratorRequested(int eventId);
    /** Right-click on event chrome "fx" button. */
    void eventFxMenuRequested(int eventId, const QPoint &globalPos);
    void eventMoreMenuRequested(int eventId, const QPoint &globalPos);
    /** Button on a transition strip → open that transition's properties window. */
    void transitionPropertiesRequested(int eventId, bool fadeIn);
    void emptyAreaContextMenuRequested(const QPoint &globalPos);
    void playheadChanged(double seconds);
    void headerWidthChanged(int width);
    void headerWidthEditFinished(int width);
    void trackHeaderContextMenuRequested(int trackIndex, const QPoint &globalPos);
    /** Audio/video header "…" (More) chip. */
    void trackMoreMenuRequested(int trackIndex, const QPoint &globalPos);
    /** Header "fx" chip → Track FX. */
    void trackFxRequested(int trackIndex);
    void trackEmptyContextMenuRequested(int trackIndex, const QPoint &globalPos);
    /** Time-ticks strip (below marker lane). */
    void rulerContextMenuRequested(const QPoint &globalPos);
    /** Marker / loop lane (empty area or loop bar). */
    void markerLaneContextMenuRequested(const QPoint &globalPos);
    /** Right-click on a marker head. */
    void markerContextMenuRequested(int markerId, const QPoint &globalPos);
    void scrollMetricsChanged();
    void scrollOffsetChanged();
    void playingChanged(bool playing);
    /**
     * Drop from Project Media (or files): place at timeline time on optional track.
     * extra carries synthetic per-kind payload (e.g. a Titles & Text animation key)
     * for path-less generator drops.
     */
    void mediaDropRequested(const QString &name, const QString &kind, double timeSec, int trackIndex,
                            double lengthSec, const QString &path, const QString &extra = QString());
    /** Document mutation about to begin / finished (for Undo). */
    void documentEditBegan();
    void documentEditCommitted(const QString &text);
    /** Event gain / fades changed — soft-sync AudioEngine without rebuild. */
    void liveAudioParamsChanged();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Hit {
        int trackIndex = -1;
        int eventId = -1;
        QRect eventRect;
    };

    enum class EventEditMode {
        None,
        Move,
        TrimStart,
        TrimEnd,
        FadeIn,
        FadeOut,
        Level // opacity (video) / gain (audio)
    };

    enum class EventChromeButton {
        None,
        Generator,
        PanCrop,
        Fx,
        More
    };

    enum class RulerDragMode {
        None,
        Playhead,
        Marker,
        LoopCreate,
        LoopMove,
        LoopStart,
        LoopEnd
    };

    int railWidth() const { return 26; }
    int splitterHitPad() const { return 4; }
    int trackResizeHitPad() const { return 5; }
    int eventEdgeHitPad() const { return 6; }
    int fadeCornerHitPad() const { return 12; }
    int minTrackHeight() const { return 36; }
    int maxTrackHeight() const { return 420; }
    double minEventLengthSec() const { return 0.05; }
    int defaultTrackHeight(TrackKind kind) const;
    bool nearHeaderSplitter(const QPoint &pos) const;
    int trackResizeIndexAt(const QPoint &pos) const;
    int trackIndexAtY(int y) const;
    /** Y of the bottom edge of the last track (next free slot). */
    int tracksBottomY() const;
    bool isBelowTracksDropZone(const QPoint &pos) const;
    /** Left track-controller column (below ruler). */
    bool isTrackHeaderDropZone(const QPoint &pos) const;
    /** Header hit → track index; empty header/body → kDropCreateNewTracks. */
    int dropTargetTrackIndex(const QPoint &pos) const;
    double dropTargetTimeSec(const QPoint &pos) const;
    int reorderInsertIndexAtY(int y) const;
    bool isHeaderEmptyDragSpace(const QPoint &pos, int *outTrackIndex = nullptr) const;
    double xToTime(int x) const;
    int timeToX(double sec) const;
    int trackY(int trackIndex) const;
    QRect eventRect(const Track &track, const TrackEvent &ev, int trackTop) const;
    std::optional<Hit> hitTest(const QPoint &pos) const;
    EventEditMode eventEditModeAt(const QPoint &pos, Hit *outHit = nullptr) const;
    void clampEventFades(TrackEvent &ev) const;
    void paintRuler(QPainter &p);
    void paintLoopRegion(QPainter &p);
    void paintMarkers(QPainter &p);
    void paintTracks(QPainter &p);
    void paintTrackHeader(QPainter &p, const Track &track, int index, int y);
    void paintReorderCue(QPainter &p);
    void paintEventBlock(QPainter &p, const Track &track, const TrackEvent &ev, const QRect &r);
    void paintEventFades(QPainter &p, const Track &track, const TrackEvent &ev, const QRect &r);
    void paintEventChrome(QPainter &p, const Track &track, const TrackEvent &ev, const QRect &r);
    void paintCrossfades(QPainter &p, const Track &track, int trackTop);
    /** Vegas strip over a fade/crossfade carrying a transition: name + properties button. */
    void paintTransitionStrips(QPainter &p, const Track &track, int trackTop);
    /** Screen rect of the strip for `ev`'s fade-in / fade-out, empty when there is none. */
    QRect transitionStripRect(const Track &track, const TrackEvent &ev, int trackTop,
                              bool fadeIn) const;
    /** Strip (and whether it is the fade-in one) under `pos`; eventId < 0 when none. */
    int transitionStripAt(const QPoint &pos, bool *outFadeIn, bool *outOnButton) const;
    /** Attaches a dropped transition preset to the fade/crossfade under `pos`.
     *  Returns false when the drop did not land on one. */
    bool applyTransitionDrop(const QPoint &pos, const QString &pluginId, const QString &presetName);
    /**
     * Append a Video FX to the event under the cursor (Video FX pane drag).
     * False when the drop missed a video event, so the drag is rejected rather than
     * silently doing nothing.
     */
    bool applyVideoFxDrop(const QPoint &pos, const QString &pluginName, const QString &presetName);
    /** Properties button inside a strip, empty when the strip is tiny. Vegas makes it a
     *  square the full height of the strip, butted against the strip's right edge. */
    static QRect transitionButtonRect(const QRect &strip)
    {
        const int side = strip.height();
        if (side < 10 || strip.width() < side + 14) {
            return QRect();
        }
        return QRect(strip.right() - side + 1, strip.top(), side, side);
    }
    void showFadeCurvePopup(int eventId, bool fadeIn, const QPoint &globalPos);
    void emitDocumentEditBegan();
    void emitDocumentEditCommitted(const QString &text);
    void paintVideoThumbs(QPainter &p, const QRect &body, const TrackEvent &ev);
    void paintStillImage(QPainter &p, const QRect &body, const TrackEvent &ev);
    void paintTitlesTextThumb(QPainter &p, const QRect &body, const TrackEvent &ev);
    void paintAudioWave(QPainter &p, const QRect &body, const TrackEvent &ev);
    void paintEventMediaMarkers(QPainter &p, const TrackEvent &ev, const QRect &body);
    QString eventMediaPath(const TrackEvent &ev) const;
    double overlapSec(const TrackEvent &a, const TrackEvent &b) const;
    double incomingCrossfadeSec(const Track &track, const TrackEvent &ev) const;
    double outgoingCrossfadeSec(const Track &track, const TrackEvent &ev) const;
    /** Screen-space "resistance" radius (converted to seconds via current zoom) that a
     *  dragged clip edge must clear before it's allowed to cross a neighbor's touching
     *  edge — i.e. the extra mouse "effort" needed to open/close a crossfade instead of
     *  the two clips just sitting flush. See magnetSnapToNeighbor(). */
    double edgeMagnetThresholdSec() const;
    /** Nearest event on `track` (excluding `excludeId`) that starts before `refStart` —
     *  its end is the "touching" position for a clip's start edge. -1 if none. */
    double nearestLeftNeighborEndSec(const Track &track, int excludeId, double refStart) const;
    /** Nearest event on `track` (excluding `excludeId`) that starts after `refEnd` — its
     *  start is the "touching" position for a clip's end edge. -1 if none. */
    double nearestRightNeighborStartSec(const Track &track, int excludeId, double refEnd) const;
    /** If `touchSec` is valid and within edgeMagnetThresholdSec() of `rawSec`, returns
     *  `touchSec` (clip edge sticks flush to the neighbor); otherwise returns `rawSec`
     *  unchanged. Symmetric by construction: works the same whether the edge is
     *  approaching from a gap (about to open a crossfade) or from an overlap (about to
     *  pull apart into a gap). */
    double magnetSnapToNeighbor(double rawSec, double touchSec) const;
    /** Left inset (px) so title/level/buttons clear an incoming crossfade. */
    int eventChromeLeftInset(const Track &track, const TrackEvent &ev) const;
    int eventChromeRightInset(const Track &track, const TrackEvent &ev) const;
    QRect eventContentRect(const Track &track, const TrackEvent &ev, const QRect &r) const;
    /** Body band where opacity/gain travels (below title, above button strip). */
    QRect eventLevelBodyRect(const Track &track, const TrackEvent &ev, const QRect &r) const;
    int levelYFromNormalized(const QRect &body, double n) const;
    double normalizedFromLevelY(const QRect &body, int y) const;
    QRect eventLevelHandleRect(const Track &track, const TrackEvent &ev, const QRect &r) const;
    double eventLevelNormalized(const TrackEvent &ev) const;
    void setEventLevelFromNormalized(TrackEvent &ev, double n) const;
    QString eventLevelTooltip(const TrackEvent &ev) const;
    QRect eventButtonRect(const Track &track, const TrackEvent &ev, const QRect &r,
                          EventChromeButton button) const;
    EventChromeButton eventButtonAt(const QPoint &pos, Hit *outHit = nullptr) const;
    void paintPlayhead(QPainter &p);
    void paintDropGhost(QPainter &p);
    void updateDropGhost(const QPoint &pos, const QMimeData *md);
    void clearDropGhost();
    static bool mimeHasMedia(const QMimeData *md);
    /** Payload kind of a drag ("video", "transition", "videofx", …); empty when unknown. */
    static QString mimeDropKind(const QMimeData *md);
    /** Id of the video event under `pos`, or 0 when there is none. */
    int videoEventIdAt(const QPoint &pos) const;
    static void parseMediaMime(const QMimeData *md, QStringList *names, QStringList *kinds,
                               QStringList *paths, QVector<double> *lengths);
    void paintHeaderSplitter(QPainter &p);
    void paintMiniButton(QPainter &p, const QRect &r, const QString &text, bool active,
                         const QColor &activeBg);
    void paintSlider(QPainter &p, const QRect &r, const QString &label, const QString &value,
                     double normalized);
    void paintCropGlyph(QPainter &p, const QRect &r) const;
    void paintGeneratorGlyph(QPainter &p, const QRect &r) const;
    void ensureContentWidth();
    void updateHoverCursor(const QPoint &pos);
    void updateEventChromeTooltip(const QPoint &pos);
    void clampScroll();
    void moveTrack(int fromIndex, int insertBefore);
    QRect trackNameRect(int trackIndex) const;
    void positionTrackNameEdit();
    void finishTrackRename(bool accept);

    QRect markerHeadRect(const TimelineMarker &marker) const;
    QRect markerLabelRect(const TimelineMarker &marker) const;
    int markerAtPos(const QPoint &pos) const;
    RulerDragMode loopHitAt(const QPoint &pos) const;
    QRect loopBarRect() const;
    QRect loopSeedRect() const;
    void positionMarkerLabelEdit();
    void finishMarkerRename(bool accept);

    ProjectModel *m_model = nullptr;
    FadeCurvePopup *m_fadeCurvePopup = nullptr;
    int m_fadeCurveEventId = -1;
    bool m_fadeCurveIsIn = true;
    QLineEdit *m_trackNameEdit = nullptr;
    QLineEdit *m_markerLabelEdit = nullptr;
    int m_renamingTrackIndex = -1;
    int m_renamingMarkerId = -1;
    int m_headerWidth = 210;
    int m_scrollX = 0;
    int m_scrollY = 0;
    bool m_dragging = false;
    bool m_draggingPlayhead = false;
    RulerDragMode m_rulerDrag = RulerDragMode::None;
    int m_dragMarkerId = -1;
    double m_dragMarkerOriginTime = 0.0;
    double m_dragLoopOriginStart = 0.0;
    double m_dragLoopOriginEnd = 0.0;
    double m_dragLoopCreateOrigin = 0.0;
    bool m_resizingHeader = false;
    int m_resizingTrackIndex = -1;
    int m_resizeTrackOriginHeight = 0;
    int m_resizeTrackOriginY = 0;
    int m_hoverResizeTrack = -1;
    int m_dragEventId = -1;
    /** Last plain- or Ctrl-clicked event; a following Shift-click range-selects between
     *  this and the newly clicked event. Shift-clicks never move it, so repeated
     *  Shift-clicks keep extending/shrinking the same range (Explorer/file-manager
     *  convention). Not persisted in ProjectModel — it's transient view state, not
     *  something Undo/serialization needs to know about. */
    int m_selectionAnchorEventId = -1;
    /** Set on a plain (no-modifier) press over a clip that's already part of a
     *  multi-selection: the selection-collapse-to-just-this-clip is deferred until
     *  mouseReleaseEvent, so a press-and-drag on any selected clip moves the whole
     *  selection, while a plain click-and-release still narrows down to one clip
     *  (Explorer/every-NLE convention). -1 when no collapse is pending. */
    int m_deferredSingleSelectEventId = -1;
    double m_dragOriginStart = 0.0;
    double m_dragOriginLength = 0.0;
    double m_dragOriginFadeIn = 0.0;
    double m_dragOriginFadeOut = 0.0;
    double m_dragOriginLevel = 1.0;
    int m_dragOriginX = 0;
    int m_dragOriginY = 0;
    EventEditMode m_eventEditMode = EventEditMode::None;
    int m_reorderingTrack = -1;
    int m_reorderInsertBefore = -1;
    int m_hoverReorderTrack = -1;
    EventChromeButton m_hoverButton = EventChromeButton::None;
    int m_hoverLevelEventId = -1;
    /** Transition strip whose properties button the pointer is over (-1 = none). Vegas only
     *  plates that button while it is hovered; at rest the glyph sits bare on the strip. */
    int m_hoverTransitionEventId = -1;
    bool m_hoverTransitionFadeIn = false;
    /** eventId → origin startSec for grouped move / trim */
    QHash<int, double> m_dragGroupOrigins;
    /** eventId → origin lengthSec for grouped trim */
    QHash<int, double> m_dragGroupLengths;
    /** Track created during this drag by dropping below the timeline; -1 if none. */
    int m_dragCreatedTrack = -1;
    bool m_docEditOpen = false;

    bool m_dropGhostActive = false;
    double m_dropGhostStartSec = 0.0;
    double m_dropGhostLengthSec = 8.0;
    QString m_dropGhostKind;
    int m_dropGhostTrack = -1;

    bool m_playing = false;
    bool m_externalTransportClock = false;
    double m_shuttleRate = 0.0;
    bool m_playheadBlinkLight = true;
    QTimer *m_blinkTimer = nullptr;
    QTimer *m_playTimer = nullptr;
    QElapsedTimer m_playClock;
};

} // namespace openvegas
