#include "model/ProjectModel.h"
#include "model/TrackColors.h"
#include "io/VegReader.h"
#include "io/ProjectInterchange.h"
#include "io/SamplePaths.h"
#include "io/MediaProbe.h"
#include "io/MediaWaveformCache.h"
#include "plugins/BuiltinAudioCatalog.h"
#include "plugins/VegasVideoPluginCatalog.h"
#include "audio/BuiltinDsp.h"
#include "video/MediaGeneratorApply.h"
#include "video/TitlesTextApply.h"

#include <QFileInfo>
#include <QDir>
#include <QHash>
#include <QSet>
#include <QRegularExpression>
#include <QDataStream>
#include <QIODevice>
#include <QVariantMap>
#include <algorithm>
#include <cmath>

namespace openvegas {
namespace {

/** Attach best-effort VEG FX state chunk when present. */
FxSlot fxSlotFromVegWithState(const QString &raw, const VegOpenResult &veg)
{
    FxSlot slot = fxSlotFromVegName(raw);
    QString key = slot.displayName;
    const int paren = key.indexOf(QLatin1Char('('));
    if (paren > 0) {
        key = key.left(paren).trimmed();
    }
    if (key.startsWith(QLatin1String("VEGAS Track "), Qt::CaseInsensitive)) {
        key = key.mid(12).trimmed();
    }
    const auto it = veg.fxStateChunks.constFind(key.toLower());
    if (it != veg.fxStateChunks.constEnd() && !it.value().isEmpty()) {
        setFxStateChunk(&slot, it.value());
    }
    // Legacy (non-OFX) VEGAS effects — Glint, Soft Contrast — keep their whole state as
    // XML in the project rather than as an OFX blob. Without this the chain recovered the
    // effect's name but every slider fell back to an invented default.
    const auto legacy = veg.legacyFxStates.constFind(key.toLower());
    if (legacy != veg.legacyFxStates.constEnd() && !legacy.value().baseParams.isEmpty()) {
        QVariantMap params = unpackFxParams(slot.state);
        for (auto pit = legacy.value().baseParams.constBegin();
             pit != legacy.value().baseParams.constEnd(); ++pit) {
            params.insert(pit.key(), pit.value());
        }
        if (!legacy.value().presetName.isEmpty()) {
            params.insert(fxVegasPresetStateKey(), legacy.value().presetName);
        }
        slot.state = packFxParams(params);
    }
    if (slot.format == PluginFormat::Ofx) {
        slot = VegasVideoPluginCatalog::resolveVegImportSlot(slot);
    }
    return slot;
}

/**
 * Automation lanes reproducing a legacy effect's animation from the project.
 *
 * One lane per parameter that actually changes across keyframes, plus the `_master`
 * marker row the FX dialogs draw as VEGAS-style diamonds. Parameters that hold the same
 * value throughout stay unanimated — VEGAS writes every parameter into every keyframe
 * blob, so importing them all would fill the panel with flat lanes.
 */
QVector<AutomationLane> legacyFxAutomationLanes(const FxSlot &slot, const VegOpenResult &veg,
                                                const QString &shortNameLower)
{
    QVector<AutomationLane> lanes;
    const auto it = veg.legacyFxStates.constFind(shortNameLower);
    if (it == veg.legacyFxStates.constEnd() || it.value().keyframes.size() < 2) {
        return lanes;
    }
    const QVector<VegLegacyFxKeyframe> &kfs = it.value().keyframes;

    QSet<QString> animated;
    for (const QString &param : kfs.first().params.keys()) {
        const double first = kfs.first().params.value(param).toDouble();
        for (const VegLegacyFxKeyframe &kf : kfs) {
            if (std::abs(kf.params.value(param, first).toDouble() - first) > 1e-9) {
                animated.insert(param);
                break;
            }
        }
    }

    AutomationLane master;
    master.targetId = fxMasterAutomationTargetId(slot);
    for (const VegLegacyFxKeyframe &kf : kfs) {
        AutomationPoint pt;
        pt.timeSec = kf.timeSec;
        pt.value = 1.0;
        master.points.push_back(pt);
    }
    lanes.push_back(master);

    for (const QString &param : animated) {
        AutomationLane lane;
        lane.targetId = fxParamAutomationTargetId(slot, param);
        for (const VegLegacyFxKeyframe &kf : kfs) {
            if (!kf.params.contains(param)) {
                continue;
            }
            AutomationPoint pt;
            pt.timeSec = kf.timeSec;
            pt.value = kf.params.value(param).toDouble();
            lane.points.push_back(pt);
        }
        if (lane.points.size() >= 2) {
            lanes.push_back(lane);
        }
    }
    return lanes;
}

/** Short catalog key for a slot ("Glint" → "glint"), matching VegOpenResult::legacyFxStates. */
QString legacyFxKeyForSlot(const FxSlot &slot)
{
    QString key = slot.displayName;
    const int paren = key.indexOf(QLatin1Char('('));
    if (paren > 0) {
        key = key.left(paren).trimmed();
    }
    return key.toLower();
}

/** Append an FX slot recovered from a `.veg` plus any animation it carries. */
void appendVegFxSlot(TrackEvent *event, const QString &raw, const VegOpenResult &veg)
{
    const FxSlot slot = fxSlotFromVegWithState(raw, veg);
    event->fxChain.push_back(slot);
    event->automationLanes += legacyFxAutomationLanes(slot, veg, legacyFxKeyForSlot(slot));
}

/** Vegas-style lanes for multichannel audio (EDL FirstChannel / Channels). */
struct AudioLane {
    int first = 0;
    int count = 2;
};

QVector<AudioLane> vegasAudioLanes(int channelCount)
{
    if (channelCount <= 0) {
        return {{0, 2}};
    }
    if (channelCount == 1) {
        return {{0, 1}};
    }
    if (channelCount == 2) {
        return {{0, 2}};
    }
    // 5.1 → 4 tracks: L/R, C, LFE, Ls/Rs (matches Vegas EDL for BBB)
    if (channelCount == 6) {
        return {{0, 2}, {2, 1}, {3, 1}, {4, 2}};
    }
    // 7.1 → 5 tracks
    if (channelCount == 8) {
        return {{0, 2}, {2, 1}, {3, 1}, {4, 2}, {6, 2}};
    }
    // Generic: stereo pairs, leftover mono
    QVector<AudioLane> lanes;
    int i = 0;
    while (i < channelCount) {
        if (i + 1 < channelCount) {
            lanes.push_back({i, 2});
            i += 2;
        } else {
            lanes.push_back({i, 1});
            ++i;
        }
    }
    return lanes;
}

} // namespace

ProjectModel::ProjectModel()
{
    loadEmptyProject();
}

QString ProjectModel::guessKindFromPath(const QString &path)
{
    const QString lower = path.toLower();
    if (lower.endsWith(QLatin1String(".wav")) || lower.endsWith(QLatin1String(".mp3"))
        || lower.endsWith(QLatin1String(".aif")) || lower.endsWith(QLatin1String(".aiff"))
        || lower.endsWith(QLatin1String(".flac"))) {
        return QStringLiteral("audio");
    }
    if (lower.endsWith(QLatin1String(".png")) || lower.endsWith(QLatin1String(".jpg"))
        || lower.endsWith(QLatin1String(".jpeg")) || lower.endsWith(QLatin1String(".tga"))
        || lower.endsWith(QLatin1String(".tif")) || lower.endsWith(QLatin1String(".tiff"))) {
        return QStringLiteral("still");
    }
    return QStringLiteral("video");
}

QString ProjectModel::resolveMediaPath(const QString &storedPath, const QString &vegPath)
{
    if (QFileInfo::exists(storedPath)) {
        return QDir::fromNativeSeparators(storedPath);
    }

    const QFileInfo vegInfo(vegPath);
    const QString fileName = QFileInfo(storedPath).fileName();
    if (fileName.isEmpty()) {
        return storedPath;
    }

    const QString vegDir = vegInfo.absolutePath();
    const QString baseName = QFileInfo(storedPath).completeBaseName();
    // Strip Vegas export suffixes: "name - 1", "name-1", "name-2"
    QString looseBase = baseName;
    looseBase.replace(QRegularExpression(QStringLiteral("\\s*-\\s*\\d+$")), QString());
    looseBase.replace(QRegularExpression(QStringLiteral("-\\d+$")), QString());

    QStringList searchDirs = {
        vegDir,
        vegDir + QStringLiteral("/screenshots"),
        vegDir + QStringLiteral("/assets"),
        vegDir + QStringLiteral("/media"),
        QDir(vegDir + QStringLiteral("/..")).absolutePath(),
        QDir(vegDir + QStringLiteral("/../assets")).absolutePath(),
        QDir(vegDir + QStringLiteral("/../screenshots")).absolutePath(),
        QDir(vegDir + QStringLiteral("/../SAMPLES")).absolutePath(),
        QDir(vegDir + QStringLiteral("/../SAMPLES/assets")).absolutePath(),
        QDir(vegDir + QStringLiteral("/../SAMPLES/veg_project")).absolutePath(),
        QDir(vegDir + QStringLiteral("/../../SAMPLES/assets")).absolutePath(),
        QDir(vegDir + QStringLiteral("/../../SAMPLES/veg_project")).absolutePath(),
        // Export folders that contain media copies
        vegDir + QStringLiteral("/edl-text-file"),
        vegDir + QStringLiteral("/final-cut-pro-7_davinci-resolve"),
        vegDir + QStringLiteral("/final-cut-pro-x"),
        vegDir + QStringLiteral("/premiere_after-effect"),
    };
    const QString samplesVp = SamplePaths::vegProjectDir();
    if (!samplesVp.isEmpty()) {
        searchDirs << samplesVp;
        searchDirs << QDir(samplesVp + QStringLiteral("/..")).absolutePath();
        searchDirs << QDir(samplesVp + QStringLiteral("/../assets")).absolutePath();
    }

    auto tryName = [&](const QString &name) -> QString {
        for (const QString &dir : searchDirs) {
            const QString candidate = QDir(dir).filePath(name);
            if (QFileInfo::exists(candidate)) {
                return QDir::cleanPath(candidate);
            }
        }
        return {};
    };

    if (QString hit = tryName(fileName); !hit.isEmpty()) {
        return hit;
    }
    // Same basename with common video/audio extensions
    const QStringList exts = {QStringLiteral(".mp4"), QStringLiteral(".mov"), QStringLiteral(".mkv"),
                              QStringLiteral(".wav"), QStringLiteral(".bwf"), QStringLiteral(".mp3"),
                              QStringLiteral(".aif"), QStringLiteral(".flac")};
    for (const QString &ext : exts) {
        if (QString hit = tryName(looseBase + ext); !hit.isEmpty()) {
            return hit;
        }
        if (QString hit = tryName(baseName + ext); !hit.isEmpty()) {
            return hit;
        }
    }
    return QDir::fromNativeSeparators(storedPath);
}

QString ProjectModel::projectTitle() const
{
    if (m_projectPath.isEmpty()) {
        return QStringLiteral("Untitled");
    }
    return QFileInfo(m_projectPath).completeBaseName();
}

void ProjectModel::loadDemoProject()
{
    m_tracks.clear();
    m_assignableFx.clear();
    m_mixerBuses.clear();
    m_mixerInputBuses.clear();
    m_mixerStripOrder.clear();
    m_mediaPool.clear();
    m_markers.clear();
    m_loopRegion = {};
    m_projectPath.clear();
    m_nextEventId = 1;
    m_nextTrackId = 1;
    m_nextGroupId = 1;
    m_nextMarkerId = 1;
    m_nextMarkerNumber = 1;
    m_nextAssignableFxId = 1;
    m_nextMixerBusId = 1;
    m_nextMixerInputBusId = 1;
    m_playheadSec = 0.0;
    m_pps = 40.0;
    m_frameRate = 29.97;
    m_sampleRate = 48000;
    m_tempoBpm = 120.0;
    m_frameWidth = 1920;
    m_frameHeight = 1080;

    Track video;
    video.id = m_nextTrackId++;
    video.name = QStringLiteral("Video 1");
    video.kind = TrackKind::Video;
    video.height = 96;
    // Matches SAMPLES project-with-fades-and-crossfades (~40 px/s)
    video.events.clear();
    auto addDemoVideo = [&](double startSec, double lengthSec, bool selected, double fadeIn,
                             double fadeOut) {
        TrackEvent ev;
        ev.id = m_nextEventId++;
        ev.name = QStringLiteral("sample_for_project_video");
        ev.startSec = startSec;
        ev.lengthSec = lengthSec;
        ev.selected = selected;
        ev.fadeInSec = fadeIn;
        ev.fadeOutSec = fadeOut;
        video.events.push_back(ev);
    };
    addDemoVideo(0.0, 15.0, false, 3.0, 2.0);
    addDemoVideo(13.0, 13.0, false, 2.0, 0.0);
    addDemoVideo(26.0, 9.0, true, 5.0, 1.0);
    addDemoVideo(34.0, 12.0, false, 1.0, 3.0);
    addDemoVideo(43.0, 27.0, false, 3.0, 0.0);

    Track audio;
    audio.id = m_nextTrackId++;
    audio.name = QStringLiteral("Audio 1");
    audio.kind = TrackKind::Audio;
    audio.height = 100;
    audio.fxChain = BuiltinAudioCatalog::defaultTrackFxChain();
    audio.events.clear();
    auto addDemoAudio = [&](double startSec, double lengthSec, bool selected, double fadeIn,
                             double fadeOut) {
        TrackEvent ev;
        ev.id = m_nextEventId++;
        // Keep the shared label used in the previous demo project data.
        ev.name = QStringLiteral("sample_for_project_video");
        ev.startSec = startSec;
        ev.lengthSec = lengthSec;
        ev.selected = selected;
        ev.fadeInSec = fadeIn;
        ev.fadeOutSec = fadeOut;
        audio.events.push_back(ev);
    };
    addDemoAudio(0.0, 15.0, false, 3.0, 2.0);
    addDemoAudio(13.0, 13.0, false, 2.0, 0.0);
    addDemoAudio(26.0, 9.0, true, 5.0, 1.0);
    addDemoAudio(34.0, 12.0, false, 1.0, 3.0);
    addDemoAudio(43.0, 27.0, false, 3.0, 0.0);

    for (TrackEvent &ev : video.events) {
        ev.mediaKind = EventMediaKind::Video;
        ev.fxChain = {makeFxSlot(QStringLiteral("Pan/Crop"), PluginFormat::Builtin)};
    }
    for (TrackEvent &ev : audio.events) {
        ev.mediaKind = EventMediaKind::Audio;
        ev.fxChain.clear();
    }
    assignPairedGroups(video, audio);

    video.displayColor = TrackColors::at(0);
    audio.displayColor = TrackColors::at(1);

    m_tracks.push_back(video);
    m_tracks.push_back(audio);

    m_mediaPool.push_back({QStringLiteral("sample_for_project_video.mp4"),
                           QStringLiteral("sample_for_project_video"), QStringLiteral("video"), true,
                           {}});
    m_mediaPool.push_back({QStringLiteral("sample_for_project_audio.wav"),
                           QStringLiteral("sample_for_project_audio"), QStringLiteral("audio"), true,
                           {}});
}

void ProjectModel::loadEmptyProject()
{
    m_tracks.clear();
    m_assignableFx.clear();
    m_mixerBuses.clear();
    m_mixerInputBuses.clear();
    m_mixerStripOrder.clear();
    m_mediaPool.clear();
    m_markers.clear();
    m_loopRegion = {};
    m_projectPath.clear();
    m_nextEventId = 1;
    m_nextTrackId = 1;
    m_nextGroupId = 1;
    m_nextMarkerId = 1;
    m_nextMarkerNumber = 1;
    m_nextAssignableFxId = 1;
    m_nextMixerBusId = 1;
    m_nextMixerInputBusId = 1;
    m_playheadSec = 0.0;
    m_pps = 40.0;
    m_frameRate = 29.97;
    m_sampleRate = 48000;
    m_tempoBpm = 120.0;
    m_frameWidth = 1920;
    m_frameHeight = 1080;
}

void ProjectModel::assignPairedGroups(Track &video, Track &audio)
{
    const int n = std::min(video.events.size(), audio.events.size());
    for (int i = 0; i < n; ++i) {
        const int gid = m_nextGroupId++;
        video.events[i].groupId = gid;
        audio.events[i].groupId = gid;
        // Keep A/V pair aligned in time like a dropped media clip
        audio.events[i].startSec = video.events[i].startSec;
        audio.events[i].lengthSec = video.events[i].lengthSec;
        audio.events[i].fadeInSec = video.events[i].fadeInSec;
        audio.events[i].fadeOutSec = video.events[i].fadeOutSec;
    }
}

int ProjectModel::ensureTrack(TrackKind kind)
{
    for (int i = 0; i < m_tracks.size(); ++i) {
        if (m_tracks[i].kind == kind) {
            return i;
        }
    }
    return addTrack(kind);
}

int ProjectModel::addTrack(TrackKind kind)
{
    int sameKind = 0;
    for (const Track &tr : m_tracks) {
        if (tr.kind == kind) {
            ++sameKind;
        }
    }
    Track t;
    t.id = m_nextTrackId++;
    t.kind = kind;
    t.displayColor = TrackColors::at(m_tracks.size());
    if (kind == TrackKind::Video) {
        t.name = QStringLiteral("Video %1").arg(sameKind + 1);
        t.height = 96;
    } else {
        t.name = QStringLiteral("Audio %1").arg(sameKind + 1);
        t.height = 100;
        t.fxChain = BuiltinAudioCatalog::defaultTrackFxChain();
    }
    m_tracks.push_back(t);
    if (kind == TrackKind::Audio) {
        appendMixerStripRef(MixerStripKind::AudioTrack, t.id);
    }
    return m_tracks.size() - 1;
}

int ProjectModel::addAssignableFxBus(const QVector<FxSlot> &chain)
{
    AssignableFxBus bus;
    bus.id = m_nextAssignableFxId++;
    bus.number = m_assignableFx.size() + 1;
    bus.fxChain = chain;
    if (!chain.isEmpty()) {
        bus.name = chain.first().displayName;
    } else {
        bus.name = QStringLiteral("FX %1").arg(bus.number);
    }
    m_assignableFx.push_back(bus);
    appendMixerStripRef(MixerStripKind::AssignableFx, bus.id);
    return m_assignableFx.size() - 1;
}

AssignableFxBus *ProjectModel::findAssignableFxBus(int id)
{
    for (AssignableFxBus &b : m_assignableFx) {
        if (b.id == id) {
            return &b;
        }
    }
    return nullptr;
}

const AssignableFxBus *ProjectModel::findAssignableFxBus(int id) const
{
    for (const AssignableFxBus &b : m_assignableFx) {
        if (b.id == id) {
            return &b;
        }
    }
    return nullptr;
}

int ProjectModel::addMixerBus()
{
    MixerBus bus;
    bus.id = m_nextMixerBusId++;
    bus.letterIndex = m_mixerBuses.size();
    const QChar letter = QChar(QLatin1Char('A' + (bus.letterIndex % 26)));
    // After Z: AA, AB... simple Vegas-style - keep single letter + index for now
    if (bus.letterIndex < 26) {
        bus.name = QStringLiteral("Bus %1").arg(letter);
    } else {
        bus.name = QStringLiteral("Bus %1").arg(bus.letterIndex + 1);
    }
    m_mixerBuses.push_back(bus);
    appendMixerStripRef(MixerStripKind::AudioBus, bus.id);
    return m_mixerBuses.size() - 1;
}

MixerBus *ProjectModel::findMixerBus(int id)
{
    for (MixerBus &b : m_mixerBuses) {
        if (b.id == id) {
            return &b;
        }
    }
    return nullptr;
}

const MixerBus *ProjectModel::findMixerBus(int id) const
{
    for (const MixerBus &b : m_mixerBuses) {
        if (b.id == id) {
            return &b;
        }
    }
    return nullptr;
}

int ProjectModel::addMixerInputBus()
{
    MixerInputBus bus;
    bus.id = m_nextMixerInputBusId++;
    bus.letterIndex = m_mixerInputBuses.size();
    const QChar letter = QChar(QLatin1Char('A' + (bus.letterIndex % 26)));
    if (bus.letterIndex < 26) {
        bus.name = QStringLiteral("Input %1").arg(letter);
    } else {
        bus.name = QStringLiteral("Input %1").arg(bus.letterIndex + 1);
    }
    m_mixerInputBuses.push_back(bus);
    appendMixerStripRef(MixerStripKind::InputBus, bus.id);
    return m_mixerInputBuses.size() - 1;
}

MixerInputBus *ProjectModel::findMixerInputBus(int id)
{
    for (MixerInputBus &b : m_mixerInputBuses) {
        if (b.id == id) {
            return &b;
        }
    }
    return nullptr;
}

const MixerInputBus *ProjectModel::findMixerInputBus(int id) const
{
    for (const MixerInputBus &b : m_mixerInputBuses) {
        if (b.id == id) {
            return &b;
        }
    }
    return nullptr;
}

void ProjectModel::appendMixerStripRef(MixerStripKind kind, int id)
{
    ensureMixerStripOrder();
    // Avoid duplicate
    for (const MixerStripRef &r : m_mixerStripOrder) {
        if (r.kind == kind && r.id == id) {
            return;
        }
    }
    MixerStripRef ref{kind, id};
    if (kind == MixerStripKind::AudioTrack) {
        int i = 0;
        while (i < m_mixerStripOrder.size()
               && m_mixerStripOrder[i].kind == MixerStripKind::AudioTrack) {
            ++i;
        }
        m_mixerStripOrder.insert(i, ref);
        return;
    }
    if (kind == MixerStripKind::Master) {
        for (const MixerStripRef &r : m_mixerStripOrder) {
            if (r.kind == MixerStripKind::Master) {
                return;
            }
        }
        m_mixerStripOrder.push_back(ref);
        return;
    }
    // Bus / Input / FX: insert before Master when present
    for (int i = 0; i < m_mixerStripOrder.size(); ++i) {
        if (m_mixerStripOrder[i].kind == MixerStripKind::Master) {
            m_mixerStripOrder.insert(i, ref);
            return;
        }
    }
    m_mixerStripOrder.push_back(ref);
}

void ProjectModel::rebuildDefaultMixerStripOrder()
{
    m_mixerStripOrder.clear();
    for (const Track &t : m_tracks) {
        if (t.kind == TrackKind::Audio) {
            m_mixerStripOrder.push_back({MixerStripKind::AudioTrack, t.id});
        }
    }
    for (const MixerBus &b : m_mixerBuses) {
        m_mixerStripOrder.push_back({MixerStripKind::AudioBus, b.id});
    }
    for (const MixerInputBus &b : m_mixerInputBuses) {
        m_mixerStripOrder.push_back({MixerStripKind::InputBus, b.id});
    }
    for (const AssignableFxBus &b : m_assignableFx) {
        m_mixerStripOrder.push_back({MixerStripKind::AssignableFx, b.id});
    }
    m_mixerStripOrder.push_back({MixerStripKind::Master, 0});
}

void ProjectModel::ensureMixerStripOrder()
{
    if (m_mixerStripOrder.isEmpty()) {
        rebuildDefaultMixerStripOrder();
        return;
    }
    // Drop stale refs
    QVector<MixerStripRef> cleaned;
    cleaned.reserve(m_mixerStripOrder.size());
    bool hasMaster = false;
    for (const MixerStripRef &r : m_mixerStripOrder) {
        bool ok = false;
        switch (r.kind) {
        case MixerStripKind::AudioTrack:
            for (const Track &t : m_tracks) {
                if (t.kind == TrackKind::Audio && t.id == r.id) {
                    ok = true;
                    break;
                }
            }
            break;
        case MixerStripKind::AudioBus:
            ok = findMixerBus(r.id) != nullptr;
            break;
        case MixerStripKind::InputBus:
            ok = findMixerInputBus(r.id) != nullptr;
            break;
        case MixerStripKind::AssignableFx:
            ok = findAssignableFxBus(r.id) != nullptr;
            break;
        case MixerStripKind::Master:
            ok = !hasMaster;
            hasMaster = true;
            break;
        }
        if (ok) {
            cleaned.push_back(r);
        }
    }
    // Append any missing entities
    auto missing = [&](MixerStripKind kind, int id) {
        for (const MixerStripRef &r : cleaned) {
            if (r.kind == kind && r.id == id) {
                return false;
            }
        }
        return true;
    };
    for (const Track &t : m_tracks) {
        if (t.kind == TrackKind::Audio && missing(MixerStripKind::AudioTrack, t.id)) {
            int i = 0;
            while (i < cleaned.size() && cleaned[i].kind == MixerStripKind::AudioTrack) {
                ++i;
            }
            cleaned.insert(i, {MixerStripKind::AudioTrack, t.id});
        }
    }
    for (const MixerBus &b : m_mixerBuses) {
        if (missing(MixerStripKind::AudioBus, b.id)) {
            int masterIdx = cleaned.size();
            for (int i = 0; i < cleaned.size(); ++i) {
                if (cleaned[i].kind == MixerStripKind::Master) {
                    masterIdx = i;
                    break;
                }
            }
            cleaned.insert(masterIdx, {MixerStripKind::AudioBus, b.id});
        }
    }
    for (const MixerInputBus &b : m_mixerInputBuses) {
        if (missing(MixerStripKind::InputBus, b.id)) {
            int masterIdx = cleaned.size();
            for (int i = 0; i < cleaned.size(); ++i) {
                if (cleaned[i].kind == MixerStripKind::Master) {
                    masterIdx = i;
                    break;
                }
            }
            cleaned.insert(masterIdx, {MixerStripKind::InputBus, b.id});
        }
    }
    for (const AssignableFxBus &b : m_assignableFx) {
        if (missing(MixerStripKind::AssignableFx, b.id)) {
            int masterIdx = cleaned.size();
            for (int i = 0; i < cleaned.size(); ++i) {
                if (cleaned[i].kind == MixerStripKind::Master) {
                    masterIdx = i;
                    break;
                }
            }
            cleaned.insert(masterIdx, {MixerStripKind::AssignableFx, b.id});
        }
    }
    if (!hasMaster) {
        cleaned.push_back({MixerStripKind::Master, 0});
    }
    if (!isValidMixerStripOrder(cleaned)) {
        rebuildDefaultMixerStripOrder();
        return;
    }
    m_mixerStripOrder = cleaned;
}

bool ProjectModel::hasMixerExtras() const
{
    return !m_mixerBuses.isEmpty() || !m_mixerInputBuses.isEmpty() || !m_assignableFx.isEmpty();
}

bool ProjectModel::isValidMixerStripOrder(const QVector<MixerStripRef> &order)
{
    bool seenNonAudio = false;
    int masters = 0;
    for (const MixerStripRef &r : order) {
        if (r.kind == MixerStripKind::AudioTrack) {
            if (seenNonAudio) {
                return false;
            }
        } else {
            seenNonAudio = true;
        }
        if (r.kind == MixerStripKind::Master) {
            ++masters;
        }
    }
    return masters == 1;
}

bool ProjectModel::moveMixerStrip(int fromIndex, int insertIndex)
{
    ensureMixerStripOrder();
    if (fromIndex < 0 || fromIndex >= m_mixerStripOrder.size()) {
        return false;
    }
    insertIndex = qBound(0, insertIndex, int(m_mixerStripOrder.size()));
    if (insertIndex == fromIndex || insertIndex == fromIndex + 1) {
        return false; // no-op
    }
    QVector<MixerStripRef> next = m_mixerStripOrder;
    const MixerStripRef item = next.takeAt(fromIndex);
    int dest = insertIndex;
    if (dest > fromIndex) {
        --dest;
    }
    dest = qBound(0, dest, int(next.size()));
    next.insert(dest, item);
    if (!isValidMixerStripOrder(next)) {
        return false;
    }
    m_mixerStripOrder = next;
    return true;
}

bool ProjectModel::removeTrackIfEmpty(int trackIndex)
{
    if (trackIndex < 0 || trackIndex >= m_tracks.size()) {
        return false;
    }
    if (!m_tracks[trackIndex].events.isEmpty()) {
        return false;
    }
    // Keep at least one track of each kind if others exist? Allow empty removal freely.
    m_tracks.removeAt(trackIndex);
    return true;
}

int ProjectModel::addGroupedAvMedia(const QString &name, double startSec, double lengthSec,
                                    double fadeInSec, double fadeOutSec, int preferTrack,
                                    const QString &mediaPath)
{
    const int channels = mediaPath.isEmpty()
                             ? 2
                             : MediaWaveformCache::instance().audioChannelCountHint(mediaPath);
    const QVector<AudioLane> lanes = vegasAudioLanes(channels > 0 ? channels : 2);

    int vi = -1;
    QVector<int> audioTracks;
    audioTracks.reserve(lanes.size());

    auto nextAudioTrackAfter = [&](int afterIndex) -> int {
        for (int i = afterIndex + 1; i < m_tracks.size(); ++i) {
            if (m_tracks[i].kind == TrackKind::Audio) {
                return i;
            }
        }
        return addTrack(TrackKind::Audio);
    };

    if (preferTrack == kDropCreateNewTracks) {
        vi = addTrack(TrackKind::Video);
        for (int i = 0; i < lanes.size(); ++i) {
            audioTracks.push_back(addTrack(TrackKind::Audio));
        }
    } else if (preferTrack >= 0 && preferTrack < m_tracks.size()) {
        if (m_tracks[preferTrack].kind == TrackKind::Video) {
            vi = preferTrack;
            int cursor = preferTrack;
            for (int i = 0; i < lanes.size(); ++i) {
                cursor = nextAudioTrackAfter(cursor);
                audioTracks.push_back(cursor);
            }
        } else {
            // Dropped on an audio header: that track is first lane; video above or create
            if (preferTrack > 0 && m_tracks[preferTrack - 1].kind == TrackKind::Video) {
                vi = preferTrack - 1;
            } else {
                vi = ensureTrack(TrackKind::Video);
            }
            audioTracks.push_back(preferTrack);
            int cursor = preferTrack;
            for (int i = 1; i < lanes.size(); ++i) {
                cursor = nextAudioTrackAfter(cursor);
                audioTracks.push_back(cursor);
            }
        }
    } else {
        vi = ensureTrack(TrackKind::Video);
        // Reuse trailing empty audio tracks when possible (Vegas-like on empty project)
        QVector<int> empties;
        for (int i = 0; i < m_tracks.size(); ++i) {
            if (m_tracks[i].kind == TrackKind::Audio && m_tracks[i].events.isEmpty()) {
                empties.push_back(i);
            }
        }
        int cursor = vi;
        for (int i = 0; i < lanes.size(); ++i) {
            if (i < empties.size()) {
                audioTracks.push_back(empties[i]);
                cursor = empties[i];
            } else {
                cursor = nextAudioTrackAfter(cursor);
                audioTracks.push_back(cursor);
            }
        }
    }

    const int gid = m_nextGroupId++;
    const double t0 = std::max(0.0, startSec);
    const double len = std::max(0.05, lengthSec);

    TrackEvent ve;
    ve.id = m_nextEventId++;
    ve.name = name;
    ve.mediaPath = mediaPath;
    ve.startSec = t0;
    ve.lengthSec = len;
    ve.fadeInSec = fadeInSec;
    ve.fadeOutSec = fadeOutSec;
    ve.mediaKind = EventMediaKind::Video;
    ve.fxChain = {makeFxSlot(QStringLiteral("Pan/Crop"), PluginFormat::Builtin)};
    ve.groupId = gid;
    m_tracks[vi].events.push_back(ve);

    for (int i = 0; i < lanes.size(); ++i) {
        TrackEvent ae;
        ae.id = m_nextEventId++;
        ae.name = name;
        ae.mediaPath = mediaPath;
        ae.startSec = t0;
        ae.lengthSec = len;
        ae.fadeInSec = fadeInSec;
        ae.fadeOutSec = fadeOutSec;
        ae.mediaKind = EventMediaKind::Audio;
        ae.opacity = 1.0;
        ae.gainDb = 0.0;
        ae.groupId = gid;
        ae.firstChannel = lanes[i].first;
        ae.channelCount = lanes[i].count;
        m_tracks[audioTracks[i]].events.push_back(ae);
    }
    return ve.id;
}

namespace {
FxSlot titlesTextSlotFor(const TitlesTextParams &p);
} // namespace

int ProjectModel::addMediaAt(const QString &name, const QString &kind, double startSec,
                             double lengthSec, int preferTrack, const QString &mediaPath,
                             const QString &extra)
{
    const QString k = kind.toLower();
    const QString path = mediaPath;
    const double resolvedLen = MediaProbe::lengthForInsert(path, k, lengthSec);

    if (k == QLatin1String("audio")) {
        const int channels = path.isEmpty()
                                 ? 2
                                 : MediaWaveformCache::instance().audioChannelCountHint(path);
        const QVector<AudioLane> lanes = vegasAudioLanes(channels > 0 ? channels : 2);

        QVector<int> audioTracks;
        if (preferTrack == kDropCreateNewTracks) {
            for (int i = 0; i < lanes.size(); ++i) {
                audioTracks.push_back(addTrack(TrackKind::Audio));
            }
        } else {
            int first = preferTrack;
            if (first < 0 || first >= m_tracks.size() || m_tracks[first].kind != TrackKind::Audio) {
                first = ensureTrack(TrackKind::Audio);
            }
            audioTracks.push_back(first);
            int cursor = first;
            for (int i = 1; i < lanes.size(); ++i) {
                int next = -1;
                for (int t = cursor + 1; t < m_tracks.size(); ++t) {
                    if (m_tracks[t].kind == TrackKind::Audio) {
                        next = t;
                        break;
                    }
                }
                if (next < 0) {
                    next = addTrack(TrackKind::Audio);
                }
                cursor = next;
                audioTracks.push_back(cursor);
            }
        }

        const int gid = lanes.size() > 1 ? m_nextGroupId++ : 0;
        int firstId = -1;
        for (int i = 0; i < lanes.size(); ++i) {
            TrackEvent ae;
            ae.id = m_nextEventId++;
            ae.name = name;
            ae.mediaPath = path;
            ae.startSec = std::max(0.0, startSec);
            ae.lengthSec = std::max(0.05, resolvedLen);
            ae.mediaKind = EventMediaKind::Audio;
            ae.firstChannel = lanes[i].first;
            ae.channelCount = lanes[i].count;
            ae.groupId = gid;
            m_tracks[audioTracks[i]].events.push_back(ae);
            if (firstId < 0) {
                firstId = ae.id;
            }
        }
        return firstId;
    }
    if (k == QLatin1String("titles")) {
        // Generator events land on a video track, same placement rule as stills
        int vi = preferTrack;
        if (preferTrack == kDropCreateNewTracks) {
            vi = addTrack(TrackKind::Video);
        } else if (vi < 0 || vi >= m_tracks.size() || m_tracks[vi].kind != TrackKind::Video) {
            vi = ensureTrack(TrackKind::Video);
        }
        TitlesTextParams p; // defaults: "Sample Text", Verdana 48pt, white, centered, no animation
        if (!extra.isEmpty()) {
            p.animationName = extra;
            // Real Vegas starting text/background color / scale for this preset (see
            // video/TitlesTextApply.h's titlesTextPresetVisuals()).
            const TitlesTextPresetVisuals visuals = titlesTextPresetVisuals(extra);
            p.textColor = visuals.textColor;
            p.backgroundColor = visuals.backgroundColor;
            p.scale = visuals.scale;
        }
        if (!name.isEmpty()) {
            p.text = name;
        }
        TrackEvent te;
        te.id = m_nextEventId++;
        // Track/tooltip label stays single-line even when the preset's real sample text
        // (e.g. a Title-N marketing line) spans multiple lines.
        te.name = name.isEmpty() ? QStringLiteral("VEGAS Titles & Text")
                                 : name.section(QLatin1Char('\n'), 0, 0).left(60);
        te.startSec = std::max(0.0, startSec);
        te.lengthSec = std::max(0.05, resolvedLen);
        te.mediaKind = EventMediaKind::Title;
        te.fxChain = {titlesTextSlotFor(p)};
        m_tracks[vi].events.push_back(te);
        return te.id;
    }
    if (k == QLatin1String("generator")) {
        // Non-text Media Generator preset (Checkerboard, Color Gradient, …) — same
        // path-less placement rule as "titles": new/existing video track, no paired audio.
        int vi = preferTrack;
        if (preferTrack == kDropCreateNewTracks) {
            vi = addTrack(TrackKind::Video);
        } else if (vi < 0 || vi >= m_tracks.size() || m_tracks[vi].kind != TrackKind::Video) {
            vi = ensureTrack(TrackKind::Video);
        }
        const MediaGeneratorParams gp = mediaGeneratorParamsFromPayload(extra);
        TrackEvent ge;
        ge.id = m_nextEventId++;
        ge.name = name.isEmpty() ? gp.pluginName : name;
        ge.startSec = std::max(0.0, startSec);
        ge.lengthSec = std::max(0.05, resolvedLen);
        ge.mediaKind = EventMediaKind::Title; // generated-media family — see isVideoFamily()
        ge.fxChain = {mediaGeneratorSlotFor(gp)};
        m_tracks[vi].events.push_back(ge);
        return ge.id;
    }
    if (k == QLatin1String("still") || k == QLatin1String("image")) {
        // Images always land on a video track (Vegas still events)
        int vi = preferTrack;
        if (preferTrack == kDropCreateNewTracks) {
            vi = addTrack(TrackKind::Video);
        } else if (vi < 0 || vi >= m_tracks.size() || m_tracks[vi].kind != TrackKind::Video) {
            // Dropped over an audio track / empty area → first video track or create one
            vi = ensureTrack(TrackKind::Video);
        }
        TrackEvent se;
        se.id = m_nextEventId++;
        se.name = name;
        se.mediaPath = path;
        se.startSec = std::max(0.0, startSec);
        se.lengthSec = std::max(0.05, resolvedLen);
        se.mediaKind = EventMediaKind::Still;
        se.fxChain = {makeFxSlot(QStringLiteral("Pan/Crop"), PluginFormat::Builtin)};
        m_tracks[vi].events.push_back(se);
        return se.id;
    }
    // Default: video file → linked video + audio group
    return addGroupedAvMedia(name, startSec, resolvedLen, 0.0, 0.0, preferTrack, path);
}

int ProjectModel::applyInterchangeEvents(const InterchangeResult &result,
                                         const QString &resolveAgainstPath)
{
    if (result.events.isEmpty()) {
        return 0;
    }

    QVector<int> videoTrackIds;
    QVector<int> audioTrackIds;

    auto trackForVegasId = [&](TrackKind kind, int vegasTrack) -> int {
        QVector<int> &map = (kind == TrackKind::Video) ? videoTrackIds : audioTrackIds;
        for (int i = 0; i < map.size(); ++i) {
            if (map[i] == vegasTrack) {
                int seen = 0;
                for (int ti = 0; ti < m_tracks.size(); ++ti) {
                    if (m_tracks[ti].kind == kind) {
                        if (seen == i) {
                            return ti;
                        }
                        ++seen;
                    }
                }
            }
        }
        map.push_back(vegasTrack);
        return addTrack(kind);
    };

    int added = 0;
    for (const InterchangeEvent &ev : result.events) {
        if (ev.kind == QLatin1String("caption")) {
            addMarkerAt(ev.startSec, ev.name);
            continue;
        }
        const bool isAudio = (ev.kind == QLatin1String("audio"));
        const bool isStill = (ev.kind == QLatin1String("still")
                              || ev.kind == QLatin1String("image"));
        const TrackKind kind = isAudio ? TrackKind::Audio : TrackKind::Video;
        const int ti = trackForVegasId(kind, ev.trackIndex);
        TrackEvent te;
        te.id = m_nextEventId++;
        te.name = ev.name.isEmpty()
                      ? (isAudio ? QStringLiteral("Audio")
                                 : (isStill ? QStringLiteral("Still") : QStringLiteral("Video")))
                      : ev.name;
        te.mediaPath = ev.sourcePath.isEmpty()
                           ? QString()
                           : resolveMediaPath(ev.sourcePath, resolveAgainstPath);
        te.startSec = std::max(0.0, ev.startSec);
        te.lengthSec = std::max(0.05, ev.lengthSec);
        te.mediaStartSec = std::max(0.0, ev.mediaStartSec);
        te.mediaLengthSec = std::max(0.0, ev.mediaLengthSec);
        te.looped = ev.looped;
        te.reversed = (ev.playRate < 0.0);
        te.fadeInSec = std::max(0.0, ev.fadeInSec);
        te.fadeOutSec = std::max(0.0, ev.fadeOutSec);
        if (te.fadeInSec + te.fadeOutSec > te.lengthSec) {
            const double scale = te.lengthSec / (te.fadeInSec + te.fadeOutSec);
            te.fadeInSec *= scale;
            te.fadeOutSec *= scale;
        }
        te.fadeInCurve = ev.fadeInCurve;
        te.fadeOutCurve = ev.fadeOutCurve;
        te.mediaKind = isAudio ? EventMediaKind::Audio
                               : (isStill ? EventMediaKind::Still : EventMediaKind::Video);
        if (isAudio) {
            te.firstChannel = ev.firstChannel;
            te.channelCount = ev.channelCount > 0 ? ev.channelCount : 2;
            if (ev.hasSustainGain) {
                if (ev.sustainGain <= 1e-6) {
                    te.gainDb = -40.0;
                } else {
                    te.gainDb = std::clamp(20.0 * std::log10(ev.sustainGain), -40.0, 12.0);
                }
            }
        } else {
            te.fxChain = {makeFxSlot(QStringLiteral("Pan/Crop"), PluginFormat::Builtin)};
            if (ev.hasSustainGain) {
                te.opacity = std::clamp(ev.sustainGain, 0.0, 1.0);
            }
        }
        m_tracks[ti].events.push_back(te);
        ++added;
    }

    // Pair A/V with matching timing into groups (skip stills).
    for (Track &vt : m_tracks) {
        if (vt.kind != TrackKind::Video) {
            continue;
        }
        for (TrackEvent &ve : vt.events) {
            if (ve.groupId > 0 || ve.mediaKind == EventMediaKind::Still) {
                continue;
            }
            int gid = 0;
            for (Track &at : m_tracks) {
                if (at.kind != TrackKind::Audio) {
                    continue;
                }
                for (TrackEvent &ae : at.events) {
                    if (ae.groupId > 0) {
                        continue;
                    }
                    if (std::abs(ae.startSec - ve.startSec) < 0.05
                        && std::abs(ae.lengthSec - ve.lengthSec) < 0.05) {
                        if (gid == 0) {
                            gid = m_nextGroupId++;
                            ve.groupId = gid;
                        }
                        ae.groupId = gid;
                    }
                }
            }
        }
    }
    return added;
}

int ProjectModel::relinkMedia(const QString &oldPath, const QString &newPath)
{
    if (newPath.isEmpty() || !QFileInfo::exists(newPath)) {
        return 0;
    }
    const QString newClean = QDir::cleanPath(newPath);
    const QString oldClean = QDir::cleanPath(oldPath);
    const QString oldName = QFileInfo(oldPath).fileName();
    const QString oldBase = QFileInfo(oldName).completeBaseName();
    const QString newName = QFileInfo(newClean).fileName();
    int n = 0;

    auto pathMatches = [&](const QString &p) {
        if (p.isEmpty()) {
            return false;
        }
        const QString c = QDir::cleanPath(p);
        if (!oldClean.isEmpty() && c.compare(oldClean, Qt::CaseInsensitive) == 0) {
            return true;
        }
        return !oldName.isEmpty()
               && QFileInfo(c).fileName().compare(oldName, Qt::CaseInsensitive) == 0;
    };

    for (MediaItem &m : m_mediaPool) {
        if (!pathMatches(m.path)
            && m.displayName.compare(oldName, Qt::CaseInsensitive) != 0) {
            continue;
        }
        m.path = newClean;
        m.displayName = newName;
        m.kind = guessKindFromPath(newClean);
        m.missing = false;
        ++n;
    }

    for (Track &tr : m_tracks) {
        for (TrackEvent &ev : tr.events) {
            const bool byPath = pathMatches(ev.mediaPath);
            const bool byName =
                !oldName.isEmpty()
                && (QFileInfo(ev.name).fileName().compare(oldName, Qt::CaseInsensitive) == 0
                    || QFileInfo(ev.name).completeBaseName().compare(oldBase, Qt::CaseInsensitive)
                           == 0);
            if (!byPath && !byName) {
                continue;
            }
            ev.mediaPath = newClean;
            ++n;
        }
    }
    return n;
}

QStringList ProjectModel::missingMediaPaths() const
{
    QStringList out;
    for (const MediaItem &m : m_mediaPool) {
        // Prefer the explicit flag (Ignore clears it to suppress re-prompts).
        if (m.missing) {
            out << m.path;
        }
    }
    out.removeDuplicates();
    return out;
}

MediaItem *ProjectModel::findMediaItemByPath(const QString &path)
{
    if (path.isEmpty()) {
        return nullptr;
    }
    const QString clean = QDir::cleanPath(path);
    const QString name = QFileInfo(path).fileName();
    for (MediaItem &m : m_mediaPool) {
        if (!clean.isEmpty()
            && QDir::cleanPath(m.path).compare(clean, Qt::CaseInsensitive) == 0) {
            return &m;
        }
        if (!name.isEmpty()
            && QFileInfo(m.path).fileName().compare(name, Qt::CaseInsensitive) == 0) {
            return &m;
        }
        if (!name.isEmpty() && m.displayName.compare(name, Qt::CaseInsensitive) == 0) {
            return &m;
        }
    }
    return nullptr;
}

const MediaItem *ProjectModel::findMediaItemByPath(const QString &path) const
{
    return const_cast<ProjectModel *>(this)->findMediaItemByPath(path);
}

MediaItem *ProjectModel::ensureMediaItem(const QString &path, const QString &displayName,
                                         const QString &kind)
{
    if (MediaItem *existing = findMediaItemByPath(path)) {
        return existing;
    }
    if (path.isEmpty() && displayName.isEmpty()) {
        return nullptr;
    }
    MediaItem item;
    item.path = path;
    item.displayName = displayName.isEmpty() ? QFileInfo(path).fileName() : displayName;
    item.kind = kind.isEmpty() ? guessKindFromPath(path.isEmpty() ? displayName : path) : kind;
    item.missing = !path.isEmpty() && !QFileInfo::exists(path);
    m_mediaPool.push_back(item);
    return &m_mediaPool.last();
}

int ProjectModel::detectBeatsIntoMediaItem(MediaItem *item, double t0, double t1)
{
    if (!item || item->path.isEmpty()) {
        return 0;
    }
    const WaveformPeaks peaks = MediaWaveformCache::instance().peaksForBlocking(item->path);
    if (!peaks.isValid() || peaks.bins < 8 || peaks.durationSec < 0.05) {
        return 0;
    }
    if (t1 < 0.0) {
        t1 = peaks.durationSec;
    }
    t0 = std::clamp(t0, 0.0, peaks.durationSec);
    t1 = std::clamp(t1, t0, peaks.durationSec);

    QVector<double> energy(peaks.bins, 0.0);
    for (int b = 0; b < peaks.bins; ++b) {
        double e = 0.0;
        for (int ch = 0; ch < peaks.channels; ++ch) {
            const int idx = (b * peaks.channels + ch) * 2;
            if (idx + 1 >= peaks.minMax.size()) {
                continue;
            }
            const double mn = peaks.minMax[idx] / 32768.0;
            const double mx = peaks.minMax[idx + 1] / 32768.0;
            e = std::max(e, std::max(std::abs(mn), std::abs(mx)));
        }
        energy[b] = e;
    }
    QVector<double> sorted = energy;
    std::sort(sorted.begin(), sorted.end());
    const int threshIdx =
        std::clamp(int(sorted.size() * 0.72), 0, std::max(0, int(sorted.size()) - 1));
    const double thresh = sorted[threshIdx];
    const double minGap = 0.18;
    QVector<double> hits;
    double lastHit = -1e9;
    for (int b = 1; b < peaks.bins - 1; ++b) {
        const double t = (double(b) / peaks.bins) * peaks.durationSec;
        if (t < t0 || t > t1) {
            continue;
        }
        if (energy[b] < thresh) {
            continue;
        }
        if (energy[b] < energy[b - 1] || energy[b] < energy[b + 1]) {
            continue;
        }
        if (t - lastHit < minGap) {
            continue;
        }
        hits.push_back(t);
        lastHit = t;
    }
    if (hits.isEmpty()) {
        for (double t = t0; t <= t1 + 1e-6; t += 0.5) {
            hits.push_back(std::clamp(t, 0.0, peaks.durationSec));
        }
    }

    item->markers.clear();
    int id = 1;
    int num = 1;
    for (double t : hits) {
        TimelineMarker m;
        m.id = id++;
        m.number = num++;
        m.timeSec = t;
        item->markers.push_back(m);
    }
    return item->markers.size();
}

void ProjectModel::seedSampleAudioBeatMarkersIfNeeded(const QString &openedPath)
{
    const QString base = QFileInfo(openedPath).completeBaseName().toLower();
    if (!base.contains(QLatin1String("reverse-fades-fx"))) {
        return;
    }
    for (MediaItem &m : m_mediaPool) {
        const QString name = m.displayName.isEmpty() ? QFileInfo(m.path).fileName() : m.displayName;
        if (!name.contains(QLatin1String("sample_for_project_audio"), Qt::CaseInsensitive)) {
            continue;
        }
        if (!m.markers.isEmpty()) {
            continue;
        }
        detectBeatsIntoMediaItem(&m);
    }
}

QString ProjectModel::mediaPathForEvent(const TrackEvent &ev) const
{
    if (!ev.mediaPath.isEmpty() && QFileInfo::exists(ev.mediaPath)) {
        return ev.mediaPath;
    }

    // VegReader assignEventNames suffixes copies: "clip.4k 2" / "clip 3".
    auto stripCopyIndex = [](QString s) {
        s = s.trimmed();
        static const QRegularExpression re(QStringLiteral(R"(\s+\d+$)"));
        return s.remove(re).trimmed();
    };

    const QString stripped = stripCopyIndex(ev.name);
    const QString wantFile = QFileInfo(stripped).fileName();
    const QString wantBase = QFileInfo(stripped).completeBaseName();

    for (const MediaItem &m : m_mediaPool) {
        if (m.path.isEmpty() || !QFileInfo::exists(m.path)) {
            continue;
        }
        const QString file = QFileInfo(m.path).fileName();
        const QString base = QFileInfo(m.path).completeBaseName();
        if ((!stripped.isEmpty()
             && (stripped.compare(m.displayName, Qt::CaseInsensitive) == 0
                 || stripped.compare(file, Qt::CaseInsensitive) == 0
                 || stripped.compare(base, Qt::CaseInsensitive) == 0))
            || (!wantFile.isEmpty()
                && (wantFile.compare(m.displayName, Qt::CaseInsensitive) == 0
                    || wantFile.compare(file, Qt::CaseInsensitive) == 0
                    || wantFile.compare(base, Qt::CaseInsensitive) == 0))
            || (!wantBase.isEmpty()
                && (wantBase.compare(base, Qt::CaseInsensitive) == 0
                    || wantBase.compare(m.displayName, Qt::CaseInsensitive) == 0))
            || (!base.isEmpty()
                && (stripped.startsWith(base, Qt::CaseInsensitive)
                    || wantFile.startsWith(base, Qt::CaseInsensitive)))) {
            return m.path;
        }
    }
    return ev.mediaPath;
}

bool ProjectModel::applyVegImport(const VegOpenResult &veg, const QString &openedPath,
                                  bool allowEdlSidecar)
{
    loadEmptyProject();
    m_projectPath = openedPath;
    m_frameRate = veg.header.frameRate > 1.0 ? veg.header.frameRate : 29.97;
    m_sampleRate = veg.header.sampleRate ? veg.header.sampleRate : 48000;
    m_tempoBpm = veg.header.tempoBpm > 0.0 ? veg.header.tempoBpm : 120.0;
    m_frameWidth = veg.header.width > 0 ? veg.header.width : 1920;
    m_frameHeight = veg.header.height > 0 ? veg.header.height : 1080;

    QStringList videoFiles;
    QStringList audioFiles;
    QStringList stillFiles;

    auto addPoolPath = [&](const QString &raw) {
        MediaItem item;
        item.path = resolveMediaPath(raw, openedPath);
        item.displayName = QFileInfo(item.path).fileName();
        item.kind = guessKindFromPath(item.path);
        item.missing = !QFileInfo::exists(item.path);
        for (const MediaItem &m : m_mediaPool) {
            if (QDir::cleanPath(m.path).compare(QDir::cleanPath(item.path), Qt::CaseInsensitive) == 0) {
                return;
            }
        }
        m_mediaPool.push_back(item);
        if (item.kind == QLatin1String("audio")) {
            audioFiles.push_back(item.displayName);
        } else if (item.kind == QLatin1String("still")) {
            stillFiles.push_back(item.displayName);
        } else {
            videoFiles.push_back(item.displayName);
        }
    };

    for (const QString &raw : veg.mediaPaths) {
        addPoolPath(raw);
    }

    auto eventNameMatchesStill = [](const QString &eventName, const QString &stillFileName) {
        const QString a = QFileInfo(eventName).completeBaseName();
        const QString b = QFileInfo(stillFileName).completeBaseName();
        return a.compare(b, Qt::CaseInsensitive) == 0
               || eventName.compare(stillFileName, Qt::CaseInsensitive) == 0;
    };

    auto appendMissingStills = [&]() {
        if (stillFiles.isEmpty()) {
            return;
        }
        const int vi = ensureTrack(TrackKind::Video);
        double t = 0.0;
        for (const TrackEvent &ev : m_tracks[vi].events) {
            t = std::max(t, ev.startSec + ev.lengthSec);
        }
        for (const QString &name : stillFiles) {
            bool already = false;
            for (const Track &tr : m_tracks) {
                for (const TrackEvent &ev : tr.events) {
                    if (eventNameMatchesStill(ev.name, name)) {
                        already = true;
                        break;
                    }
                }
                if (already) {
                    break;
                }
            }
            if (already) {
                continue;
            }
            TrackEvent se;
            se.id = m_nextEventId++;
            se.name = name;
            // Resolve still path from media pool by display name
            for (const MediaItem &m : m_mediaPool) {
                if (m.displayName.compare(name, Qt::CaseInsensitive) == 0
                    || QFileInfo(m.path).fileName().compare(name, Qt::CaseInsensitive) == 0) {
                    se.mediaPath = m.path;
                    break;
                }
            }
            se.startSec = t;
            se.lengthSec = kDefaultStillLengthSec;
            se.mediaKind = EventMediaKind::Still;
            se.fxChain = {makeFxSlot(QStringLiteral("Pan/Crop"), PluginFormat::Builtin)};
            m_tracks[vi].events.push_back(se);
            t += kDefaultStillLengthSec;
        }
    };

    auto pathForName = [&](const QString &name) -> QString {
        TrackEvent tmp;
        tmp.name = name;
        return mediaPathForEvent(tmp);
    };
    auto backfillEventMediaPaths = [&]() {
        for (Track &tr : m_tracks) {
            for (TrackEvent &ev : tr.events) {
                if (!ev.mediaPath.isEmpty() && QFileInfo::exists(ev.mediaPath)) {
                    continue;
                }
                const QString p = mediaPathForEvent(ev);
                if (!p.isEmpty()) {
                    ev.mediaPath = p;
                }
            }
        }
    };

    // Ruler markers from binary .veg (EDL CSV has no markers)
    for (const VegMarkerInfo &mk : veg.markers) {
        addMarkerAt(mk.timeSec, mk.label);
    }

    // --- Preferred: Vegas EDL CSV sidecar (veg_project/edl-text-file/<name>.txt) ---
    // Renamed copies (e.g. Downloads/1-просто-видео.veg) still embed the original
    // project path - use that basename so EDL from SAMPLES/veg_project still matches.
    QStringList edlAltNames;
    if (!veg.projectPathHint.isEmpty()) {
        edlAltNames << veg.projectPathHint;
    }
    const QString edlPath =
        allowEdlSidecar ? SamplePaths::sidecarEdlPath(openedPath, edlAltNames) : QString();
    if (!edlPath.isEmpty()) {
        const InterchangeResult edl = ProjectInterchange::importVegasCsvEdl(edlPath, nullptr);
        for (const InterchangeMediaRef &m : edl.media) {
            addPoolPath(m.path);
        }
        if (!edl.events.isEmpty()) {
            QVector<int> videoTrackIds;
            QVector<int> audioTrackIds;

            auto trackForVegasId = [&](TrackKind kind, int vegasTrack) -> int {
                QVector<int> &map = (kind == TrackKind::Video) ? videoTrackIds : audioTrackIds;
                for (int i = 0; i < map.size(); ++i) {
                    if (map[i] == vegasTrack) {
                        int seen = 0;
                        for (int ti = 0; ti < m_tracks.size(); ++ti) {
                            if (m_tracks[ti].kind == kind) {
                                if (seen == i) {
                                    return ti;
                                }
                                ++seen;
                            }
                        }
                    }
                }
                map.push_back(vegasTrack);
                return addTrack(kind);
            };

            bool appliedVideoEventFx = false;
            for (const InterchangeEvent &ev : edl.events) {
                const bool isAudio = (ev.kind == QLatin1String("audio"));
                const bool isStill = (ev.kind == QLatin1String("still")
                                      || ev.kind == QLatin1String("image"));
                const TrackKind kind = isAudio ? TrackKind::Audio : TrackKind::Video;
                const int ti = trackForVegasId(kind, ev.trackIndex);
                TrackEvent te;
                te.id = m_nextEventId++;
                te.name = ev.name.isEmpty()
                              ? (isAudio ? QStringLiteral("Audio")
                                         : (isStill ? QStringLiteral("Still") : QStringLiteral("Video")))
                              : ev.name;
                te.mediaPath = ev.sourcePath.isEmpty()
                                   ? QString()
                                   : resolveMediaPath(ev.sourcePath, openedPath);
                te.startSec = ev.startSec;
                te.lengthSec = std::max(0.05, ev.lengthSec);
                te.mediaStartSec = std::max(0.0, ev.mediaStartSec);
                te.mediaLengthSec = std::max(0.0, ev.mediaLengthSec);
                te.looped = ev.looped;
                te.fadeInSec = std::max(0.0, ev.fadeInSec);
                te.fadeOutSec = std::max(0.0, ev.fadeOutSec);
                if (te.fadeInSec + te.fadeOutSec > te.lengthSec) {
                    const double scale = te.lengthSec / (te.fadeInSec + te.fadeOutSec);
                    te.fadeInSec *= scale;
                    te.fadeOutSec *= scale;
                }
                te.fadeInCurve = ev.fadeInCurve;
                te.fadeOutCurve = ev.fadeOutCurve;
                te.mediaKind = isAudio ? EventMediaKind::Audio
                                       : (isStill ? EventMediaKind::Still : EventMediaKind::Video);
                {
                    const QString base = QFileInfo(te.mediaPath.isEmpty() ? te.name : te.mediaPath)
                                             .fileName()
                                             .toLower();
                    const QString ext = QFileInfo(base).suffix().toLower();
                    const bool videoContainer =
                        (ext == QLatin1String("mp4") || ext == QLatin1String("mov")
                         || ext == QLatin1String("mkv") || ext == QLatin1String("avi")
                         || ext == QLatin1String("m2ts") || ext == QLatin1String("mxf"));
                    // Reverse SubClip on an A/V file usually applies to the video take only
                    // (paired audio from the same mp4 stays forward - see FCPX timeMap).
                    // Same basename may also appear as a normal forward event (fx1: short
                    // reverse wav + second full-length forward wav) - do not reverse all.
                    bool reverseOk =
                        !base.isEmpty() && veg.reversedMediaBasenames.contains(base)
                        && !(isAudio && videoContainer);
                    if (reverseOk && isAudio) {
                        const double metaLen =
                            veg.reversedSubclipLengthSec.value(base, 0.0);
                        const bool suffixOrPartial =
                            te.mediaStartSec > 1e-3
                            || (metaLen > 1e-6 && te.mediaLengthSec > 1e-6
                                && te.mediaLengthSec < metaLen * 0.95);
                        const bool loopedPastEdge =
                            te.looped && te.mediaLengthSec > 1e-6
                            && te.lengthSec > te.mediaLengthSec * 1.5;
                        reverseOk = suffixOrPartial || loopedPastEdge;
                    }
                    if (reverseOk || ev.playRate < 0.0) {
                        te.reversed = true;
                        const double metaStart = veg.reversedSubclipStartSec.value(base, -1.0);
                        const double metaLen = veg.reversedSubclipLengthSec.value(base, 0.0);
                        // META start=0: media is a reverse SubClip of length metaLen.
                        // EDL StreamStart is the in-point on that reversed item (keep it).
                        // Cycle must be the full reverse-subclip length so
                        // source = cycle - fmod(StreamStart + local, cycle) matches FCPX timeMap
                        // (wav fx: start~2.1s going backwards with wrap; not silence at file end).
                        if (metaStart >= 0.0 && metaStart < 1e-6 && metaLen > 1e-6) {
                            if (te.mediaLengthSec < 1e-6 || metaLen > te.mediaLengthSec + 1e-3) {
                                te.mediaLengthSec = metaLen;
                            }
                        }
                    }
                }
                if (isAudio) {
                    te.firstChannel = ev.firstChannel;
                    te.channelCount = ev.channelCount > 0 ? ev.channelCount : 2;
                    if (ev.hasSustainGain) {
                        // Linear amplitude → dB (0 → -Inf / UI floor -40 dB, Vegas-like)
                        if (ev.sustainGain <= 1e-6) {
                            te.gainDb = -40.0;
                        } else {
                            te.gainDb = 20.0 * std::log10(ev.sustainGain);
                            te.gainDb = std::clamp(te.gainDb, -40.0, 12.0);
                        }
                    }
                    // Audio Event FX from .veg belong on dedicated audio clips (e.g. wav),
                    // not on A/V-paired stream from a video file.
                    const QString ext = QFileInfo(te.mediaPath).suffix().toLower();
                    const bool audioOnlyFile = (ext == QLatin1String("wav")
                                                || ext == QLatin1String("flac")
                                                || ext == QLatin1String("mp3")
                                                || ext == QLatin1String("ogg")
                                                || ext == QLatin1String("aif")
                                                || ext == QLatin1String("aiff")
                                                || ext == QLatin1String("wma"));
                    if (audioOnlyFile) {
                        for (const QString &fx : veg.audioEventFxNames) {
                            te.fxChain.push_back(fxSlotFromVegWithState(fx, veg));
                        }
                    }
                } else {
                    te.fxChain = {makeFxSlot(QStringLiteral("Pan/Crop"), PluginFormat::Builtin)};
                    if (ev.hasSustainGain) {
                        te.opacity = std::clamp(ev.sustainGain, 0.0, 1.0);
                    }
                    if (!appliedVideoEventFx && !veg.eventFxNames.isEmpty()) {
                        for (const QString &fx : veg.eventFxNames) {
                            appendVegFxSlot(&te, fx, veg);
                        }
                        appliedVideoEventFx = true;
                    }
                }
                m_tracks[ti].events.push_back(te);
            }

            // Apply VEGAS track FX names from binary strings onto audio tracks
            if (!veg.trackFxNames.isEmpty()) {
                QVector<FxSlot> mapped;
                for (const QString &raw : veg.trackFxNames) {
                    mapped.push_back(fxSlotFromVegWithState(raw, veg));
                }
                for (Track &tr : m_tracks) {
                    if (tr.kind == TrackKind::Audio) {
                        tr.fxChain = mapped;
                    }
                }
            }

            // Pair A/V with matching timing into groups (skip stills).
            // Multichannel audio (e.g. 5.1 → 4 tracks) shares one group with the video.
            for (Track &vt : m_tracks) {
                if (vt.kind != TrackKind::Video) {
                    continue;
                }
                for (TrackEvent &ve : vt.events) {
                    if (ve.groupId > 0 || ve.mediaKind == EventMediaKind::Still) {
                        continue;
                    }
                    int gid = 0;
                    for (Track &at : m_tracks) {
                        if (at.kind != TrackKind::Audio) {
                            continue;
                        }
                        for (TrackEvent &ae : at.events) {
                            if (ae.groupId > 0) {
                                continue;
                            }
                            if (std::abs(ae.startSec - ve.startSec) < 0.05
                                && std::abs(ae.lengthSec - ve.lengthSec) < 0.05) {
                                if (gid == 0) {
                                    gid = m_nextGroupId++;
                                    ve.groupId = gid;
                                }
                                ae.groupId = gid;
                            }
                        }
                    }
                }
            }

            // EDL is authoritative for timeline - do not re-append stillFiles from media pool
            // (that duplicated sample_for_project_pictures: 2 EDL stills + 2 pool stills).
            applyAudioEventFxFromVeg(veg);
            applyMixerChannelsFromVeg(veg);
            applyTrackMotionFromVeg(veg);
            applyPanCropFromVeg(veg);
            applyColorGradingFromVeg(veg);
            applyVideoTrackFxFromVeg(veg);
            applyTitlesTextFromVeg(veg);
            applyTransitionsFromVeg(veg);
            applyDefaultTrackDisplayColors();
            backfillEventMediaPaths();
            seedSampleAudioBeatMarkersIfNeeded(openedPath);
            return true;
        }
    }

    // --- Binary timeline timings (VegReader v1) ---
    if (veg.hasTimelineTimings && !veg.events.isEmpty()) {
        QVector<VegEventInfo> videoEv;
        QVector<VegEventInfo> audioEv;
        for (const VegEventInfo &ev : veg.events) {
            if (ev.kind == VegEventInfo::Kind::Video) {
                videoEv.push_back(ev);
            } else if (ev.kind == VegEventInfo::Kind::Audio) {
                audioEv.push_back(ev);
            }
        }

        QVector<bool> audioUsed(audioEv.size(), false);
        auto nearlyEqual = [](double a, double b, double eps = 0.05) {
            return std::abs(a - b) <= eps;
        };

        QVector<double> vFadeIn(videoEv.size(), 0.0);
        QVector<double> vFadeOut(videoEv.size(), 0.0);
        for (int i = 0; i + 1 < videoEv.size(); ++i) {
            const double prevEnd = videoEv[i].startSec + videoEv[i].lengthSec;
            const double overlap = prevEnd - videoEv[i + 1].startSec;
            if (overlap > 0.05) {
                const double fade = std::min(overlap, std::min(videoEv[i].lengthSec,
                                                               videoEv[i + 1].lengthSec) * 0.5);
                vFadeOut[i] = fade;
                vFadeIn[i + 1] = fade;
            }
        }

        // A "Video kind" timing record with no name isn't necessarily a real video clip —
        // Titles & Text generator instances (and other non-file events) share the same
        // binary timeline position records, and parseVideoTitlesText() pairs each
        // recovered instance with one of them by ascending start time (see
        // VegTitleTextInfo doc). Guessing a pooled video file for those positions was
        // wrong on two counts: it mislabeled generator-only projects as having real video
        // clips, and it gave applyTitlesTextFromVeg() no blank ("no media") placeholder to
        // convert in place, so it fell back to a second, separate video track for every
        // instance — the same "2 tracks instead of 1" bug already fixed for the EDL
        // sidecar path, just reachable here too when no sidecar exists.
        auto isGeneratorSlot = [&](double startSec) {
            for (const VegTitleTextInfo &t : veg.titlesAndText) {
                if (std::abs(t.startSec - startSec) < 0.05) {
                    return true;
                }
            }
            return false;
        };
        for (int vi = 0; vi < videoEv.size(); ++vi) {
            const VegEventInfo &ve = videoEv[vi];
            int matchAi = -1;
            for (int ai = 0; ai < audioEv.size(); ++ai) {
                if (audioUsed[ai]) {
                    continue;
                }
                const VegEventInfo &ae = audioEv[ai];
                if (nearlyEqual(ae.startSec, ve.startSec) && nearlyEqual(ae.lengthSec, ve.lengthSec)
                    && nearlyEqual(ae.playbackRate, ve.playbackRate, 1e-3)) {
                    matchAi = ai;
                    break;
                }
            }
            // Real-clip-or-not can't be told from ve.name alone: VegReader's own timeline
            // scan (assignEventNames) already guesses a pooled media filename for every
            // "Video kind" position regardless of whether it's a real clip, so an empty
            // name here would never actually happen. Position match against the recovered
            // generator instances is the only reliable signal.
            const bool generatorSlot = matchAi < 0 && isGeneratorSlot(ve.startSec);

            const QString name = generatorSlot ? QStringLiteral("Video")
                                 : !ve.name.isEmpty() ? ve.name
                                 : (!videoFiles.isEmpty() ? videoFiles[vi % videoFiles.size()]
                                                         : QStringLiteral("Video %1").arg(vi + 1));
            if (matchAi >= 0) {
                audioUsed[matchAi] = true;
                addGroupedAvMedia(name, ve.startSec, ve.lengthSec, vFadeIn[vi], vFadeOut[vi], -1,
                                  pathForName(name));
            } else {
                const int track = ensureTrack(TrackKind::Video);
                TrackEvent te;
                te.id = m_nextEventId++;
                te.name = name;
                te.mediaPath = generatorSlot ? QString() : pathForName(name);
                te.startSec = ve.startSec;
                te.lengthSec = std::max(0.05, ve.lengthSec);
                te.fadeInSec = vFadeIn[vi];
                te.fadeOutSec = vFadeOut[vi];
                te.mediaKind = EventMediaKind::Video;
                te.fxChain = {makeFxSlot(QStringLiteral("Pan/Crop"), PluginFormat::Builtin)};
                if (!veg.eventFxNames.isEmpty() && vi == 0) {
                    for (const QString &fx : veg.eventFxNames) {
                        appendVegFxSlot(&te, fx, veg);
                    }
                }
                m_tracks[track].events.push_back(te);
            }
        }

        // Unmatched (audio-only) events all land on the same first audio track
        // (ensureTrack always returns it), so overlap-based crossfade recovery
        // mirrors the video pass above: any two neighboring standalone clips
        // that overlap on the timeline get a fade sized to that overlap.
        QVector<int> unmatchedAudio;
        for (int ai = 0; ai < audioEv.size(); ++ai) {
            if (!audioUsed[ai]) {
                unmatchedAudio.push_back(ai);
            }
        }
        std::sort(unmatchedAudio.begin(), unmatchedAudio.end(), [&](int a, int b) {
            return audioEv[a].startSec < audioEv[b].startSec;
        });
        QVector<double> aFadeIn(audioEv.size(), 0.0);
        QVector<double> aFadeOut(audioEv.size(), 0.0);
        for (int k = 0; k + 1 < unmatchedAudio.size(); ++k) {
            const int curIdx = unmatchedAudio[k];
            const int nextIdx = unmatchedAudio[k + 1];
            const VegEventInfo &cur = audioEv[curIdx];
            const VegEventInfo &next = audioEv[nextIdx];
            const double curEnd = cur.startSec + cur.lengthSec;
            const double overlap = curEnd - next.startSec;
            if (overlap > 0.05) {
                const double fade = std::min(overlap, std::min(cur.lengthSec, next.lengthSec) * 0.5);
                aFadeOut[curIdx] = fade;
                aFadeIn[nextIdx] = fade;
            }
        }

        for (int ai = 0; ai < audioEv.size(); ++ai) {
            if (audioUsed[ai]) {
                continue;
            }
            const VegEventInfo &ae = audioEv[ai];
            const int track = ensureTrack(TrackKind::Audio);
            TrackEvent te;
            te.id = m_nextEventId++;
            te.name = !ae.name.isEmpty() ? ae.name
                      : (!audioFiles.isEmpty() ? audioFiles[ai % audioFiles.size()]
                                               : QStringLiteral("Audio %1").arg(ai + 1));
            te.mediaPath = pathForName(te.name);
            te.startSec = ae.startSec;
            te.lengthSec = std::max(0.05, ae.lengthSec);
            te.fadeInSec = aFadeIn[ai];
            te.fadeOutSec = aFadeOut[ai];
            te.mediaKind = EventMediaKind::Audio;
            for (const QString &fx : veg.audioEventFxNames) {
                te.fxChain.push_back(fxSlotFromVegWithState(fx, veg));
            }
            m_tracks[track].events.push_back(te);
        }

        appendMissingStills();

        // Fit playhead / empty-project safety
        if (m_tracks.isEmpty() && !veg.eventLabels.isEmpty()) {
            const QString n = veg.eventLabels.first();
            addGroupedAvMedia(n, 0.0, 8.0, 0.0, 0.0, -1, pathForName(n));
        }
        applyAudioEventFxFromVeg(veg);
        applyMixerChannelsFromVeg(veg);
        applyTrackMotionFromVeg(veg);
        applyPanCropFromVeg(veg);
        applyColorGradingFromVeg(veg);
        applyVideoTrackFxFromVeg(veg);
        applyTitlesTextFromVeg(veg);
        applyTransitionsFromVeg(veg);
        applyDefaultTrackDisplayColors();
        backfillEventMediaPaths();
        seedSampleAudioBeatMarkersIfNeeded(openedPath);
        return false;
    }

    // --- Fallback: heuristic timeline (VegReader v0 behavior, improved) ---
    QStringList videoLabels;
    QStringList audioLabels;
    for (const QString &label : veg.eventLabels) {
        const QString lower = label.toLower();
        if (lower.contains(QLatin1String("audio")) || lower.endsWith(QLatin1String("_audio"))) {
            audioLabels.push_back(label);
        } else {
            videoLabels.push_back(label);
        }
    }
    if (videoLabels.isEmpty()) {
        videoLabels = videoFiles;
    }
    if (audioLabels.isEmpty()) {
        audioLabels = audioFiles;
    }

    const double defaultLen = 8.0;

    if (!videoLabels.isEmpty()) {
        double t = 0.0;
        for (int i = 0; i < videoLabels.size(); ++i) {
            const double len = defaultLen;
            const double step = (videoLabels.size() > 1) ? (len * 0.75) : len;
            addGroupedAvMedia(videoLabels[i], t, len, 0.0, 0.0, -1, pathForName(videoLabels[i]));
            t += step;
        }
    }

    appendMissingStills();

    if (!audioLabels.isEmpty() && videoLabels.isEmpty()) {
        const int ai = ensureTrack(TrackKind::Audio);
        double t = 0.0;
        for (const QString &name : audioLabels) {
            TrackEvent ae;
            ae.id = m_nextEventId++;
            ae.name = name;
            ae.mediaPath = pathForName(name);
            ae.startSec = t;
            ae.lengthSec = defaultLen + 2.0;
            ae.mediaKind = EventMediaKind::Audio;
            m_tracks[ai].events.push_back(ae);
            t += (audioLabels.size() > 1) ? (defaultLen * 0.75) : (defaultLen + 2.0);
        }
    }

    if (m_tracks.isEmpty() && !veg.eventLabels.isEmpty()) {
        double t = 0.0;
        for (const QString &name : veg.eventLabels) {
            addGroupedAvMedia(name, t, defaultLen, 0.0, 0.0, -1, pathForName(name));
            t += defaultLen * 0.75;
        }
    }
    applyAudioEventFxFromVeg(veg);
    applyMixerChannelsFromVeg(veg);
    applyTrackMotionFromVeg(veg);
    applyPanCropFromVeg(veg);
    applyColorGradingFromVeg(veg);
    applyVideoTrackFxFromVeg(veg);
    applyTitlesTextFromVeg(veg);
    applyTransitionsFromVeg(veg);
    applyDefaultTrackDisplayColors();
    backfillEventMediaPaths();
    seedSampleAudioBeatMarkersIfNeeded(openedPath);
    return false;
}

void ProjectModel::applyDefaultTrackDisplayColors()
{
    for (int i = 0; i < m_tracks.size(); ++i) {
        if (!m_tracks[i].displayColor.isValid()) {
            m_tracks[i].displayColor = TrackColors::at(i);
        }
    }
}

void ProjectModel::applyTrackMotionFromVeg(const VegOpenResult &veg)
{
    if (!veg.hasTrackMotion || veg.trackMotion.motionKeyframes.isEmpty()) {
        return;
    }
    for (Track &tr : m_tracks) {
        if (tr.kind != TrackKind::Video) {
            continue;
        }
        tr.motion = veg.trackMotion;
        const double aspect =
            (m_frameHeight > 0) ? double(m_frameWidth) / double(m_frameHeight) : (16.0 / 9.0);
        tr.motion.ensureDefault(aspect);
        return; // first video track (Vegas sample is single-track)
    }
}

void ProjectModel::applyPanCropFromVeg(const VegOpenResult &veg)
{
    if (!veg.hasEventPanCrop) {
        return;
    }
    if (veg.eventPanCrop.positionKeyframes.isEmpty()
        && veg.eventPanCrop.maskKeyframes.isEmpty()) {
        return;
    }
    const int fw = m_frameWidth > 0 ? m_frameWidth : 1920;
    const int fh = m_frameHeight > 0 ? m_frameHeight : 1080;
    for (Track &tr : m_tracks) {
        if (tr.kind != TrackKind::Video) {
            continue;
        }
        for (TrackEvent &ev : tr.events) {
            if (!isVideoFamily(ev.mediaKind)) {
                continue;
            }
            ev.panCrop = veg.eventPanCrop;
            // Keep Vegas Source defaults (stretch/aspect). Media-space KF mapping is in applyPanCrop.
            ev.panCrop.ensureDefault(fw, fh);
            ensureFxFirst(ev.fxChain, QStringLiteral("Pan/Crop"), PluginFormat::Builtin);
            return; // first video event
        }
    }
}

void ProjectModel::applyColorGradingFromVeg(const VegOpenResult &veg)
{
    if (!veg.hasColorGrading) {
        return;
    }
    for (Track &tr : m_tracks) {
        if (tr.kind != TrackKind::Video) {
            continue;
        }
        FxSlot slot = makeFxSlot(QStringLiteral("Color Grading"), PluginFormat::Builtin,
                                 QStringLiteral("builtin:Color Grading"));
        if (!veg.colorGradingParams.isEmpty()) {
            QByteArray ba;
            QDataStream out(&ba, QIODevice::WriteOnly);
            out.setVersion(QDataStream::Qt_6_0);
            out << veg.colorGradingParams;
            slot.state = ba;
        }
        const int existing = indexOfFxName(tr.fxChain, QStringLiteral("Color Grading"));
        if (existing >= 0) {
            tr.fxChain[existing] = slot;
        } else {
            tr.fxChain.push_back(slot);
        }
        return; // first video track
    }
}

void ProjectModel::applyVideoTrackFxFromVeg(const VegOpenResult &veg)
{
    if (veg.videoTrackFxNames.isEmpty()) {
        return;
    }
    for (Track &tr : m_tracks) {
        if (tr.kind != TrackKind::Video) {
            continue;
        }
        for (const QString &raw : veg.videoTrackFxNames) {
            const FxSlot slot = fxSlotFromVegWithState(raw, veg);
            if (indexOfFxName(tr.fxChain, slot.displayName) < 0) {
                tr.fxChain.push_back(slot);
                tr.automationLanes +=
                    legacyFxAutomationLanes(slot, veg, legacyFxKeyForSlot(slot));
            }
        }
        return; // first video track
    }
}

namespace {
TitlesTextParams titlesTextParamsFromVeg(const VegTitleTextInfo &src)
{
    TitlesTextParams p;
    p.text = src.text;
    p.fontFamily = src.fontFamily;
    p.fontSize = src.fontSize;
    p.bold = src.bold;
    p.italic = src.italic;
    p.alignment = static_cast<TitlesTextAlignment>(std::clamp(src.alignment, 0, 2));
    p.textColor = src.textColor;
    p.animationName = src.animationName;
    p.scale = src.scale;
    p.locationX = src.locationX;
    p.locationY = src.locationY;
    p.cropBackgroundToText = src.cropBackgroundToText;
    p.backgroundColor = src.backgroundColor;
    p.tracking = src.tracking;
    p.lineSpacing = src.lineSpacing;
    p.outlineWidth = src.outlineWidth;
    p.outlineColor = src.outlineColor;
    p.shadowEnable = src.shadowEnable;
    p.shadowColor = src.shadowColor;
    p.shadowOffsetX = src.shadowOffsetX;
    p.shadowOffsetY = src.shadowOffsetY;
    p.shadowBlur = src.shadowBlur;
    return p;
}

FxSlot titlesTextSlotFor(const TitlesTextParams &p)
{
    FxSlot slot = makeFxSlot(QStringLiteral("VEGAS Titles & Text"), PluginFormat::Builtin,
                             QStringLiteral("{Svfx:com.vegascreativesoftware:titlesandtext}"));
    titlesTextSaveToSlot(&slot, p);
    return slot;
}
} // namespace

void ProjectModel::applyTransitionsFromVeg(const VegOpenResult &veg)
{
    if (veg.transitions.isEmpty()) {
        return;
    }
    // Match by the owning event's start time rather than by index: that works the same
    // whether the events came from the EDL sidecar or from the binary timing blocks.
    for (const VegTransitionInfo &info : veg.transitions) {
        if (info.eventStartSec < 0.0) {
            continue;
        }
        TrackEvent *best = nullptr;
        double bestDelta = 0.0;
        for (Track &track : m_tracks) {
            if (track.kind != TrackKind::Video) {
                continue;
            }
            for (TrackEvent &ev : track.events) {
                if (!isVideoFamily(ev.mediaKind)) {
                    continue;
                }
                const double delta = std::abs(ev.startSec - info.eventStartSec);
                if (delta > 0.05) {
                    continue;
                }
                if (!best || delta < bestDelta) {
                    best = &ev;
                    bestDelta = delta;
                }
            }
        }
        if (!best) {
            continue;
        }

        // Every group used to be imported as 3D Blinds, because that was the only id
        // the catalog had: a project full of 3D Cascade and 3D Shuffle came back with
        // every strip reading "3D Blinds".
        QString pluginId;
        switch (info.kind) {
        case VegTransitionKind::Blinds3D:
            pluginId = transition3dBlindsId();
            break;
        case VegTransitionKind::Cascade3D:
            pluginId = transitionCascade3dId();
            break;
        case VegTransitionKind::FlyInOut3D:
            pluginId = transitionFlyInOut3dId();
            break;
        case VegTransitionKind::Shuffle3D:
            pluginId = transitionShuffle3dId();
            break;
        case VegTransitionKind::GradientWipe:
            pluginId = transitionGradientWipeId();
            break;
        case VegTransitionKind::VenetianBlinds:
            pluginId = transitionVenetianBlindsId();
            break;
        case VegTransitionKind::Portals:
            pluginId = transitionPortalsId();
            break;
        case VegTransitionKind::Ofx:
            pluginId = transitionIdForOfxPlugin(info.ofxPluginId);
            break;
        case VegTransitionKind::Unknown:
            break;
        }
        if (pluginId.isEmpty()) {
            continue;
        }

        TransitionInstance t = makeTransitionInstance(pluginId, info.presetName);
        if (!t.isValid()) {
            continue;
        }
        // Parameters come from the file, not from the preset table: a user may have
        // tweaked sliders away from the stock preset before saving. Only the fields the
        // record actually decoded are pushed — a group whose layout is still unknown
        // keeps its preset defaults instead of inheriting another group's numbers.
        if (!info.paramsUndecoded) {
            switch (info.kind) {
            case VegTransitionKind::Blinds3D:
                transitionSetParamValue(&t, QStringLiteral("divisions"), info.divisions);
                transitionSetParamValue(&t, QStringLiteral("extraSpins"), info.extraSpins);
                transitionSetParamValue(&t, QStringLiteral("stagger"), info.stagger);
                transitionSetParamValue(&t, QStringLiteral("specularLight"),
                                        info.specularLight);
                transitionSetParamValue(&t, QStringLiteral("direction"), info.direction);
                break;
            case VegTransitionKind::Cascade3D:
                transitionSetParamValue(&t, QStringLiteral("divisions"), info.divisions);
                transitionSetParamValue(&t, QStringLiteral("direction"), info.direction);
                // 3D Cascade calls this field Twist. The reader still carries it under
                // the 3D Blinds name, where the same slot really is a stagger.
                transitionSetParamValue(&t, QStringLiteral("twist"), info.stagger);
                transitionSetParamValue(&t, QStringLiteral("specularLight"),
                                        info.specularLight);
                break;
            case VegTransitionKind::Shuffle3D:
                transitionSetParamValue(&t, QStringLiteral("specularLight"),
                                        info.specularLight);
                break;
            case VegTransitionKind::VenetianBlinds:
                transitionSetParamValue(&t, QStringLiteral("count"), info.blindCount);
                transitionSetParamValue(&t, QStringLiteral("angle"), info.blindAngleDeg);
                transitionSetParamValue(&t, QStringLiteral("feather"), info.blindFeather);
                break;
            case VegTransitionKind::Ofx: {
                // Named values straight from the file. Only keys this group actually has
                // are taken, so a stub group keeps its preset defaults rather than
                // collecting parameters that belong to a different transition.
                static const QHash<QString, QString> keyFor = {
                    {QStringLiteral("Angle"), QStringLiteral("angle")},
                    {QStringLiteral("Feather"), QStringLiteral("feather")},
                    {QStringLiteral("FeatherAngle"), QStringLiteral("featherAngle")},
                    {QStringLiteral("Direction"), QStringLiteral("direction")},
                    {QStringLiteral("Orientation"), QStringLiteral("orientation")},
                    {QStringLiteral("Shape"), QStringLiteral("shape")},
                    {QStringLiteral("BorderSize"), QStringLiteral("borderSize")},
                    {QStringLiteral("BorderFeather"), QStringLiteral("borderFeather")},
                };
                const TransitionPluginInfo *catalogInfo = transitionPluginById(pluginId);
                for (auto it = info.ofxParams.constBegin(); it != info.ofxParams.constEnd();
                     ++it) {
                    const QString key = keyFor.value(it.key());
                    if (key.isEmpty() || !catalogInfo) {
                        continue;
                    }
                    bool known = false;
                    for (const TransitionParamInfo &pi : catalogInfo->params) {
                        if (pi.key == key) {
                            known = true;
                            break;
                        }
                    }
                    if (known) {
                        transitionSetParamValue(&t, key, it.value().toDouble());
                    }
                }
                // Centre arrives as a pair.
                const QVariantList centre =
                    info.ofxParams.value(QStringLiteral("Center")).toList();
                if (centre.size() == 2 && catalogInfo) {
                    for (const TransitionParamInfo &pi : catalogInfo->params) {
                        if (pi.key == QLatin1String("centerX")) {
                            transitionSetParamValue(&t, QStringLiteral("centerX"),
                                                    centre[0].toDouble());
                            transitionSetParamValue(&t, QStringLiteral("centerY"),
                                                    centre[1].toDouble());
                            break;
                        }
                    }
                }
                break;
            }
            default:
                break;
            }
        }
        // transitionSetParamValue clears presetName once anything differs from the stock
        // preset; restore it when the file's values still match it exactly.
        if (const TransitionPresetInfo *preset = transitionPreset(pluginId, info.presetName)) {
            if (preset->params == t.params) {
                t.presetName = preset->name;
            }
        }

        if (info.fadeOut) {
            best->transitionOut = t;
        } else {
            best->transitionIn = t;
        }
    }
}

void ProjectModel::applyTitlesTextFromVeg(const VegOpenResult &veg)
{
    if (veg.titlesAndText.isEmpty()) {
        return;
    }

    // Prefer converting existing blank ("no media") video placeholder events in place —
    // an EDL sidecar import (authoritative for timing/fades/track layout) has no notion
    // of generators, so it exports each Titles & Text instance as a plain empty VIDEO
    // clip. Converting in place keeps the EDL's real start/length/crossfades, which is
    // strictly better than the binary scan's own heuristic timing (see VegTitleTextInfo
    // doc) used below only when there are no placeholders at all.
    QVector<TrackEvent *> candidates;
    for (Track &tr : m_tracks) {
        if (tr.kind != TrackKind::Video) {
            continue;
        }
        for (TrackEvent &ev : tr.events) {
            if (ev.mediaKind == EventMediaKind::Video && ev.mediaPath.isEmpty()) {
                candidates.push_back(&ev);
            }
        }
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const TrackEvent *a, const TrackEvent *b) { return a->startSec < b->startSec; });

