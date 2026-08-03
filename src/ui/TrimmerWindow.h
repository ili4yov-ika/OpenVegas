#pragma once

#include "model/ProjectModel.h"

#include <QVector>
#include <QWidget>
#include <QString>
#include <functional>

class QLabel;
class QToolButton;
class QTimer;
class QHBoxLayout;

namespace openvegas {

class TrimmerCanvas;

class TrimmerWindow : public QWidget {
    Q_OBJECT
public:
    explicit TrimmerWindow(QWidget *parent = nullptr);
    ~TrimmerWindow() override;

    /** Legacy helper — infers kind from name. */
    void setMediaName(const QString &name);
    void setMedia(const QString &name, EventMediaKind kind, double durationSec,
                  const QString &pathHint = QString(),
                  const QVector<TimelineMarker> &markers = {}, bool reversed = false);

    QVector<TimelineMarker> markers() const { return m_markers; }
    void setMarkers(const QVector<TimelineMarker> &markers);
    bool markersVisible() const { return m_markersVisible; }
    void setMarkersVisible(bool on);

    /** Persist Event Media Markers back to the project media pool. */
    std::function<void(const QString &mediaPath, const QVector<TimelineMarker> &)> onMarkersChanged;

private:
    void buildUi();
    void rebuildTransport();
    void updateChrome();
    void updateStatusLabels();
    void showMoreMenu();
    void togglePlay(bool play);
    void stopPlayback();
    void seekTo(double sec);
    void setInPoint();
    void setOutPoint();
    void insertMarker();
    void insertRegion();
    void runBeatDetection();
    void clearMarkers();
    void renumberMarkers();
    void renameMarker(int markerId);
    void deleteMarker(int markerId);
    void moveMarker(int markerId, double mediaTimeSec);
    void notifyMarkersChanged();
    void setupShortcuts();
    QString formatTC(double sec) const;
    QToolButton *makeTransportBtn(const QString &tip, const QString &svg);

    TrimmerCanvas *m_canvas = nullptr;
    QLabel *m_mediaTitle = nullptr;
    QLabel *m_posLabel = nullptr;
    QLabel *m_inLabel = nullptr;
    QLabel *m_outLabel = nullptr;
    QLabel *m_lenLabel = nullptr;
    QWidget *m_transportHost = nullptr;
    QHBoxLayout *m_transportLay = nullptr;
    QToolButton *m_loopBtn = nullptr;
    QToolButton *m_moreBtn = nullptr;
    QTimer *m_timer = nullptr;

    QString m_name;
    QString m_path;
    EventMediaKind m_kind = EventMediaKind::Video;
    double m_duration = 10.0;
    double m_current = 0.0;
    double m_inPoint = 0.0;
    double m_outPoint = 10.0;
    bool m_loop = false;
    bool m_playing = false;
    bool m_overwrite = true;
    bool m_markersVisible = true;
    bool m_reversed = false;
    QVector<TimelineMarker> m_markers;
    int m_nextMarkerId = 1;
    int m_nextMarkerNumber = 1;
    int m_selectedMarkerId = 0;
};

} // namespace openvegas
