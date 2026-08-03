#include "audio/AudioGraph.h"

#include "audio/AudioDecodeCache.h"
#include "audio/AudioUtil.h"
#include "audio/BuiltinDsp.h"
#include "audio/FadeCurves.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace openvegas {

void AudioGraph::rebuild(const ProjectModel &model, AudioPluginHost *host)
{
    m_host = host;
    m_sampleRate = int(model.sampleRate() > 0 ? model.sampleRate() : 48000);
    m_tracks.clear();
    m_buses.clear();
    m_trackMeters.clear();
    m_busMeters.clear();
    m_trackFxStates.clear();
    m_clipFxStates.clear();
    m_busFxStates.clear();
    m_masterFx.clear();
    m_masterFxStates.clear();

    for (const MixerBus &b : model.mixerBuses()) {
        AudioGraphBus gb;
        gb.busId = b.id;
        gb.muted = b.muted;
        gb.solo = b.solo;
        gb.volumeDb = b.volumeDb;
        gb.pan = b.pan;
        gb.fxChain = b.fxChain;
        m_buses.push_back(gb);
        m_busMeters.push_back(std::make_unique<AudioMeterLevels>());
    }
    m_busFxStates.resize(size_t(m_buses.size()));
    for (int i = 0; i < m_buses.size(); ++i) {
        m_busFxStates[size_t(i)].assign(size_t(m_buses[i].fxChain.size()), BuiltinDspState{});
    }

    const auto &tracks = model.tracks();
    for (int ti = 0; ti < tracks.size(); ++ti) {
        const Track &tr = tracks[ti];
        if (tr.kind != TrackKind::Audio) {
            continue;
        }
        AudioGraphTrack gt;
        gt.trackId = tr.id;
        gt.audible = model.isTrackAudible(ti);
        gt.volumeDb = tr.volumeDb;
        gt.pan = tr.pan;
        gt.busId = tr.busId;
        gt.fxChain = tr.fxChain;
        gt.automationLanes = tr.automationLanes;
        for (const TrackEvent &ev : tr.events) {
            if (!isAudioFamily(ev.mediaKind) || ev.mediaPath.isEmpty() || ev.lengthSec <= 0.0) {
                continue;
            }
            AudioGraphClip clip;
            clip.eventId = ev.id;
            clip.trackId = tr.id;
            clip.mediaPath = ev.mediaPath;
            clip.startSec = ev.startSec;
            clip.lengthSec = ev.lengthSec;
            clip.inPointSec = std::max(0.0, ev.mediaStartSec);
            clip.mediaLengthSec = std::max(0.0, ev.mediaLengthSec);
            clip.looped = ev.looped;
            clip.reversed = ev.reversed;
            clip.gainDb = ev.gainDb;
            clip.fadeInSec = ev.fadeInSec;
            clip.fadeOutSec = ev.fadeOutSec;
            clip.fadeInCurve = ev.fadeInCurve;
            clip.fadeOutCurve = ev.fadeOutCurve;
            clip.firstChannel = ev.firstChannel;
            clip.channelCount = ev.channelCount;
            clip.fxChain = ev.fxChain;
            clip.automationLanes = ev.automationLanes;
            gt.clips.push_back(clip);
            AudioDecodeCache::instance().requestAsync(ev.mediaPath, m_sampleRate);
        }
        m_tracks.push_back(gt);
        m_trackMeters.push_back(std::make_unique<AudioMeterLevels>());
    }

    m_trackFxStates.resize(size_t(m_tracks.size()));
    m_clipFxStates.resize(size_t(m_tracks.size()));
    for (int i = 0; i < m_tracks.size(); ++i) {
        m_trackFxStates[size_t(i)].assign(size_t(m_tracks[i].fxChain.size()), BuiltinDspState{});
        m_clipFxStates[size_t(i)].resize(size_t(m_tracks[i].clips.size()));
        for (int c = 0; c < m_tracks[i].clips.size(); ++c) {
            m_clipFxStates[size_t(i)][size_t(c)].assign(
                size_t(m_tracks[i].clips[c].fxChain.size()), BuiltinDspState{});
        }
    }

    // Assignable FX buses: process as additional insert chains mixed at unity if they have
    // return-to-master semantics — for now attach their FX to master chain in order.
    for (const AssignableFxBus &fx : model.assignableFxBuses()) {
        for (const FxSlot &s : fx.fxChain) {
            m_masterFx.push_back(s);
        }
    }
    m_masterFxStates.assign(size_t(m_masterFx.size()), BuiltinDspState{});
    m_masterGainDb = model.masterVolumeDb();
}

