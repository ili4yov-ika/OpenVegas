#include "plugins/BuiltinAudioCatalog.h"

#include <QDataStream>
#include <QIODevice>
#include <QVariantMap>

namespace openvegas {
namespace {

AudioPluginDesc make(const QString &name, const QString &category, const QString &vendor = {},
                     bool trackOpt = false)
{
    AudioPluginDesc d;
    d.id = QStringLiteral("builtin:") + name;
    d.name = name;
    d.vendor = vendor.isEmpty() ? QStringLiteral("VEGAS") : vendor;
    d.format = PluginFormat::Builtin;
    d.category = category;
    d.automatable = true;
    d.trackOptimized = trackOpt;
    return d;
}

QByteArray packParams(const QVariantMap &m)
{
    QByteArray ba;
    QDataStream out(&ba, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_0);
    out << m;
    return ba;
}

FxSlot makeTrackFx(const QString &name, const QVariantMap &params)
{
    FxSlot s = makeFxSlot(name, PluginFormat::Builtin, QStringLiteral("builtin:") + name);
    s.state = packParams(params);
    return s;
}

QVariantMap defaultNoiseGateParams()
{
    return {
        {QStringLiteral("thresholdDb"), -60.0}, // visual top ≈ −Inf
        {QStringLiteral("attackMs"), 3.0},
        {QStringLiteral("releaseMs"), 100.0},
    };
}

QVariantMap defaultTrackEqParams()
{
    // Vegas Track EQ: 4 bands; band 1 = Low Shelf @ 100 Hz / 0 dB / 12 dB/oct
    return {
        {QStringLiteral("band0.enabled"), true},
        {QStringLiteral("band0.type"), 0}, // Low Shelf
        {QStringLiteral("band0.freq"), 100.0},
        {QStringLiteral("band0.gain"), 0.0},
        {QStringLiteral("band0.rolloff"), 12.0},
        {QStringLiteral("band1.enabled"), true},
        {QStringLiteral("band1.type"), 1}, // Peak
        {QStringLiteral("band1.freq"), 1000.0},
        {QStringLiteral("band1.gain"), 0.0},
        {QStringLiteral("band1.rolloff"), 12.0},
        {QStringLiteral("band2.enabled"), true},
        {QStringLiteral("band2.type"), 1},
        {QStringLiteral("band2.freq"), 4000.0},
        {QStringLiteral("band2.gain"), 0.0},
        {QStringLiteral("band2.rolloff"), 12.0},
        {QStringLiteral("band3.enabled"), true},
        {QStringLiteral("band3.type"), 2}, // High Shelf
        {QStringLiteral("band3.freq"), 10000.0},
        {QStringLiteral("band3.gain"), 0.0},
        {QStringLiteral("band3.rolloff"), 12.0},
    };
}

QVariantMap defaultTrackCompressorParams()
{
    return {
        {QStringLiteral("inputGain"), 0.0},
        {QStringLiteral("outputGain"), 0.0},
        {QStringLiteral("threshold"), 0.0},
        {QStringLiteral("amount"), 1.0},
        {QStringLiteral("attackMs"), 15.0},
        {QStringLiteral("releaseMs"), 250.0},
        {QStringLiteral("autoGain"), true},
        {QStringLiteral("smoothSat"), false},
    };
}

} // namespace

QVector<AudioPluginDesc> BuiltinAudioCatalog::all()
{
    QVector<AudioPluginDesc> out;
    // Default / Track Optimized
    out.push_back(make(QStringLiteral("Track Noise Gate"), QStringLiteral("Track Optimized"),
                       QStringLiteral("VEGAS"), true));
    out.push_back(make(QStringLiteral("Track EQ"), QStringLiteral("Track Optimized"),
                       QStringLiteral("VEGAS"), true));
    out.push_back(make(QStringLiteral("Track Compressor"), QStringLiteral("Track Optimized"),
                       QStringLiteral("VEGAS"), true));
    out.push_back(make(QStringLiteral("TrackEQ"), QStringLiteral("VEGAS"), QStringLiteral("VEGAS"),
                       true));
    out.push_back(make(QStringLiteral("TrackFX"), QStringLiteral("VEGAS"), QStringLiteral("VEGAS"),
                       true));
    out.push_back(make(QStringLiteral("Wave Hammer"), QStringLiteral("VEGAS")));

    // Classic VEGAS / ExpressFX-style (Mixing Console Assignable FX chooser)
    const QStringList vegasFx = {
        QStringLiteral("Amplitude Modulation"),
        QStringLiteral("Chorus"),
        QStringLiteral("Volume"),
        QStringLiteral("Delay"),
        QStringLiteral("Distortion"),
        QStringLiteral("ExpressFX Chorus"),
        QStringLiteral("ExpressFX Delay"),
        QStringLiteral("ExpressFX Distortion"),
        QStringLiteral("ExpressFX EQ"),
        QStringLiteral("Flange"),
        QStringLiteral("Noise Gate"),
        QStringLiteral("Pitch Shift"),
        QStringLiteral("Reverb"),
        QStringLiteral("Simple Delay"),
        QStringLiteral("Smooth/Enhance"),
        QStringLiteral("Time Stretch"),
        QStringLiteral("Vibrato"),
    };
    for (const QString &n : vegasFx) {
        out.push_back(make(n, QStringLiteral("VEGAS")));
    }

    // MAGIX essentialFX-style
    out.push_back(make(QStringLiteral("eFX ChorusFlanger"), QStringLiteral("VEGAS"),
                       QStringLiteral("MAGIX")));
    out.push_back(
        make(QStringLiteral("eFX Compressor"), QStringLiteral("VEGAS"), QStringLiteral("MAGIX")));
    out.push_back(make(QStringLiteral("eFX Gate"), QStringLiteral("VEGAS"), QStringLiteral("MAGIX")));
    out.push_back(
        make(QStringLiteral("eFX Reverb"), QStringLiteral("VEGAS"), QStringLiteral("MAGIX")));

    // Common third-party names shown in Vegas chooser (catalog only)
    const QStringList third = {
        QStringLiteral("Auto-Key"),
        QStringLiteral("GClip"),
        QStringLiteral("GGate"),
        QStringLiteral("GMulti"),
        QStringLiteral("GNormal"),
        QStringLiteral("GSnap"),
    };
    for (const QString &n : third) {
        out.push_back(make(n, QStringLiteral("Third Party"), QStringLiteral("Third Party")));
    }

    // 5.1 FX placeholder category entry
    out.push_back(make(QStringLiteral("Surround Panner"), QStringLiteral("5.1 FX")));

    return out;
}

QVector<FxSlot> BuiltinAudioCatalog::defaultTrackFxChain()
{
    // Vegas Audio Track FX default order: Noise Gate → EQ → Compressor
    return {
        makeTrackFx(QStringLiteral("Track Noise Gate"), defaultNoiseGateParams()),
        makeTrackFx(QStringLiteral("Track EQ"), defaultTrackEqParams()),
        makeTrackFx(QStringLiteral("Track Compressor"), defaultTrackCompressorParams()),
    };
}

} // namespace openvegas
