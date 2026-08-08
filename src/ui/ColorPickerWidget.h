#pragma once

#include <QColor>
#include <QWidget>

class QSlider;
class QSpinBox;
class QLabel;

namespace openvegas {

class SvSquare;

/**
 * Functional color picker: saturation/value square + hue slider + alpha slider +
 * swatch + RGBA spinboxes. A working approximation, not a pixel match for Vegas's
 * own gradient-square/eyedropper picker chrome (explicitly out of scope for this
 * pass — see the Titles & Text generator implementation plan).
 */
class ColorPickerWidget : public QWidget {
    Q_OBJECT
public:
    explicit ColorPickerWidget(QWidget *parent = nullptr);

    QColor color() const;
    void setColor(const QColor &c);

signals:
    void colorChanged(const QColor &color);

private:
    void onSvChanged(double sat, double val);
    void onSliderChanged();
    void onSpinChanged();
    void syncAllFromColor();
    void emitColor();

    SvSquare *m_svSquare = nullptr;
    QSlider *m_hueSlider = nullptr;
    QSlider *m_alphaSlider = nullptr;
    QSpinBox *m_rSpin = nullptr;
    QSpinBox *m_gSpin = nullptr;
    QSpinBox *m_bSpin = nullptr;
    QSpinBox *m_aSpin = nullptr;
    QLabel *m_swatch = nullptr;

    double m_hue = 0.0;
    double m_sat = 0.0;
    double m_val = 1.0;
    int m_alpha = 255;
    bool m_block = false;
};

} // namespace openvegas