    const int convertCount = std::min(candidates.size(), veg.titlesAndText.size());
    for (int i = 0; i < convertCount; ++i) {
        const TitlesTextParams p = titlesTextParamsFromVeg(veg.titlesAndText[i]);
        TrackEvent *ev = candidates[i];
        ev->mediaKind = EventMediaKind::Title;
        ev->fxChain = {titlesTextSlotFor(p)};
        if (ev->name.isEmpty() || ev->name == QStringLiteral("Video")) {
            ev->name = p.text.section(QLatin1Char('\n'), 0, 0).left(60);
        }
    }
    if (!candidates.isEmpty()) {
        // There WERE real timeline position anchors (an EDL sidecar, or this project's
        // own "Video kind" binary timeline scan) — trust their count over the generator
        // parameter blocks' own. Vegas appears to store more titlesandtext parameter
        // blocks in the file than real timeline instances exist (55 extra on the sample
        // project, exactly the same count as this file's real instances — almost
        // certainly a cached/default copy per used preset, not a second on-timeline
        // occurrence: real Vegas's own EDL export, authoritative for its timeline, has
        // exactly as many events as there are real placeholders here, not one more).
        // Treating every recovered block as a placeable instance regardless produced a
        // second, ~9-minute-longer pass appended after the real ~4.5-minute timeline —
        // wrong duration, still on one track. Any leftover blocks beyond the real
        // placeholder count are discarded rather than guessed at.
        return;
    }

