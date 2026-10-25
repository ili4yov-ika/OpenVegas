#pragma once

#include "ui/MediaGeneratorDialog.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QToolButton;

namespace openvegas {

class GradientCanvas;
class DxtColorBars;

/**
 * VEGAS Color Gradient's own page (sftrans1.dll dialog 191): Gradient Type and Aspect Ratio
 * Angle across the top; the control points drawn over the gradient, numbered, with the
 * buttons under them — add, remove, previous, next, and moving the selected point up or
 * down the order (strings 40003–40008); and the selected point's properties — its
 * distance and its colour.
 *
 * The parameters are the decoded block's (vegLegacyGeneratorParams): GradientType,
 * AspectAngle in radians (shown in degrees, as VEGAS shows it) and Points.
 */
class ColorGradientPage : public GeneratorPage {
    Q_OBJECT
public:
    explicit ColorGradientPage(QWidget *parent = nullptr);
    void setParams(const QVariantMap &params) override;

private:
    void refresh();
    void edited();
    void select(int index);
    QVariantList points() const;
    void setPoints(const QVariantList &points);
    double distanceOf(const QVariantMap &point) const;
    void setDistance(double d);
    void setPointColor(const QColor &c);

    QVariantMap m_params;
    int m_selected = 0;
    bool m_block = false;

    QComboBox *m_type = nullptr;
    QLineEdit *m_angle = nullptr;
    GradientCanvas *m_canvas = nullptr;
    QLineEdit *m_distance = nullptr;
    DxtColorBars *m_bars = nullptr;
    QSpinBox *m_r = nullptr;
    QSpinBox *m_g = nullptr;
    QSpinBox *m_b = nullptr;
    QSpinBox *m_a = nullptr;
    QLabel *m_hint = nullptr;
};

} // namespace openvegas