void AudioGraph::applyLiveMixer(const ProjectModel &model)
{
    m_masterGainDb = model.masterVolumeDb();

    for (AudioGraphBus &gb : m_buses) {
        for (const MixerBus &b : model.mixerBuses()) {
            if (b.id != gb.busId) {
                continue;
            }
            gb.muted = b.muted;
            gb.solo = b.solo;
            gb.volumeDb = b.volumeDb;
            gb.pan = b.pan;
            break;
        }
    }

    const auto &tracks = model.tracks();
    for (AudioGraphTrack &gt : m_tracks) {
        for (int ti = 0; ti < tracks.size(); ++ti) {
            const Track &tr = tracks[ti];
            if (tr.id != gt.trackId) {
                continue;
            }
            gt.audible = model.isTrackAudible(ti);
            gt.volumeDb = tr.volumeDb;
            gt.pan = tr.pan;
            gt.busId = tr.busId;
            gt.automationLanes = tr.automationLanes;
            // Live FX state / bypass without full rebuild
            if (gt.fxChain.size() == tr.fxChain.size()) {
                for (int i = 0; i < gt.fxChain.size(); ++i) {
                    gt.fxChain[i].bypass = tr.fxChain[i].bypass;
                    gt.fxChain[i].state = tr.fxChain[i].state;
                    // Keep graph hostKey (instance identity); refresh pluginId if resolved
                    if (!tr.fxChain[i].pluginId.isEmpty()) {
                        gt.fxChain[i].pluginId = tr.fxChain[i].pluginId;
                    }
                }
            }
            // Event gain / fades / envelopes — previously only applied on full rebuild,
            // so timeline Level edits were ignored while Play was active.
            for (AudioGraphClip &clip : gt.clips) {
                for (const TrackEvent &ev : tr.events) {
                    if (ev.id != clip.eventId) {
                        continue;
                    }
                    clip.gainDb = ev.gainDb;
                    clip.fadeInSec = ev.fadeInSec;
                    clip.fadeOutSec = ev.fadeOutSec;
                    clip.fadeInCurve = ev.fadeInCurve;
                    clip.fadeOutCurve = ev.fadeOutCurve;
                    clip.automationLanes = ev.automationLanes;
                    if (clip.fxChain.size() == ev.fxChain.size()) {
                        for (int i = 0; i < clip.fxChain.size(); ++i) {
                            clip.fxChain[i].bypass = ev.fxChain[i].bypass;
                            clip.fxChain[i].state = ev.fxChain[i].state;
                            if (!ev.fxChain[i].pluginId.isEmpty()) {
                                clip.fxChain[i].pluginId = ev.fxChain[i].pluginId;
                            }
                        }
                    }
                    break;
                }
            }
            break;
        }
    }
}

