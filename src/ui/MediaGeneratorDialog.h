#pragma once

#include "video/MediaGeneratorApply.h"

#include <QDialog>
#include <QVariantMap>

#include <functional>

class QComboBox;
class QLabel;
class QLineEdit;
class QScrollArea;
class QToolButton;

namespace openvegas {

struct TrackEvent;

/**
 * One generator's parameter page: shows a parameter map and reports every edit of it.
 * The OFX generators get one built from their schema, Color Gradient and Credit Roll
 * pages of their own.
 */
class GeneratorPage : public QWidget {
    Q_OBJECT
public:
    using QWidget::QWidget;
    virtual void setParams(const QVariantMap &params) = 0;
    /**
     * Animation, for a page that offers it: `{parameter: [t0, v0, t1, v1, …]}` and where
     * the timeline cursor is in the event, in seconds. A page without keyframes ignores it.
     */
    virtual void setAnimation(const QVariantMap &curves, double timeSec)
    {
        Q_UNUSED(curves);
        Q_UNUSED(timeSec);
    }

signals:
    void paramsEdited(const QVariantMap &params);
    void curvesEdited(const QVariantMap &curves);
};

/**
 * VEGAS's "Video Media Generators" window for the six non-text generators — Checkerboard,
 * Color Gradient, Credit Roll, Noise Texture, Solid Color, Test Pattern (Titles & Text has
 * its own editor). What VEGAS shows, from the top:
 *
 *   Video Media Generator: <name>
 *   Frame Size: <w> x <h>        Duration: <timecode>
 *   Preset: [stock and saved presets]  [save] [delete]
 *   the generator's page
 *
 * The OFX generators' page is built the way VEGAS builds one for any OFX effect, from what
 * the plug-in declares (GeneratorParamSchema): groups that fold, a slider and a value per
 * number, a check box per switch, a list per choice, a colour row that opens a picker.
 * Color Gradient and Credit Roll are legacy DirectX plug-ins with dialogs of their own in
 * sftrans1.dll; their pages follow those layouts (dialog resources 191 and 228–236).
 *
 * Every edit is written to the event at once and the preview redrawn, as in VEGAS.
 */
class MediaGeneratorDialog : public QDialog {
    Q_OBJECT
public:
    explicit MediaGeneratorDialog(QWidget *parent = nullptr);

    /** Show `ev`'s generator; nothing when its first slot is not a Media Generator. */
    void setEvent(TrackEvent *ev, int frameWidth, int frameHeight, double frameRateFps);
    /** Where the timeline cursor is, in project seconds — what keyframes are set at. */
    void setCursorSource(std::function<double()> cursorSec) { m_cursorSec = std::move(cursorSec); }
    /** The cursor moved: show the animated values where it now is. */
    void cursorMoved();
    TrackEvent *trackEvent() const { return m_event; }

signals:
    /** After any edit that should repaint the Video Preview. */
    void previewInvalidated();
    /** After the duration changed the event's length. */
    void durationChanged();

private:
    void buildUi();
    void rebuildPage();
    void fillPresets();
    void applyPreset(const QString &name);
    void saveUserPreset();
    void deleteUserPreset();
    void storeParams(const QVariantMap &params);
    void storeCurves(const QVariantMap &curves);
    double eventTime() const;
    void applyDuration();
    void openMediaProperties();
    QString durationText() const;

    TrackEvent *m_event = nullptr;
    std::function<double()> m_cursorSec;
    MediaGeneratorParams m_gen;
    QString m_id;
    int m_frameWidth = 1920;
    int m_frameHeight = 1080;
    double m_frameRateFps = 30.0;

    QLabel *m_nameLabel = nullptr;
    QLineEdit *m_frameW = nullptr;
    QLineEdit *m_frameH = nullptr;
    QLineEdit *m_duration = nullptr;
    QComboBox *m_preset = nullptr;
    QToolButton *m_savePreset = nullptr;
    QToolButton *m_deletePreset = nullptr;
    QToolButton *m_mediaProps = nullptr;
    QLabel *m_pageTitle = nullptr;
    QScrollArea *m_scroll = nullptr;
    GeneratorPage *m_page = nullptr;
    bool m_block = false;
};

} // namespace openvegas