    // No real timeline position anchors at all — e.g. a fresh import path that doesn't
    // pre-create per-instance events — so there is nothing better to trust than the
    // binary scan's own heuristic timing (see VegTitleTextInfo doc). One dedicated
    // track holding every recovered instance, inserted at the front so it composites on
    // top of the existing video per VideoCompositor's "index 0 = topmost" convention.
    const int newTrack = addTrack(TrackKind::Video);
    m_tracks[newTrack].name = QStringLiteral("Titles & Text");
    m_tracks.move(newTrack, 0);

    for (const VegTitleTextInfo &src : veg.titlesAndText) {
        const TitlesTextParams p = titlesTextParamsFromVeg(src);
        TrackEvent ev;
        ev.id = m_nextEventId++;
        ev.name = p.text.section(QLatin1Char('\n'), 0, 0).left(60);
        if (ev.name.isEmpty()) {
            ev.name = QStringLiteral("VEGAS Titles & Text");
        }
        ev.startSec = src.startSec;
        ev.lengthSec = std::max(0.05, src.lengthSec);
        ev.mediaKind = EventMediaKind::Title;
        ev.fxChain = {titlesTextSlotFor(p)};
        m_tracks[0].events.push_back(ev);
    }
}

int ProjectModel::addTitlesTextEvent(const QString &animationKey, const QString &sampleText)
{
    int vi = -1;
    for (int i = 0; i < m_tracks.size(); ++i) {
        if (m_tracks[i].kind == TrackKind::Video
            && m_tracks[i].name == QStringLiteral("Titles & Text")) {
            vi = i;
            break;
        }
    }
    if (vi < 0) {
        vi = addTrack(TrackKind::Video);
        m_tracks[vi].name = QStringLiteral("Titles & Text");
        m_tracks.move(vi, 0);
        vi = 0;
    }

    double trackEnd = 0.0;
    for (const TrackEvent &ev : m_tracks[vi].events) {
        trackEnd = std::max(trackEnd, ev.startSec + ev.lengthSec);
    }
    const double startSec = std::max(m_playheadSec, trackEnd);

    TitlesTextParams p; // defaults: "Sample Text", Verdana 48pt, white, centered, no animation
    if (!animationKey.isEmpty()) {
        p.animationName = animationKey;
        const TitlesTextPresetVisuals visuals = titlesTextPresetVisuals(animationKey);
        p.textColor = visuals.textColor;
        p.backgroundColor = visuals.backgroundColor;
        p.scale = visuals.scale;
    }
    if (!sampleText.isEmpty()) {
        p.text = sampleText;
    }
    FxSlot slot = makeFxSlot(QStringLiteral("VEGAS Titles & Text"), PluginFormat::Builtin,
                             QStringLiteral("{Svfx:com.vegascreativesoftware:titlesandtext}"));
    titlesTextSaveToSlot(&slot, p);

    TrackEvent ev;
    ev.id = m_nextEventId++;
    ev.name = p.text.section(QLatin1Char('\n'), 0, 0).left(60);
    ev.startSec = startSec;
    ev.lengthSec = 10.0;
    ev.mediaKind = EventMediaKind::Title;
    ev.fxChain = {slot};
    m_tracks[vi].events.push_back(ev);
    return ev.id;
}

