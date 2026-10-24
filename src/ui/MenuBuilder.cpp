#include "ui/MenuBuilder.h"
#include "app/MainWindow.h"
#include "ui/IconFactory.h"
#include "model/ProjectModel.h"

#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QKeySequence>
#include <QSettings>
#include <QFileInfo>
#include <QDir>
#include <QObject>
#include <QUndoStack>
#include <QSignalBlocker>
#include <QSet>
#include <limits>
#include "timeline/TimelineView.h"

namespace openvegas {

namespace {

QAction *addStubDisabled(QMenu *menu, const QString &text, const QKeySequence &shortcut = {})
{
    auto *a = menu->addAction(text);
    if (!shortcut.isEmpty()) {
        a->setShortcut(shortcut); // safe: disabled actions do not consume shortcuts
    }
    a->setEnabled(false);
    a->setToolTip(QObject::tr("This function is not available yet"));
    return a;
}

} // namespace

void MenuBuilder::build(MainWindow *window, QMenuBar *menuBar)
{
    auto *fileMenu = menuBar->addMenu(QObject::tr("File"));
    // Structure matches VEGAS Pro 22 (SAMPLES/screenshots/sample_for_project_window_menu_file.png)
    {
        auto *a = fileMenu->addAction(IconFactory::iconFromSvgBody(IconFactory::svgNew()),
                                      QObject::tr("New…"), QKeySequence::New, window,
                                      &MainWindow::onNewProject);
        Q_UNUSED(a);
    }
    fileMenu->addAction(QObject::tr("Welcome Screen"), window, &MainWindow::onWelcome);
    fileMenu->addAction(IconFactory::iconFromSvgBody(IconFactory::svgOpen()), QObject::tr("Open…"),
                        QKeySequence::Open, window, &MainWindow::onOpenProject);
    fileMenu->addAction(QObject::tr("Close"), QKeySequence(QStringLiteral("Ctrl+F4")), window,
                        &MainWindow::onNewProject);
    fileMenu->addSeparator();
    fileMenu->addAction(IconFactory::iconFromSvgBody(IconFactory::svgSave()), QObject::tr("Save"),
                        QKeySequence::Save, window, &MainWindow::onSaveProject);
    fileMenu->addAction(IconFactory::iconFromSvgBody(IconFactory::svgSave()), QObject::tr("Save As…"),
                        QKeySequence(QStringLiteral("Ctrl+Shift+S")), window,
                        &MainWindow::onSaveProjectAs);
    fileMenu->addAction(QObject::tr("Incremental Save"), QKeySequence(QStringLiteral("Ctrl+Alt+S")), window, &MainWindow::onIncrementalSave);
    fileMenu->addSeparator();
    fileMenu->addAction(IconFactory::iconFromSvgBody(IconFactory::svgRender()), QObject::tr("Render As…"),
                        QKeySequence(QStringLiteral("Ctrl+Shift+M")), window, &MainWindow::onRenderAs);
    fileMenu->addAction(QObject::tr("Bounce Audio Mixdown…"), window, &MainWindow::onBounceAudioMixdown);
    addStubDisabled(fileMenu, QObject::tr("Real-Time Render…"));
    fileMenu->addSeparator();
    {
        auto *importMenu = fileMenu->addMenu(IconFactory::iconFromSvgBody(IconFactory::svgImport()),
                                             QObject::tr("Import"));
        importMenu->addAction(QObject::tr("Media…"), window, &MainWindow::onImportMedia);
        importMenu->addAction(QObject::tr("Media from Project…"), window,
                              &MainWindow::onImportMediaFromProject);
        importMenu->addAction(QObject::tr("Premiere/After Effects (*.prproj)…"), window,
                              &MainWindow::onImportPremiere);
        importMenu->addAction(QObject::tr("Final Cut Pro 7/DaVinci Resolve (*.xml)…"), window,
                              &MainWindow::onImportFinalCutXml);
        importMenu->addAction(QObject::tr("Final Cut Pro X (*.fcpxml)…"), window,
                              &MainWindow::onImportFcpxml);
        importMenu->addAction(QObject::tr("EDL Text File (*.txt)…"), window, &MainWindow::onImportEdl);
        importMenu->addAction(QObject::tr("Broadcast Wave Format…"), window,
                              &MainWindow::onImportBroadcastWave);
        importMenu->addAction(QObject::tr("Closed Captioning…"), window,
                              &MainWindow::onImportClosedCaptions);
    }
    {
        auto *exportMenu = fileMenu->addMenu(QObject::tr("Export"));
        exportMenu->addAction(QObject::tr("VEGAS Project Archive (*.veg)…"), window,
                              &MainWindow::onExportProjectArchive);
        exportMenu->addAction(QObject::tr("Premiere/After Effects (*.prproj)…"), window,
                              &MainWindow::onExportPremiere);
        exportMenu->addAction(QObject::tr("Final Cut Pro 7/DaVinci Resolve (*.xml)…"), window,
                              &MainWindow::onExportFinalCutXml);
        exportMenu->addAction(QObject::tr("Final Cut Pro X (*.fcpxml)…"), window,
                              &MainWindow::onExportFcpxml);
        exportMenu->addAction(QObject::tr("EDL Text File (*.txt)…"), window, &MainWindow::onExportEdl);
    }
    fileMenu->addAction(IconFactory::iconFromSvgBody(IconFactory::svgCapture()),
                        QObject::tr("Capture…"), window, &MainWindow::onCapture);
    fileMenu->addAction(IconFactory::iconFromSvgBody(IconFactory::svgCdExtract()),
                        QObject::tr("Extract Audio from CD…"), window,
                        &MainWindow::onExtractAudioFromCd);
    fileMenu->addSeparator();
    fileMenu->addAction(IconFactory::iconFromSvgBody(IconFactory::svgGear()), QObject::tr("Properties…"),
                        QKeySequence(QStringLiteral("Alt+Return")), window,
                        &MainWindow::onProjectProperties);
    fileMenu->addSeparator();
    fileMenu->addAction(QObject::tr("Exit"), QKeySequence(QStringLiteral("Alt+F4")), window,
                        &MainWindow::close);

    // Recent projects (QSettings recent/files) — filled like VEGAS File menu list
    {
        QSettings settings(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
        QStringList recent = settings.value(QStringLiteral("recent/files")).toStringList();
        recent.removeAll(QString());
        int shown = 0;
        for (const QString &path : recent) {
            if (!QFileInfo::exists(path)) {
                continue;
            }
            if (shown == 0) {
                fileMenu->addSeparator();
            }
            ++shown;
            const QString label =
                QStringLiteral("&%1  %2").arg(shown).arg(QDir::toNativeSeparators(path));
            auto *a = fileMenu->addAction(label);
            QObject::connect(a, &QAction::triggered, window, [window, path]() {
                window->openProjectPath(path);
            });
            if (shown >= 9) {
                break;
            }
        }
    }

    // Edit — structure matches VEGAS Pro 22 (SAMPLES/screenshots/sample_for_project_window_menu_edit.png)
    auto *editMenu = menuBar->addMenu(QObject::tr("Edit"));
    if (window->undoAction()) {
        editMenu->addAction(window->undoAction());
    } else {
        addStubDisabled(editMenu, QObject::tr("Undo"), QKeySequence::Undo);
    }
    if (window->redoAction()) {
        editMenu->addAction(window->redoAction());
    } else {
        auto *redo = addStubDisabled(editMenu, QObject::tr("Redo"));
        redo->setShortcuts({QKeySequence(QStringLiteral("Ctrl+Shift+Z")), QKeySequence::Redo});
    }
    editMenu->addSeparator();
    editMenu->addAction(QObject::tr("Cut"), QKeySequence::Cut, window, &MainWindow::onEditCut);
    editMenu->addAction(QObject::tr("Copy"), QKeySequence::Copy, window, &MainWindow::onEditCopy);
    editMenu->addAction(QObject::tr("Paste"), QKeySequence::Paste, window, &MainWindow::onEditPaste);
    addStubDisabled(editMenu, QObject::tr("Paste Repeat…"), QKeySequence(QStringLiteral("Ctrl+B")));
    addStubDisabled(editMenu, QObject::tr("Paste Insert"), QKeySequence(QStringLiteral("Ctrl+Shift+V")));
    addStubDisabled(editMenu, QObject::tr("Paste Event Attributes"));
    addStubDisabled(editMenu, QObject::tr("Selectively Paste Event Attributes"));
    editMenu->addSeparator();
    editMenu->addAction(QObject::tr("Delete"), QKeySequence::Delete, window, &MainWindow::onEditDelete);
    editMenu->addAction(QObject::tr("Trim"), QKeySequence(QStringLiteral("Ctrl+T")), window, &MainWindow::onEditTrim);
    editMenu->addAction(QObject::tr("Trim Start"), QKeySequence(QStringLiteral("Alt+[")), window,
                        &MainWindow::onEditTrimStart);
    editMenu->addAction(QObject::tr("Trim End"), QKeySequence(QStringLiteral("Alt+]")), window,
                        &MainWindow::onEditTrimEnd);
    auto *split = editMenu->addAction(QObject::tr("Split"), window, &MainWindow::onEditSplit);
    split->setProperty("keyboardCommand", "Edit.Split");
    addStubDisabled(editMenu, QObject::tr("Smart Split"), QKeySequence(QStringLiteral("Alt+S")));
    editMenu->addAction(QObject::tr("Quantize to Frames"), window, [window]() {
        window->runDocumentEdit(QObject::tr("Quantize to Frames"), [window]() {
            auto &model = window->projectModel();
            const double fps = std::max(1.0, model.frameRate());
            for (auto &track : model.tracks()) for (auto &ev : track.events) if (ev.selected) {
                const double start = std::round(ev.startSec * fps) / fps;
                const double end = std::round((ev.startSec + ev.lengthSec) * fps) / fps;
                ev.startSec = start; ev.lengthSec = std::max(1.0 / fps, end - start);
                ev.fadeInSec = std::min(ev.fadeInSec, ev.lengthSec);
                ev.fadeOutSec = std::min(ev.fadeOutSec, ev.lengthSec - ev.fadeInSec);
            }
        });
        window->refreshTimeline();
    });
    addStubDisabled(editMenu, QObject::tr("Close Gaps"));
    addStubDisabled(editMenu, QObject::tr("Freeze selected Adjustment Events to Project"));
    addStubDisabled(editMenu, QObject::tr("Freeze all Adjustment Events to Project"));
    editMenu->addSeparator();

    auto *navigate = editMenu->addMenu(QObject::tr("Navigate"));
    navigate->addAction(QObject::tr("Go to Start"), QKeySequence(QStringLiteral("Ctrl+Home")), window,
                        &MainWindow::onGoToStart);
    navigate->addAction(QObject::tr("Go to End"), QKeySequence(QStringLiteral("Ctrl+End")), window,
                        &MainWindow::onGoToEnd);
    navigate->addAction(QObject::tr("Go to Previous Marker"), window, [window]() { window->onNavigateMarker(false); });
    navigate->addAction(QObject::tr("Go to Next Marker"), window, [window]() { window->onNavigateMarker(true); });

    auto *ripple = editMenu->addMenu(QObject::tr("Post-Edit Ripple"));
    addStubDisabled(ripple, QObject::tr("Affected Tracks"));
    addStubDisabled(ripple, QObject::tr("Affected Tracks, Bus Tracks, Markers, and Regions"));
    addStubDisabled(ripple, QObject::tr("All Tracks, Markers, and Regions"));

    auto *selectMenu = editMenu->addMenu(QObject::tr("Select"));
    selectMenu->addAction(QObject::tr("Select All"), QKeySequence::SelectAll, window, &MainWindow::onSelectAll);
    for (bool end : {false, true}) {
        selectMenu->addAction(end ? QObject::tr("Select Event End") : QObject::tr("Select Event Start"), window, [window, end]() {
            const auto ids = window->projectModel().selectedEventIds();
            if (ids.isEmpty() || !window->timelineView()) return;
            double start = std::numeric_limits<double>::infinity(), finish = 0.0;
            for (int id : ids) if (const TrackEvent *ev = window->projectModel().findEvent(id)) {
                start = std::min(start, ev->startSec); finish = std::max(finish, ev->startSec + ev->lengthSec);
            }
            if (std::isfinite(start)) window->timelineView()->seekPlayhead(end ? finish : start);
        });
    }
    selectMenu->addAction(QObject::tr("Select Events to End"), window, [window]() {
        auto &model = window->projectModel();
        const double cursor = model.playheadSec();
        model.clearSelection();
        for (auto &track : model.tracks()) for (auto &ev : track.events)
            if (ev.startSec + ev.lengthSec > cursor) ev.selected = true;
        window->refreshTimeline();
    });

    auto *editTool = editMenu->addMenu(QObject::tr("Editing Tool"));
    addStubDisabled(editTool, QObject::tr("Normal Edit Tool"), QKeySequence(QStringLiteral("Ctrl+D")));
    addStubDisabled(editTool, QObject::tr("Envelope Edit Tool"));
    addStubDisabled(editTool, QObject::tr("Selection Edit Tool"));
    addStubDisabled(editTool, QObject::tr("Zoom Edit Tool"));
    editTool->addAction(QObject::tr("Delete"), window, &MainWindow::onEditDelete);

    auto *extensions = editMenu->addMenu(QObject::tr("Extensions"));
    addStubDisabled(extensions, QObject::tr("(none)"));

    editMenu->addSeparator();

    auto *switches = editMenu->addMenu(QObject::tr("Switches"));
    addStubDisabled(switches, QObject::tr("Mute"));
    addStubDisabled(switches, QObject::tr("Lock"));
    auto *eventLoop = switches->addAction(QObject::tr("Loop"));
    eventLoop->setCheckable(true);
    QObject::connect(switches, &QMenu::aboutToShow, window, [window, eventLoop]() {
        const auto ids = window->projectModel().selectedEventIds();
        eventLoop->setEnabled(!ids.isEmpty());
        bool all = !ids.isEmpty();
        for (int id : ids) if (const auto *ev = window->projectModel().findEvent(id)) all &= ev->looped;
        QSignalBlocker block(eventLoop); eventLoop->setChecked(all);
    });
    QObject::connect(eventLoop, &QAction::triggered, window, [window](bool on) {
        window->runDocumentEdit(QObject::tr("Loop Events"), [window, on]() {
            for (int id : window->projectModel().selectedEventIds())
                if (auto *ev = window->projectModel().findEvent(id)) ev->looped = on;
        });
        window->refreshTimeline();
    });
    addStubDisabled(switches, QObject::tr("Invert Phase"));
    addStubDisabled(switches, QObject::tr("Normalize"));
    switches->addAction(QObject::tr("Maintain Aspect Ratio"), window, [window]() {
        const auto ids = window->projectModel().selectedEventIds();
        bool on = true;
        for (int id : ids) if (const auto *ev = window->projectModel().findEvent(id); ev && isVideoFamily(ev->mediaKind)) {
            on = !ev->panCrop.maintainAspectRatio; break;
        }
        window->runDocumentEdit(QObject::tr("Maintain Aspect Ratio"), [window, ids, on]() {
            for (int id : ids) if (auto *ev = window->projectModel().findEvent(id); ev && isVideoFamily(ev->mediaKind))
                ev->panCrop.maintainAspectRatio = on;
        });
        window->refreshTimeline();
    });
    addStubDisabled(switches, QObject::tr("Reduce Interlace Flicker"));

    auto *tags = editMenu->addMenu(QObject::tr("Tags"));
    addStubDisabled(tags, QObject::tr("(none)"));

    auto *group = editMenu->addMenu(QObject::tr("Group"));
    const auto createGroup = [window]() {
        window->runDocumentEdit(QObject::tr("Group"), [window]() { window->projectModel().groupSelectedEvents(); });
        window->refreshTimeline();
    };
    auto *groupSelected = group->addAction(QObject::tr("Group"), window, createGroup);
    groupSelected->setProperty("keyboardCommand", "Group.CreateNew");
    group->addAction(QObject::tr("Ungroup"), window, [window]() {
        window->runDocumentEdit(QObject::tr("Ungroup"), [window]() {
            for (int id : window->projectModel().selectedEventIds()) window->projectModel().ungroupEvent(id);
        }); window->refreshTimeline();
    });
    auto *ignoreGroup = group->addAction(QObject::tr("Ignore Event Grouping"));
    ignoreGroup->setCheckable(true); ignoreGroup->setProperty("editingOption", "group");
    QObject::connect(ignoreGroup, &QAction::toggled, window, &MainWindow::setIgnoreEventGrouping);
    auto *clearGroup = group->addAction(QObject::tr("Clear Group"), window, [window]() {
        QSet<int> groups;
        for (int id : window->projectModel().selectedEventIds())
            if (const auto *ev = window->projectModel().findEvent(id); ev && ev->groupId > 0) groups.insert(ev->groupId);
        window->runDocumentEdit(QObject::tr("Clear Group"), [window, groups]() {
            for (int id : groups) window->projectModel().clearGroup(id);
        }); window->refreshTimeline();
    });
    clearGroup->setProperty("keyboardCommand", "Group.Clear");
    group->addAction(QObject::tr("Select Group"), window, [window]() {
        QSet<int> groups;
        for (int id : window->projectModel().selectedEventIds())
            if (const auto *ev = window->projectModel().findEvent(id); ev && ev->groupId > 0) groups.insert(ev->groupId);
        for (int id : groups) for (int eventId : window->projectModel().eventIdsInGroup(id))
            if (auto *ev = window->projectModel().findEvent(eventId)) ev->selected = true;
        window->refreshTimeline();
    });
    group->addAction(QObject::tr("Create New Group"), window, createGroup);

    auto *stream = editMenu->addMenu(QObject::tr("Stream"));
    addStubDisabled(stream, QObject::tr("Stream 0"));
    addStubDisabled(stream, QObject::tr("Stream 1"));

    auto *channels = editMenu->addMenu(QObject::tr("Channels"));
    for (int mode = 0; mode < 3; ++mode) {
        const QStringList labels{QObject::tr("Both"), QObject::tr("Left Only"), QObject::tr("Right Only")};
        channels->addAction(labels[mode], window, [window, mode]() {
            window->runDocumentEdit(QObject::tr("Audio Channels"), [window, mode]() {
                for (int id : window->projectModel().selectedEventIds())
                    if (auto *ev = window->projectModel().findEvent(id); ev && isAudioFamily(ev->mediaKind)) {
                        ev->firstChannel = mode == 2 ? 1 : 0; ev->channelCount = mode == 0 ? 2 : 1;
                    }
            }); window->refreshTimeline();
        });
    }
    addStubDisabled(channels, QObject::tr("Combine"));
    addStubDisabled(channels, QObject::tr("Swap Channels"));

    editMenu->addSeparator();
    editMenu->addAction(QObject::tr("Undo All"), window, [window]() { if (window->undoStack()) window->undoStack()->setIndex(0); });
    editMenu->addAction(QObject::tr("Clear Edit History…"), window, &MainWindow::clearUndoHistory);

    auto *viewMenu = menuBar->addMenu(QObject::tr("View"));
    addStubDisabled(viewMenu, QObject::tr("Show Bus Tracks"))->setCheckable(true);
    addStubDisabled(viewMenu, QObject::tr("Active Take Information"))->setCheckable(true);
    viewMenu->addSeparator();
    viewMenu->addAction(QObject::tr("Zoom In Time"), QKeySequence(QStringLiteral("Up")), window, [window]() { window->onZoomTime(1.25); });
    viewMenu->addAction(QObject::tr("Zoom Out Time"), QKeySequence(QStringLiteral("Down")), window, [window]() { window->onZoomTime(0.8); });
    viewMenu->addAction(QObject::tr("Zoom Time to Selection"), window, &MainWindow::onZoomTimeSelection);
    viewMenu->addSeparator();
    {
        auto *layouts = viewMenu->addMenu(QObject::tr("Window Layouts"));
        layouts->addAction(QObject::tr("Default Layout"), window, &MainWindow::onDefaultLayout);
        layouts->addAction(QObject::tr("Save Layout…"), window, &MainWindow::onSaveLayout);
        layouts->addAction(QObject::tr("Load Layout…"), window, &MainWindow::onLoadLayout);
    }
    {
        auto *tb = viewMenu->addMenu(QObject::tr("Toolbar"));
        auto *mainTb = tb->addAction(QObject::tr("Main Toolbar"));
        QObject::connect(mainTb, &QAction::toggled, window, &MainWindow::setMainToolbarVisible);
        QObject::connect(tb, &QMenu::aboutToShow, window, [window, mainTb]() { QSignalBlocker b(mainTb); mainTb->setChecked(window->mainToolbarVisible()); });
        mainTb->setCheckable(true);
        mainTb->setChecked(true);
    }
    {
        auto *dock = viewMenu->addMenu(QObject::tr("Docking Layouts"));
        dock->addAction(QObject::tr("Default Docking"), window, &MainWindow::onDefaultLayout);
        dock->addAction(QObject::tr("Save Docking Layout…"), window, &MainWindow::onSaveLayout);
    }
    viewMenu->addSeparator();
    auto *mixingConsole = viewMenu->addAction(QObject::tr("Mixing Console"), window, &MainWindow::onMixingConsole);
    mixingConsole->setProperty("keyboardCommand", "View.MixingConsole");
    viewMenu->addAction(QObject::tr("Welcome Screen"), window, &MainWindow::onWelcome);

    auto *insertMenu = menuBar->addMenu(QObject::tr("Insert"));
    insertMenu->addAction(QObject::tr("Audio Track"), QKeySequence(QStringLiteral("Ctrl+Q")), window, [window]() {
        window->runDocumentEdit(QObject::tr("Insert audio track"), [window]() { window->projectModel().addTrack(TrackKind::Audio); });
        window->refreshTimeline();
    });
    insertMenu->addAction(QObject::tr("Video Track"), QKeySequence(QStringLiteral("Ctrl+Shift+Q")), window, [window]() {
        window->runDocumentEdit(QObject::tr("Insert video track"), [window]() { window->projectModel().addTrack(TrackKind::Video); });
        window->refreshTimeline();
    });
    insertMenu->addSeparator();
    {
        auto *ae = insertMenu->addMenu(QObject::tr("Audio Envelope"));
        addStubDisabled(ae, QObject::tr("Volume"));
        addStubDisabled(ae, QObject::tr("Pan"));
    }
    {
        auto *ve = insertMenu->addMenu(QObject::tr("Video Envelope"));
        addStubDisabled(ve, QObject::tr("Composite Level"));
        addStubDisabled(ve, QObject::tr("Fade to Color"));
    }
    insertMenu->addSeparator();
    addStubDisabled(insertMenu, QObject::tr("Audio Bus Track"));
    addStubDisabled(insertMenu, QObject::tr("Video Bus Track"));
    insertMenu->addSeparator();
    addStubDisabled(insertMenu, QObject::tr("Empty Event"));
    insertMenu->addAction(QObject::tr("Text Media…"), window, [window]() { window->createTitlesTextEvent(); });

    auto *toolsMenu = menuBar->addMenu(QObject::tr("Tools"));
    {
        auto *scripting = toolsMenu->addMenu(QObject::tr("Scripting"));
        addStubDisabled(scripting, QObject::tr("Run Script…"));
        addStubDisabled(scripting, QObject::tr("Rescan Script Menu Folder"));
    }
    {
        auto *ext = toolsMenu->addMenu(QObject::tr("External Tools"));
        addStubDisabled(ext, QObject::tr("(none)"));
    }
    toolsMenu->addSeparator();
    addStubDisabled(toolsMenu, QObject::tr("Build Dynamic RAM Preview"), QKeySequence(QStringLiteral("Shift+B")));
    addStubDisabled(toolsMenu, QObject::tr("Selectively Prune Dynamic RAM Preview"));
    toolsMenu->addSeparator();
    {
        auto *audio = toolsMenu->addMenu(QObject::tr("Audio"));
        audio->addAction(QObject::tr("Audio Mixer"), window, &MainWindow::onMixingConsole);
        addStubDisabled(audio, QObject::tr("Rebuild Audio Peaks"));
    }
    {
        auto *video = toolsMenu->addMenu(QObject::tr("Video"));
        addStubDisabled(video, QObject::tr("Video Scopes"));
        addStubDisabled(video, QObject::tr("Apply Non-Real-Time Event FX…"));
    }
    toolsMenu->addSeparator();
    toolsMenu->addAction(QObject::tr("Plug-In Chooser…"), window, &MainWindow::onPluginChooser);

    auto *optionsMenu = menuBar->addMenu(QObject::tr("Options"));
    {
        auto *snap = optionsMenu->addAction(QObject::tr("Enable Snapping"));
        snap->setProperty("editingOption", "snap");
        QObject::connect(snap, &QAction::toggled, window, &MainWindow::setSnappingEnabled);
        snap->setCheckable(true);
        snap->setChecked(true);
    }
    {
        auto *q = optionsMenu->addAction(QObject::tr("Quantize to Frames"));
        q->setShortcut(QKeySequence(QStringLiteral("Alt+F8")));
        q->setProperty("editingOption", "quantize");
        QObject::connect(q, &QAction::toggled, window, [window](bool on) { window->projectModel().setQuantizeToFrames(on); window->syncEditingControls(); });
        q->setCheckable(true);
        q->setChecked(true);
    }
    {
        auto *enableRipple = optionsMenu->addMenu(QObject::tr("Enable Ripple Editing"));
        addStubDisabled(enableRipple, QObject::tr("Affected Tracks"));
        addStubDisabled(enableRipple, QObject::tr("Affected Tracks, Bus Tracks, Markers, and Regions"));
        addStubDisabled(enableRipple, QObject::tr("All Tracks, Markers, and Regions"));
    }
    optionsMenu->addSeparator();
    addStubDisabled(optionsMenu, QObject::tr("Metronome"))->setCheckable(true);
    {
        auto *loop = optionsMenu->addAction(QObject::tr("Loop Playback"));
        loop->setCheckable(true);
        loop->setChecked(window->projectModel().loopPlaybackEnabled());
        loop->setProperty("editingOption", "loop");
        QObject::connect(loop, &QAction::toggled, window, [window](bool on) {
            window->setLoopPlaybackEnabled(on);
        });
    }
    auto *ignore = optionsMenu->addAction(QObject::tr("Ignore Event Grouping"));
    ignore->setCheckable(true); ignore->setProperty("editingOption", "group");
    QObject::connect(ignore, &QAction::toggled, window, &MainWindow::setIgnoreEventGrouping);
    optionsMenu->addSeparator();
    addStubDisabled(optionsMenu, QObject::tr("Customize Toolbar…"));
    addStubDisabled(optionsMenu, QObject::tr("Customize Timeline Toolbar…"));
    optionsMenu->addAction(QObject::tr("Customize Keyboard…"), window,
                           &MainWindow::onCustomizeKeyboard);
    addStubDisabled(optionsMenu, QObject::tr("Export/Import Preferences…"));
    optionsMenu->addAction(QObject::tr("Preferences…"), window, &MainWindow::onPreferences);

    for (QMenu *menu : menuBar->findChildren<QMenu *>())
        QObject::connect(menu, &QMenu::aboutToShow, window, &MainWindow::syncEditingControls);
    window->syncEditingControls();

    auto *helpMenu = menuBar->addMenu(QObject::tr("Help"));
    helpMenu->addAction(QObject::tr("Contents and Index"), QKeySequence::HelpContents, window, &MainWindow::onHelp);
    addStubDisabled(helpMenu, QObject::tr("OpenVegas Interactive Tutorials"));
    helpMenu->addSeparator();
    helpMenu->addAction(QObject::tr("Keyboard Mapping…"), window, &MainWindow::onCustomizeKeyboard);
    helpMenu->addAction(QObject::tr("About OpenVegas…"), window, &MainWindow::onAbout);
}

} // namespace openvegas