void AudioGraph::prepare(double sampleRate, int blockSize)
{
    m_sampleRate = int(sampleRate);
    m_blockSize = blockSize;
    m_tmpL.assign(size_t(blockSize), 0.f);
    m_tmpR.assign(size_t(blockSize), 0.f);
    m_trackL.assign(size_t(blockSize), 0.f);
    m_trackR.assign(size_t(blockSize), 0.f);
    m_accL.assign(size_t(blockSize), 0.f);
    m_accR.assign(size_t(blockSize), 0.f);
    m_busL.assign(size_t(std::max(1, int(m_buses.size())) * blockSize), 0.f);
    m_busR.assign(size_t(std::max(1, int(m_buses.size())) * blockSize), 0.f);

    auto prep = [sampleRate](std::vector<BuiltinDspState> &states) {
        for (auto &st : states) {
            st.prepare(sampleRate);
        }
    };
    for (auto &states : m_trackFxStates) {
        prep(states);
    }
    for (auto &clips : m_clipFxStates) {
        for (auto &states : clips) {
            prep(states);
        }
    }
    for (auto &states : m_busFxStates) {
        prep(states);
    }
    prep(m_masterFxStates);

    if (m_host) {
        auto prepHost = [this, sampleRate, blockSize](QVector<FxSlot> &chain) {
            for (FxSlot &slot : chain) {
                if (slot.format != PluginFormat::Builtin) {
                    m_host->prepare(&slot, sampleRate, blockSize);
                }
            }
        };
        for (AudioGraphTrack &tr : m_tracks) {
            prepHost(tr.fxChain);
            for (AudioGraphClip &clip : tr.clips) {
                prepHost(clip.fxChain);
            }
        }
        for (AudioGraphBus &bus : m_buses) {
            prepHost(bus.fxChain);
        }
        prepHost(m_masterFx);
    }
}

void AudioGraph::reset()
{
    for (auto &states : m_trackFxStates) {
        for (auto &st : states) {
            st.reset();
        }
    }
    for (auto &clips : m_clipFxStates) {
        for (auto &states : clips) {
            for (auto &st : states) {
                st.reset();
            }
        }
    }
    for (auto &states : m_busFxStates) {
        for (auto &st : states) {
            st.reset();
        }
    }
    for (auto &st : m_masterFxStates) {
        st.reset();
    }
    m_seekFade = 0.f;
}

int AudioGraph::busIndexForId(int busId) const
{
    for (int i = 0; i < m_buses.size(); ++i) {
        if (m_buses[i].busId == busId) {
            return i;
        }
    }
    return -1;
}

float AudioGraph::automationGain(const QVector<AutomationLane> &lanes, const QString &targetId,
                                 double timeSec, float fallback) const
{
    for (const AutomationLane &lane : lanes) {
        if (lane.targetId == targetId) {
            return float(lane.evaluate(timeSec, double(fallback)));
        }
    }
    return fallback;
}

void AudioGraph::processFxChain(QVector<FxSlot> &chain, std::vector<BuiltinDspState> &states,
                                float *L, float *R, int frames,
                                const QVector<AutomationLane> *lanes, double timeSec)
{
    if (states.size() < size_t(chain.size())) {
        states.resize(size_t(chain.size()));
        for (auto &st : states) {
            if (st.sampleRate <= 0) {
                st.prepare(m_sampleRate);
            }
        }
    }
    for (int i = 0; i < chain.size(); ++i) {
        FxSlot &slot = chain[i];
        if (slot.bypass) {
            continue;
        }
        // Optional per-slot gain automation: fx:N:gainDb
        float slotGain = 1.f;
        if (lanes) {
            const QString key = QStringLiteral("fx:%1:gainDb").arg(i);
            const float db = automationGain(*lanes, key, timeSec, 0.f);
            slotGain = dbToLinear(double(db));
        }
        if (slot.format == PluginFormat::Builtin) {
            processBuiltinFx(&slot, &states[size_t(i)], L, R, frames);
        } else if (m_host) {
            float *in[2] = {L, R};
            float *out[2] = {L, R};
            m_host->process(&slot, in, out, 2, frames);
        }
        if (std::abs(slotGain - 1.f) > 1e-6f) {
            for (int f = 0; f < frames; ++f) {
                L[f] *= slotGain;
                R[f] *= slotGain;
            }
        }
    }
}

