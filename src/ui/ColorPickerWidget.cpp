#include "ui/ColorPickerWidget.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

namespace openvegas {

namespace {
constexpr int kSvSize = 150;
}

/**
 * Saturation (x) / Value (y) picker for a fixed hue; paints its own gradient.
 * Plain QWidget (no signals) — reports picks via a callback to avoid a
 * MOC-in-.cpp class just for this internal helper.
 */
class SvSquare : public QWidget {
public:
    explicit SvSquare(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(kSvSize, kSvSize);
    }

    void setOnPick(std::function<void(double, double)> cb) { m_onPick = std::move(cb); }

    void setHue(double hue)
    {
        m_hue = hue;
        update();
    }

    void setSatVal(double sat, double val)
    {
        m_sat = sat;
        m_val = val;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        QImage img(width(), height(), QImage::Format_RGB32);
        for (int y = 0; y < height(); ++y) {
            const double val = 1.0 - double(y) / std::max(1, height() - 1);
            auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
            for (int x = 0; x < width(); ++x) {
                const double sat = double(x) / std::max(1, width() - 1);
                line[x] = QColor::fromHsvF(m_hue / 360.0, sat, val).rgb();
            }
        }
        p.drawImage(0, 0, img);

        const int px = int(m_sat * (width() - 1));
        const int py = int((1.0 - m_val) * (height() - 1));
        p.setPen(QPen(Qt::white, 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPoint(px, py), 5, 5);
        p.setPen(QPen(Qt::black, 1));
        p.drawEllipse(QPoint(px, py), 6, 6);
    }

    void mousePressEvent(QMouseEvent *e) override { pick(e->pos()); }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (e->buttons() & Qt::LeftButton) {
            pick(e->pos());
        }
    }

private:
    void pick(const QPoint &pos)
    {
        m_sat = std::clamp(double(pos.x()) / std::max(1, width() - 1), 0.0, 1.0);
        m_val = std::clamp(1.0 - double(pos.y()) / std::max(1, height() - 1), 0.0, 1.0);
        update();
        if (m_onPick) {
            m_onPick(m_sat, m_val);
        }
    }

    double m_hue = 0.0;
    double m_sat = 0.0;
    double m_val = 1.0;
    std::function<void(double, double)> m_onPick;
};

ColorPickerWidget::ColorPickerWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    auto *top = new QHBoxLayout();
    m_svSquare = new SvSquare(this);
    m_svSquare->setOnPick([this](double sat, double val) { onSvChanged(sat, val); });
    top->addWidget(m_svSquare);

    auto *sliders = new QVBoxLayout();
    m_hueSlider = new QSlider(Qt::Horizontal, this);
    m_hueSlider->setRange(0, 359);
    auto *hueRow = new QHBoxLayout();
    hueRow->addWidget(new QLabel(tr("Hue"), this));
    hueRow->addWidget(m_hueSlider, 1);
    sliders->addLayout(hueRow);

    m_alphaSlider = new QSlider(Qt::Horizontal, this);
    m_alphaSlider->setRange(0, 255);
    auto *alphaRow = new QHBoxLayout();
    alphaRow->addWidget(new QLabel(tr("Alpha"), this));
    alphaRow->addWidget(m_alphaSlider, 1);
    sliders->addLayout(alphaRow);

    m_swatch = new QLabel(this);
    m_swatch->setFixedHeight(24);
    m_swatch->setAutoFillBackground(true);
    sliders->addWidget(m_swatch);
    sliders->addStretch(1);
    top->addLayout(sliders, 1);
    root->addLayout(top);

    auto *grid = new QGridLayout();
    auto addSpin = [&](const QString &label, int col) {
        auto *spin = new QSpinBox(this);
        spin->setRange(0, 255);
        grid->addWidget(new QLabel(label, this), 0, col);
        grid->addWidget(spin, 1, col);
        return spin;
    };
    m_rSpin = addSpin(tr("R"), 0);
    m_gSpin = addSpin(tr("G"), 1);
    m_bSpin = addSpin(tr("B"), 2);
    m_aSpin = addSpin(tr("A"), 3);
    root->addLayout(grid);

    connect(m_hueSlider, &QSlider::valueChanged, this, [this](int) { onSliderChanged(); });
    connect(m_alphaSlider, &QSlider::valueChanged, this, [this](int) { onSliderChanged(); });
    for (QSpinBox *spin : {m_rSpin, m_gSpin, m_bSpin, m_aSpin}) {
        connect(spin, &QSpinBox::valueChanged, this, [this](int) { onSpinChanged(); });
    }

    setColor(QColor(255, 255, 255, 255));
}

QColor ColorPickerWidget::color() const
{
    QColor c = QColor::fromHsvF(m_hue / 360.0, m_sat, m_val);
    c.setAlpha(m_alpha);
    return c;
}

void ColorPickerWidget::setColor(const QColor &c)
{
    m_hue = c.hueF() < 0 ? 0.0 : c.hueF() * 360.0;
    m_sat = c.saturationF();
    m_val = c.valueF();
    m_alpha = c.alpha();
    syncAllFromColor();
}

void ColorPickerWidget::onSvChanged(double sat, double val)
{
    m_sat = sat;
    m_val = val;
    m_block = true;
    syncAllFromColor();
    m_block = false;
    emitColor();
}

void ColorPickerWidget::onSliderChanged()
{
    if (m_block) {
        return;
    }
    m_hue = m_hueSlider->value();
    m_alpha = m_alphaSlider->value();
    m_block = true;
    syncAllFromColor();
    m_block = false;
    emitColor();
}

void ColorPickerWidget::onSpinChanged()
{
    if (m_block) {
        return;
    }
    const QColor c(m_rSpin->value(), m_gSpin->value(), m_bSpin->value(), m_aSpin->value());
    m_hue = c.hueF() < 0 ? m_hue : c.hueF() * 360.0;
    m_sat = c.saturationF();
    m_val = c.valueF();
    m_alpha = c.alpha();
    m_block = true;
    syncAllFromColor();
    m_block = false;
    emitColor();
}

void ColorPickerWidget::syncAllFromColor()
{
    const bool prevBlock = m_block;
    m_block = true;
    const QColor c = color();
    m_svSquare->setHue(m_hue);
    m_svSquare->setSatVal(m_sat, m_val);
    m_hueSlider->setValue(int(std::round(m_hue)));
    m_alphaSlider->setValue(m_alpha);
    m_rSpin->setValue(c.red());
    m_gSpin->setValue(c.green());
    m_bSpin->setValue(c.blue());
    m_aSpin->setValue(c.alpha());
    QPalette pal = m_swatch->palette();
    pal.setColor(QPalette::Window, c);
    m_swatch->setPalette(pal);
    m_block = prevBlock;
}

void ColorPickerWidget::emitColor()
{
    emit colorChanged(color());
}

} // namespace openvegas
