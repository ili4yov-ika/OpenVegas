#include "ui/CaptureTrayIcon.h"

#include <QAction>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSystemTrayIcon>

namespace openvegas {

namespace {

/** Colours that read on both a dark taskbar and a light one. */
const QColor kGlyph(0xe6, 0xe6, 0xe6);
const QColor kGlyphEdge(0x20, 0x20, 0x20);
const QColor kRecord(0xe0, 0x1b, 0x24);

} // namespace

QImage CaptureTrayIcon::image(int size, bool recording)
{
    QImage img(size, size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    const double s = size;

    // A camera: body, lens, and the barrel poking out to the right. Drawn in proportions of
    // the requested size so it stays crisp at 16 as well as at 48.
    const QRectF body(s * 0.10, s * 0.26, s * 0.58, s * 0.46);
    const double edge = qMax(1.0, s * 0.075);
    p.setPen(QPen(kGlyphEdge, edge * 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(body, s * 0.08, s * 0.08);
    p.setPen(QPen(kGlyph, edge, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawRoundedRect(body, s * 0.08, s * 0.08);

    QPainterPath barrel;
    barrel.moveTo(s * 0.68, s * 0.40);
    barrel.lineTo(s * 0.86, s * 0.30);
    barrel.lineTo(s * 0.86, s * 0.68);
    barrel.lineTo(s * 0.68, s * 0.58);
    barrel.closeSubpath();
    p.setPen(QPen(kGlyphEdge, edge * 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(barrel);
    p.fillPath(barrel, kGlyph);

    // The idle icon carries no red at all, so a glance tells the two states apart rather
    // than a comparison: anything red on this icon means a take is running.
    if (recording) {
        // Kept clear of the upper-left quadrants so the badge never sits over the glyph:
        // at 16 pixels the dot is still 7 across, which is what has to be readable.
        const double r = s * 0.225;
        const QPointF centre(s * 0.755, s * 0.775); // low and right of the glyph, as OBS does
        p.setPen(QPen(kGlyphEdge, qMax(1.0, s * 0.055)));
        p.setBrush(kRecord);
        p.drawEllipse(centre, r, r);
    }
    p.end();
    return img;
}

CaptureTrayIcon::~CaptureTrayIcon()
{
    delete m_menu;
}

bool CaptureTrayIcon::isAvailable()
{
    return QSystemTrayIcon::isSystemTrayAvailable();
}

CaptureTrayIcon::CaptureTrayIcon(QObject *parent)
    : QObject(parent)
{
    if (!isAvailable()) {
        return; // no tray on this desktop; every method below stays a no-op
    }
    m_tray = new QSystemTrayIcon(this);

    m_menu = new QMenu();
    QMenu *menu = m_menu;
    m_recordAction = menu->addAction(tr("Start Recording"));
    connect(m_recordAction, &QAction::triggered, this,
            &CaptureTrayIcon::toggleRecordingRequested);
    menu->addSeparator();
    QAction *open = menu->addAction(tr("Open OpenVegas Capture"));
    connect(open, &QAction::triggered, this, &CaptureTrayIcon::showWindowRequested);
    m_tray->setContextMenu(menu);

    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger
                    || reason == QSystemTrayIcon::DoubleClick) {
                    emit showWindowRequested();
                }
            });

    refreshIcon();
}

void CaptureTrayIcon::refreshIcon()
{
    if (!m_tray) {
        return;
    }
    QIcon icon;
    // Windows asks for whichever of these fits the tray it is drawing; giving it all four
    // means none of them is a scaled-down 48.
    for (int size : {16, 20, 24, 32, 48}) {
        icon.addPixmap(QPixmap::fromImage(image(size, m_recording)));
    }
    m_tray->setIcon(icon);

    QString tip = m_recording ? tr("OpenVegas Capture — recording")
                              : tr("OpenVegas Capture");
    if (!m_status.isEmpty()) {
        tip += QLatin1Char('\n') + m_status;
    }
    m_tray->setToolTip(tip);

    if (m_recordAction) {
        m_recordAction->setText(m_recording ? tr("Stop Recording") : tr("Start Recording"));
    }
}

void CaptureTrayIcon::setRecording(bool recording)
{
    if (m_recording == recording) {
        return;
    }
    m_recording = recording;
    if (!recording) {
        m_status.clear();
    }
    refreshIcon();
}

void CaptureTrayIcon::setStatusText(const QString &text)
{
    if (m_status == text) {
        return;
    }
    m_status = text;
    refreshIcon();
}

void CaptureTrayIcon::show()
{
    if (m_tray) {
        m_tray->show();
    }
}

void CaptureTrayIcon::hide()
{
    if (m_tray) {
        m_tray->hide();
    }
}

} // namespace openvegas
