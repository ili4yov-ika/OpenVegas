#include "ui/CaptureSourceCard.h"

#include <QApplication>
#include <QCheckBox>
#include <QDrag>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QVBoxLayout>

namespace openvegas {

namespace {

/** The live picture's box. Wide enough to tell a desktop from a camera at a glance. */
constexpr int kPreviewWidth = 96;
constexpr int kPreviewHeight = 56;

QString describe(const CaptureSource &source)
{
    if (!source.nativeSize.isValid() || source.nativeSize.isEmpty()) {
        return {};
    }
    QString text = QStringLiteral("%1×%2")
                       .arg(source.nativeSize.width())
                       .arg(source.nativeSize.height());
    if (source.frameRate > 0.0) {
        text += QStringLiteral(" @ %1").arg(source.frameRate, 0, 'g', 4);
    }
    return text;
}

} // namespace

CaptureSourceCard::CaptureSourceCard(const CaptureSource &source, int index,
                                     const QColor &accent, QWidget *parent)
    : QWidget(parent)
    , m_source(source)
    , m_index(index)
    , m_accent(accent)
{
    setObjectName(QStringLiteral("captureSourceCard"));
    setCursor(Qt::OpenHandCursor);
    setMinimumHeight(kPreviewHeight + 12);

    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(6, 6, 8, 6);
    row->setSpacing(8);

    m_preview = new QLabel(this);
    m_preview->setFixedSize(kPreviewWidth, kPreviewHeight);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setWordWrap(true);
    m_preview->setStyleSheet(
        QStringLiteral("background: #101010; border: 1px solid #FFFFFF; color: #DDDDDD;"
                       " font-size: 10px;"));
    m_preview->setText(tr("Preview\n%1").arg(source.name));
    row->addWidget(m_preview, 0, Qt::AlignVCenter);

    auto *col = new QVBoxLayout();
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(1);

    m_title = new QLabel(source.name, this);
    m_title->setStyleSheet(QStringLiteral("font-weight: bold; color: #FFFFFF;"));
    col->addWidget(m_title);

    m_detail = new QLabel(describe(source), this);
    m_detail->setStyleSheet(QStringLiteral("color: #EAEAEA;"));
    m_detail->setVisible(!m_detail->text().isEmpty());
    col->addWidget(m_detail);

    m_audio = new QLabel(this);
    m_audio->setStyleSheet(QStringLiteral("color: #EAEAEA;"));
    m_audio->setVisible(false);
    col->addWidget(m_audio);

    m_check = new QCheckBox(this);
    m_check->setToolTip(tr("Record this source"));
    // The card is a saturated colour and the theme's own tick box is nearly black on it.
    // Drawn light with a dark tick, it reads on every colour in the palette.
    m_check->setStyleSheet(QStringLiteral(
        "QCheckBox::indicator { width: 14px; height: 14px; border: 1px solid #202020;"
        " background: #F0F0F0; }"
        "QCheckBox::indicator:checked { background: #202020; border: 1px solid #F0F0F0; }"));
    col->addWidget(m_check, 0, Qt::AlignLeft);
    col->addStretch(1);

    row->addLayout(col, 1);

    connect(m_check, &QCheckBox::toggled, this, [this](bool on) {
        emit checkedChanged(m_index, on);
    });
}

QSize CaptureSourceCard::previewSize() const
{
    // Device pixels, so the picture is sharp on a scaled display rather than blown up.
    const qreal dpr = devicePixelRatioF();
    return QSize(int(kPreviewWidth * dpr), int(kPreviewHeight * dpr));
}

void CaptureSourceCard::setPreview(const QImage &frame)
{
    if (frame.isNull()) {
        return;
    }
    QPixmap pm = QPixmap::fromImage(frame);
    pm.setDevicePixelRatio(devicePixelRatioF());
    m_preview->setPixmap(pm);
}

void CaptureSourceCard::setPreviewNote(const QString &note)
{
    m_preview->setPixmap(QPixmap());
    // ffmpeg's reasons run to a paragraph and the box is 96 pixels wide, so the box gets
    // as much as fits and the whole of it goes to the tooltip. Truncating without keeping
    // the rest would throw away the only explanation there is.
    const QString shown = m_preview->fontMetrics().elidedText(
        note.simplified(), Qt::ElideRight, (kPreviewWidth - 8) * 3);
    m_preview->setText(shown);
    m_preview->setToolTip(note);
}

bool CaptureSourceCard::isChecked() const
{
    return m_check->isChecked();
}

void CaptureSourceCard::setChecked(bool on)
{
    const QSignalBlocker block(m_check);
    m_check->setChecked(on);
}

void CaptureSourceCard::setPairedAudio(const QString &name)
{
    m_audio->setText(name.isEmpty() ? QString() : QStringLiteral("🔊 %1").arg(name));
    m_audio->setVisible(!name.isEmpty());
}

void CaptureSourceCard::setSelected(bool on)
{
    if (m_selected == on) {
        return;
    }
    m_selected = on;
    update();
}

void CaptureSourceCard::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    QColor fill = m_accent;
    // The mockup's own two levels: the selected source solid, the rest half-strength, so
    // the panel reads as one list rather than five competing blocks.
    fill.setAlphaF(m_selected ? 0.8 : 0.5);
    p.fillRect(rect(), fill);
    if (m_selected) {
        p.setPen(QPen(QColor(255, 255, 255, 200), 1));
        p.drawRect(rect().adjusted(0, 0, -1, -1));
    }
}

void CaptureSourceCard::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressAt = event->pos();
        emit clicked(m_index);
    }
    QWidget::mousePressEvent(event);
}

void CaptureSourceCard::mouseMoveEvent(QMouseEvent *event)
{
    if (!(event->buttons() & Qt::LeftButton)
        || (event->pos() - m_pressAt).manhattanLength() < QApplication::startDragDistance()) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    auto *mime = new QMimeData();
    mime->setData(QByteArray(kCaptureSourceMime), QByteArray::number(m_index));

    auto *drag = new QDrag(this);
    drag->setMimeData(mime);
    // The card itself as the cursor, so what is being moved is never in doubt.
    QPixmap shot(size() * devicePixelRatioF());
    shot.setDevicePixelRatio(devicePixelRatioF());
    shot.fill(Qt::transparent);
    render(&shot);
    drag->setPixmap(shot);
    drag->setHotSpot(m_pressAt);
    drag->exec(Qt::MoveAction, Qt::MoveAction);
}

} // namespace openvegas
