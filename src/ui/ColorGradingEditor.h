#pragma once

#include "plugins/AudioPluginTypes.h"

#include <QWidget>
#include <QVector>
#include <QPointF>

class QTabWidget;
class QComboBox;
class QDoubleSpinBox;
class QLabel;

namespace openvegas {

/** Vegas-style Color Grading controls (wheels + curves) bound to an FxSlot.state. */
class ColorGradingEditor : public QWidget {
    Q_OBJECT
public:
    explicit ColorGradingEditor(FxSlot *slot, QWidget *parent = nullptr);

private:
    void buildUi();
    void loadFromSlot();
    void saveToSlot();
    void rebuildWheelsPage();
    void rebuildCurvesPage();

    FxSlot *m_slot = nullptr;
    QTabWidget *m_leftTabs = nullptr;
    QTabWidget *m_rightTabs = nullptr;
    QWidget *m_wheelsHost = nullptr;
    QWidget *m_curvesHost = nullptr;
};

} // namespace openvegas
