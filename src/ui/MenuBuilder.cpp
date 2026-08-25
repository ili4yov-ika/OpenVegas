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

namespace openvegas {

namespace {

QAction *addStub(QMenu *menu, const QString &text, const QKeySequence &shortcut = {})
{
    // Do not call setShortcut on stubs — enabled menu actions steal accelerators
    // application-wide even with no slot connected (Delete/Ctrl+C were broken by this).
    auto *a = menu->addAction(text);
    if (!shortcut.isEmpty()) {
        a->setText(text + QLatin1Char('\t') + shortcut.toString(QKeySequence::NativeText));
    }
    return a;
}

QAction *addStubDisabled(QMenu *menu, const QString &text, const QKeySequence &shortcut = {})
{
    auto *a = menu->addAction(text);
    if (!shortcut.isEmpty()) {
        a->setShortcut(shortcut); // safe: disabled actions do not consume shortcuts
    }
    a->setEnabled(false);
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
    addStub(fileMenu, QObject::tr("Incremental Save"), QKeySequence(QStringLiteral("Ctrl+Alt+S")));
    fileMenu->addSeparator();
    fileMenu->addAction(IconFactory::iconFromSvgBody(IconFactory::svgRender()), QObject::tr("Render As…"),
                        QKeySequence(QStringLiteral("Ctrl+Shift+M")), window, &MainWindow::onRenderAs);
    fileMenu->addAction(QObject::tr("Bounce Audio Mixdown…"), window, &MainWindow::onBounceAudioMixdown);
    addStub(fileMenu, QObject::tr("Real-Time Render…"));
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
    addStubDisabled(editMenu, QObject::tr("Trim"), QKeySequence(QStringLiteral("Ctrl+T")));
    editMenu->addAction(QObject::tr("Trim Start"), QKeySequence(QStringLiteral("Alt+[")), window,
                        &MainWindow::onEditTrimStart);
    editMenu->addAction(QObject::tr("Trim End"), QKeySequence(QStringLiteral("Alt+]")), window,
                        &MainWindow::onEditTrimEnd);
    editMenu->addAction(QObject::tr("Split"), QKeySequence(QStringLiteral("S")), window,
                        &MainWindow::onEditSplit);
    addStubDisabled(editMenu, QObject::tr("Smart Split"), QKeySequence(QStringLiteral("Alt+S")));
    addStub(editMenu, QObject::tr("Quantize to Frames"));
    addStub(editMenu, QObject::tr("Close Gaps"));
    addStub(editMenu, QObject::tr("Freeze selected Adjustment Events to Project"));
    addStub(editMenu, QObject::tr("Freeze all Adjustment Events to Project"));
    editMenu->addSeparator();

    auto *navigate = editMenu->addMenu(QObject::tr("Navigate"));
    navigate->addAction(QObject::tr("Go to Start"), QKeySequence(QStringLiteral("Ctrl+Home")), window,
                        &MainWindow::onGoToStart);
    navigate->addAction(QObject::tr("Go to End"), QKeySequence(QStringLiteral("Ctrl+End")), window,
                        &MainWindow::onGoToEnd);
    addStub(navigate, QObject::tr("Go to Previous Marker"));
    addStub(navigate, QObject::tr("Go to Next Marker"));

    auto *ripple = editMenu->addMenu(QObject::tr("Post-Edit Ripple"));
    addStub(ripple, QObject::tr("Affected Tracks"));
    addStub(ripple, QObject::tr("Affected Tracks, Bus Tracks, Markers, and Regions"));
    addStub(ripple, QObject::tr("All Tracks, Markers, and Regions"));

    auto *selectMenu = editMenu->addMenu(QObject::tr("Select"));
    selectMenu->addAction(QObject::tr("Select All"), QKeySequence::SelectAll, window, &MainWindow::onSelectAll);
    addStub(selectMenu, QObject::tr("Select Event Start"));
    addStub(selectMenu, QObject::tr("Select Event End"));
    addStub(selectMenu, QObject::tr("Select Events to End"));

    auto *editTool = editMenu->addMenu(QObject::tr("Editing Tool"));
    addStub(editTool, QObject::tr("Normal Edit Tool"), QKeySequence(QStringLiteral("Ctrl+D")));
    addStub(editTool, QObject::tr("Envelope Edit Tool"));
    addStub(editTool, QObject::tr("Selection Edit Tool"));
    addStub(editTool, QObject::tr("Zoom Edit Tool"));
    addStub(editTool, QObject::tr("Delete"));

    auto *extensions = editMenu->addMenu(QObject::tr("Extensions"));
    addStubDisabled(extensions, QObject::tr("(none)"));

    editMenu->addSeparator();

    auto *switches = editMenu->addMenu(QObject::tr("Switches"));
    addStub(switches, QObject::tr("Mute"));
    addStub(switches, QObject::tr("Lock"));
    addStub(switches, QObject::tr("Loop"));
    addStub(switches, QObject::tr("Invert Phase"));
    addStub(switches, QObject::tr("Normalize"));
    addStub(switches, QObject::tr("Maintain Aspect Ratio"));
    addStub(switches, QObject::tr("Reduce Interlace Flicker"));

    auto *tags = editMenu->addMenu(QObject::tr("Tags"));
    addStubDisabled(tags, QObject::tr("(none)"));

    auto *group = editMenu->addMenu(QObject::tr("Group"));
    addStub(group, QObject::tr("Group"), QKeySequence(QStringLiteral("G")));
    addStub(group, QObject::tr("Ungroup"), QKeySequence(QStringLiteral("U")));
    addStub(group, QObject::tr("Ignore Event Grouping"), QKeySequence(QStringLiteral("Ctrl+Shift+U")));
    addStub(group, QObject::tr("Clear Group"));
    addStub(group, QObject::tr("Select Group"));
    addStub(group, QObject::tr("Create New Group"));

    auto *stream = editMenu->addMenu(QObject::tr("Stream"));
    addStub(stream, QObject::tr("Stream 0"));
    addStub(stream, QObject::tr("Stream 1"));

    auto *channels = editMenu->addMenu(QObject::tr("Channels"));
    addStub(channels, QObject::tr("Both"));
    addStub(channels, QObject::tr("Left Only"));
    addStub(channels, QObject::tr("Right Only"));
    addStub(channels, QObject::tr("Combine"));
    addStub(channels, QObject::tr("Swap Channels"));

    editMenu->addSeparator();
    addStubDisabled(editMenu, QObject::tr("Undo All"));
    addStubDisabled(editMenu, QObject::tr("Clear Edit History…"));

    auto *viewMenu = menuBar->addMenu(QObject::tr("View"));
    addStub(viewMenu, QObject::tr("Show Bus Tracks"))->setCheckable(true);
    addStub(viewMenu, QObject::tr("Active Take Information"))->setCheckable(true);
    viewMenu->addSeparator();
    addStub(viewMenu, QObject::tr("Zoom In Time"), QKeySequence(QStringLiteral("Up")));
    addStub(viewMenu, QObject::tr("Zoom Out Time"), QKeySequence(QStringLiteral("Down")));
    addStub(viewMenu, QObject::tr("Zoom Time to Selection"));
    viewMenu->addSeparator();
    {
        auto *layouts = viewMenu->addMenu(QObject::tr("Window Layouts"));
        addStub(layouts, QObject::tr("Default Layout"));
        addStub(layouts, QObject::tr("Save Layout…"));
        addStub(layouts, QObject::tr("Load Layout…"));
    }
    {
        auto *tb = viewMenu->addMenu(QObject::tr("Toolbar"));
        auto *mainTb = addStub(tb, QObject::tr("Main Toolbar"));
        mainTb->setCheckable(true);
        mainTb->setChecked(true);
    }
    {
        auto *dock = viewMenu->addMenu(QObject::tr("Docking Layouts"));
        addStub(dock, QObject::tr("Default Docking"));
        addStub(dock, QObject::tr("Save Docking Layout…"));
    }
    viewMenu->addSeparator();
    viewMenu->addAction(QObject::tr("Mixing Console"), QKeySequence(QStringLiteral("Alt+6")), window,
                        &MainWindow::onMixingConsole);
    viewMenu->addAction(QObject::tr("Welcome Screen"), window, &MainWindow::onWelcome);

    auto *insertMenu = menuBar->addMenu(QObject::tr("Insert"));
    insertMenu->addAction(QObject::tr("Audio Track"), QKeySequence(QStringLiteral("Ctrl+Q")), window,
                          [window]() {
                              Track t;
                              t.id = window->projectModel().tracks().isEmpty()
                                         ? 1
                                         : (window->projectModel().tracks().last().id + 1);
                              t.name = QObject::tr("Audio %1").arg(window->projectModel().tracks().size() + 1);
                              t.kind = TrackKind::Audio;
                              t.height = 100;
                              window->projectModel().tracks().push_back(t);
                              window->refreshTimeline();
                          });
    insertMenu->addAction(QObject::tr("Video Track"), QKeySequence(QStringLiteral("Ctrl+Shift+Q")),
                          window, [window]() {
                              Track t;
                              t.id = window->projectModel().tracks().isEmpty()
                                         ? 1
                                         : (window->projectModel().tracks().last().id + 1);
                              t.name = QObject::tr("Video %1").arg(window->projectModel().tracks().size() + 1);
                              t.kind = TrackKind::Video;
                              t.height = 96;
                              window->projectModel().tracks().push_back(t);
                              window->refreshTimeline();
                          });
    insertMenu->addSeparator();
    {
        auto *ae = insertMenu->addMenu(QObject::tr("Audio Envelope"));
        addStub(ae, QObject::tr("Volume"));
        addStub(ae, QObject::tr("Pan"));
    }
    {
        auto *ve = insertMenu->addMenu(QObject::tr("Video Envelope"));
        addStub(ve, QObject::tr("Composite Level"));
        addStub(ve, QObject::tr("Fade to Color"));
    }
    insertMenu->addSeparator();
    addStub(insertMenu, QObject::tr("Audio Bus Track"));
    addStub(insertMenu, QObject::tr("Video Bus Track"));
    insertMenu->addSeparator();
    addStub(insertMenu, QObject::tr("Empty Event"));
    addStub(insertMenu, QObject::tr("Text Media…"));

    auto *toolsMenu = menuBar->addMenu(QObject::tr("Tools"));
    {
        auto *scripting = toolsMenu->addMenu(QObject::tr("Scripting"));
        addStub(scripting, QObject::tr("Run Script…"));
        addStub(scripting, QObject::tr("Rescan Script Menu Folder"));
    }
    {
        auto *ext = toolsMenu->addMenu(QObject::tr("External Tools"));
        addStubDisabled(ext, QObject::tr("(none)"));
    }
    toolsMenu->addSeparator();
    addStub(toolsMenu, QObject::tr("Build Dynamic RAM Preview"), QKeySequence(QStringLiteral("Shift+B")));
    addStub(toolsMenu, QObject::tr("Selectively Prune Dynamic RAM Preview"));
    toolsMenu->addSeparator();
    {
        auto *audio = toolsMenu->addMenu(QObject::tr("Audio"));
        addStub(audio, QObject::tr("Audio Mixer"));
        addStub(audio, QObject::tr("Rebuild Audio Peaks"));
    }
    {
        auto *video = toolsMenu->addMenu(QObject::tr("Video"));
        addStub(video, QObject::tr("Video Scopes"));
        addStub(video, QObject::tr("Apply Non-Real-Time Event FX…"));
    }
    toolsMenu->addSeparator();
    toolsMenu->addAction(QObject::tr("Plug-In Chooser…"), window, &MainWindow::onPluginChooser);

    auto *optionsMenu = menuBar->addMenu(QObject::tr("Options"));
    {
        auto *snap = addStub(optionsMenu, QObject::tr("Enable Snapping"), QKeySequence(QStringLiteral("F8")));
        snap->setCheckable(true);
        snap->setChecked(true);
    }
    {
        auto *q = addStub(optionsMenu, QObject::tr("Quantize to Frames"),
                          QKeySequence(QStringLiteral("Alt+F8")));
        q->setCheckable(true);
        q->setChecked(true);
    }
    {
        auto *enableRipple = optionsMenu->addMenu(QObject::tr("Enable Ripple Editing"));
        addStub(enableRipple, QObject::tr("Affected Tracks"));
        addStub(enableRipple, QObject::tr("Affected Tracks, Bus Tracks, Markers, and Regions"));
        addStub(enableRipple, QObject::tr("All Tracks, Markers, and Regions"));
    }
    optionsMenu->addSeparator();
    addStub(optionsMenu, QObject::tr("Metronome"))->setCheckable(true);
    {
        auto *loop = optionsMenu->addAction(QObject::tr("Loop Playback"));
        loop->setCheckable(true);
        loop->setChecked(window->projectModel().loopPlaybackEnabled());
        loop->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+L")));
        QObject::connect(loop, &QAction::toggled, window, [window](bool on) {
            window->projectModel().setLoopPlaybackEnabled(on);
        });
    }
    addStub(optionsMenu, QObject::tr("Ignore Event Grouping"),
            QKeySequence(QStringLiteral("Ctrl+Shift+U")))
        ->setCheckable(true);
    optionsMenu->addSeparator();
    addStub(optionsMenu, QObject::tr("Customize Toolbar…"));
    addStub(optionsMenu, QObject::tr("Customize Timeline Toolbar…"));
    optionsMenu->addAction(QObject::tr("Customize Keyboard…"), window,
                           &MainWindow::onCustomizeKeyboard);
    addStub(optionsMenu, QObject::tr("Export/Import Preferences…"));
    optionsMenu->addAction(QObject::tr("Preferences…"), window, &MainWindow::onPreferences);

    auto *helpMenu = menuBar->addMenu(QObject::tr("Help"));
    addStub(helpMenu, QObject::tr("Contents and Index"), QKeySequence::HelpContents);
    addStub(helpMenu, QObject::tr("OpenVegas Interactive Tutorials"));
    helpMenu->addSeparator();
    helpMenu->addAction(QObject::tr("Keyboard Mapping…"), window, &MainWindow::onCustomizeKeyboard);
    helpMenu->addAction(QObject::tr("About OpenVegas…"), window, &MainWindow::onAbout);
}

} // namespace openvegas