void AudioGraph::processClip(const AudioGraphClip &clip, double startSec, float *left, float *right,
                             int frames)
{
    const double endSec = startSec + double(frames) / double(m_sampleRate);
    if (endSec <= clip.startSec || startSec >= clip.startSec + clip.lengthSec) {
        return;
    }

    auto buf = AudioDecodeCache::instance().peek(clip.mediaPath);
    if (!buf || !buf->ready || buf->channels <= 0 || buf->frameCount() <= 0) {
        return;
    }

    const int srcCh = buf->channels;
    int ch0 = clip.firstChannel;
    int ch1 = ch0 + 1;
    if (clip.channelCount == 1) {
        ch1 = ch0;
    } else if (clip.channelCount >= 2) {
        ch1 = ch0 + 1;
    }
    ch0 = std::clamp(ch0, 0, srcCh - 1);
    ch1 = std::clamp(ch1, 0, srcCh - 1);

    const float baseGain = dbToLinear(
        double(automationGain(clip.automationLanes, QStringLiteral("event.gain"), startSec,
                              float(clip.gainDb))));
    const double micro = 64.0 / double(m_sampleRate);

    for (int i = 0; i < frames; ++i) {
        const double t = startSec + double(i) / double(m_sampleRate);
        if (t < clip.startSec || t >= clip.startSec + clip.lengthSec) {
            continue;
        }
        const double local = t - clip.startSec;
        float fade = 1.f;
        if (clip.fadeInSec > 1e-9 && local < clip.fadeInSec) {
            fade *= float(fadeCurveAmplitude(clip.fadeInCurve, local / clip.fadeInSec));
        } else if (clip.fadeInSec <= 1e-9 && local < micro) {
            fade *= float(local / micro);
        }
        const double fromEnd = clip.lengthSec - local;
        if (clip.fadeOutSec > 1e-9 && fromEnd < clip.fadeOutSec) {
            fade *= float(1.0 - fadeCurveAmplitude(clip.fadeOutCurve, 1.0 - fromEnd / clip.fadeOutSec));
        } else if (clip.fadeOutSec <= 1e-9 && fromEnd < micro) {
            fade *= float(fromEnd / micro);
        }

        // Match TrackEvent::sourceTimeSec (reverse SubClip in-point + loop wrap).
        double cycle = clip.mediaLengthSec > 1e-6 ? clip.mediaLengthSec : 0.0;
        if (cycle < 1e-6) {
            cycle = double(buf->frameCount()) / double(std::max(1, buf->sampleRate));
            cycle = std::max(0.05, cycle);
        }
        double srcTime = 0.0;
        if (clip.reversed) {
            double r = clip.inPointSec + local;
            const double rem = (clip.inPointSec > 1e-3 && clip.inPointSec < cycle)
                                   ? (cycle - clip.inPointSec)
                                   : cycle;
            if (clip.looped && clip.lengthSec > std::min(cycle, rem) + 1e-6) {
                r = std::fmod(r, cycle);
                if (r < 0.0) {
                    r += cycle;
                }
            } else {
                const double takeEnd =
                    clip.inPointSec
                    + std::min(clip.lengthSec, std::max(0.0, cycle - clip.inPointSec));
                if (r > takeEnd) {
                    r = takeEnd;
                }
                if (r > cycle) {
                    r = cycle;
                }
            }
            srcTime = (r <= 1e-12) ? std::max(0.0, cycle) : std::max(0.0, cycle - r);
        } else {
            double loc = local;
            if (clip.looped && clip.lengthSec > cycle + 1e-6) {
                loc = std::fmod(loc, cycle);
                if (loc < 0.0) {
                    loc += cycle;
                }
            } else if (loc > cycle) {
                loc = cycle;
            }
            srcTime = std::max(0.0, clip.inPointSec + loc);
        }
        const double srcFrame = srcTime * double(buf->sampleRate);
        const qint64 i0 = qint64(srcFrame);
        if (i0 < 0 || i0 >= buf->frameCount()) {
            continue;
        }
        const qint64 i1 = std::min(i0 + 1, buf->frameCount() - 1);
        const float frac = float(srcFrame - double(i0));
        const float sL0 = buf->samples[int(i0 * srcCh + ch0)];
        const float sL1 = buf->samples[int(i1 * srcCh + ch0)];
        const float sR0 = buf->samples[int(i0 * srcCh + ch1)];
        const float sR1 = buf->samples[int(i1 * srcCh + ch1)];
        const float g = baseGain * fade;
        left[i] += (sL0 + (sL1 - sL0) * frac) * g;
        right[i] += (sR0 + (sR1 - sR0) * frac) * g;
    }
}

