#pragma once

#include "model/ProjectModel.h"

#include <QDialog>
#include <QDoubleSpinBox>
#include <QListWidget>
#include <QLabel>
#include <QString>

namespace openvegas {

class VideoEventFxDialog : public QDialog {
    Q_OBJECT
public:
    explicit VideoEventFxDialog(QWidget *parent = nullptr);

    void setEvent(TrackEvent *ev);

private:
    void rebuildChainList();
    void ensurePanCropFirst();
    void syncUiFromChain();

    TrackEvent *m_event = nullptr;
    QLabel *m_eventName = nullptr;
    QListWidget *m_chain = nullptr;

    // Minimal set of Pan/Crop fields (matches screenshot layout conceptually)
    QDoubleSpinBox *m_width = nullptr;
    QDoubleSpinBox *m_height = nullptr;
    QDoubleSpinBox *m_xCenter = nullptr;
    QDoubleSpinBox *m_yCenter = nullptr;
    QDoubleSpinBox *m_angle = nullptr;
};

} // namespace openvegas
