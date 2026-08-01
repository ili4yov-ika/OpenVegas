#pragma once

#include <QWidget>

namespace openvegas {

/**
 * Vegas-style shuttle Rate control: −20…+20, default 0.
 * On left-button release the thumb springs back to 0.
 */
class RateSlider : public QWidget {
    Q_OBJECT
public:
    explicit RateSlider(QWidget *parent = nullptr);

    double rate() const { return m_rate; }
    void setRate(double rate);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void rateChanged(double rate);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QRect trackRect() const;
    double rateFromX(int x) const;
    int xFromRate(double rate) const;
    void applyRateFromPos(const QPoint &pos);

    double m_rate = 0.0;
    bool m_dragging = false;
    bool m_hover = false;

    static constexpr double kMinRate = -20.0;
    static constexpr double kMaxRate = 20.0;
};

} // namespace openvegas