void ProjectModel::applyAudioEventFxFromVeg(const VegOpenResult &veg)
{
    if (veg.audioEventFxNames.isEmpty()) {
        return;
    }
    auto isAudioOnlyPath = [](const QString &path) -> bool {
        const QString ext = QFileInfo(path).suffix().toLower();
        return ext == QLatin1String("wav") || ext == QLatin1String("flac")
               || ext == QLatin1String("mp3") || ext == QLatin1String("ogg")
               || ext == QLatin1String("aif") || ext == QLatin1String("aiff")
               || ext == QLatin1String("wma");
    };
    bool haveAudioOnly = false;
    for (const Track &tr : m_tracks) {
        if (tr.kind != TrackKind::Audio) {
            continue;
        }
        for (const TrackEvent &ev : tr.events) {
            if (isAudioOnlyPath(ev.mediaPath)) {
                haveAudioOnly = true;
                break;
            }
        }
    }
    for (Track &tr : m_tracks) {
        if (tr.kind != TrackKind::Audio) {
            continue;
        }
        for (TrackEvent &ev : tr.events) {
            if (!ev.fxChain.isEmpty()) {
                continue;
            }
            if (haveAudioOnly && !isAudioOnlyPath(ev.mediaPath)) {
                continue;
            }
            for (const QString &fx : veg.audioEventFxNames) {
                ev.fxChain.push_back(fxSlotFromVegWithState(fx, veg));
            }
        }
    }
}

