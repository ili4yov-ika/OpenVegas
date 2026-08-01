#include "ui/VideoEventFxDialog.h"

#include "model/ProjectModel.h"
#include "ui/IconFactory.h"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QAbstractSpinBox>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

namespace openvegas {

namespace {

class FxPreviewCanvas : public QWidget {
public:
    explicit FxPreviewCanvas(QWidget *parent = nullptr) : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMinimumHeight(360);
        setMouseTracking(true);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0x10, 0x10, 0x10));

        const QRectF er(width() * 0.08, height() * 0.06, width() * 0.84, height() * 0.78);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(QColor(0xd0, 0xd0, 0xd0), 1, Qt::DashLine));
        p.drawEllipse(er);

        const QRectF cr(width() * 0.21, height() * 0.20, width() * 0.58, height() * 0.56);
        p.setPen(QPen(QColor(0xff, 0xff, 0xff), 2));
        p.setBrush(QColor(0, 0, 0, 40));
        p.drawRect(cr);

        p.setPen(QPen(QColor(0xcc, 0xcc, 0xcc), 1, Qt::DashLine));
        p.drawLine(QPointF(cr.left() + cr.width() / 2, cr.top()), QPointF(cr.left() + cr.width() / 2, cr.bottom()));
        p.drawLine(QPointF(cr.left(), cr.top() + cr.height() / 2), QPointF(cr.right(), cr.top() + cr.height() / 2));

        auto handle = [&](QPointF c) {
            const QRectF r(c.x() - 6, c.y() - 6, 12, 12);
            p.setBrush(QColor(0xffa500));
            p.setPen(QPen(QColor(0x2a, 0x10, 0x00), 1));
            p.drawRect(r);
        };

        handle({cr.left(), cr.top()});
        handle({cr.right(), cr.top()});
        handle({cr.left(), cr.bottom()});
        handle({cr.right(), cr.bottom()});
        handle({cr.left() + cr.width() / 2, cr.top()});
        handle({cr.left() + cr.width() / 2, cr.bottom()});
        handle({cr.left(), cr.top() + cr.height() / 2});
        handle({cr.right(), cr.top() + cr.height() / 2});

        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(0x5a, 0x8a, 0xff), 2));
        const QRectF pr(width() * 0.38, height() * 0.34, width() * 0.24, height() * 0.16);
        p.drawRect(pr);
        p.drawLine(QPointF(pr.left(), pr.center().y()), QPointF(pr.right(), pr.center().y()));
        p.drawLine(QPointF(pr.center().x(), pr.top()), QPointF(pr.center().x(), pr.bottom()));
    }
};

class PositionTimelineCanvas : public QWidget {
public:
    explicit PositionTimelineCanvas(QWidget *parent = nullptr) : QWidget(parent)
    {
        setFixedHeight(48);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0x2b, 0x2b, 0x2b));

        const int leftPad = 110;
        const int rightPad = 8;
        QRect timeline = rect().adjusted(leftPad, 8, -rightPad, -18);
        p.setPen(QColor(0x55, 0x55, 0x55));
        p.drawRect(timeline);

        const double totalSec = 60.0;
        for (int s = 0; s <= int(totalSec); s += 5) {
            const double t = double(s) / totalSec;
            const int x = timeline.left() + int(t * timeline.width());
            const bool major = (s % 10) == 0;
            const int h = major ? 12 : 6;
            p.setPen(major ? QColor(0xd0, 0xd0, 0xd0) : QColor(0x88, 0x88, 0x88));
            p.drawLine(x, timeline.bottom(), x, timeline.bottom() - h);
        }

        auto label = [&](int sec) {
            const int mm = sec / 60;
            const int ss = sec % 60;
            const QString text = QStringLiteral("%1:%2")
                                     .arg(mm, 2, 10, QChar('0'))
                                     .arg(ss, 2, 10, QChar('0'));
            const double t = double(sec) / totalSec;
            const int x = timeline.left() + int(t * timeline.width());
            p.setPen(QColor(0xff, 0x66, 0x00));
            p.setFont(QFont(font().family(), 9));
            p.drawText(QRect(x - 28, timeline.top() - 12, 56, 12), Qt::AlignCenter, text);
        };

        for (int s = 0; s <= int(totalSec); s += 10) {
            label(s);
        }

        const double selStart = 12.0 / totalSec;
        const double selEnd = 30.0 / totalSec;
        const QRect selRect(timeline.left() + int(selStart * timeline.width()),
                             timeline.top() + 10,
                             int((selEnd - selStart) * timeline.width()),
                             timeline.height() - 20);
        p.fillRect(selRect, QColor(0xff, 0xb0, 0x40));
        p.setPen(QColor(0x60, 0x30, 0x00));
        p.drawRect(selRect);

        const double play = 36.49 / totalSec;
        const int px = timeline.left() + int(play * timeline.width());
        p.setPen(QPen(QColor(0xec, 0xec, 0xec), 1));
        p.drawLine(px, rect().top(), px, rect().bottom());
    }
};

} // namespace

