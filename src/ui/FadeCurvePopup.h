#pragma once

#include "model/ProjectModel.h"

#include <QPainterPath>
#include <QWidget>

class QRectF;

namespace openvegas {

// fadeCurveAmplitude — see audio/FadeCurves.h (shared with DSP)

/** Line along the fade curve inside rect (fadeIn: bottom→top; fadeOut: top→bottom). */
QPainterPath fadeCurveLinePath(const QRectF &r, FadeCurveType type, bool fadeIn);

/** Fill under/above the fade curve for dimming the event body. */
QPainterPath fadeCurveFillPath(const QRectF &r, FadeCurveType type, bool fadeIn);

/**
 * Vegas-style popup: 5 fade curve icons + selection radio.
 * Shown on right-click of an event fade handle.
 */
class FadeCurvePopup : public QWidget {
    Q_OBJECT
public:
    explicit FadeCurvePopup(QWidget *parent = nullptr);
    ~FadeCurvePopup() override;

    void setFadeIn(bool fadeIn);
    void setCurrent(FadeCurveType type);
    FadeCurveType current() const { return m_current; }

    /** Popup near globalPos (clamped to screen). */
    void popupAt(const QPoint &globalPos);

signals:
    void curveChosen(FadeCurveType type);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    int indexAt(const QPoint &pos) const;
    QRect itemRect(int index) const;
    QRect iconRect(int index) const;

    bool m_fadeIn = true;
    FadeCurveType m_current = FadeCurveType::Smooth;
    int m_hover = -1;
};

} // namespace openvegas
