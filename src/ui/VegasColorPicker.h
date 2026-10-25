#pragma once

#include <QColor>
#include <QWidget>

#include <functional>

class QComboBox;
class QLineEdit;
class QToolButton;

namespace openvegas {

/**
 * An eyedropper: the next click anywhere on screen samples the pixel under it and hands it
 * to `done` (Esc cancels). Used by the colour pickers of the generator pages.
 */
void pickScreenColor(std::function<void(const QColor &)> done);

class VegasColorPlane;
class VegasColorStrip;

/**
 * The colour control of VEGAS's OFX parameter pages (the Video Media Generators window,
 * Checkerboard's "Color 1" and the like): a swatch bar and the four channels as text
 * ("1.0, 1.0, 1.0, 1.0") on one row, and under it, when the row is expanded, a picker —
 * a colour model, the plane it draws (e.g. "(GxB) R": green across, blue up, red on the
 * slider), an eyedropper, the plane itself, the third channel's strip and an alpha strip
 * over a checkerboard.
 *
 * Values are straight (not premultiplied) RGBA in 0..1, as OFX RGBA parameters are.
 */
class VegasColorPicker : public QWidget {
    Q_OBJECT
public:
    explicit VegasColorPicker(QWidget *parent = nullptr);

    QColor color() const { return m_color; }
    void setColor(const QColor &c);

    bool isExpanded() const;
    void setExpanded(bool expanded);

    /** The swatch row alone, for a page that lays out its own label and arrow. */
    QWidget *rowWidget() const { return m_row; }

signals:
    void colorChanged(const QColor &color);
    void expandedChanged(bool expanded);

private:
    void syncFromColor();
    void applyText();
    void setFromPicker(const QColor &c);
    void startEyedropper();

    QColor m_color = Qt::white;
    QWidget *m_row = nullptr;
    QToolButton *m_swatch = nullptr;
    QLineEdit *m_text = nullptr;
    QWidget *m_panel = nullptr;
    QComboBox *m_model = nullptr;
    QComboBox *m_plane = nullptr;
    QToolButton *m_eyedropper = nullptr;
    VegasColorPlane *m_planeWidget = nullptr;
    VegasColorStrip *m_channelStrip = nullptr;
    VegasColorStrip *m_alphaStrip = nullptr;
    bool m_block = false;
};

} // namespace openvegas
