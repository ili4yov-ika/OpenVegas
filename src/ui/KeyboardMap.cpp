#include "ui/KeyboardMap.h"

#include <QSettings>

#include <algorithm>

namespace openvegas {
namespace {

KeyboardCommand cmd(const char *id, KeyboardContext ctx, const QStringList &keys = {})
{
    KeyboardCommand c;
    c.id = QString::fromUtf8(id);
    const int dot = c.id.lastIndexOf(QLatin1Char('.'));
    c.displayName = dot >= 0 ? c.id.mid(dot + 1) : c.id;
    c.context = ctx;
    for (const QString &k : keys) {
        if (!k.isEmpty()) {
            c.shortcuts.push_back(QKeySequence(k));
        }
    }
    return c;
}

KeyboardCommand cmdId(const QString &id, KeyboardContext ctx, const QStringList &keys = {})
{
    KeyboardCommand c;
    c.id = id;
    const int dot = c.id.lastIndexOf(QLatin1Char('.'));
    c.displayName = dot >= 0 ? c.id.mid(dot + 1) : c.id;
    c.context = ctx;
    for (const QString &k : keys) {
        if (!k.isEmpty()) {
            c.shortcuts.push_back(QKeySequence(k));
        }
    }
    return c;
}

} // namespace

QString keyboardContextId(KeyboardContext ctx)
{
    switch (ctx) {
    case KeyboardContext::TrackView:
        return QStringLiteral("TrackView");
    case KeyboardContext::TrackList:
        return QStringLiteral("TrackList");
    case KeyboardContext::Explorer:
        return QStringLiteral("Explorer");
    case KeyboardContext::Trimmer:
        return QStringLiteral("Trimmer");
    case KeyboardContext::Mixer:
        return QStringLiteral("Mixer");
    case KeyboardContext::EditDetails:
        return QStringLiteral("EditDetails");
    case KeyboardContext::TrackMotion:
        return QStringLiteral("TrackMotion");
    case KeyboardContext::Global:
        return QStringLiteral("Global");
    case KeyboardContext::MixingConsole:
        return QStringLiteral("MixingConsole");
    case KeyboardContext::Count:
        break;
    }
    return QStringLiteral("Global");
}

QString keyboardContextLabel(KeyboardContext ctx)
{
    switch (ctx) {
    case KeyboardContext::TrackView:
        return QObject::tr("TrackView");
    case KeyboardContext::TrackList:
        return QObject::tr("TrackList");
    case KeyboardContext::Explorer:
        return QObject::tr("Explorer");
    case KeyboardContext::Trimmer:
        return QObject::tr("Trimmer");
    case KeyboardContext::Mixer:
        return QObject::tr("Mixer");
    case KeyboardContext::EditDetails:
        return QObject::tr("EditDetails");
    case KeyboardContext::TrackMotion:
        return QObject::tr("TrackMotion");
    case KeyboardContext::Global:
        return QObject::tr("Global");
    case KeyboardContext::MixingConsole:
        return QObject::tr("Mixing Console");
    case KeyboardContext::Count:
        break;
    }
    return keyboardContextId(ctx);
}

KeyboardContext keyboardContextFromId(const QString &id)
{
    for (int i = 0; i < int(KeyboardContext::Count); ++i) {
        const auto ctx = static_cast<KeyboardContext>(i);
        if (keyboardContextId(ctx) == id) {
            return ctx;
        }
    }
    return KeyboardContext::Global;
}

KeyboardMap &KeyboardMap::instance()
{
    static KeyboardMap map;
    return map;
}

KeyboardMap::KeyboardMap(QObject *parent)
    : QObject(parent)
{
    ensureCatalog();
    load();
}

QVector<KeyboardCommand> KeyboardMap::defaultCatalog()
{
    QVector<KeyboardCommand> c;
    using KC = KeyboardContext;

    // Media keys as shown in Vegas Default (Pause←Play key, Stop←Stop key).
    const QString mediaPlay = QKeySequence(Qt::Key_MediaPlay).toString(QKeySequence::PortableText);
    const QString mediaStop = QKeySequence(Qt::Key_MediaStop).toString(QKeySequence::PortableText);

    auto addMarkers = [&](KC ctx) {
        for (int i = 0; i <= 9; ++i) {
            c.push_back(cmdId(QStringLiteral("CursorTo.Marker%1").arg(i), ctx, {QString::number(i)}));
        }
    };

    enum class TransportStyle { TrackView, TrackList, Trimmer, Global };

    auto addTransportVegas = [&](KC ctx, TransportStyle style) {
        // Per-context shortcuts match Vegas Default Customize Keyboard refs.
        QStringList loopKeys;
        QStringList pauseKeys;
        QStringList playFromStartKeys;
        QStringList playToggleKeys;
        QStringList recordKeys;
        QStringList scrubL;
        QStringList scrubP;
        QStringList scrubR;
        QStringList stopKeys;
        QStringList cursorPreviewKeys;

        switch (style) {
        case TransportStyle::TrackView:
        case TransportStyle::Trimmer:
            cursorPreviewKeys = {QStringLiteral("Num+0")};
            loopKeys = {QStringLiteral("Q"), QStringLiteral("Ctrl+Shift+L")};
            if (!mediaPlay.isEmpty()) {
                pauseKeys = {mediaPlay};
            }
            playFromStartKeys = {QStringLiteral("Shift+Space")};
            playToggleKeys = {QStringLiteral("Space")}; // OpenVegas UX; Vegas TrackView often empty
            recordKeys = {QStringLiteral("Ctrl+R")};
            scrubL = {QStringLiteral("J")};
            scrubP = {QStringLiteral("K")};
            scrubR = {QStringLiteral("L")};
            stopKeys = mediaStop.isEmpty()
                           ? QStringList{QStringLiteral("Ctrl+Space")}
                           : QStringList{mediaStop, QStringLiteral("Ctrl+Space")};
            break;
        case TransportStyle::TrackList:
            // Vegas TrackList Transport: mostly unbound (cursor/track keys live elsewhere).
            break;
        case TransportStyle::Global:
            // Vegas Global: F12 / Ctrl+Space / Shift+F12 / Ctrl+F12.
            // Also keep Space for OpenVegas UX (TrackView-only Space needs focus routing we don't have yet).
            pauseKeys = {QStringLiteral("Ctrl+F12")};
            playFromStartKeys = {QStringLiteral("Shift+F12")};
            playToggleKeys = {QStringLiteral("F12"), QStringLiteral("Ctrl+Space"),
                              QStringLiteral("Space")};
            // J/K/L shuttle — same keys as TrackView/Trimmer above. These were already
            // whitelisted in MainWindow::applyKeyboardMap() and fully wired in
            // invokeKeyboardCommand(), but silently unreachable from any keypress because
            // this Global case never actually set scrubL/scrubP/scrubR (TrackView/TrackList/
            // Explorer contexts are cataloged but never turned into live QShortcuts at all —
            // Global is the only context that is).
            scrubL = {QStringLiteral("J")};
            scrubP = {QStringLiteral("K")};
            scrubR = {QStringLiteral("L")};
            break;
        }

        c.push_back(cmd("Transport.CursorPreview", ctx, cursorPreviewKeys));
        c.push_back(cmd("Transport.Fastforward", ctx, {}));
        c.push_back(cmd("Transport.LoopPlayback", ctx, loopKeys));
        c.push_back(cmd("Transport.Pause", ctx, pauseKeys));
        c.push_back(cmd("Transport.Play", ctx, {}));
        c.push_back(cmd("Transport.PlayFromStart", ctx, playFromStartKeys));
        c.push_back(cmd("Transport.PlayInReverse", ctx, {}));
        c.push_back(cmd("Transport.PlayToggle", ctx, playToggleKeys));
        c.push_back(cmd("Transport.Record", ctx, recordKeys));
        c.push_back(cmd("Transport.Rewind", ctx, {}));
        c.push_back(cmd("Transport.ScrubLeft", ctx, scrubL));
        c.push_back(cmd("Transport.ScrubPause", ctx, scrubP));
        c.push_back(cmd("Transport.ScrubRight", ctx, scrubR));
        c.push_back(cmd("Transport.StepBack", ctx, {}));
        c.push_back(cmd("Transport.StepForward", ctx, {}));
        c.push_back(cmd("Transport.Stop", ctx, stopKeys));
        c.push_back(cmd("Transport.TogglePlayFromStart", ctx, {}));
    };

    auto addCursorNav = [&](KC ctx, bool withHomeEndKeys) {
        c.push_back(cmd("CursorTo.Center", ctx, {}));
        c.push_back(cmd("CursorTo.End", ctx,
                        withHomeEndKeys ? QStringList{QStringLiteral("End"), QStringLiteral("E")}
                                        : QStringList{QStringLiteral("E")}));
        c.push_back(cmd("CursorTo.Home", ctx,
                        withHomeEndKeys ? QStringList{QStringLiteral("Home"), QStringLiteral("W")}
                                        : QStringList{QStringLiteral("W")}));
        c.push_back(cmd("CursorTo.LeftByFrame", ctx, {QStringLiteral("Alt+Left")}));
        c.push_back(cmd("CursorTo.RightByFrame", ctx, {QStringLiteral("Alt+Right")}));
        c.push_back(cmd("CursorTo.LeftByPixel", ctx, {QStringLiteral("Left")}));
        c.push_back(cmd("CursorTo.RightByPixel", ctx, {QStringLiteral("Right")}));
        c.push_back(cmd("CursorTo.LeftByMarker", ctx, {QStringLiteral("Ctrl+Left")}));
        c.push_back(cmd("CursorTo.RightByMarker", ctx, {QStringLiteral("Ctrl+Right")}));
        c.push_back(cmd("CursorTo.LeftByGrid", ctx, {QStringLiteral("PgUp")}));
        c.push_back(cmd("CursorTo.RightByGrid", ctx, {QStringLiteral("PgDown")}));
        c.push_back(cmd("CursorTo.LeftBySelected", ctx, {}));
        c.push_back(cmd("CursorTo.RightBySelected", ctx, {}));
        c.push_back(cmd("CursorTo.LoopEnd", ctx, {}));
        c.push_back(cmd("CursorTo.LoopHome", ctx, {}));
        addMarkers(ctx);
    };

    // --- TrackView (Vegas Default) ---
    addCursorNav(KC::TrackView, true);
    c.push_back(cmd("Edit.ConvertTransitionToCut", KC::TrackView, {QStringLiteral("Ctrl+/")}));
    c.push_back(cmd("Edit.Copy", KC::TrackView, {QStringLiteral("Ctrl+C")}));
    c.push_back(cmd("Edit.Cut", KC::TrackView, {QStringLiteral("Ctrl+X")}));
    c.push_back(cmd("Edit.Delete", KC::TrackView, {QStringLiteral("Del")}));
    c.push_back(cmd("Edit.Paste", KC::TrackView, {QStringLiteral("Ctrl+V")}));
    c.push_back(cmd("Edit.PasteInsert", KC::TrackView, {QStringLiteral("Ctrl+Shift+V")}));
    c.push_back(cmd("Edit.PasteRepeat", KC::TrackView, {QStringLiteral("Ctrl+B")}));
    c.push_back(cmd("Edit.Redo", KC::TrackView, {QStringLiteral("Ctrl+Y")}));
    c.push_back(cmd("Edit.SmartSplit", KC::TrackView, {QStringLiteral("Alt+S")}));
    c.push_back(cmd("Edit.Split", KC::TrackView, {QStringLiteral("S")}));
    c.push_back(cmd("Edit.Trim", KC::TrackView, {QStringLiteral("Ctrl+T")}));
    c.push_back(cmd("Edit.Undo", KC::TrackView, {QStringLiteral("Ctrl+Z")}));
    c.push_back(cmd("Group.Clear", KC::TrackView, {QStringLiteral("Ctrl+U")}));
    c.push_back(cmd("Group.CreateNew", KC::TrackView, {QStringLiteral("G")}));
    c.push_back(cmd("Group.RemoveFrom", KC::TrackView, {}));
    c.push_back(cmd("Group.SelectAll", KC::TrackView, {}));
    c.push_back(cmd("Insert.AudioTrack", KC::TrackView, {QStringLiteral("Ctrl+Q")}));
    c.push_back(cmd("Insert.VideoTrack", KC::TrackView, {QStringLiteral("Ctrl+Shift+Q")}));
    c.push_back(cmd("Insert.Marker", KC::TrackView, {QStringLiteral("M")}));
    c.push_back(cmd("Insert.Region", KC::TrackView, {}));
    c.push_back(cmd("Marker.Insert", KC::TrackView, {QStringLiteral("M")}));
    c.push_back(cmd("LoopRegion.Insert", KC::TrackView, {}));
    c.push_back(cmd("Options.EnableSnapping", KC::TrackView, {QStringLiteral("F8")}));
    c.push_back(cmd("Options.SnapToGrid", KC::TrackView, {QStringLiteral("Ctrl+F8")}));
    c.push_back(cmd("Options.QuantizeToFrames", KC::TrackView, {QStringLiteral("Alt+F8")}));
    c.push_back(cmd("Options.IgnoreEventGrouping", KC::TrackView, {QStringLiteral("Ctrl+Shift+U")}));
    c.push_back(cmd("Options.DoAutoRipple", KC::TrackView, {QStringLiteral("Ctrl+L")}));
    c.push_back(cmd("Select.All", KC::TrackView, {QStringLiteral("Ctrl+A")}));
    c.push_back(cmd("Select.None", KC::TrackView, {}));
    c.push_back(cmd("Select.Loop", KC::TrackView, {QStringLiteral("Shift+L")}));
    c.push_back(cmd("Take.NextTake", KC::TrackView, {QStringLiteral("T")}));
    c.push_back(cmd("Take.PreviousTake", KC::TrackView, {}));
    c.push_back(cmd("Tools.BuildRAMPreview", KC::TrackView, {QStringLiteral("Shift+B")}));
    c.push_back(cmd("Tools.ClearRAMPreview", KC::TrackView, {QStringLiteral("Alt+Shift+B")}));
    c.push_back(cmd("Tools.PrerenderVideo", KC::TrackView, {QStringLiteral("Shift+M")}));
    c.push_back(cmd("Tools.RenderToNew", KC::TrackView, {QStringLiteral("Ctrl+M")}));
    c.push_back(cmd("Tools.Refresh", KC::TrackView, {QStringLiteral("F5")}));
    c.push_back(cmd("MarkIn", KC::TrackView, {QStringLiteral("I")}));
    c.push_back(cmd("MarkOut", KC::TrackView, {QStringLiteral("O")}));
    addTransportVegas(KC::TrackView, TransportStyle::TrackView);
    c.push_back(cmd("Track.ToggleMute", KC::TrackView, {QStringLiteral("Z")}));
    c.push_back(cmd("Track.ToggleSolo", KC::TrackView, {QStringLiteral("X")}));
    c.push_back(cmd("Track.ToggleMuteExclusive", KC::TrackView, {QStringLiteral("Shift+Z")}));
    c.push_back(cmd("Track.ToggleSoloExclusive", KC::TrackView, {QStringLiteral("Shift+X")}));
    c.push_back(cmd("View.AudioBusTracks", KC::TrackView, {QStringLiteral("B")}));
    c.push_back(cmd("View.Envelope.TrackPan", KC::TrackView, {QStringLiteral("P")}));
    c.push_back(cmd("View.Envelope.TrackVolume", KC::TrackView, {QStringLiteral("V")}));
    c.push_back(cmd("View.VideoBusTrack", KC::TrackView, {QStringLiteral("Ctrl+Shift+B")}));

    // --- TrackList ---
    addCursorNav(KC::TrackList, true);
    c.push_back(cmd("Edit.Copy", KC::TrackList, {QStringLiteral("Ctrl+C")}));
    c.push_back(cmd("Edit.Cut", KC::TrackList, {QStringLiteral("Ctrl+X")}));
    c.push_back(cmd("Edit.Delete", KC::TrackList, {QStringLiteral("Del")}));
    c.push_back(cmd("Edit.Paste", KC::TrackList, {QStringLiteral("Ctrl+V")}));
    c.push_back(cmd("Edit.Split", KC::TrackList, {QStringLiteral("S")}));
    c.push_back(cmd("Edit.SmartSplit", KC::TrackList, {QStringLiteral("Alt+S")}));
    c.push_back(cmd("Group.Clear", KC::TrackList, {QStringLiteral("Ctrl+U")}));
    c.push_back(cmd("Group.CreateNew", KC::TrackList, {QStringLiteral("G")}));
    c.push_back(cmd("Insert.AudioTrack", KC::TrackList, {QStringLiteral("Ctrl+Q")}));
    c.push_back(cmd("Insert.VideoTrack", KC::TrackList, {QStringLiteral("Ctrl+Shift+Q")}));
    c.push_back(cmd("Options.EnableSnapping", KC::TrackList, {QStringLiteral("F8")}));
    c.push_back(cmd("Select.All", KC::TrackList, {QStringLiteral("Ctrl+A")}));
    c.push_back(cmd("Track.CursorDown", KC::TrackList, {QStringLiteral("Down")}));
    c.push_back(cmd("Track.CursorUp", KC::TrackList, {QStringLiteral("Up")}));
    c.push_back(cmd("Track.Rename", KC::TrackList, {QStringLiteral("F2")}));
    c.push_back(cmd("Track.ToggleMute", KC::TrackList, {QStringLiteral("Z")}));
    c.push_back(cmd("Track.ToggleSolo", KC::TrackList, {QStringLiteral("X")}));
    c.push_back(cmd("Track.ToggleMuteExclusive", KC::TrackList, {QStringLiteral("Shift+Z")}));
    c.push_back(cmd("Track.ToggleSoloExclusive", KC::TrackList, {QStringLiteral("Shift+X")}));
    addTransportVegas(KC::TrackList, TransportStyle::TrackList); // Vegas: Play/Toggle empty here

    // --- Explorer ---
    c.push_back(cmd("Edit.Copy", KC::Explorer, {QStringLiteral("Ctrl+C")}));
    c.push_back(cmd("Edit.Cut", KC::Explorer, {QStringLiteral("Ctrl+X")}));
    c.push_back(cmd("Edit.Paste", KC::Explorer, {QStringLiteral("Ctrl+V")}));
    c.push_back(cmd("Edit.Delete", KC::Explorer, {QStringLiteral("Del")}));
    c.push_back(cmd("Edit.SelectAll", KC::Explorer, {QStringLiteral("Ctrl+A")}));
    c.push_back(cmd("Edit.Redo", KC::Explorer, {QStringLiteral("Ctrl+Y")}));
    c.push_back(cmd("Group.Clear", KC::Explorer, {}));
    c.push_back(cmd("Group.CreateNew", KC::Explorer, {}));
    c.push_back(cmd("Group.RemoveFrom", KC::Explorer, {}));
    c.push_back(cmd("Group.SelectAll", KC::Explorer, {}));
    c.push_back(cmd("Select.All", KC::Explorer, {QStringLiteral("Ctrl+A")}));
    c.push_back(cmd("Select.None", KC::Explorer, {}));
    c.push_back(cmd("Edit.SmartSplit", KC::Explorer, {QStringLiteral("Alt+S")}));
    c.push_back(cmd("Edit.Split", KC::Explorer, {QStringLiteral("S")}));

    // --- Trimmer ---
    addCursorNav(KC::Trimmer, false);
    c.push_back(cmd("MarkIn", KC::Trimmer, {QStringLiteral("I"), QStringLiteral("[")}));
    c.push_back(cmd("MarkOut", KC::Trimmer, {QStringLiteral("O"), QStringLiteral("]")}));
    c.push_back(cmd("Select.Loop", KC::Trimmer, {QStringLiteral("Shift+L")}));
    c.push_back(cmd("Options.EnableSnapping", KC::Trimmer, {QStringLiteral("F8")}));
    c.push_back(cmd("Options.QuantizeToFrames", KC::Trimmer, {QStringLiteral("Alt+F8")}));
    c.push_back(cmd("Options.DoAutoRipple", KC::Trimmer, {QStringLiteral("Ctrl+L")}));
    addTransportVegas(KC::Trimmer, TransportStyle::Trimmer);

    // --- Mixer ---
    c.push_back(cmd("Insert.AudioBus", KC::Mixer, {}));
    c.push_back(cmd("Track.ToggleMute", KC::Mixer, {QStringLiteral("Z")}));
    c.push_back(cmd("Track.ToggleSolo", KC::Mixer, {QStringLiteral("X")}));
    c.push_back(cmd("Track.ToggleMuteExclusive", KC::Mixer, {QStringLiteral("Shift+Z")}));
    c.push_back(cmd("Track.ToggleSoloExclusive", KC::Mixer, {QStringLiteral("Shift+X")}));
    c.push_back(cmd("View.MixingConsole", KC::Mixer, {}));
    c.push_back(cmd("View.MixerPreviewFader", KC::Mixer, {}));

    // --- Mixing Console ---
    c.push_back(cmd("Edit.Copy", KC::MixingConsole, {QStringLiteral("Ctrl+C")}));
    c.push_back(cmd("Edit.Cut", KC::MixingConsole, {QStringLiteral("Ctrl+X")}));
    c.push_back(cmd("Edit.Paste", KC::MixingConsole, {QStringLiteral("Ctrl+V")}));
    c.push_back(cmd("Insert.AudioTrack", KC::MixingConsole, {QStringLiteral("Ctrl+Q")}));
    c.push_back(cmd("MixingConsole.ChannelWidth.Default", KC::MixingConsole,
                    {QStringLiteral("D"), QStringLiteral("Ctrl+'")}));
    c.push_back(cmd("MixingConsole.Channels.All", KC::MixingConsole, {QStringLiteral("Q")}));
    c.push_back(cmd("MixingConsole.AudioTracks", KC::MixingConsole, {QStringLiteral("A")}));
    c.push_back(cmd("MixingConsole.MasterBus", KC::MixingConsole, {QStringLiteral("T")}));
    c.push_back(cmd("MixingConsole.PreviewBus", KC::MixingConsole, {QStringLiteral("P")}));
    c.push_back(cmd("MixingConsole.Meters", KC::MixingConsole, {QStringLiteral("M")}));
    c.push_back(cmd("MixingConsole.Sends", KC::MixingConsole, {QStringLiteral("S")}));
    c.push_back(cmd("Track.ToggleMute", KC::MixingConsole, {QStringLiteral("Z")}));
    c.push_back(cmd("Track.ToggleSolo", KC::MixingConsole, {QStringLiteral("X")}));

    // --- EditDetails ---
    c.push_back(cmd("Details.Copy", KC::EditDetails, {QStringLiteral("Ctrl+C")}));
    c.push_back(cmd("Details.Cut", KC::EditDetails, {QStringLiteral("Ctrl+X")}));
    c.push_back(cmd("Details.Delete", KC::EditDetails, {QStringLiteral("Del")}));
    c.push_back(cmd("Details.Edit", KC::EditDetails, {}));
    c.push_back(cmd("Details.Paste", KC::EditDetails, {QStringLiteral("Ctrl+V")}));
    c.push_back(cmd("Edit.Copy", KC::EditDetails, {QStringLiteral("Ctrl+C")}));
    c.push_back(cmd("Edit.Cut", KC::EditDetails, {QStringLiteral("Ctrl+X")}));
    c.push_back(cmd("Edit.Paste", KC::EditDetails, {QStringLiteral("Ctrl+V")}));
    c.push_back(cmd("Edit.Delete", KC::EditDetails, {QStringLiteral("Del")}));
    c.push_back(cmd("Edit.Take.ChooseActive", KC::EditDetails, {QStringLiteral("Shift+T")}));
    c.push_back(cmd("TrackView.Event.OpenInTrimmer", KC::EditDetails, {}));

    // --- TrackMotion ---
    for (int i = 1; i <= 6; ++i) {
        c.push_back(cmdId(QStringLiteral("TrackMotion.3DLayout%1").arg(i), KC::TrackMotion,
                          {QString::number(i)}));
    }
    c.push_back(cmd("TrackMotion.Toggle.Constrain.Aspect", KC::TrackMotion, {QStringLiteral("A")}));
    c.push_back(cmd("TrackMotion.Center", KC::TrackMotion, {QStringLiteral("C")}));
    c.push_back(cmd("TrackMotion.ScaleX", KC::TrackMotion, {QStringLiteral("Shift+X")}));
    c.push_back(cmd("TrackMotion.ScaleY", KC::TrackMotion, {QStringLiteral("Shift+Y")}));
    c.push_back(cmd("TrackMotion.ScaleZ", KC::TrackMotion, {QStringLiteral("Shift+Z")}));
    c.push_back(cmd("TrackMotion.X", KC::TrackMotion, {QStringLiteral("X")}));
    c.push_back(cmd("TrackMotion.Y", KC::TrackMotion, {QStringLiteral("Y")}));
    c.push_back(cmd("TrackMotion.Z", KC::TrackMotion, {QStringLiteral("Z")}));
    c.push_back(cmd("TrackMotion.EnableRotation", KC::TrackMotion, {QStringLiteral("Shift+F8")}));
    c.push_back(cmd("TrackMotion.EnableSnap", KC::TrackMotion, {QStringLiteral("F8")}));
    c.push_back(cmd("TrackMotion.ObjectSpace", KC::TrackMotion, {QStringLiteral("O")}));

    // --- Global (ApplicationShortcuts) — Vegas Global Transport uses F12 family ---
    addTransportVegas(KC::Global, TransportStyle::Global);
    c.push_back(cmd("Edit.Undo", KC::Global, {QStringLiteral("Ctrl+Z")}));
    c.push_back(cmd("Edit.Redo", KC::Global, {QStringLiteral("Ctrl+Y"), QStringLiteral("Ctrl+Shift+Z")}));
    c.push_back(cmd("Edit.Cut", KC::Global, {QStringLiteral("Ctrl+X")}));
    c.push_back(cmd("Edit.Copy", KC::Global, {QStringLiteral("Ctrl+C")}));
    c.push_back(cmd("Edit.Paste", KC::Global, {QStringLiteral("Ctrl+V")}));
    c.push_back(cmd("Edit.Delete", KC::Global, {QStringLiteral("Del")}));
    c.push_back(cmd("Edit.Split", KC::Global, {QStringLiteral("S")}));
    c.push_back(cmd("Edit.SmartSplit", KC::Global, {QStringLiteral("Alt+S")}));
    c.push_back(cmd("Select.All", KC::Global, {QStringLiteral("Ctrl+A")}));
    // TrackView/TrackList already catalog these (G / Ctrl+U), but only Global ever
    // becomes a real live QShortcut (see applyKeyboardMap()) — without a Global entry
    // too, Group/Ungroup were keyboard-dead everywhere except the right-click submenu's
    // own transient (menu-lifetime-only) QAction shortcuts.
    c.push_back(cmd("Group.CreateNew", KC::Global, {QStringLiteral("G")}));
    c.push_back(cmd("Group.Clear", KC::Global, {QStringLiteral("Ctrl+U")}));
    c.push_back(cmd("CursorTo.Home", KC::Global, {QStringLiteral("Home"), QStringLiteral("W")}));
    c.push_back(cmd("CursorTo.End", KC::Global, {QStringLiteral("End"), QStringLiteral("E")}));
    c.push_back(cmd("CursorTo.LeftByFrame", KC::Global, {QStringLiteral("Alt+Left")}));
    c.push_back(cmd("CursorTo.RightByFrame", KC::Global, {QStringLiteral("Alt+Right")}));
    c.push_back(cmd("CursorTo.LeftByPixel", KC::Global, {QStringLiteral("Left")}));
    c.push_back(cmd("CursorTo.RightByPixel", KC::Global, {QStringLiteral("Right")}));
    c.push_back(cmd("File.New", KC::Global, {QStringLiteral("Ctrl+N")}));
    c.push_back(cmd("File.Open", KC::Global, {QStringLiteral("Ctrl+O")}));
    c.push_back(cmd("File.Save", KC::Global, {QStringLiteral("Ctrl+S")}));
    c.push_back(cmd("File.Close", KC::Global, {QStringLiteral("Ctrl+F4")}));
    c.push_back(cmd("File.Exit", KC::Global, {QStringLiteral("Alt+F4")}));
    c.push_back(cmd("File.Preferences", KC::Global, {}));
    c.push_back(cmd("Help.Contents", KC::Global, {QStringLiteral("F1")}));
    c.push_back(cmd("Help.CustomizeKeyboard", KC::Global, {}));
    c.push_back(cmd("Mixer.Dim", KC::Global, {QStringLiteral("Ctrl+Shift+F12")}));
    c.push_back(cmd("Options.EnableSnapping", KC::Global, {QStringLiteral("F8")}));
    c.push_back(cmd("Options.IgnoreEventGrouping", KC::Global, {QStringLiteral("Ctrl+Shift+U")}));
    c.push_back(cmd("Options.MuteAllAudio", KC::Global, {}));
    c.push_back(cmd("Options.MuteAllVideo", KC::Global, {}));
    c.push_back(cmd("Options.Preferences", KC::Global, {}));
    c.push_back(cmd("Preview.DefaultZoom", KC::Global, {QStringLiteral("Ctrl+Num+1")}));
    c.push_back(cmd("Preview.MinusZoom", KC::Global, {QStringLiteral("Ctrl+Num+-")}));
    c.push_back(cmd("Preview.PlusZoom", KC::Global, {QStringLiteral("Ctrl+Num++")}));
    c.push_back(cmd("Marker.Insert", KC::Global, {QStringLiteral("M")}));
    c.push_back(cmd("Track.ToggleMute", KC::Global, {QStringLiteral("Z")}));
    c.push_back(cmd("Track.ToggleSolo", KC::Global, {QStringLiteral("X")}));
    c.push_back(cmd("View.FocusToTrackView", KC::Global, {QStringLiteral("Alt+0")}));
    c.push_back(cmd("View.UnifiedExplorer", KC::Global, {QStringLiteral("Alt+1")}));
    c.push_back(cmd("View.Trimmer", KC::Global, {QStringLiteral("Alt+2")}));
    c.push_back(cmd("View.MasterBus", KC::Global, {QStringLiteral("Alt+3")}));
    c.push_back(cmd("View.VideoPreview", KC::Global, {QStringLiteral("Alt+4")}));
    c.push_back(cmd("View.ProjectMedia", KC::Global, {QStringLiteral("Alt+5")}));
    c.push_back(cmd("View.ProjectNotes", KC::Global, {QStringLiteral("Alt+6")}));
    c.push_back(cmd("View.Transitions", KC::Global, {QStringLiteral("Alt+7")}));
    c.push_back(cmd("View.VideoFX", KC::Global, {QStringLiteral("Alt+8")}));
    c.push_back(cmd("View.MixingConsole", KC::Global, {QStringLiteral("Ctrl+Alt+5")}));
    c.push_back(cmd("View.MinimizeDock", KC::Global, {QStringLiteral("F11")}));
    c.push_back(cmd("View.MinimizeAll", KC::Global, {QStringLiteral("Ctrl+F11")}));
    c.push_back(cmd("View.ShowEnvelopes", KC::Global, {QStringLiteral("Ctrl+Shift+E")}));
    c.push_back(cmd("View.ShowEventButtons", KC::Global, {QStringLiteral("Ctrl+Shift+C")}));
    c.push_back(cmd("View.ShowFadeLengths", KC::Global, {QStringLiteral("Ctrl+Shift+T")}));
    c.push_back(cmd("View.EventMediaMarkers", KC::Global, {QStringLiteral("Ctrl+Shift+K")}));
    c.push_back(cmd("Video.TrackMotion", KC::Global, {}));
    c.push_back(cmd("Video.VideoEventFX", KC::Global, {}));
    c.push_back(cmd("Video.VideoEventPanCrop", KC::Global, {}));
    c.push_back(cmd("Video.VideoTrackFX", KC::Global, {}));

    return c;
}

void KeyboardMap::ensureCatalog()
{
    m_commands = defaultCatalog();
    m_index.clear();
    for (int i = 0; i < m_commands.size(); ++i) {
        // Unique key: context + id (same id may exist in multiple contexts)
        const QString key =
            keyboardContextId(m_commands[i].context) + QLatin1Char('/') + m_commands[i].id;
        m_index.insert(key, i);
    }
}

void KeyboardMap::load()
{
    ensureCatalog();
    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    s.beginGroup(QStringLiteral("keyboard/default"));
    for (KeyboardCommand &c : m_commands) {
        const QString key =
            keyboardContextId(c.context) + QLatin1Char('/') + c.id;
        if (!s.contains(key)) {
            continue;
        }
        const QStringList parts = s.value(key).toStringList();
        c.shortcuts.clear();
        for (const QString &p : parts) {
            const QKeySequence seq(p);
            if (!seq.isEmpty()) {
                c.shortcuts.push_back(seq);
            }
        }
    }
    s.endGroup();
}

void KeyboardMap::save() const
{
    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    s.beginGroup(QStringLiteral("keyboard/default"));
    s.remove(QString());
    for (const KeyboardCommand &c : m_commands) {
        const QString key =
            keyboardContextId(c.context) + QLatin1Char('/') + c.id;
        QStringList parts;
        for (const QKeySequence &seq : c.shortcuts) {
            parts.push_back(seq.toString(QKeySequence::PortableText));
        }
        s.setValue(key, parts);
    }
    s.endGroup();
}

void KeyboardMap::resetToDefaults()
{
    ensureCatalog();
    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    s.remove(QStringLiteral("keyboard/default"));
    emit mapChanged();
}

QVector<KeyboardCommand> KeyboardMap::commandsFor(KeyboardContext ctx) const
{
    QVector<KeyboardCommand> out;
    for (const KeyboardCommand &c : m_commands) {
        if (c.context == ctx) {
            out.push_back(c);
        }
    }
    return out;
}

KeyboardCommand *KeyboardMap::findMutable(const QString &id)
{
    const QString globalKey =
        keyboardContextId(KeyboardContext::Global) + QLatin1Char('/') + id;
    if (m_index.contains(globalKey)) {
        return &m_commands[m_index.value(globalKey)];
    }
    for (auto it = m_index.constBegin(); it != m_index.constEnd(); ++it) {
        if (it.key().endsWith(QLatin1Char('/') + id)) {
            return &m_commands[it.value()];
        }
    }
    return nullptr;
}

KeyboardCommand *KeyboardMap::findMutable(KeyboardContext ctx, const QString &id)
{
    const QString key = keyboardContextId(ctx) + QLatin1Char('/') + id;
    const auto it = m_index.constFind(key);
    if (it == m_index.cend()) {
        return nullptr;
    }
    return &m_commands[*it];
}

const KeyboardCommand *KeyboardMap::find(const QString &id) const
{
    return const_cast<KeyboardMap *>(this)->findMutable(id);
}

const KeyboardCommand *KeyboardMap::find(KeyboardContext ctx, const QString &id) const
{
    return const_cast<KeyboardMap *>(this)->findMutable(ctx, id);
}

QStringList KeyboardMap::commandIdsUsing(const QKeySequence &seq, const QString &exceptId) const
{
    QStringList out;
    if (seq.isEmpty()) {
        return out;
    }
    for (const KeyboardCommand &c : m_commands) {
        if (!exceptId.isEmpty() && c.id == exceptId) {
            continue;
        }
        for (const QKeySequence &s : c.shortcuts) {
            if (s == seq) {
                out.push_back(keyboardContextId(c.context) + QLatin1Char('.') + c.id);
                break;
            }
        }
    }
    return out;
}

bool KeyboardMap::setShortcuts(const QString &id, const QVector<QKeySequence> &seqs)
{
    KeyboardCommand *c = findMutable(id);
    if (!c || !c->assignable) {
        return false;
    }
    c->shortcuts = seqs;
    emit mapChanged();
    return true;
}

bool KeyboardMap::addShortcut(const QString &id, const QKeySequence &seq)
{
    KeyboardCommand *c = findMutable(id);
    if (!c || !c->assignable || seq.isEmpty()) {
        return false;
    }
    for (const QKeySequence &s : c->shortcuts) {
        if (s == seq) {
            return true;
        }
    }
    c->shortcuts.push_back(seq);
    emit mapChanged();
    return true;
}

bool KeyboardMap::removeShortcut(const QString &id, const QKeySequence &seq)
{
    KeyboardCommand *c = findMutable(id);
    if (!c) {
        return false;
    }
    const int before = c->shortcuts.size();
    c->shortcuts.erase(std::remove_if(c->shortcuts.begin(), c->shortcuts.end(),
                                      [&](const QKeySequence &s) { return s == seq; }),
                       c->shortcuts.end());
    if (c->shortcuts.size() != before) {
        emit mapChanged();
        return true;
    }
    return false;
}

bool KeyboardMap::replaceShortcut(const QString &id, const QKeySequence &oldSeq,
                                  const QKeySequence &newSeq)
{
    KeyboardCommand *c = findMutable(id);
    if (!c || newSeq.isEmpty()) {
        return false;
    }
    for (QKeySequence &s : c->shortcuts) {
        if (s == oldSeq) {
            s = newSeq;
            emit mapChanged();
            return true;
        }
    }
    return false;
}

void KeyboardMap::notifyChanged()
{
    emit mapChanged();
}

} // namespace openvegas