void ProjectModel::applyMixerChannelsFromVeg(const VegOpenResult &veg)
{
    // Create missing buses/inputs named in the .veg UTF-16 metadata
    auto letterFromName = [](const QString &name, const QString &prefix) -> int {
        QString rest = name.trimmed();
        if (rest.startsWith(prefix, Qt::CaseInsensitive)) {
            rest = rest.mid(prefix.size()).trimmed();
        }
        if (rest.size() == 1 && rest[0].isLetter()) {
            return rest[0].toUpper().unicode() - QLatin1Char('A').unicode();
        }
        bool ok = false;
        const int n = rest.toInt(&ok);
        if (ok && n >= 1) {
            return n - 1;
        }
        return -1;
    };

    for (const QString &name : veg.mixerBusNames) {
        bool exists = false;
        for (const MixerBus &b : m_mixerBuses) {
            if (b.name.compare(name, Qt::CaseInsensitive) == 0) {
                exists = true;
                break;
            }
        }
        if (exists) {
            continue;
        }
        const int want = letterFromName(name, QStringLiteral("Bus"));
        while (want >= 0 && m_mixerBuses.size() <= want) {
            addMixerBus();
        }
        if (want < 0) {
            addMixerBus();
            m_mixerBuses.last().name = name;
        } else if (want < m_mixerBuses.size()) {
            m_mixerBuses[want].name = name;
        }
    }

    for (const QString &name : veg.mixerInputBusNames) {
        bool exists = false;
        for (const MixerInputBus &b : m_mixerInputBuses) {
            if (b.name.compare(name, Qt::CaseInsensitive) == 0) {
                exists = true;
                break;
            }
        }
        if (exists) {
            continue;
        }
        const int want = letterFromName(name, QStringLiteral("Input"));
        while (want >= 0 && m_mixerInputBuses.size() <= want) {
            addMixerInputBus();
        }
        if (want < 0) {
            addMixerInputBus();
            m_mixerInputBuses.last().name = name;
        } else if (want < m_mixerInputBuses.size()) {
            m_mixerInputBuses[want].name = name;
        }
    }

    // Assignable FX: "FX 1" + "Chorus", "FX 2" + "Volume", ...
    const int fxCount =
        std::min(veg.mixerAssignableFxLabels.size(), veg.mixerAssignableFxPlugins.size());
    for (int i = 0; i < fxCount; ++i) {
        const QString &plugin = veg.mixerAssignableFxPlugins.at(i);
        bool exists = false;
        for (const AssignableFxBus &b : m_assignableFx) {
            if (b.name.compare(plugin, Qt::CaseInsensitive) == 0) {
                exists = true;
                break;
            }
        }
        if (exists) {
            continue;
        }
        QVector<FxSlot> chain;
        chain.push_back(makeFxSlot(plugin, PluginFormat::Builtin,
                                   QStringLiteral("builtin:") + plugin));
        addAssignableFxBus(chain);
    }

    rebuildDefaultMixerStripOrder();

    // Prefer Vegas-like layouts when sample has extras.
    // FX-only (mix-console after Bus/Input removal): tracks | FX1 | Master | FX2...
    if (!m_assignableFx.isEmpty() && m_mixerBuses.isEmpty() && m_mixerInputBuses.isEmpty()) {
        QVector<MixerStripRef> order;
        for (const Track &t : m_tracks) {
            if (t.kind == TrackKind::Audio) {
                order.push_back({MixerStripKind::AudioTrack, t.id});
            }
        }
        if (m_assignableFx.size() >= 1) {
            order.push_back({MixerStripKind::AssignableFx, m_assignableFx.first().id});
        }
        order.push_back({MixerStripKind::Master, 0});
        for (int i = 1; i < m_assignableFx.size(); ++i) {
            order.push_back({MixerStripKind::AssignableFx, m_assignableFx[i].id});
        }
        if (isValidMixerStripOrder(order)) {
            m_mixerStripOrder = order;
        }
        return;
    }

    // Bus + Input sample layout (older mix-console): interleaved after tracks
    if (!m_mixerBuses.isEmpty() && !m_mixerInputBuses.isEmpty()) {
        QVector<MixerStripRef> order;
        for (const Track &t : m_tracks) {
            if (t.kind == TrackKind::Audio) {
                order.push_back({MixerStripKind::AudioTrack, t.id});
            }
        }
        auto pushInput = [&](QChar letter) {
            for (const MixerInputBus &b : m_mixerInputBuses) {
                if (b.letterIndex == letter.toUpper().unicode() - QLatin1Char('A').unicode()) {
                    order.push_back({MixerStripKind::InputBus, b.id});
                    return;
                }
            }
        };
        auto pushBus = [&](QChar letter) {
            for (const MixerBus &b : m_mixerBuses) {
                if (b.letterIndex == letter.toUpper().unicode() - QLatin1Char('A').unicode()) {
                    order.push_back({MixerStripKind::AudioBus, b.id});
                    return;
                }
            }
        };
        if (m_mixerInputBuses.size() >= 4 && m_mixerBuses.size() >= 2) {
            pushInput(QLatin1Char('D'));
            pushInput(QLatin1Char('B'));
            pushBus(QLatin1Char('B'));
            pushInput(QLatin1Char('A'));
            pushInput(QLatin1Char('C'));
            pushBus(QLatin1Char('A'));
        } else {
            for (const MixerInputBus &b : m_mixerInputBuses) {
                order.push_back({MixerStripKind::InputBus, b.id});
            }
            for (const MixerBus &b : m_mixerBuses) {
                order.push_back({MixerStripKind::AudioBus, b.id});
            }
        }
        for (const AssignableFxBus &b : m_assignableFx) {
            order.push_back({MixerStripKind::AssignableFx, b.id});
        }
        order.push_back({MixerStripKind::Master, 0});
        if (isValidMixerStripOrder(order)) {
            m_mixerStripOrder = order;
        }
    }
}

