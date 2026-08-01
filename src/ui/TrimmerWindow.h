#pragma once

#include "model/ProjectModel.h"

#include <QWidget>
#include <QString>

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
                  const QString &pathHint = QString());

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
};

} // namespace openvegas
