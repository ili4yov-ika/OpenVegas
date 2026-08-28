#pragma once

#include "capture/CaptureSource.h"

#include <QColor>
#include <QWidget>

class QCheckBox;
class QLabel;

namespace openvegas {

/** MIME type a source card carries while being dragged to a new position. */
const char *const kCaptureSourceMime = "application/x-openvegas-capture-source";

/**
 * One video source in the picker: what it looks like now, what it is, and whether it is
 * going into the take.
 *
 * The picture is live rather than a thumbnail from a moment ago, because that is the only
 * thing that tells two monitors apart. The colour is not decoration either — the audio
 * input that belongs to this source is drawn in the same colour further down the panel,
 * which is how a row of devices becomes "this camera and its microphone" at a glance.
 *
 * Dragging the card moves it in the list; the order decides which source the take's
 * resolution follows, so it has to be changeable without retyping anything.
 */
class CaptureSourceCard : public QWidget {
    Q_OBJECT
public:
    CaptureSourceCard(const CaptureSource &source, int index, const QColor &accent,
                      QWidget *parent = nullptr);

    CaptureSource source() const { return m_source; }
    int sourceIndex() const { return m_index; }

    /** The newest frame off the live preview. */
    void setPreview(const QImage &frame);
    /** Say why there is no picture instead of leaving an empty box. */
    void setPreviewNote(const QString &note);

    bool isChecked() const;
    void setChecked(bool on);

    /** The audio input recorded alongside this source, drawn under its name. */
    void setPairedAudio(const QString &name);

    /** Selected cards are drawn solid; the rest are washed out. */
    void setSelected(bool on);

    /** The size the live preview should be asked for. */
    QSize previewSize() const;

signals:
    /** The card was dropped `toIndex` places along; the window re-orders and rebuilds. */
    void reorderRequested(int fromIndex, int toIndex);
    /** The tick box changed — this source is in the take, or is not. */
    void checkedChanged(int index, bool checked);
    /** The card was clicked, which selects it. */
    void clicked(int index);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    CaptureSource m_source;
    int m_index;
    QColor m_accent;
    bool m_selected = false;

    QLabel *m_preview = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_detail = nullptr;
    QLabel *m_audio = nullptr;
    QCheckBox *m_check = nullptr;
    QPoint m_pressAt;
};

} // namespace openvegas