void ProjectModel::setPixelsPerSecond(double pps)
{
    // Floor allows fitting long projects (e.g. ~634 s BBB) into the view.
    m_pps = std::clamp(pps, 0.5, 400.0);
}

void ProjectModel::setPlayheadSec(double s)
{
    m_playheadSec = std::max(0.0, s);
}

double ProjectModel::timelineEndSec() const
{
    double end = 1.0 / std::max(1.0, m_frameRate);
    for (const Track &t : m_tracks) {
        for (const TrackEvent &ev : t.events) {
            end = std::max(end, ev.startSec + ev.lengthSec);
        }
    }
    return end;
}

bool ProjectModel::anyTrackSoloed() const
{
    for (const Track &t : m_tracks) {
        if (t.solo) {
            return true;
        }
    }
    return false;
}

bool ProjectModel::isTrackAudible(int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_tracks.size()) {
        return false;
    }
    const Track &t = m_tracks[trackIndex];
    if (t.muted) {
        return false;
    }
    if (anyTrackSoloed() && !t.solo) {
        return false;
    }
    return true;
}

TrackEvent *ProjectModel::findEvent(int eventId, int *outTrackIndex)
{
    for (int ti = 0; ti < m_tracks.size(); ++ti) {
        for (TrackEvent &ev : m_tracks[ti].events) {
            if (ev.id == eventId) {
                if (outTrackIndex) {
                    *outTrackIndex = ti;
                }
                return &ev;
            }
        }
    }
    return nullptr;
}