void AudioGraph::process(double startSec, float *left, float *right, int frames)
{
    if (!left || !right || frames <= 0) {
        return;
    }
    if (int(m_tmpL.size()) < frames) {
        m_tmpL.assign(size_t(frames), 0.f);
        m_tmpR.assign(size_t(frames), 0.f);
        m_trackL.assign(size_t(frames), 0.f);
        m_trackR.assign(size_t(frames), 0.f);
        m_accL.assign(size_t(frames), 0.f);
        m_accR.assign(size_t(frames), 0.f);
        m_busL.assign(size_t(std::max(1, int(m_buses.size())) * frames), 0.f);
        m_busR.assign(size_t(std::max(1, int(m_buses.size())) * frames), 0.f);
    }
    std::fill(m_accL.begin(), m_accL.begin() + frames, 0.f);
    std::fill(m_accR.begin(), m_accR.begin() + frames, 0.f);
    if (!m_buses.isEmpty()) {
        std::fill(m_busL.begin(), m_busL.begin() + m_buses.size() * frames, 0.f);
        std::fill(m_busR.begin(), m_busR.begin() + m_buses.size() * frames, 0.f);
    }

    if (m_seekFade < 1.f) {
        m_seekFade = std::min(1.f, m_seekFade + float(frames) / float(m_sampleRate) * 80.f);
    }

    bool anyBusSolo = false;
    for (const AudioGraphBus &b : m_buses) {
        if (b.solo) {
            anyBusSolo = true;
            break;
        }
    }

    for (int ti = 0; ti < m_tracks.size(); ++ti) {
        AudioGraphTrack &tr = m_tracks[ti];
        if (!tr.audible) {
            if (ti < int(m_trackMeters.size()) && m_trackMeters[size_t(ti)]) {
                m_trackMeters[size_t(ti)]->peakL.store(0.f);
                m_trackMeters[size_t(ti)]->peakR.store(0.f);
            }
            continue;
        }

        std::fill(m_trackL.begin(), m_trackL.begin() + frames, 0.f);
        std::fill(m_trackR.begin(), m_trackR.begin() + frames, 0.f);

        for (int ci = 0; ci < tr.clips.size(); ++ci) {
            std::fill(m_tmpL.begin(), m_tmpL.begin() + frames, 0.f);
            std::fill(m_tmpR.begin(), m_tmpR.begin() + frames, 0.f);
            processClip(tr.clips[ci], startSec, m_tmpL.data(), m_tmpR.data(), frames);
            if (!tr.clips[ci].fxChain.isEmpty()) {
                processFxChain(tr.clips[ci].fxChain, m_clipFxStates[size_t(ti)][size_t(ci)],
                               m_tmpL.data(), m_tmpR.data(), frames, &tr.clips[ci].automationLanes,
                               startSec);
            }
            for (int i = 0; i < frames; ++i) {
                m_trackL[size_t(i)] += m_tmpL[size_t(i)];
                m_trackR[size_t(i)] += m_tmpR[size_t(i)];
            }
        }

        if (!tr.fxChain.isEmpty()) {
            processFxChain(tr.fxChain, m_trackFxStates[size_t(ti)], m_trackL.data(), m_trackR.data(),
                           frames, &tr.automationLanes, startSec);
        }

        const float volDb = automationGain(tr.automationLanes, QStringLiteral("track.volume"),
                                           startSec, float(tr.volumeDb));
        const float pan = automationGain(tr.automationLanes, QStringLiteral("track.pan"), startSec,
                                         tr.pan);
        float gL = 1.f, gR = 1.f;
        panGains(pan, gL, gR);
        const float trackLin = dbToLinear(double(volDb));
        gL *= trackLin;
        gR *= trackLin;

        const int bi = busIndexForId(tr.busId);
        float peakL = 0.f, peakR = 0.f;
        double sumL = 0.0, sumR = 0.0;
        for (int i = 0; i < frames; ++i) {
            const float l = m_trackL[size_t(i)] * gL;
            const float r = m_trackR[size_t(i)] * gR;
            if (bi >= 0) {
                m_busL[size_t(bi * frames + i)] += l;
                m_busR[size_t(bi * frames + i)] += r;
            } else {
                m_accL[size_t(i)] += l;
                m_accR[size_t(i)] += r;
            }
            peakL = std::max(peakL, std::abs(l));
            peakR = std::max(peakR, std::abs(r));
            sumL += double(l) * l;
            sumR += double(r) * r;
        }
        if (ti < int(m_trackMeters.size()) && m_trackMeters[size_t(ti)]) {
            m_trackMeters[size_t(ti)]->peakL.store(peakL);
            m_trackMeters[size_t(ti)]->peakR.store(peakR);
            m_trackMeters[size_t(ti)]->rmsL.store(float(std::sqrt(sumL / frames)));
            m_trackMeters[size_t(ti)]->rmsR.store(float(std::sqrt(sumR / frames)));
        }
    }

    for (int bi = 0; bi < m_buses.size(); ++bi) {
        AudioGraphBus &bus = m_buses[bi];
        const bool silenced = bus.muted || (anyBusSolo && !bus.solo);
        float *bL = m_busL.data() + bi * frames;
        float *bR = m_busR.data() + bi * frames;
        if (silenced) {
            if (bi < int(m_busMeters.size()) && m_busMeters[size_t(bi)]) {
                m_busMeters[size_t(bi)]->peakL.store(0.f);
                m_busMeters[size_t(bi)]->peakR.store(0.f);
            }
            continue;
        }
        if (!bus.fxChain.isEmpty()) {
            processFxChain(bus.fxChain, m_busFxStates[size_t(bi)], bL, bR, frames, nullptr,
                           startSec);
        }
        float gL = 1.f, gR = 1.f;
        panGains(bus.pan, gL, gR);
        const float lin = dbToLinear(bus.volumeDb);
        gL *= lin;
        gR *= lin;
        float peakL = 0.f, peakR = 0.f;
        for (int i = 0; i < frames; ++i) {
            const float l = bL[i] * gL;
            const float r = bR[i] * gR;
            m_accL[size_t(i)] += l;
            m_accR[size_t(i)] += r;
            peakL = std::max(peakL, std::abs(l));
            peakR = std::max(peakR, std::abs(r));
        }
        if (bi < int(m_busMeters.size()) && m_busMeters[size_t(bi)]) {
            m_busMeters[size_t(bi)]->peakL.store(peakL);
            m_busMeters[size_t(bi)]->peakR.store(peakR);
        }
    }

    if (!m_masterFx.isEmpty()) {
        processFxChain(m_masterFx, m_masterFxStates, m_accL.data(), m_accR.data(), frames, nullptr,
                       startSec);
    }

    const float masterG = dbToLinear(m_masterGainDb) * m_seekFade;
    float peakL = 0.f, peakR = 0.f;
    double sumL = 0.0, sumR = 0.0;
    for (int i = 0; i < frames; ++i) {
        left[i] = m_accL[size_t(i)] * masterG;
        right[i] = m_accR[size_t(i)] * masterG;
        peakL = std::max(peakL, std::abs(left[i]));
        peakR = std::max(peakR, std::abs(right[i]));
        sumL += double(left[i]) * left[i];
        sumR += double(right[i]) * right[i];
    }
    m_masterMeter.peakL.store(peakL);
    m_masterMeter.peakR.store(peakR);
    m_masterMeter.rmsL.store(float(std::sqrt(sumL / frames)));
    m_masterMeter.rmsR.store(float(std::sqrt(sumR / frames)));
}

QVector<AudioMeterLevels *> AudioGraph::trackMeters()
{
    QVector<AudioMeterLevels *> out;
    out.reserve(int(m_trackMeters.size()));
    for (auto &m : m_trackMeters) {
        out.push_back(m.get());
    }
    return out;
}

QVector<AudioMeterLevels *> AudioGraph::busMeters()
{
    QVector<AudioMeterLevels *> out;
    out.reserve(int(m_busMeters.size()));
    for (auto &m : m_busMeters) {
        out.push_back(m.get());
    }
    return out;
}

} // namespace openvegas
