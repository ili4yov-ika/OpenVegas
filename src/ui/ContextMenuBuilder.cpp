#include "ui/ContextMenuBuilder.h"
#include "app/MainWindow.h"
#include "model/ProjectModel.h"
#include "timeline/TimelineView.h"
#include "ui/ConfirmWarningDialog.h"
#include "ui/PluginChooserDialog.h"
#include "plugins/AudioPluginHost.h"
#include "plugins/AudioPluginTypes.h"
#include "audio/CompositePluginHost.h"

#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QKeySequence>
#include <QWidget>
#include <QToolButton>
#include <QTimer>
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QColor>
#include <QSettings>
#include <algorithm>

namespace openvegas {

namespace {

QString cleanLabel(QString s)
{
    return s.remove(QStringLiteral("<u>")).remove(QStringLiteral("</u>"));
}

QAction *addItem(QMenu *menu, const QString &label, const QKeySequence &shortcut = {},
                 bool enabled = true)
{
    auto *a = menu->addAction(cleanLabel(label));
    if (!shortcut.isEmpty()) {
        a->setShortcut(shortcut);
    }
    a->setEnabled(enabled);
    return a;
}

void addSwitchesVideo(QMenu *parent)
{
    auto *m = parent->addMenu(QObject::tr("Switches"));
    addItem(m, QObject::tr("Mute"))->setCheckable(true);
    addItem(m, QObject::tr("Lock"))->setCheckable(true);
    m->addSeparator();
    addItem(m, QObject::tr("Hold last frame"))->setCheckable(true);
    {
        auto *g = new QActionGroup(m);
        g->setExclusive(true);
        auto *loop = addItem(m, QObject::tr("Loop event"));
        loop->setCheckable(true);
        loop->setChecked(true);
        g->addAction(loop);
        auto *trimAll = addItem(m, QObject::tr("Trim event to include all frames"));
        trimAll->setCheckable(true);
        g->addAction(trimAll);
    }
    m->addSeparator();
    {
        auto *aspect = addItem(m, QObject::tr("Maintain Aspect Ratio"));
        aspect->setCheckable(true);
        aspect->setChecked(true);
        addItem(m, QObject::tr("Reduce Interlace Flicker"))->setCheckable(true);
    }
    m->addSeparator();
    {
        auto *g = new QActionGroup(m);
        g->setExclusive(true);
        auto addRadio = [&](const QString &label, bool on = false) {
            auto *a = addItem(m, label);
            a->setCheckable(true);
            a->setChecked(on);
            g->addAction(a);
        };
        addRadio(QObject::tr("Use Project Resample Mode"), true);
        addRadio(QObject::tr("Frame Blend"));
        addRadio(QObject::tr("Optical Flow"));
        addRadio(QObject::tr("Disable Resample"));
    }
}

void addSwitchesAudio(QMenu *parent)
{
    auto *m = parent->addMenu(QObject::tr("Switches"));
    addItem(m, QObject::tr("Mute"))->setCheckable(true);
    addItem(m, QObject::tr("Lock"))->setCheckable(true);
    {
        auto *loop = addItem(m, QObject::tr("Loop"));
        loop->setCheckable(true);
        loop->setChecked(true);
    }
    addItem(m, QObject::tr("Invert Phase"))->setCheckable(true);
    addItem(m, QObject::tr("Normalize"))->setCheckable(true);
    addItem(m, QObject::tr("Auto Normalize"))->setCheckable(true);
}

void addGroupMenu(QMenu *parent, MainWindow *window, int eventId, bool createNewEnabled = true)
{
    auto *m = parent->addMenu(QObject::tr("Group"));
    auto *create = addItem(m, QObject::tr("Create New"), QKeySequence(QStringLiteral("G")),
                           createNewEnabled);
    QObject::connect(create, &QAction::triggered, window, [window]() {
        window->runDocumentEdit(QObject::tr("Group"),
                                [window]() { window->projectModel().groupSelectedEvents(); });
        window->refreshTimeline();
    });
    m->addSeparator();
    auto *remove = addItem(m, QObject::tr("Remove From"), QKeySequence(QStringLiteral("U")));
    QObject::connect(remove, &QAction::triggered, window, [window, eventId]() {
        window->runDocumentEdit(QObject::tr("Ungroup"),
                                [window, eventId]() { window->projectModel().ungroupEvent(eventId); });
        window->refreshTimeline();
    });
    auto *clear = addItem(m, QObject::tr("Clear"), QKeySequence(QStringLiteral("Ctrl+U")));
    QObject::connect(clear, &QAction::triggered, window, [window, eventId]() {
        window->runDocumentEdit(QObject::tr("Clear Group"), [window, eventId]() {
            if (TrackEvent *ev = window->projectModel().findEvent(eventId)) {
                window->projectModel().clearGroup(ev->groupId);
            }
        });
        window->refreshTimeline();
    });
    auto *selAll = addItem(m, QObject::tr("Select All"), QKeySequence(QStringLiteral("Shift+G")));
    QObject::connect(selAll, &QAction::triggered, window, [window, eventId]() {
        if (TrackEvent *ev = window->projectModel().findEvent(eventId)) {
            window->projectModel().selectGroup(ev->groupId);
            window->refreshTimeline();
        }
    });
    m->addSeparator();
    addItem(m, QObject::tr("Cut All"));
    addItem(m, QObject::tr("Copy All"));
    auto *delAll = addItem(m, QObject::tr("Delete All"));
    QObject::connect(delAll, &QAction::triggered, window, [window, eventId]() {
        window->runDocumentEdit(QObject::tr("Delete"), [window, eventId]() {
            window->projectModel().removeEventOrGroup(eventId);
        });
        window->refreshTimeline();
    });
}

void addTakeMenu(QMenu *parent, const QString &activeTakeName = {})
{
    auto *m = parent->addMenu(QObject::tr("Take"));
    addItem(m, QObject::tr("Rename Active"), QKeySequence(Qt::Key_F2));
    addItem(m, QObject::tr("Choose Active…"));
    m->addSeparator();
    addItem(m, QObject::tr("Next Take"), QKeySequence(QStringLiteral("T")), false);
    addItem(m, QObject::tr("Previous Take"), QKeySequence(QStringLiteral("Shift+T")), false);
    m->addSeparator();
    addItem(m, QObject::tr("Delete Active"));
    addItem(m, QObject::tr("Delete…"));
    m->addSeparator();
    {
        auto *g = new QActionGroup(m);
        g->setExclusive(true);
        const QString label =
            activeTakeName.isEmpty() ? QObject::tr("(Active Take)") : activeTakeName;
        auto *take = addItem(m, label);
        take->setCheckable(true);
        take->setChecked(true);
        g->addAction(take);
    }
}

void addVideoEventEnvelopeMenu(QMenu *parent)
{
    auto *env = parent->addMenu(QObject::tr("Insert/Remove Envelope"));
    addItem(env, QObject::tr("Velocity"))->setCheckable(true);
    addItem(env, QObject::tr("Freeze Frame at Cursor"));
    addItem(env, QObject::tr("Transition Progress"), {}, false)->setCheckable(true);
}

void addStreamMenu(QMenu *parent)
{
    auto *stream = parent->addMenu(QObject::tr("Stream"));
    auto *g = new QActionGroup(stream);
    g->setExclusive(true);
    auto *s1 = addItem(stream, QObject::tr("Stream 1"));
    s1->setCheckable(true);
    s1->setChecked(true);
    g->addAction(s1);
}

void addChannelsMenu(QMenu *parent)
{
    auto *channels = parent->addMenu(QObject::tr("Channels"));
    auto *g = new QActionGroup(channels);
    g->setExclusive(true);
    auto addRadio = [&](const QString &label, bool on = false) {
        auto *a = addItem(channels, label);
        a->setCheckable(true);
        a->setChecked(on);
        g->addAction(a);
        return a;
    };
    addRadio(QObject::tr("Both"), true);
    addRadio(QObject::tr("Left Only"));
    addRadio(QObject::tr("Right Only"));
    addRadio(QObject::tr("Combine"));
    addItem(channels, QObject::tr("Swap"));
}

void addSynchronizeSyncLinkMenus(QMenu *parent)
{
    {
        auto *sync = parent->addMenu(QObject::tr("Synchronize"));
        addItem(sync, QObject::tr("By Moving"));
        addItem(sync, QObject::tr("By Slipping"));
    }
    addItem(parent, QObject::tr("Create Sync Link with Selected Events"), {}, false);
    {
        auto *link = parent->addMenu(QObject::tr("Sync Link"));
        addItem(link, QObject::tr("Unlink"), {}, false);
        addItem(link, QObject::tr("Select All"), {}, false);
    }
}

void addVideoEventStreamSyncMenus(QMenu *parent)
{
    addStreamMenu(parent);
    addSynchronizeSyncLinkMenus(parent);
}

void addAudioEventStreamSyncMenus(QMenu *parent)
{
    addStreamMenu(parent);
    addChannelsMenu(parent);
    addSynchronizeSyncLinkMenus(parent);
}

void insertTrack(MainWindow *window, TrackKind kind, const QString &namePrefix, int height,
                 int atIndex = -1)
{
    window->runDocumentEdit(QObject::tr("Insert Track"), [window, kind, namePrefix, height, atIndex]() {
        Track t;
        t.id = window->projectModel().tracks().isEmpty()
                   ? 1
                   : (window->projectModel().tracks().last().id + 1);
        int maxId = 0;
        for (const Track &tr : window->projectModel().tracks()) {
            maxId = std::max(maxId, tr.id);
        }
        t.id = maxId + 1;
        t.name = QObject::tr("%1 %2").arg(namePrefix).arg(window->projectModel().tracks().size() + 1);
        t.kind = kind;
        t.height = height;
        auto &tracks = window->projectModel().tracks();
        if (atIndex < 0 || atIndex > tracks.size()) {
            tracks.push_back(t);
        } else {
            tracks.insert(atIndex, t);
        }
    });
    window->refreshTimeline();
}

QIcon colorSwatchIcon(const QColor &c, bool selected)
{
    QPixmap pm(16, 16);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.fillRect(1, 1, 14, 14, c);
    p.setPen(QColor(0x88, 0x88, 0x88));
    p.drawRect(1, 1, 13, 13);
    if (selected) {
        p.setPen(QPen(Qt::white, 2));
        p.drawPoint(4, 4);
        p.drawPoint(5, 4);
        p.drawPoint(4, 5);
    }
    p.end();
    return QIcon(pm);
}

void renameTrack(MainWindow *window, int trackIndex)
{
    if (!window) {
        return;
    }
    // Defer so the context menu fully closes before the inline editor takes focus.
    QTimer::singleShot(0, window, [window, trackIndex]() { window->beginTrackRename(trackIndex); });
}

void duplicateTrack(MainWindow *window, int trackIndex)
{
    if (trackIndex < 0 || trackIndex >= window->projectModel().tracks().size()) {
        return;
    }
    window->runDocumentEdit(QObject::tr("Duplicate Track"), [window, trackIndex]() {
        Track copy = window->projectModel().tracks()[trackIndex];
        int maxId = 0;
        for (const Track &tr : window->projectModel().tracks()) {
            maxId = std::max(maxId, tr.id);
        }
        copy.id = maxId + 1;
        copy.name = QObject::tr("%1 (copy)").arg(copy.name);
        int nextEv = 1;
        for (const Track &tr : window->projectModel().tracks()) {
            for (const TrackEvent &ev : tr.events) {
                nextEv = std::max(nextEv, ev.id + 1);
            }
        }
        for (TrackEvent &ev : copy.events) {
            ev.id = nextEv++;
            ev.selected = false;
        }
        window->projectModel().tracks().insert(trackIndex + 1, copy);
    });
    window->refreshTimeline();
}

void deleteTrack(MainWindow *window, int trackIndex)
{
    if (trackIndex < 0 || trackIndex >= window->projectModel().tracks().size()) {
        return;
    }
    const Track &tr = window->projectModel().tracks().at(trackIndex);
    if (!tr.events.isEmpty()) {
        const bool ok = ConfirmWarningDialog::confirm(
            window,
            QObject::tr(
                "A track that you are deleting holds at least one event. "
                "Are you sure you want to delete this track and all of its events?"),
            QStringLiteral("warnings/skipDeleteTrackWithEvents"));
        if (!ok) {
            return;
        }
    }
    window->runDocumentEdit(QObject::tr("Delete Track"), [window, trackIndex]() {
        window->projectModel().tracks().removeAt(trackIndex);
    });
    window->refreshTimeline();
}

void addAutomationWriteModes(QMenu *menu, MainWindow *window, int trackIndex,
                             bool includeShowControls = false)
{
    if (includeShowControls) {
        auto *show = addItem(menu, QObject::tr("Show Automation Controls"));
        show->setCheckable(true);
        show->setChecked(true);
        menu->addSeparator();
    }
    auto *g = new QActionGroup(menu);
    g->setExclusive(true);
    auto addAuto = [&](const QString &label, AutomationWriteMode mode, bool on = false) {
        auto *a = addItem(menu, label);
        a->setCheckable(true);
        a->setChecked(on);
        g->addAction(a);
        if (window && trackIndex >= 0) {
            QObject::connect(a, &QAction::triggered, window, [window, trackIndex, mode]() {
                window->runDocumentEdit(QObject::tr("Automation Mode"), [window, trackIndex, mode]() {
                    auto &tracks = window->projectModel().tracks();
                    if (trackIndex >= 0 && trackIndex < tracks.size()) {
                        tracks[trackIndex].automationMode = mode;
                    }
                });
            });
        }
    };
    addAuto(QObject::tr("Automation Off"), AutomationWriteMode::Off);
    addAuto(QObject::tr("Automation Read"), AutomationWriteMode::Read);
    addAuto(QObject::tr("Automation Write (Touch)"), AutomationWriteMode::Touch, true);
    addAuto(QObject::tr("Automation Write (Latch)"), AutomationWriteMode::Latch);
}

void ensureAutomationLane(Track &track, const QString &targetId, double defaultValue)
{
    for (const AutomationLane &lane : track.automationLanes) {
        if (lane.targetId == targetId) {
            return;
        }
    }
    AutomationLane lane;
    lane.targetId = targetId;
    lane.points.push_back({0.0, defaultValue, AutomationPointType::Linear});
    track.automationLanes.push_back(lane);
}

void ensureEventAutomationLane(TrackEvent &ev, const QString &targetId, double defaultValue)
{
    for (const AutomationLane &lane : ev.automationLanes) {
        if (lane.targetId == targetId) {
            return;
        }
    }
    AutomationLane lane;
    lane.targetId = targetId;
    lane.points.push_back({0.0, defaultValue, AutomationPointType::Linear});
    lane.points.push_back({std::max(0.01, ev.lengthSec), defaultValue, AutomationPointType::Linear});
    ev.automationLanes.push_back(lane);
}

void addTrackGroupSubmenu(QMenu *parent)
{
    auto *grp = parent->addMenu(QObject::tr("Track Group"));
    addItem(grp, QObject::tr("Rename Track Group"), {}, false);
    grp->addSeparator();
    addItem(grp, QObject::tr("Group Selected Tracks"), {}, false);
    addItem(grp, QObject::tr("Ungroup Selected Tracks"), {}, false);
    addItem(grp, QObject::tr("Collapse"), {}, false);
    addItem(grp, QObject::tr("Expand"), {}, false);
    grp->addSeparator();
    addItem(grp, QObject::tr("Mute Track Group"), QKeySequence(QStringLiteral("Ctrl+Alt+Z")), false);
    addItem(grp, QObject::tr("Solo Track Group"), QKeySequence(QStringLiteral("Ctrl+Alt+X")), false);
}

void addTrackDisplayColorSubmenu(QMenu *parent, int selectedIndex = 0)
{
    auto *colors = parent->addMenu(QObject::tr("Track Display Color"));
    static const QColor kSwatches[] = {
        QColor(0x6b, 0x4a, 0x8a), QColor(0x9a, 0x6a, 0x7a), QColor(0xc9, 0x8a, 0x8a),
        QColor(0xd0, 0x7a, 0x40), QColor(0xc8, 0xa0, 0x30), QColor(0xd8, 0xd0, 0x70),
        QColor(0x7a, 0xc0, 0x60), QColor(0x60, 0xc0, 0xa0), QColor(0x50, 0xa0, 0xd0),
        QColor(0x40, 0x70, 0xc0),
    };
    auto *g = new QActionGroup(colors);
    g->setExclusive(true);
    for (int i = 0; i < int(sizeof(kSwatches) / sizeof(kSwatches[0])); ++i) {
        auto *a = colors->addAction(QString());
        a->setIcon(colorSwatchIcon(kSwatches[i], i == selectedIndex));
        a->setCheckable(true);
        a->setChecked(i == selectedIndex);
        a->setData(kSwatches[i]);
        g->addAction(a);
    }
}

void addSoundMapperInputMenu(QMenu *parent, const QString &title)
{
    auto *dev = parent->addMenu(title);
    auto *g = new QActionGroup(dev);
    g->setExclusive(true);
    auto addRadio = [&](const QString &label, bool on = false) {
        auto *a = addItem(dev, label);
        a->setCheckable(true);
        a->setChecked(on);
        g->addAction(a);
    };
    addRadio(QObject::tr("Microsoft Sound Mapper (Default)"), true);
    dev->addSeparator();
    addRadio(QObject::tr("Microsoft Sound Mapper"));
    addRadio(QObject::tr("Microsoft Sound Mapper - Left"));
    addRadio(QObject::tr("Microsoft Sound Mapper - Right"));
}

void buildVideoTrackMoreMenu(QMenu *more, MainWindow *window, int trackIndex)
{
    addItem(more, QObject::tr("Bypass Motion Blur"));
    more->addAction(QObject::tr("Track Motion"), window, [window, trackIndex]() {
        window->onTrackMotion(trackIndex);
    });
    more->addAction(QObject::tr("Track FX"), window, [window, trackIndex]() {
        window->onTrackFx(trackIndex);
    });
    addItem(more, QObject::tr("Activate Automation Controls"));
    {
        auto *autoM = more->addMenu(QObject::tr("Automation Settings"));
        addAutomationWriteModes(autoM, window, trackIndex);
    }
    {
        auto *comp = more->addMenu(QObject::tr("Compositing Mode"));
        auto *g = new QActionGroup(comp);
        g->setExclusive(true);
        auto addMode = [&](const QString &label, bool on = false) {
            auto *a = addItem(comp, label);
            a->setCheckable(true);
            a->setChecked(on);
            g->addAction(a);
        };
        addMode(QObject::tr("3D Source Alpha"));
        comp->addSeparator();
        addItem(comp, QObject::tr("Custom…"));
        addMode(QObject::tr("Add"));
        addMode(QObject::tr("Subtract"));
        addMode(QObject::tr("Multiply (Mask)"));
        addMode(QObject::tr("Source Alpha"), true);
        addMode(QObject::tr("Cut"));
        addMode(QObject::tr("Screen"));
        addMode(QObject::tr("Overlay"));
        addMode(QObject::tr("Hard Light"));
        addMode(QObject::tr("Dodge"));
        addMode(QObject::tr("Burn"));
        addMode(QObject::tr("Darken"));
        addMode(QObject::tr("Lighten"));
        addMode(QObject::tr("Difference"));
        addMode(QObject::tr("Difference Squared"));
    }
    addItem(more, QObject::tr("Make Compositing Parent"), {}, false);
    addItem(more, QObject::tr("Make Compositing Child"), {}, false);
    addItem(more, QObject::tr("Minimize"));
    addItem(more, QObject::tr("Maximize"));
    more->addAction(QObject::tr("Color Grading"), window, [window, trackIndex]() {
        window->onColorGrading(trackIndex);
    });
    more->addSeparator();
    addItem(more, QObject::tr("Edit Visible Button Set…"));
}

void buildVideoTrackHeaderMenu(QMenu *menu, MainWindow *window, int trackIndex)
{
    auto *more = menu->addMenu(QObject::tr("More"));
    buildVideoTrackMoreMenu(more, window, trackIndex);
    menu->addSeparator();

    menu->addAction(QObject::tr("Rename"), QKeySequence(Qt::Key_F2), window,
                    [window, trackIndex]() { renameTrack(window, trackIndex); });
    menu->addSeparator();

    menu->addAction(QObject::tr("Insert Video Track"), QKeySequence(QStringLiteral("Ctrl+Shift+Q")),
                    window, [window, trackIndex]() {
                        insertTrack(window, TrackKind::Video, QObject::tr("Video"), 96, trackIndex);
                    });
    menu->addAction(QObject::tr("Insert Adjustment Track"),
                    QKeySequence(QStringLiteral("Ctrl+Alt+Shift+Q")), window,
                    [window, trackIndex]() {
                        insertTrack(window, TrackKind::Video, QObject::tr("Adjustment"), 48,
                                    trackIndex);
                    });
    menu->addAction(QObject::tr("Duplicate Track"), window,
                    [window, trackIndex]() { duplicateTrack(window, trackIndex); });
    menu->addAction(QObject::tr("Delete Track"), window,
                    [window, trackIndex]() { deleteTrack(window, trackIndex); });
    addItem(menu, QObject::tr("Pair as Stereoscopic 3D Subclips"), {}, false);
    menu->addSeparator();

    {
        auto *env = menu->addMenu(QObject::tr("Insert/Remove Envelope"));
        addItem(env, QObject::tr("Mute"));
        addItem(env, QObject::tr("Composite Level"));
        addItem(env, QObject::tr("Fade to Color"));
    }
    menu->addSeparator();

    {
        auto *sw = menu->addMenu(QObject::tr("Switches"));
        addItem(sw, QObject::tr("Mute"), QKeySequence(QStringLiteral("Z")))->setCheckable(true);
        addItem(sw, QObject::tr("Solo"), QKeySequence(QStringLiteral("X")))->setCheckable(true);
    }
    {
        auto *fade = menu->addMenu(QObject::tr("Fade Colors"));
        addItem(fade, QObject::tr("Top…"));
        addItem(fade, QObject::tr("Bottom…"));
    }
    {
        auto *expand = addItem(menu, QObject::tr("Expand Track Layers"));
        expand->setCheckable(true);
        expand->setChecked(true);
    }
    menu->addSeparator();
    addItem(menu, QObject::tr("Set Default Track Properties…"));
    menu->addSeparator();
    addTrackGroupSubmenu(menu);
    addTrackDisplayColorSubmenu(menu);
}

void buildAudioTrackMoreMenu(QMenu *more, MainWindow *window, int trackIndex)
{
    addItem(more, QObject::tr("Arm For Record"));
    addItem(more, QObject::tr("Invert Track Phase"));
    addItem(more, QObject::tr("Activate Automation Controls"));
    {
        auto *autoM = more->addMenu(QObject::tr("Automation Settings"));
        addAutomationWriteModes(autoM, window, trackIndex);
    }
    more->addAction(QObject::tr("Track FX"), window, [window, trackIndex]() {
        window->onTrackFx(trackIndex);
    });
    {
        auto *bus = more->addMenu(QObject::tr("Output Bus"));
        auto *master = addItem(bus, QObject::tr("Master (Microsoft Sound Mapper)"));
        master->setCheckable(true);
        master->setChecked(true);
    }
    addSoundMapperInputMenu(more, QObject::tr("Record Input"));
    addItem(more, QObject::tr("Minimize"));
    addItem(more, QObject::tr("Maximize"));
    addItem(more, QObject::tr("Auto Ducking Controller"));
    more->addSeparator();
    addItem(more, QObject::tr("Edit Visible Button Set…"));
}

void buildAudioTrackHeaderMenu(QMenu *menu, MainWindow *window, int trackIndex)
{
    auto *more = menu->addMenu(QObject::tr("More"));
    buildAudioTrackMoreMenu(more, window, trackIndex);
    menu->addSeparator();

    menu->addAction(QObject::tr("Rename"), QKeySequence(Qt::Key_F2), window,
                    [window, trackIndex]() { renameTrack(window, trackIndex); });
    menu->addSeparator();

    menu->addAction(QObject::tr("Insert Audio Track"), QKeySequence(QStringLiteral("Ctrl+Q")), window,
                    [window, trackIndex]() {
                        insertTrack(window, TrackKind::Audio, QObject::tr("Audio"), 100, trackIndex);
                    });
    menu->addAction(QObject::tr("Duplicate Track"), window,
                    [window, trackIndex]() { duplicateTrack(window, trackIndex); });
    menu->addAction(QObject::tr("Delete Track"), window,
                    [window, trackIndex]() { deleteTrack(window, trackIndex); });
    menu->addSeparator();

    addItem(menu, QObject::tr("Arm for Record"));
    addSoundMapperInputMenu(menu, QObject::tr("Input"));
    {
        auto *out = menu->addMenu(QObject::tr("Output"));
        auto *master = addItem(out, QObject::tr("Master (Microsoft Sound Mapper)"));
        master->setCheckable(true);
        master->setChecked(true);
    }
    {
        auto *autoM = menu->addMenu(QObject::tr("Automation"));
        addAutomationWriteModes(autoM, window, trackIndex, true);
    }
    {
        auto *pan = menu->addMenu(QObject::tr("Pan Type"));
        auto *g = new QActionGroup(pan);
        g->setExclusive(true);
        auto addPan = [&](const QString &label, bool on = false) {
            auto *a = addItem(pan, label);
            a->setCheckable(true);
            a->setChecked(on);
            g->addAction(a);
        };
        addPan(QObject::tr("Add Channels (0 dB Center)"), true);
        addPan(QObject::tr("Balance (0 dB Center)"));
        addPan(QObject::tr("Balance (-3 dB Center)"));
        addPan(QObject::tr("Balance (-6 dB Center)"));
        addPan(QObject::tr("Constant Power"));
        addPan(QObject::tr("Film"));
    }
    menu->addSeparator();

    {
        auto *env = menu->addMenu(QObject::tr("Insert/Remove Envelope"));
        addItem(env, QObject::tr("Mute"));
        auto *vol = addItem(env, QObject::tr("Volume"), QKeySequence(QStringLiteral("Shift+V")));
        auto *panEnv = addItem(env, QObject::tr("Pan"), QKeySequence(QStringLiteral("Shift+P")));
        QObject::connect(vol, &QAction::triggered, window, [window, trackIndex]() {
            window->runDocumentEdit(QObject::tr("Insert Volume Envelope"), [window, trackIndex]() {
                auto &tracks = window->projectModel().tracks();
                if (trackIndex >= 0 && trackIndex < tracks.size()) {
                    ensureAutomationLane(tracks[trackIndex], QStringLiteral("track.volume"),
                                         tracks[trackIndex].volumeDb);
                }
            });
            window->refreshTimeline();
        });
        QObject::connect(panEnv, &QAction::triggered, window, [window, trackIndex]() {
            window->runDocumentEdit(QObject::tr("Insert Pan Envelope"), [window, trackIndex]() {
                auto &tracks = window->projectModel().tracks();
                if (trackIndex >= 0 && trackIndex < tracks.size()) {
                    ensureAutomationLane(tracks[trackIndex], QStringLiteral("track.pan"),
                                         double(tracks[trackIndex].pan));
                }
            });
            window->refreshTimeline();
        });
        env->addSeparator();
        auto *fxAuto = addItem(env, QObject::tr("FX Automation…"));
        QObject::connect(fxAuto, &QAction::triggered, window, [window, trackIndex]() {
            window->runDocumentEdit(QObject::tr("Insert FX Automation"), [window, trackIndex]() {
                auto &tracks = window->projectModel().tracks();
                if (trackIndex < 0 || trackIndex >= tracks.size()) {
                    return;
                }
                Track &tr = tracks[trackIndex];
                for (int i = 0; i < tr.fxChain.size(); ++i) {
                    ensureAutomationLane(tr, QStringLiteral("fx:%1:gainDb").arg(i), 0.0);
                }
            });
            window->refreshTimeline();
        });
    }
    {
        auto *fxEnv = menu->addMenu(QObject::tr("FX Automation Envelopes"));
        auto *fxAuto = addItem(fxEnv, QObject::tr("FX Automation…"));
        QObject::connect(fxAuto, &QAction::triggered, window, [window, trackIndex]() {
            window->onTrackFx(trackIndex);
        });
    }
    menu->addSeparator();

    {
        auto *sw = menu->addMenu(QObject::tr("Switches"));
        addItem(sw, QObject::tr("Mute"), QKeySequence(QStringLiteral("Z")))->setCheckable(true);
        addItem(sw, QObject::tr("Solo"), QKeySequence(QStringLiteral("X")))->setCheckable(true);
        addItem(sw, QObject::tr("Invert Phase"))->setCheckable(true);
    }
    menu->addSeparator();
    addItem(menu, QObject::tr("Set Default Track Properties…"));
    menu->addSeparator();
    addTrackGroupSubmenu(menu);
    addTrackDisplayColorSubmenu(menu, 1);
    menu->addSeparator();

    {
        auto *vert = addItem(menu, QObject::tr("Use Vertical Meters"));
        vert->setCheckable(true);
        vert->setChecked(true);
        auto *outM = addItem(menu, QObject::tr("Show Output Meters"));
        outM->setCheckable(true);
        outM->setChecked(true);
    }
}

} // namespace

void ContextMenuBuilder::showEventMenu(MainWindow *window, int eventId, const QPoint &globalPos)
{
    TrackEvent *ev = window->projectModel().findEvent(eventId);
    if (!ev) {
        return;
    }
    int trackIndex = -1;
    window->projectModel().findEvent(eventId, &trackIndex);
    const bool isVideo = isVideoFamily(ev->mediaKind)
                         || (trackIndex >= 0
                             && window->projectModel().tracks()[trackIndex].kind == TrackKind::Video);
    const double playhead = window->projectModel().playheadSec();
    const bool playheadInEvent =
        playhead > ev->startSec + 0.02 && playhead < ev->startSec + ev->lengthSec - 0.02;

    QMenu menu(window);

    if (isVideo) {
        menu.addAction(QObject::tr("Open in Trimmer"), window,
                       [window, eventId]() { window->openTrimmer(eventId); });
        addItem(&menu, QObject::tr("Open Parent Media in Trimmer"), {}, false);
        addItem(&menu, QObject::tr("Select in Project Media list"));
        addItem(&menu, QObject::tr("Edit Source Project"), {}, false);
        menu.addSeparator();
        addItem(&menu, QObject::tr("Create Nested Timeline"), QKeySequence(QStringLiteral("Alt+C")));
        addItem(&menu, QObject::tr("Open Nested Timeline"), QKeySequence(QStringLiteral("Alt+N")),
                false);
        menu.addSeparator();
        addItem(&menu, QObject::tr("Media FX…"));
        menu.addSeparator();
        addItem(&menu, QObject::tr("Video Event Pan/Crop…"));
        addItem(&menu, QObject::tr("Video Event FX…"));
        addItem(&menu, QObject::tr("Motion Tracking"), QKeySequence(QStringLiteral("Alt+M")));
        menu.addSeparator();
        menu.addAction(QObject::tr("Cut"), QKeySequence::Cut, window, &MainWindow::onEditCut);
        menu.addAction(QObject::tr("Copy"), QKeySequence::Copy, window, &MainWindow::onEditCopy);
        addItem(&menu, QObject::tr("Paste Event Attributes"), {}, false);
        addItem(&menu, QObject::tr("Selectively Paste Event Attributes"), {}, false);
        menu.addSeparator();
        menu.addAction(QObject::tr("Delete"), QKeySequence::Delete, window, &MainWindow::onEditDelete);
        addItem(&menu, QObject::tr("Trim"), QKeySequence(QStringLiteral("Ctrl+T")), false);
        {
            auto *a = menu.addAction(QObject::tr("Trim Start"), QKeySequence(QStringLiteral("Alt+[")),
                                     window, &MainWindow::onEditTrimStart);
            a->setEnabled(playheadInEvent);
        }
        {
            auto *a = menu.addAction(QObject::tr("Trim End"), QKeySequence(QStringLiteral("Alt+]")),
                                     window, &MainWindow::onEditTrimEnd);
            a->setEnabled(playheadInEvent);
        }
        addItem(&menu, QObject::tr("Detect Scenes and Split"));
        {
            auto *a = menu.addAction(QObject::tr("Split"), QKeySequence(QStringLiteral("S")), window,
                                     &MainWindow::onEditSplit);
            a->setEnabled(playheadInEvent);
        }
        addItem(&menu, QObject::tr("Smart Split"), QKeySequence(QStringLiteral("Alt+S")), false);
        addItem(&menu, QObject::tr("Event Heal"), {}, false);
        addItem(&menu, QObject::tr("Close Gaps"), {}, false);
        menu.addSeparator();
        addItem(&menu, QObject::tr("Resize Adjustment Event to Project"), {}, false);
        menu.addSeparator();
        addItem(&menu, QObject::tr("Add Missing Stream for Selected Event"), {}, false);
        addItem(&menu, QObject::tr("Create Subclip"));
        addItem(&menu, QObject::tr("Reverse"));
        addItem(&menu, QObject::tr("Pair as Stereoscopic 3D Subclip"), {}, false);
        menu.addSeparator();
        addItem(&menu, QObject::tr("Select Events to End"));
        addItem(&menu, QObject::tr("Select All After Cursor"));
        addVideoEventEnvelopeMenu(&menu);
        menu.addSeparator();
        addSwitchesVideo(&menu);
        addTakeMenu(&menu, ev->name);
        addGroupMenu(&menu, window, eventId);
        addVideoEventStreamSyncMenus(&menu);
        menu.addSeparator();
        menu.addAction(QObject::tr("Properties…"), window,
                       [window, eventId]() { window->openEventProperties(eventId); });
        menu.exec(globalPos);
        return;
    }

    // Audio event — Vegas Pro structure
    menu.addAction(QObject::tr("Open in Trimmer"), window,
                   [window, eventId]() { window->openTrimmer(eventId); });
    addItem(&menu, QObject::tr("Open Parent Media in Trimmer"), {}, false);
    addItem(&menu, QObject::tr("Open in Audio Editor"));
    addItem(&menu, QObject::tr("Open Copy in Audio Editor"));
    addItem(&menu, QObject::tr("Edit Source Project"), {}, false);
    addItem(&menu, QObject::tr("Create Nested Timeline"), QKeySequence(QStringLiteral("Alt+C")));
    addItem(&menu, QObject::tr("Open Nested Timeline"), QKeySequence(QStringLiteral("Alt+N")), false);
    addItem(&menu, QObject::tr("Select in Project Media list"));
    menu.addSeparator();
    addItem(&menu, QObject::tr("Beat Detection"));
    addItem(&menu, QObject::tr("Tempo Detection"));
    menu.addSeparator();
    addItem(&menu, QObject::tr("Audio Event FX…"));
    addItem(&menu, QObject::tr("Apply Non-Real-Time Event FX…"));
    menu.addSeparator();
    menu.addAction(QObject::tr("Cut"), QKeySequence::Cut, window, &MainWindow::onEditCut);
    menu.addAction(QObject::tr("Copy"), QKeySequence::Copy, window, &MainWindow::onEditCopy);
    addItem(&menu, QObject::tr("Paste Event Attributes"), {}, false);
    addItem(&menu, QObject::tr("Selectively Paste Event Attributes"), {}, false);
    menu.addSeparator();
    menu.addAction(QObject::tr("Delete"), QKeySequence::Delete, window, &MainWindow::onEditDelete);
    addItem(&menu, QObject::tr("Trim"), QKeySequence(QStringLiteral("Ctrl+T")), false);
    {
        auto *a = menu.addAction(QObject::tr("Trim Start"), QKeySequence(QStringLiteral("Alt+[")),
                                 window, &MainWindow::onEditTrimStart);
        a->setEnabled(playheadInEvent);
    }
    {
        auto *a = menu.addAction(QObject::tr("Trim End"), QKeySequence(QStringLiteral("Alt+]")),
                                 window, &MainWindow::onEditTrimEnd);
        a->setEnabled(playheadInEvent);
    }
    {
        auto *a = menu.addAction(QObject::tr("Split"), QKeySequence(QStringLiteral("S")), window,
                                 &MainWindow::onEditSplit);
        a->setEnabled(playheadInEvent);
    }
    addItem(&menu, QObject::tr("Event Heal"), {}, false);
    addItem(&menu, QObject::tr("Close Gaps"), {}, false);
    menu.addSeparator();
    addItem(&menu, QObject::tr("Add Missing Stream for Selected Event"), {}, false);
    addItem(&menu, QObject::tr("Create Subclip"));
    addItem(&menu, QObject::tr("Reverse"));
    menu.addSeparator();
    addItem(&menu, QObject::tr("Select Events to End"));
    addItem(&menu, QObject::tr("Select All After Cursor"));
    menu.addSeparator();
    addSwitchesAudio(&menu);
    addTakeMenu(&menu, ev->name);
    addGroupMenu(&menu, window, eventId);
    addAudioEventStreamSyncMenus(&menu);
    menu.addSeparator();
    {
        auto *env = menu.addMenu(QObject::tr("Insert/Remove Envelope"));
        auto *vol = addItem(env, QObject::tr("Volume"), QKeySequence(QStringLiteral("Shift+V")));
        QObject::connect(vol, &QAction::triggered, window, [window, eventId]() {
            window->runDocumentEdit(QObject::tr("Insert Event Gain Envelope"), [window, eventId]() {
                if (TrackEvent *e = window->projectModel().findEvent(eventId)) {
                    ensureEventAutomationLane(*e, QStringLiteral("event.gain"), e->gainDb);
                }
            });
            window->refreshTimeline();
        });
        addItem(env, QObject::tr("FX Automation…"));
    }
    menu.addSeparator();
    menu.addAction(QObject::tr("Properties…"), window,
                   [window, eventId]() { window->openEventProperties(eventId); });

    menu.exec(globalPos);
}

void ContextMenuBuilder::showEventMoreMenu(MainWindow *window, int eventId, const QPoint &globalPos)
{
    TrackEvent *ev = window->projectModel().findEvent(eventId);
    if (!ev) {
        return;
    }
    int trackIndex = -1;
    window->projectModel().findEvent(eventId, &trackIndex);
    const bool isVideo = isVideoFamily(ev->mediaKind)
                         || (trackIndex >= 0
                             && window->projectModel().tracks()[trackIndex].kind == TrackKind::Video);

    QMenu menu(window);
    auto addCheck = [&](const QString &label, bool on = false) {
        auto *a = addItem(&menu, label);
        a->setCheckable(true);
        a->setChecked(on);
        return a;
    };

    if (isVideo) {
        addCheck(QObject::tr("Active Take Information"), true);
        addItem(&menu, QObject::tr("Playback Rate"));
        addItem(&menu, QObject::tr("Freeze Frame at Cursor"), {}, false);
        addItem(&menu, QObject::tr("Selectively Paste Event Attributes"), {}, false);
        addCheck(QObject::tr("Event Headers"), true);
        addCheck(QObject::tr("Event Length"));
        addItem(&menu, QObject::tr("Color Grading"));
        addItem(&menu, QObject::tr("Media FX"));
        addCheck(QObject::tr("Event Handles"), true);
        addItem(&menu, QObject::tr("Motion Tracking"));
        addItem(&menu, QObject::tr("Detect Scenes and Split"));
        menu.addSeparator();
        addItem(&menu, QObject::tr("Edit Visible Button Set…"));
    } else {
        addCheck(QObject::tr("Active Take Information"), true);
        addCheck(QObject::tr("Event Headers"), true);
        addCheck(QObject::tr("Event Length"));
        addCheck(QObject::tr("Event Handles"), true);
        addItem(&menu, QObject::tr("Normalize"));
        addItem(&menu, QObject::tr("Auto Normalize"));
        menu.addSeparator();
        addItem(&menu, QObject::tr("Edit Visible Button Set…"));
    }
    menu.exec(globalPos);
}

void ContextMenuBuilder::showEventFxMenu(MainWindow *window, int eventId, const QPoint &globalPos)
{
    TrackEvent *ev = window->projectModel().findEvent(eventId);
    if (!ev || !window) {
        return;
    }
    const bool isAudio = isAudioFamily(ev->mediaKind);
    const bool hasFx = !ev->fxChain.isEmpty();
    // Video keeps structural Pan/Crop — treat chain as "empty" for bulk ops if only that remains
    auto removableCount = [&]() {
        int n = 0;
        for (const FxSlot &s : ev->fxChain) {
            if (!isAudio && s.displayName.compare(QStringLiteral("Pan/Crop"), Qt::CaseInsensitive) == 0) {
                continue;
            }
            ++n;
        }
        return n;
    };
    const bool hasRemovable = removableCount() > 0;

    QMenu menu(window);
    auto *bypass = menu.addAction(QObject::tr("&Bypass All"));
    bypass->setEnabled(hasFx);
    QObject::connect(bypass, &QAction::triggered, window, [window, eventId]() {
        window->runDocumentEdit(QObject::tr("Bypass All FX"), [window, eventId]() {
            if (TrackEvent *e = window->projectModel().findEvent(eventId)) {
                for (FxSlot &s : e->fxChain) {
                    s.bypass = true;
                }
            }
        });
        window->refreshTimeline();
    });

    auto *enable = menu.addAction(QObject::tr("&Enable All"));
    enable->setEnabled(hasFx);
    QObject::connect(enable, &QAction::triggered, window, [window, eventId]() {
        window->runDocumentEdit(QObject::tr("Enable All FX"), [window, eventId]() {
            if (TrackEvent *e = window->projectModel().findEvent(eventId)) {
                for (FxSlot &s : e->fxChain) {
                    s.bypass = false;
                }
            }
        });
        window->refreshTimeline();
    });

    auto *delAll = menu.addAction(QObject::tr("&Delete All"));
    delAll->setEnabled(hasRemovable);
    QObject::connect(delAll, &QAction::triggered, window, [window, eventId, isAudio]() {
        window->runDocumentEdit(QObject::tr("Delete All FX"), [window, eventId, isAudio]() {
            if (TrackEvent *e = window->projectModel().findEvent(eventId)) {
                if (isAudio) {
                    e->fxChain.clear();
                } else {
                    QVector<FxSlot> kept;
                    for (const FxSlot &s : e->fxChain) {
                        if (s.displayName.compare(QStringLiteral("Pan/Crop"), Qt::CaseInsensitive)
                            == 0) {
                            kept.push_back(s);
                        }
                    }
                    e->fxChain = kept;
                    if (e->fxChain.isEmpty()) {
                        ensureFxFirst(e->fxChain, QStringLiteral("Pan/Crop"), PluginFormat::Builtin);
                    }
                }
            }
        });
        window->refreshTimeline();
    });

    menu.addSeparator();

    auto *chooser = menu.addAction(QObject::tr("&Plug-In Chooser…"));
    QObject::connect(chooser, &QAction::triggered, window, [window, eventId, isAudio]() {
        TrackEvent *e = window->projectModel().findEvent(eventId);
        if (!e) {
            return;
        }
        window->beginDocumentEdit();
        PluginChooserDialog dlg(nullptr, window);
        dlg.setAudioMode(isAudio);
        if (dlg.exec() != QDialog::Accepted) {
            window->discardDocumentEdit();
            return;
        }
        bool added = false;
        if (isAudio) {
            for (const AudioPluginDesc &d : dlg.selectedAudioPlugins()) {
                FxSlot slot;
                CompositePluginHost::instance().createInstance(d, &slot);
                e->fxChain.push_back(slot);
                added = true;
            }
        } else {
            const QString name = dlg.selectedPluginName();
            if (!name.isEmpty()) {
                if (name.compare(QStringLiteral("Pan/Crop"), Qt::CaseInsensitive) == 0) {
                    ensureFxFirst(e->fxChain, QStringLiteral("Pan/Crop"), PluginFormat::Builtin);
                } else {
                    e->fxChain.push_back(makeFxSlot(name, PluginFormat::Ofx));
                }
                added = true;
            }
        }
        if (!added) {
            window->discardDocumentEdit();
            return;
        }
        window->commitDocumentEdit(QObject::tr("Add Event FX"));
        window->refreshTimeline();
        if (isAudio) {
            window->onAudioEventFx(eventId);
        }
    });

    menu.exec(globalPos);
}

void ContextMenuBuilder::showTrackHeaderMenu(MainWindow *window, int trackIndex,
                                             const QPoint &globalPos)
{
    if (!window || trackIndex < 0 || trackIndex >= window->projectModel().tracks().size()) {
        return;
    }
    const Track &track = window->projectModel().tracks()[trackIndex];
    const bool isVideo = track.kind == TrackKind::Video;

    QMenu menu(window);
    if (isVideo) {
        buildVideoTrackHeaderMenu(&menu, window, trackIndex);
    } else {
        buildAudioTrackHeaderMenu(&menu, window, trackIndex);
    }
    menu.exec(globalPos);
}

void ContextMenuBuilder::showTrackMoreMenu(MainWindow *window, int trackIndex,
                                           const QPoint &globalPos)
{
    if (!window || trackIndex < 0 || trackIndex >= window->projectModel().tracks().size()) {
        return;
    }
    const TrackKind kind = window->projectModel().tracks()[trackIndex].kind;
    QMenu menu(window);
    if (kind == TrackKind::Audio) {
        buildAudioTrackMoreMenu(&menu, window, trackIndex);
    } else {
        buildVideoTrackMoreMenu(&menu, window, trackIndex);
    }
    menu.exec(globalPos);
}

void ContextMenuBuilder::showTrackEmptyMenu(MainWindow *window, int trackIndex,
                                            const QPoint &globalPos)
{
    if (!window || trackIndex < 0 || trackIndex >= window->projectModel().tracks().size()) {
        showTimelineEmptyMenu(window, globalPos);
        return;
    }
    const Track &track = window->projectModel().tracks()[trackIndex];
    const bool isVideo = track.kind == TrackKind::Video;

    QMenu menu(window);
    addItem(&menu, QObject::tr("Insert Empty Event"));
    if (isVideo) {
        addItem(&menu, QObject::tr("Insert Text Media…"));
    }
    menu.addSeparator();
    menu.addAction(QObject::tr("Paste"), QKeySequence::Paste, window, &MainWindow::onEditPaste);
    menu.addSeparator();
    addItem(&menu, QObject::tr("Select All on Track"));
    if (isVideo) {
        addItem(&menu, QObject::tr("Track FX…"));
        addItem(&menu, QObject::tr("Insert/Remove Envelope"));
    } else {
        addItem(&menu, QObject::tr("Insert/Remove Envelope"));
        menu.addAction(QObject::tr("Track FX…"), window, [window, trackIndex]() {
            window->onTrackFx(trackIndex);
        });
        addItem(&menu, QObject::tr("Record Input…"));
    }
    menu.exec(globalPos);
}

void ContextMenuBuilder::showTimelineEmptyMenu(MainWindow *window, const QPoint &globalPos)
{
    QMenu menu(window);
    menu.addAction(QObject::tr("Open…"), QKeySequence(QStringLiteral("Ctrl+O")), window,
                   &MainWindow::onOpenProject);
    menu.addSeparator();
    menu.addAction(QObject::tr("Insert Audio Track"), QKeySequence(QStringLiteral("Ctrl+Q")), window,
                   [window]() { insertTrack(window, TrackKind::Audio, QObject::tr("Audio"), 100); });
    menu.addAction(QObject::tr("Insert Video Track"), QKeySequence(QStringLiteral("Ctrl+Shift+Q")),
                   window, [window]() { insertTrack(window, TrackKind::Video, QObject::tr("Video"), 96); });
    menu.addAction(QObject::tr("Insert Adjustment Track"),
                   QKeySequence(QStringLiteral("Ctrl+Alt+Shift+Q")), window, [window]() {
                       insertTrack(window, TrackKind::Video, QObject::tr("Adjustment"), 48);
                   });
    menu.addSeparator();
    menu.addAction(QObject::tr("Preferences…"), window, &MainWindow::onPreferences);
    menu.exec(globalPos);
}

void ContextMenuBuilder::showRulerMenu(MainWindow *window, const QPoint &globalPos)
{
    QMenu menu(window);
    auto *group = new QActionGroup(&menu);
    group->setExclusive(true);
    const auto cur =
        window ? window->projectModel().rulerTimeFormat() : RulerTimeFormat::TimeFrames;

    auto addRadio = [&](const QString &label, RulerTimeFormat fmt, bool enabled = true) {
        auto *a = menu.addAction(label);
        a->setCheckable(true);
        a->setChecked(cur == fmt);
        a->setEnabled(enabled);
        a->setActionGroup(group);
        a->setData(static_cast<int>(fmt));
        if (!enabled) {
            return a;
        }
        QObject::connect(a, &QAction::triggered, &menu, [window, fmt]() {
            if (!window) {
                return;
            }
            window->projectModel().setRulerTimeFormat(fmt);
            QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
            s.setValue(QStringLiteral("timeline/rulerTimeFormat"), static_cast<int>(fmt));
            window->refreshTimeline();
            window->refreshTimecodeLabels();
        });
        return a;
    };

    // Labels / mnemonics match Vegas Pro ruler context menu.
    addRadio(QObject::tr("&Samples"), RulerTimeFormat::Samples);
    addRadio(QObject::tr("&Time"), RulerTimeFormat::Time);
    addRadio(QObject::tr("Secon&ds"), RulerTimeFormat::Seconds);
    addRadio(QObject::tr("&Time && Frames"), RulerTimeFormat::TimeFrames);
    addRadio(QObject::tr("Absolu&te Frames"), RulerTimeFormat::AbsoluteFrames);
    addRadio(QObject::tr("Measures && &Beats"), RulerTimeFormat::MeasuresBeats);
    addRadio(QObject::tr("Feet and Frames &16mm (40 fpf)"), RulerTimeFormat::Feet16mm);
    addRadio(QObject::tr("Feet and Frames &35mm (16 fpf)"), RulerTimeFormat::Feet35mm);
    addRadio(QObject::tr("SMPTE Fi&lm Sync IVTC (23.976 fps, Video)"),
             RulerTimeFormat::SMPTE_IVTC);
    addRadio(QObject::tr("SMPTE &Film Sync (24 fps, Film)"), RulerTimeFormat::SMPTE_Film);
    addRadio(QObject::tr("SMPTE &EBU (25 fps, Video)"), RulerTimeFormat::SMPTE_EBU);
    addRadio(QObject::tr("SMPTE &Non-Drop (29.97 fps, Video)"), RulerTimeFormat::SMPTE_NonDrop);
    addRadio(QObject::tr("SMPTE &Drop (29.97 fps, Video)"), RulerTimeFormat::SMPTE_Drop);
    addRadio(QObject::tr("SM&PTE 30 (30 fps, Audio)"), RulerTimeFormat::SMPTE_30);
    addRadio(QObject::tr("Audio CD Time"), RulerTimeFormat::AudioCDTime, false);
    menu.addSeparator();
    addItem(&menu, QObject::tr("Set Time &at Cursor..."));
    addItem(&menu, QObject::tr("Set Project Tempo..."));
    menu.exec(globalPos);
}

void ContextMenuBuilder::showMarkerLaneMenu(MainWindow *window, const QPoint &globalPos)
{
    if (!window) {
        return;
    }
    auto &model = window->projectModel();
    TimelineView *tl = window->timelineView();
    QMenu menu(window);

    auto saveTimelineOpts = [window]() {
        QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
        auto &m = window->projectModel();
        s.setValue(QStringLiteral("timeline/snappingEnabled"), m.snappingEnabled());
        s.setValue(QStringLiteral("timeline/snapToGrid"), m.snapToGrid());
        s.setValue(QStringLiteral("timeline/snapToMarkers"), m.snapToMarkers());
        s.setValue(QStringLiteral("timeline/snapToAllEvents"), m.snapToAllEvents());
        s.setValue(QStringLiteral("timeline/quantizeToFrames"), m.quantizeToFrames());
        s.setValue(QStringLiteral("timeline/gridSpacing"), static_cast<int>(m.gridSpacing()));
    };

    auto *loop = menu.addAction(QObject::tr("&Loop Playback"));
    loop->setCheckable(true);
    loop->setChecked(model.loopPlaybackEnabled());
    loop->setShortcuts(
        {QKeySequence(QStringLiteral("Ctrl+Shift+L")), QKeySequence(QStringLiteral("Q"))});
    QObject::connect(loop, &QAction::triggered, &menu, [window](bool on) {
        window->setLoopPlaybackEnabled(on);
    });

    auto *setView = menu.addAction(QObject::tr("Set Selection to &View"));
    QObject::connect(setView, &QAction::triggered, &menu, [window, tl]() {
        if (!tl) {
            return;
        }
        double a = 0.0;
        double b = 0.0;
        tl->visibleTimeRange(&a, &b);
        window->runDocumentEdit(QObject::tr("Set Selection to View"), [window, a, b]() {
            window->projectModel().setLoopRegion(a, b);
        });
        window->refreshTimeline();
    });

    auto *setProj = menu.addAction(QObject::tr("Set Selection to &Project"));
    QObject::connect(setProj, &QAction::triggered, &menu, [window]() {
        const double end = window->projectModel().timelineEndSec();
        window->runDocumentEdit(QObject::tr("Set Selection to Project"), [window, end]() {
            window->projectModel().setLoopRegion(0.0, end);
        });
        window->refreshTimeline();
    });

    auto *selLoop = menu.addAction(QObject::tr("Select &Loop Region"));
    selLoop->setShortcuts(
        {QKeySequence(QStringLiteral("Shift+L")), QKeySequence(QStringLiteral("Shift+Q"))});
    selLoop->setEnabled(model.hasLoopRegion());
    QObject::connect(selLoop, &QAction::triggered, &menu, [window, tl]() {
        if (!window->projectModel().hasLoopRegion() || !tl) {
            return;
        }
        tl->seekPlayhead(window->projectModel().loopRegion().startSec, false);
    });

    menu.addSeparator();

    auto *markersMenu = menu.addMenu(QObject::tr("&Markers/Regions"));
    {
        auto *insMarker = markersMenu->addAction(QObject::tr("&Marker"));
        insMarker->setShortcut(QKeySequence(QStringLiteral("M")));
        QObject::connect(insMarker, &QAction::triggered, &menu, [tl]() {
            if (tl) {
                tl->insertMarkerAtPlayhead();
            }
        });
        addItem(markersMenu, QObject::tr("&Region"))->setEnabled(false);
        addItem(markersMenu, QObject::tr("&Command Marker"))->setEnabled(false);
        addItem(markersMenu, QObject::tr("CD &Track Region"))->setEnabled(false);
        addItem(markersMenu, QObject::tr("CD &Index Marker"))->setEnabled(false);
        markersMenu->addSeparator();
        auto *delAll = markersMenu->addAction(QObject::tr("&Delete All"));
        delAll->setEnabled(!model.markers().isEmpty());
        QObject::connect(delAll, &QAction::triggered, &menu, [window]() {
            window->runDocumentEdit(QObject::tr("Delete All Markers"),
                                    [window]() { window->projectModel().removeAllMarkers(); });
            window->refreshTimeline();
        });
        markersMenu->addAction(QObject::tr("Delete All in &Selection"))->setEnabled(false);
    }

    menu.addSeparator();

    auto *quantize = menu.addAction(QObject::tr("&Quantize to Frames"));
    quantize->setCheckable(true);
    quantize->setChecked(model.quantizeToFrames());
    quantize->setShortcut(QKeySequence(QStringLiteral("Alt+F8")));
    QObject::connect(quantize, &QAction::triggered, &menu, [window, saveTimelineOpts](bool on) {
        window->projectModel().setQuantizeToFrames(on);
        saveTimelineOpts();
    });

    menu.addSeparator();

    auto *snap = menu.addAction(QObject::tr("&Enable Snapping"));
    snap->setCheckable(true);
    snap->setChecked(model.snappingEnabled());
    snap->setShortcut(QKeySequence(QStringLiteral("F8")));

    auto *snapGrid = menu.addAction(QObject::tr("Snap to &Grid"));
    snapGrid->setCheckable(true);
    snapGrid->setChecked(model.snapToGrid());
    snapGrid->setShortcut(QKeySequence(QStringLiteral("Ctrl+F8")));
    snapGrid->setEnabled(model.snappingEnabled());

    auto *snapMarkers = menu.addAction(QObject::tr("Snap to Marke&rs"));
    snapMarkers->setCheckable(true);
    snapMarkers->setChecked(model.snapToMarkers());
    snapMarkers->setShortcut(QKeySequence(QStringLiteral("Shift+F8")));
    snapMarkers->setEnabled(model.snappingEnabled());

    auto *snapEvents = menu.addAction(QObject::tr("Snap to &All Events"));
    snapEvents->setCheckable(true);
    snapEvents->setChecked(model.snapToAllEvents());
    snapEvents->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+F8")));
    snapEvents->setEnabled(model.snappingEnabled());

    QObject::connect(snap, &QAction::triggered, &menu,
                     [window, saveTimelineOpts, snapGrid, snapMarkers, snapEvents](bool on) {
                         window->projectModel().setSnappingEnabled(on);
                         snapGrid->setEnabled(on);
                         snapMarkers->setEnabled(on);
                         snapEvents->setEnabled(on);
                         saveTimelineOpts();
                     });
    QObject::connect(snapGrid, &QAction::triggered, &menu, [window, saveTimelineOpts](bool on) {
        window->projectModel().setSnapToGrid(on);
        saveTimelineOpts();
    });
    QObject::connect(snapMarkers, &QAction::triggered, &menu, [window, saveTimelineOpts](bool on) {
        window->projectModel().setSnapToMarkers(on);
        saveTimelineOpts();
    });
    QObject::connect(snapEvents, &QAction::triggered, &menu, [window, saveTimelineOpts](bool on) {
        window->projectModel().setSnapToAllEvents(on);
        saveTimelineOpts();
    });

    menu.addSeparator();

    auto *gridMenu = menu.addMenu(QObject::tr("&Grid Spacing"));
    {
        auto *group = new QActionGroup(gridMenu);
        group->setExclusive(true);
        const auto cur = model.gridSpacing();
        auto addGrid = [&](const QString &label, TimelineGridSpacing sp) {
            auto *a = gridMenu->addAction(label);
            a->setCheckable(true);
            a->setChecked(cur == sp);
            a->setActionGroup(group);
            QObject::connect(a, &QAction::triggered, gridMenu, [window, sp, saveTimelineOpts]() {
                window->projectModel().setGridSpacing(sp);
                saveTimelineOpts();
            });
        };
        addGrid(QObject::tr("&Ruler Marks"), TimelineGridSpacing::RulerMarks);
        addGrid(QObject::tr("&Seconds"), TimelineGridSpacing::Seconds);
        addGrid(QObject::tr("&Half Seconds"), TimelineGridSpacing::HalfSeconds);
        addGrid(QObject::tr("&Quarter Seconds"), TimelineGridSpacing::QuarterSeconds);
        gridMenu->addSeparator();
        addGrid(QObject::tr("&Measures"), TimelineGridSpacing::Measures);
        addGrid(QObject::tr("Half Measures"), TimelineGridSpacing::HalfMeasures);
        addGrid(QObject::tr("Quarter &Notes"), TimelineGridSpacing::QuarterNotes);
        addGrid(QObject::tr("&Eighth Notes"), TimelineGridSpacing::EighthNotes);
        addGrid(QObject::tr("S&ixteenth Notes"), TimelineGridSpacing::SixteenthNotes);
        addGrid(QObject::tr("&Thirty-Second Notes"), TimelineGridSpacing::ThirtySecondNotes);
        addGrid(QObject::tr("Si&xty-Fourth Notes"), TimelineGridSpacing::SixtyFourthNotes);
        gridMenu->addSeparator();
        addGrid(QObject::tr("&Frames"), TimelineGridSpacing::Frames);
        addGrid(QObject::tr("Half Frames"), TimelineGridSpacing::HalfFrames);
    }

    menu.addSeparator();

    auto *prerender = menu.addAction(QObject::tr("&Selectively Prerender Video..."));
    prerender->setShortcut(QKeySequence(QStringLiteral("Shift+M")));
    prerender->setEnabled(false);
    menu.addAction(QObject::tr("&Clean Up Prerendered Video..."));

    menu.exec(globalPos);
}

void ContextMenuBuilder::showMarkerMenu(MainWindow *window, int markerId, const QPoint &globalPos)
{
    if (!window || !window->projectModel().findMarker(markerId)) {
        return;
    }
    TimelineView *tl = window->timelineView();
    QMenu menu(window);
    auto *go = menu.addAction(QObject::tr("&Go To"));
    QObject::connect(go, &QAction::triggered, &menu, [window, markerId, tl]() {
        const auto *m = window->projectModel().findMarker(markerId);
        if (m && tl) {
            tl->seekPlayhead(m->timeSec, false);
        }
    });
    auto *rename = menu.addAction(QObject::tr("&Rename"));
    QObject::connect(rename, &QAction::triggered, &menu, [markerId, tl]() {
        if (tl) {
            tl->beginMarkerRename(markerId);
        }
    });
    menu.addSeparator();
    auto *del = menu.addAction(QObject::tr("&Delete"));
    QObject::connect(del, &QAction::triggered, &menu, [window, markerId]() {
        window->runDocumentEdit(QObject::tr("Delete Marker"),
                                [window, markerId]() { window->projectModel().removeMarker(markerId); });
        window->refreshTimeline();
    });
    menu.exec(globalPos);
}

void ContextMenuBuilder::showTimeDisplayMenu(MainWindow *window, const QPoint &globalPos)
{
    QMenu menu(window);
    auto *cursor = addItem(&menu, QObject::tr("Time at Cursor"));
    cursor->setCheckable(true);
    cursor->setChecked(true);
    addItem(&menu, QObject::tr("MIDI Timecode In"))->setCheckable(true);
    addItem(&menu, QObject::tr("MIDI Timecode Out"))->setCheckable(true);
    addItem(&menu, QObject::tr("MIDI Clock Out"))->setCheckable(true);
    menu.addSeparator();
    auto *fmt = menu.addMenu(QObject::tr("Time Format"));
    auto addFmt = [&](const QString &t, bool on = false) {
        auto *a = addItem(fmt, t);
        a->setCheckable(true);
        a->setChecked(on);
    };
    addFmt(QObject::tr("Time"));
    addFmt(QObject::tr("Seconds"));
    addFmt(QObject::tr("Time & Frames"), true);
    addFmt(QObject::tr("Absolute Frames"));
    addFmt(QObject::tr("Measures & Beats"));
    addFmt(QObject::tr("SMPTE Drop (29.97 fps)"));
    addFmt(QObject::tr("SMPTE Non-Drop (29.97 fps)"));
    menu.exec(globalPos);
}

void ContextMenuBuilder::showSplitScreenMenu(QWidget *parent, const QPoint &globalPos)
{
    QMenu menu(parent);
    auto *bypass = addItem(&menu, QObject::tr("FX Bypassed"));
    bypass->setCheckable(true);
    bypass->setChecked(true);
    addItem(&menu, QObject::tr("Clipboard"))->setCheckable(true);
    menu.addSeparator();
    addItem(&menu, QObject::tr("Select Left Half"));
    addItem(&menu, QObject::tr("Select Right Half"));
    addItem(&menu, QObject::tr("Select All"));
    menu.addSeparator();
    addItem(&menu, QObject::tr("Retain Split Screen Setting"))->setCheckable(true);
    menu.exec(globalPos);
}

void ContextMenuBuilder::showQualityMenu(MainWindow *window, QToolButton *chip, const QPoint &globalPos)
{
    QMenu menu(chip);
    const QStringList levels = {QObject::tr("Draft"), QObject::tr("Preview"), QObject::tr("Good"),
                                QObject::tr("Best")};
    const QStringList res = {QObject::tr("Auto"), QObject::tr("Full"), QObject::tr("Half"),
                             QObject::tr("Quarter")};
    for (const QString &level : levels) {
        auto *sub = menu.addMenu(level);
        for (int i = 0; i < res.size(); ++i) {
            auto *a = addItem(sub, res[i]);
            a->setCheckable(true);
            const bool on = (level == QObject::tr("Preview") && i == 0);
            a->setChecked(on);
            QObject::connect(a, &QAction::triggered, chip, [window, chip, level, r = res[i]]() {
                if (chip) {
                    chip->setText(QStringLiteral("%1 (%2) ▾").arg(level, r));
                }
                if (window) {
                    window->setPreviewQuality(level, r);
                }
            });
        }
    }
    menu.exec(globalPos);
}

void ContextMenuBuilder::showZoomMenu(QToolButton *chip, const QPoint &globalPos)
{
    QMenu menu(chip);
    addItem(&menu, QObject::tr("+"), QKeySequence(QStringLiteral("Ctrl++")));
    addItem(&menu, QObject::tr("−"), QKeySequence(QStringLiteral("Ctrl+-")));
    menu.addSeparator();
    addItem(&menu, QObject::tr("Original resolution"));
    const QStringList zooms = {QObject::tr("50 %"),  QObject::tr("100 %"), QObject::tr("125 %"),
                               QObject::tr("150 %"), QObject::tr("200 %"), QObject::tr("400 %"),
                               QObject::tr("800 %")};
    for (const QString &z : zooms) {
        auto *a = addItem(&menu, z);
        a->setCheckable(true);
        a->setChecked(z == QObject::tr("100 %"));
        if (chip) {
            QObject::connect(a, &QAction::triggered, chip, [chip, z]() {
                chip->setText(z + QStringLiteral(" ▾"));
            });
        }
    }
    menu.addSeparator();
    addItem(&menu, QObject::tr("Bypass Zoom"), QKeySequence(QStringLiteral("Ctrl+Shift+Alt+B")))
        ->setCheckable(true);
    menu.exec(globalPos);
}

void ContextMenuBuilder::showOverlaysMenu(MainWindow *window, QToolButton *btn, const QPoint &globalPos)
{
    QMenu menu(btn);
    auto *grid = addItem(&menu, QObject::tr("Grid"));
    grid->setCheckable(true);
    grid->setChecked(window->overlayGrid());
    auto *safe = addItem(&menu, QObject::tr("Safe Areas"));
    safe->setCheckable(true);
    safe->setChecked(window->overlaySafeAreas());
    QObject::connect(grid, &QAction::toggled, window, &MainWindow::setOverlayGrid);
    QObject::connect(safe, &QAction::toggled, window, &MainWindow::setOverlaySafeAreas);
    menu.addSeparator();
    {
        auto *ccGroup = new QActionGroup(&menu);
        ccGroup->setExclusive(true);
        addItem(&menu, QObject::tr("Closed Captioning CC1 (Primary)"))->setCheckable(true);
        addItem(&menu, QObject::tr("Closed Captioning CC2"))->setCheckable(true);
        addItem(&menu, QObject::tr("Closed Captioning CC3 (Secondary)"))->setCheckable(true);
        addItem(&menu, QObject::tr("Closed Captioning CC4"))->setCheckable(true);
        for (QAction *a : menu.actions()) {
            if (a->text().startsWith(QObject::tr("Closed Captioning"))) {
                ccGroup->addAction(a);
            }
        }
    }
    menu.addSeparator();
    {
        auto *chGroup = new QActionGroup(&menu);
        chGroup->setExclusive(true);
        for (const QString &label :
             {QObject::tr("Red"), QObject::tr("Green"), QObject::tr("Blue"),
              QObject::tr("Red as Grayscale"), QObject::tr("Green as Grayscale"),
              QObject::tr("Blue as Grayscale"), QObject::tr("Alpha as Grayscale")}) {
            auto *a = addItem(&menu, label);
            a->setCheckable(true);
            chGroup->addAction(a);
        }
    }
    menu.exec(globalPos);
}

void ContextMenuBuilder::showPreviewMenu(MainWindow *window, const QPoint &globalPos)
{
    QMenu menu(window);
    addItem(&menu, QObject::tr("Video Output Color Grading…"));
    menu.addSeparator();
    auto *bgDef = addItem(&menu, QObject::tr("Default Background"));
    bgDef->setCheckable(true);
    bgDef->setChecked(true);
    addItem(&menu, QObject::tr("Black Background"))->setCheckable(true);
    addItem(&menu, QObject::tr("White Background"))->setCheckable(true);
    menu.addSeparator();
    auto addCheck = [&](const QString &t, bool on = true) {
        auto *a = addItem(&menu, t);
        a->setCheckable(true);
        a->setChecked(on);
    };
    addCheck(QObject::tr("Simulate Device Aspect Ratio"));
    addCheck(QObject::tr("Enable Preview Scaling"));
    addCheck(QObject::tr("Adjust Size and Quality for Optimal Playback"));
    menu.addSeparator();
    addCheck(QObject::tr("Show Toolbar"));
    addCheck(QObject::tr("Show Status Bar"));
    addCheck(QObject::tr("Show Transport Bar"));
    menu.addSeparator();
    addItem(&menu, QObject::tr("Video Preview Preferences…"));
    addItem(&menu, QObject::tr("Preview Device Preferences…"));
    menu.addSeparator();
    {
        auto *q = menu.addMenu(QObject::tr("Preview Quality"));
        addItem(q, QObject::tr("Draft"));
        addItem(q, QObject::tr("Preview"));
        addItem(q, QObject::tr("Good"));
        auto *best = q->addMenu(QObject::tr("Best"));
        auto *autoA = addItem(best, QObject::tr("Auto"));
        autoA->setCheckable(true);
        autoA->setChecked(true);
        addItem(best, QObject::tr("Full"))->setCheckable(true);
        addItem(best, QObject::tr("Half"))->setCheckable(true);
        addItem(best, QObject::tr("Quarter"))->setCheckable(true);
    }
    {
        auto *fr = menu.addMenu(QObject::tr("Display Frame Rate"));
        auto *full = addItem(fr, QObject::tr("Full"));
        full->setCheckable(true);
        full->setChecked(true);
        addItem(fr, QObject::tr("Half"))->setCheckable(true);
        addItem(fr, QObject::tr("Quarter"))->setCheckable(true);
    }
    menu.addSeparator();
    addItem(&menu, QObject::tr("Copy Frame to Clipboard"));
    addItem(&menu, QObject::tr("Save Frame to File…"));
    menu.exec(globalPos);
}

} // namespace openvegas