const TrackEvent *ProjectModel::findEvent(int eventId, int *outTrackIndex) const
{
    for (int ti = 0; ti < m_tracks.size(); ++ti) {
        for (const TrackEvent &ev : m_tracks[ti].events) {
            if (ev.id == eventId) {
                if (outTrackIndex) {
                    *outTrackIndex = ti;
                }
                return &ev;
            }
        }
    }
    return nullptr;
}

QVector<int> ProjectModel::eventIdsInGroup(int groupId) const
{
    QVector<int> ids;
    if (groupId <= 0) {
        return ids;
    }
    for (const Track &t : m_tracks) {
        for (const TrackEvent &ev : t.events) {
            if (ev.groupId == groupId) {
                ids.push_back(ev.id);
            }
        }
    }
    return ids;
}

int ProjectModel::groupSelectedEvents()
{
    const QVector<int> selected = selectedEventIds();
    if (selected.size() < 2) {
        return 0;
    }
    const int gid = m_nextGroupId++;
    for (int id : selected) {
        if (TrackEvent *ev = findEvent(id)) {
            ev->groupId = gid;
        }
    }
    return gid;
}

void ProjectModel::ungroupEvent(int eventId)
{
    TrackEvent *ev = findEvent(eventId);
    if (!ev || ev->groupId <= 0) {
        return;
    }
    const int gid = ev->groupId;
    const QVector<int> members = eventIdsInGroup(gid);
    for (int id : members) {
        if (TrackEvent *m = findEvent(id)) {
            m->groupId = 0;
        }
    }
}

void ProjectModel::clearGroup(int groupId)
{
    if (groupId <= 0) {
        return;
    }
    for (int id : eventIdsInGroup(groupId)) {
        if (TrackEvent *ev = findEvent(id)) {
            ev->groupId = 0;
        }
    }
}

void ProjectModel::selectGroup(int groupId)
{
    if (groupId <= 0) {
        return;
    }
    clearSelection();
    for (int id : eventIdsInGroup(groupId)) {
        if (TrackEvent *ev = findEvent(id)) {
            ev->selected = true;
        }
    }
}

bool ProjectModel::moveEventToTrack(int eventId, int toTrackIndex)
{
    if (toTrackIndex < 0 || toTrackIndex >= m_tracks.size()) {
        return false;
    }
    int fromTrack = -1;
    TrackEvent *src = findEvent(eventId, &fromTrack);
    if (!src || fromTrack < 0) {
        return false;
    }
    if (fromTrack == toTrackIndex) {
        return true;
    }
    if (!canPlaceEventOnTrack(src->mediaKind, m_tracks[toTrackIndex].kind)) {
        return false;
    }

    TrackEvent moved = *src;
    auto &fromEvents = m_tracks[fromTrack].events;
    for (int i = 0; i < fromEvents.size(); ++i) {
        if (fromEvents[i].id == eventId) {
            fromEvents.removeAt(i);
            break;
        }
    }
    m_tracks[toTrackIndex].events.push_back(moved);
    return true;
}

bool ProjectModel::removeEvent(int eventId)
{
    for (Track &t : m_tracks) {
        for (int i = 0; i < t.events.size(); ++i) {
            if (t.events[i].id == eventId) {
                t.events.removeAt(i);
                return true;
            }
        }
    }
    return false;
}

bool ProjectModel::removeEventOrGroup(int eventId)
{
    TrackEvent *ev = findEvent(eventId);
    if (!ev) {
        return false;
    }
    if (!m_ignoreEventGrouping && ev->groupId > 0) {
        const QVector<int> members = eventIdsInGroup(ev->groupId);
        bool any = false;
        for (int id : members) {
            any = removeEvent(id) || any;
        }
        return any;
    }
    return removeEvent(eventId);
}

bool ProjectModel::splitEventAt(int eventId, double timeSec)
{
    int trackIndex = -1;
    TrackEvent *ev = findEvent(eventId, &trackIndex);
    if (!ev || trackIndex < 0) {
        return false;
    }

    // Split whole group at the same absolute time (Vegas-like)
    QVector<int> targets;
    if (!m_ignoreEventGrouping && ev->groupId > 0) {
        targets = eventIdsInGroup(ev->groupId);
    } else {
        targets.push_back(eventId);
    }

    bool any = false;
    // New right halves share a fresh group id so they stay paired
    const int newGid = (!m_ignoreEventGrouping && ev->groupId > 0) ? m_nextGroupId++ : 0;
    const int oldGid = ev->groupId;

    for (int id : targets) {
        int ti = -1;
        TrackEvent *cur = findEvent(id, &ti);
        if (!cur || ti < 0) {
            continue;
        }
        const double end = cur->startSec + cur->lengthSec;
        if (timeSec <= cur->startSec + 0.02 || timeSec >= end - 0.02) {
            continue;
        }
        TrackEvent right = *cur;
        right.id = m_nextEventId++;
        right.startSec = timeSec;
        right.lengthSec = end - timeSec;
        right.fadeInSec = 0.0;
        right.selected = false;
        right.groupId = newGid > 0 ? newGid : 0;
        cur->lengthSec = timeSec - cur->startSec;
        cur->fadeOutSec = 0.0;
        if (cur->fadeInSec > cur->lengthSec) {
            cur->fadeInSec = cur->lengthSec;
        }
        if (right.fadeOutSec > right.lengthSec) {
            right.fadeOutSec = right.lengthSec;
        }
        // Left half keeps old group (or stays ungrouped)
        if (newGid > 0) {
            cur->groupId = oldGid;
        }
        m_tracks[ti].events.push_back(right);
        any = true;
    }
    return any;
}

bool ProjectModel::trimEventStartTo(int eventId, double timeSec)
{
    TrackEvent *ev = findEvent(eventId);
    if (!ev) {
        return false;
    }
    const double end = ev->startSec + ev->lengthSec;
    if (timeSec <= ev->startSec + 0.01 || timeSec >= end - 0.05) {
        return false;
    }
    ev->lengthSec = end - timeSec;
    ev->startSec = timeSec;
    if (ev->fadeInSec + ev->fadeOutSec > ev->lengthSec) {
        const double s = ev->lengthSec / (ev->fadeInSec + ev->fadeOutSec);
        ev->fadeInSec *= s;
        ev->fadeOutSec *= s;
    }
    return true;
}

bool ProjectModel::trimEventEndTo(int eventId, double timeSec)
{
    TrackEvent *ev = findEvent(eventId);
    if (!ev) {
        return false;
    }
    if (timeSec <= ev->startSec + 0.05 || timeSec >= ev->startSec + ev->lengthSec - 0.01) {
        return false;
    }
    ev->lengthSec = timeSec - ev->startSec;
    if (ev->fadeInSec + ev->fadeOutSec > ev->lengthSec) {
        const double s = ev->lengthSec / (ev->fadeInSec + ev->fadeOutSec);
        ev->fadeInSec *= s;
        ev->fadeOutSec *= s;
    }
    return true;
}

void ProjectModel::clearSelection()
{
    for (Track &t : m_tracks) {
        for (TrackEvent &ev : t.events) {
            ev.selected = false;
        }
    }
}

void ProjectModel::selectEvent(int eventId, bool additive)
{
    TrackEvent *ev = findEvent(eventId);
    if (!ev) {
        return;
    }
    if (!additive) {
        clearSelection();
        if (!m_ignoreEventGrouping && ev->groupId > 0) {
            for (int id : eventIdsInGroup(ev->groupId)) {
                if (TrackEvent *m = findEvent(id)) {
                    m->selected = true;
                }
            }
        } else {
            ev->selected = true;
        }
        return;
    }
    // Ctrl-click: real toggle in/out of the existing selection (every other app's
    // convention) rather than only ever adding — flip the whole A/V group together so
    // it stays consistent with the non-additive branch above and with selectRange().
    const bool nowSelected = !ev->selected;
    if (!m_ignoreEventGrouping && ev->groupId > 0) {
        for (int id : eventIdsInGroup(ev->groupId)) {
            if (TrackEvent *m = findEvent(id)) {
                m->selected = nowSelected;
            }
        }
    } else {
        ev->selected = nowSelected;
    }
}

void ProjectModel::selectRange(int anchorEventId, int targetEventId)
{
    int anchorTrack = -1;
    TrackEvent *anchorEv = findEvent(anchorEventId, &anchorTrack);
    int targetTrack = -1;
    TrackEvent *targetEv = findEvent(targetEventId, &targetTrack);
    if (!targetEv) {
        return;
    }
    if (!anchorEv) {
        // Anchor vanished (undo, delete, …) since it was recorded — degrade to a plain
        // select of the target rather than doing nothing.
        selectEvent(targetEventId, false);
        return;
    }

    const int trackLo = std::min(anchorTrack, targetTrack);
    const int trackHi = std::max(anchorTrack, targetTrack);
    const double timeLo = std::min(anchorEv->startSec, targetEv->startSec);
    const double timeHi = std::max(anchorEv->startSec + anchorEv->lengthSec,
                                   targetEv->startSec + targetEv->lengthSec);

    clearSelection();
    for (int ti = trackLo; ti <= trackHi && ti < m_tracks.size(); ++ti) {
        for (TrackEvent &ev : m_tracks[ti].events) {
            const double evEnd = ev.startSec + ev.lengthSec;
            if (evEnd > timeLo + 1e-6 && ev.startSec < timeHi - 1e-6) {
                ev.selected = true;
            }
        }
    }
    // Touching-but-not-overlapping edges (e.g. a zero-gap neighbor exactly at timeLo)
    // can miss the strict overlap test above — always include the two clicked events.
    anchorEv->selected = true;
    targetEv->selected = true;

    // Keep A/V groups whole: if any member of a group fell inside the range, pull in
    // the rest of that group too (same rule plain/Ctrl-click selection already follows).
    if (!m_ignoreEventGrouping) {
        for (int id : selectedEventIds()) {
            const TrackEvent *ev = findEvent(id);
            if (!ev || ev->groupId <= 0) {
                continue;
            }
            for (int gid : eventIdsInGroup(ev->groupId)) {
                if (TrackEvent *m = findEvent(gid)) {
                    m->selected = true;
                }
            }
        }
    }
}