VideoEventFxDialog::VideoEventFxDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Video Event FX"));
    setMinimumSize(520, 420);

    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(10);

    // Left: FX chain list
    auto *left = new QWidget(this);
    auto *leftLay = new QVBoxLayout(left);
    leftLay->setContentsMargins(0, 0, 0, 0);
    leftLay->setSpacing(6);

    auto *title = new QLabel(tr("FX Chain"), left);
    title->setStyleSheet(QStringLiteral("color:#e0e0e0; font-weight:bold;"));
    leftLay->addWidget(title);

    m_chain = new QListWidget(left);
    m_chain->setSelectionMode(QAbstractItemView::SingleSelection);
    leftLay->addWidget(m_chain, 1);

    root->addWidget(left, 0);

    // Right: Pan/Crop parameters (simplified)
    auto *right = new QWidget(this);
    auto *rightLay = new QVBoxLayout(right);
    rightLay->setContentsMargins(0, 0, 0, 0);
    rightLay->setSpacing(8);

    auto *rTitle = new QLabel(tr("Pan/Crop (first in chain)"), right);
    rTitle->setStyleSheet(QStringLiteral("color:#e0e0e0; font-weight:bold;"));
    rightLay->addWidget(rTitle);

    auto *form = new QFormLayout();
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setLabelAlignment(Qt::AlignLeft);

    auto mkSpin = [&](double min, double max) {
        auto *s = new QDoubleSpinBox(right);
        s->setRange(min, max);
        s->setDecimals(2);
        s->setSingleStep(10.0);
        s->setKeyboardTracking(false);
        return s;
    };

    m_width = mkSpin(0, 100000);
    m_height = mkSpin(0, 100000);
    m_xCenter = mkSpin(-100000, 100000);
    m_yCenter = mkSpin(-100000, 100000);
    m_angle = mkSpin(-360, 360);
    m_angle->setDecimals(1);
    m_angle->setSingleStep(1.0);

    form->addRow(tr("Width:"), m_width);
    form->addRow(tr("Height:"), m_height);
    form->addRow(tr("X Center:"), m_xCenter);
    form->addRow(tr("Y Center:"), m_yCenter);
    form->addRow(tr("Angle:"), m_angle);

    rightLay->addLayout(form, 0);

    auto *btnRow = new QHBoxLayout();
    btnRow->addStretch(1);
    auto *closeBtn = new QPushButton(tr("Close"), right);
    btnRow->addWidget(closeBtn, 0);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    rightLay->addLayout(btnRow, 0);

    root->addWidget(right, 1);
}

void VideoEventFxDialog::setEvent(TrackEvent *ev)
{
    m_event = ev;
    ensurePanCropFirst();
    rebuildChainList();
    syncUiFromChain();
}

void VideoEventFxDialog::ensurePanCropFirst()
{
    if (!m_event) {
        return;
    }
    ensureFxFirst(m_event->fxChain, QStringLiteral("Pan/Crop"), PluginFormat::Builtin);
}

void VideoEventFxDialog::rebuildChainList()
{
    if (!m_chain) {
        return;
    }
    m_chain->clear();
    if (!m_event) {
        return;
    }
    for (const FxSlot &fx : m_event->fxChain) {
        m_chain->addItem(fx.displayName);
    }
    if (m_chain->count() > 0) {
        m_chain->setCurrentRow(0);
    }
}

void VideoEventFxDialog::syncUiFromChain()
{
    // For now the parameters are display-only placeholders; real parameter persistence
    // will be implemented once event FX slots / parameters modelled.
    // Defaults match the common 1920×1080 coordinates in screenshots.
    m_width->setValue(1920.0);
    m_height->setValue(1080.0);
    m_xCenter->setValue(960.0);
    m_yCenter->setValue(540.0);
    m_angle->setValue(0.0);
}

} // namespace openvegas

