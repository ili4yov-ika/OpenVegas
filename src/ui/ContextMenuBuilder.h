#pragma once

#include "model/ProjectModel.h"

#include <QPoint>

class QMenu;
class QToolButton;
class QWidget;

namespace openvegas {

class MainWindow;

/** Context menus from SAMPLES/js/menus.js (ui-catalog). */
class ContextMenuBuilder {
public:
    static void showEventMenu(MainWindow *window, int eventId, const QPoint &globalPos);
    /** Event chrome "…" button — visible-button / display toggles (Vegas). */
    static void showEventMoreMenu(MainWindow *window, int eventId, const QPoint &globalPos);
    /** Event chrome "fx" button — Bypass / Enable / Delete All + Plug-In Chooser. */
    static void showEventFxMenu(MainWindow *window, int eventId, const QPoint &globalPos);
    static void showTrackHeaderMenu(MainWindow *window, int trackIndex, const QPoint &globalPos);
    /** Track header "…" chip — Vegas More menu (audio or video). */
    static void showTrackMoreMenu(MainWindow *window, int trackIndex, const QPoint &globalPos);
    static void showTrackEmptyMenu(MainWindow *window, int trackIndex, const QPoint &globalPos);
    static void showTimelineEmptyMenu(MainWindow *window, const QPoint &globalPos);
    static void showRulerMenu(MainWindow *window, const QPoint &globalPos);
    /** Marker bar / loop region lane (Vegas marker-bar shortcut menu). */
    static void showMarkerLaneMenu(MainWindow *window, const QPoint &globalPos);
    /** Single marker head (Go To / Rename / Delete). */
    static void showMarkerMenu(MainWindow *window, int markerId, const QPoint &globalPos);
    static void showPreviewMenu(MainWindow *window, const QPoint &globalPos);
    static void showTimeDisplayMenu(MainWindow *window, const QPoint &globalPos);
    static void showSplitScreenMenu(QWidget *parent, const QPoint &globalPos);
    static void showQualityMenu(MainWindow *window, QToolButton *chip, const QPoint &globalPos);
    static void showZoomMenu(QToolButton *chip, const QPoint &globalPos);
    static void showOverlaysMenu(MainWindow *window, QToolButton *btn, const QPoint &globalPos);
};

} // namespace openvegas