QVector<int> ProjectModel::selectedEventIds() const
{
    QVector<int> ids;
    for (const Track &t : m_tracks) {
        for (const TrackEvent &ev : t.events) {
            if (ev.selected) {
                ids.push_back(ev.id);
            }
        }
    }
    return ids;
}

void ProjectModel::selectAllEvents()
{
    for (Track &t : m_tracks) {
        for (TrackEvent &ev : t.events) {
            ev.selected = true;
        }
    }
}

void ProjectModel::copySelectedEvents()
{
    m_eventClipboard = {};
    struct Row {
        TrackEvent ev;
        TrackKind kind;
        int trackIndex;
    };
    QVector<Row> rows;
    for (int ti = 0; ti < m_tracks.size(); ++ti) {
        for (const TrackEvent &ev : m_tracks[ti].events) {
            if (ev.selected) {
                rows.push_back({ev, m_tracks[ti].kind, ti});
            }
        }
    }
    if (rows.isEmpty()) {
        return;
    }

    int minTrack = rows.first().trackIndex;
    double anchor = rows.first().ev.startSec;
    for (const Row &r : rows) {
        minTrack = std::min(minTrack, r.trackIndex);
        anchor = std::min(anchor, r.ev.startSec);
    }
    m_eventClipboard.anchorSec = anchor;
    for (const Row &r : rows) {
        ClipboardEvent c;
        c.ev = r.ev;
        c.ev.selected = false;
        c.trackKind = r.kind;
        c.trackDelta = r.trackIndex - minTrack;
        m_eventClipboard.items.push_back(c);
    }
}

void ProjectModel::cutSelectedEvents()
{
    copySelectedEvents();
    deleteSelectedEvents();
}

bool ProjectModel::deleteSelectedEvents()
{
    QVector<int> ids = selectedEventIds();
    if (ids.isEmpty()) {
        // Markers take priority in UI; allow deleting selected marker here too
        for (const TimelineMarker &m : m_markers) {
            if (m.selected) {
                removeMarker(m.id);
                return true;
            }
        }
        return false;
    }
    // Expand to whole groups once, then remove unique ids
    QSet<int> unique;
    for (int id : ids) {
        TrackEvent *ev = findEvent(id);
        if (!ev) {
            continue;
        }
        if (!m_ignoreEventGrouping && ev->groupId > 0) {
            for (int gid : eventIdsInGroup(ev->groupId)) {
                unique.insert(gid);
            }
        } else {
            unique.insert(id);
        }
    }
    bool any = false;
    for (int id : unique) {
        any = removeEvent(id) || any;
    }
    return any;
}

int ProjectModel::pasteEventsAt(double timeSec)
{
    if (m_eventClipboard.empty()) {
        return 0;
    }

    clearSelection();
    const double base = std::max(0.0, timeSec);
    const double anchor = m_eventClipboard.anchorSec;

    // Map old groupId → new groupId for pasted set
    QHash<int, int> groupMap;
    int maxDelta = 0;
    for (const ClipboardEvent &c : m_eventClipboard.items) {
        maxDelta = std::max(maxDelta, c.trackDelta);
        if (c.ev.groupId > 0 && !groupMap.contains(c.ev.groupId)) {
            groupMap.insert(c.ev.groupId, m_nextGroupId++);
        }
    }

    // Ensure we have enough tracks from a starting index; prefer appending when empty
    int startTrack = 0;
    if (m_tracks.isEmpty()) {
        // Will create per item as needed
        startTrack = 0;
    }

    // Build track slots: for each trackDelta, find/create a track of the needed kind
    QHash<int, int> deltaToTrack;
    for (const ClipboardEvent &c : m_eventClipboard.items) {
        if (deltaToTrack.contains(c.trackDelta)) {
            continue;
        }
        // Prefer existing track at startTrack + delta if kind matches; else find/create
        int ti = -1;
        const int preferred = startTrack + c.trackDelta;
        if (preferred >= 0 && preferred < m_tracks.size() && m_tracks[preferred].kind == c.trackKind) {
            ti = preferred;
        } else {
            // Search for unused track of this kind after startTrack
            for (int i = startTrack; i < m_tracks.size(); ++i) {
                if (m_tracks[i].kind != c.trackKind) {
                    continue;
                }
                bool used = false;
                for (auto it = deltaToTrack.cbegin(); it != deltaToTrack.cend(); ++it) {
                    if (it.value() == i) {
                        used = true;
                        break;
                    }
                }
                if (!used) {
                    ti = i;
                    break;
                }
            }
            if (ti < 0) {
                ti = addTrack(c.trackKind);
            }
        }
        deltaToTrack.insert(c.trackDelta, ti);
    }

    int pasted = 0;
    for (const ClipboardEvent &c : m_eventClipboard.items) {
        const int ti = deltaToTrack.value(c.trackDelta, -1);
        if (ti < 0 || ti >= m_tracks.size()) {
            continue;
        }
        TrackEvent ev = c.ev;
        ev.id = m_nextEventId++;
        ev.startSec = base + (c.ev.startSec - anchor);
        ev.selected = true;
        if (ev.groupId > 0) {
            ev.groupId = groupMap.value(ev.groupId, 0);
        }
        m_tracks[ti].events.push_back(ev);
        ++pasted;
    }
    return pasted;
}

bool ProjectModel::splitSelectedAt(double timeSec)
{
    const QVector<int> ids = selectedEventIds();
    bool any = false;
    // Copy ids - split mutates selection/structure
    for (int id : ids) {
        any = splitEventAt(id, timeSec) || any;
    }
    return any;
}

bool ProjectModel::splitAllAt(double timeSec)
{
    QVector<int> ids;
    for (const Track &t : m_tracks) {
        for (const TrackEvent &ev : t.events) {
            ids.push_back(ev.id);
        }
    }
    // Copy ids first - splitEventAt mutates m_tracks (inserts new right-half events).
    bool any = false;
    for (int id : ids) {
        any = splitEventAt(id, timeSec) || any;
    }
    return any;
}

bool ProjectModel::trimSelectedStartTo(double timeSec)
{
    const QVector<int> ids = selectedEventIds();
    bool any = false;
    for (int id : ids) {
        any = trimEventStartTo(id, timeSec) || any;
    }
    return any;
}

bool ProjectModel::trimSelectedEndTo(double timeSec)
{
    const QVector<int> ids = selectedEventIds();
    bool any = false;
    for (int id : ids) {
        any = trimEventEndTo(id, timeSec) || any;
    }
    return any;
}

bool ProjectModel::applyAutomaticCrossfade(int eventId)
{
    int trackIndex = -1;
    TrackEvent *ev = findEvent(eventId, &trackIndex);
    if (!ev || trackIndex < 0 || trackIndex >= m_tracks.size()) {
        return false;
    }

    auto overlapOf = [](const TrackEvent &a, const TrackEvent &b) {
        const double o0 = std::max(a.startSec, b.startSec);
        const double o1 = std::min(a.startSec + a.lengthSec, b.startSec + b.lengthSec);
        return o1 > o0 ? (o1 - o0) : 0.0;
    };
    auto clampFades = [](TrackEvent &e) {
        const double maxFade = std::max(0.0, e.lengthSec - 0.05);
        e.fadeInSec = std::clamp(e.fadeInSec, 0.0, maxFade);
        e.fadeOutSec = std::clamp(e.fadeOutSec, 0.0, maxFade);
        if (e.fadeInSec + e.fadeOutSec > e.lengthSec) {
            const double scale = e.lengthSec / (e.fadeInSec + e.fadeOutSec);
            e.fadeInSec *= scale;
            e.fadeOutSec *= scale;
        }
    };

    Track &track = m_tracks[trackIndex];
    TrackEvent *prevNeighbor = nullptr;
    TrackEvent *nextNeighbor = nullptr;
    for (TrackEvent &other : track.events) {
        if (other.id == ev->id) {
            continue;
        }
        if (other.startSec < ev->startSec - 1e-6) {
            if (!prevNeighbor || other.startSec > prevNeighbor->startSec) {
                prevNeighbor = &other;
            }
        } else if (other.startSec > ev->startSec + 1e-6) {
            if (!nextNeighbor || other.startSec < nextNeighbor->startSec) {
                nextNeighbor = &other;
            }
        }
    }

    bool changed = false;
    if (prevNeighbor) {
        const double ov = overlapOf(*prevNeighbor, *ev);
        if (ov > 0.05) {
            prevNeighbor->fadeOutSec = ov;
            ev->fadeInSec = ov;
            changed = true;
        }
    }
    if (nextNeighbor) {
        const double ov = overlapOf(*ev, *nextNeighbor);
        if (ov > 0.05) {
            ev->fadeOutSec = ov;
            nextNeighbor->fadeInSec = ov;
            changed = true;
        }
    }

    if (changed) {
        clampFades(*ev);
        if (prevNeighbor) {
            clampFades(*prevNeighbor);
        }
        if (nextNeighbor) {
            clampFades(*nextNeighbor);
        }
    }
    return changed;
}

void ProjectModel::setLoopRegion(double startSec, double endSec)
{
    double a = std::max(0.0, startSec);
    double b = std::max(0.0, endSec);
    if (b < a) {
        std::swap(a, b);
    }
    constexpr double kMinLen = 0.01;
    if (b - a < kMinLen) {
        clearLoopRegion(a);
        return;
    }
    m_loopRegion.active = true;
    m_loopRegion.startSec = a;
    m_loopRegion.endSec = b;
}

void ProjectModel::clearLoopRegion(double seedSec)
{
    m_loopRegion.active = false;
    m_loopRegion.startSec = std::max(0.0, seedSec);
    m_loopRegion.endSec = m_loopRegion.startSec;
}

TimelineMarker *ProjectModel::findMarker(int markerId)
{
    for (TimelineMarker &m : m_markers) {
        if (m.id == markerId) {
            return &m;
        }
    }
    return nullptr;
}

const TimelineMarker *ProjectModel::findMarker(int markerId) const
{
    for (const TimelineMarker &m : m_markers) {
        if (m.id == markerId) {
            return &m;
        }
    }
    return nullptr;
}

int ProjectModel::addMarkerAt(double timeSec, const QString &label)
{
    clearMarkerSelection();
    TimelineMarker m;
    m.id = m_nextMarkerId++;
    m.number = m_nextMarkerNumber++;
    m.timeSec = std::max(0.0, timeSec);
    m.label = label;
    m.selected = true;
    m_markers.push_back(m);
    return m.id;
}

bool ProjectModel::removeMarker(int markerId)
{
    for (int i = 0; i < m_markers.size(); ++i) {
        if (m_markers[i].id == markerId) {
            m_markers.removeAt(i);
            renumberMarkers();
            return true;
        }
    }
    return false;
}

void ProjectModel::removeAllMarkers()
{
    m_markers.clear();
    m_nextMarkerNumber = 1;
}

void ProjectModel::clearMarkerSelection()
{
    for (TimelineMarker &m : m_markers) {
        m.selected = false;
    }
}

void ProjectModel::selectMarker(int markerId, bool additive)
{
    TimelineMarker *m = findMarker(markerId);
    if (!m) {
        return;
    }
    if (!additive) {
        clearMarkerSelection();
    }
    m->selected = true;
}

void ProjectModel::renumberMarkers()
{
    std::sort(m_markers.begin(), m_markers.end(),
              [](const TimelineMarker &a, const TimelineMarker &b) {
                  if (a.timeSec != b.timeSec) {
                      return a.timeSec < b.timeSec;
                  }
                  return a.id < b.id;
              });
    for (int i = 0; i < m_markers.size(); ++i) {
        m_markers[i].number = i + 1;
    }
    m_nextMarkerNumber = m_markers.size() + 1;
}

QString ProjectModel::formatRulerTime(double sec) const
{
    if (!std::isfinite(sec)) {
        sec = 0.0;
    }
    sec = std::max(0.0, sec);

    const double bpm = std::max(0.001, m_tempoBpm);
    const double beatLen = 60.0 / bpm;
    constexpr int beatsPerMeasure = 4;
    const double measureLen = beatLen * beatsPerMeasure;

    const int fpsInt = std::max(1, static_cast<int>(std::llround(m_frameRate)));

    switch (m_rulerTimeFormat) {
    case RulerTimeFormat::Samples: {
        const qint64 samples = static_cast<qint64>(std::llround(sec * double(m_sampleRate)));
        return QString::number(samples);
    }
    case RulerTimeFormat::Time: {
        const int measure = 1 + static_cast<int>(std::floor(sec));
        const double frac = sec - std::floor(sec);
        const int ticks = static_cast<int>(std::llround(frac * 1000.0)) % 1000;
        return QStringLiteral("%1.1.%2").arg(measure).arg(ticks, 3, 10, QChar('0'));
    }
    case RulerTimeFormat::Seconds: {
        // Simple "seconds with 1 decimal" (Vegas-like for this simplified editor).
        return QString::number(sec, 'f', 1);
    }
    case RulerTimeFormat::TimeFrames: {
        // For major ticks we display FF=0; fractional part uses frames.
        const int totalFrames = static_cast<int>(std::llround(sec * m_frameRate));
        const int frames = totalFrames % fpsInt;
        const int totalSeconds = static_cast<int>(totalFrames / fpsInt);
        const int s = totalSeconds % 60;
        const int m = (totalSeconds / 60) % 60;
        const int h = totalSeconds / 3600;
        return QStringLiteral("%1:%2:%3:%4")
            .arg(h, 2, 10, QChar('0'))
            .arg(m, 2, 10, QChar('0'))
            .arg(s, 2, 10, QChar('0'))
            .arg(frames, 2, 10, QChar('0'));
    }
    case RulerTimeFormat::AbsoluteFrames: {
        const qint64 frames = static_cast<qint64>(std::llround(sec * m_frameRate));
        return QString::number(frames);
    }
    case RulerTimeFormat::MeasuresBeats: {
        const int measure = static_cast<int>(std::floor(sec / measureLen)) + 1;
        const double inMeasure = sec - (measure - 1) * measureLen;
        const int beat = static_cast<int>(std::floor(inMeasure / beatLen)) + 1;
        return QStringLiteral("%1.%2").arg(measure).arg(beat);
    }
    case RulerTimeFormat::Feet16mm:
    case RulerTimeFormat::Feet35mm:
    case RulerTimeFormat::SMPTE_IVTC:
    case RulerTimeFormat::SMPTE_Film:
    case RulerTimeFormat::SMPTE_EBU:
    case RulerTimeFormat::SMPTE_NonDrop:
    case RulerTimeFormat::SMPTE_Drop:
    case RulerTimeFormat::SMPTE_30:
    case RulerTimeFormat::AudioCDTime:
    default: {
        // Not fully implemented: fall back to Time.
        const int measure = 1 + static_cast<int>(std::floor(sec));
        const double frac = sec - std::floor(sec);
        const int ticks = static_cast<int>(std::llround(frac * 1000.0)) % 1000;
        return QStringLiteral("%1.1.%2").arg(measure).arg(ticks, 3, 10, QChar('0'));
    }
    }
}

} // namespace openvegas
