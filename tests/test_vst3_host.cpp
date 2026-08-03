#include <cmath>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "audio/Vst3Host.h"
#include "plugins/AudioPluginScanner.h"
#include "plugins/AudioPluginTypes.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

using namespace openvegas;

namespace {

QString findFixtureVst3()
{
    const QByteArray env = qgetenv("OPENVGAS_TEST_VST3");
    if (!env.isEmpty() && QFileInfo::exists(QString::fromLocal8Bit(env))) {
        return QString::fromLocal8Bit(env);
    }
    const QStringList roots = {
        QStringLiteral("tests/fixtures/plugins"),
        QStringLiteral("SAMPLES/plugins"),
    };
    for (const QString &root : roots) {
        QDir dir(root);
        if (!dir.exists()) {
            continue;
        }
        const QFileInfoList bundles =
            dir.entryInfoList({QStringLiteral("*.vst3")}, QDir::Dirs | QDir::Files);
        for (const QFileInfo &fi : bundles) {
            return fi.absoluteFilePath();
        }
    }
    return {};
}

} // namespace

TEST_CASE("AudioPluginScanner finds .vst3 bundles", "[vst3][scan]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString bundle = dir.path() + QStringLiteral("/Demo.vst3");
    REQUIRE(QDir().mkpath(bundle));

    AudioPluginScanner scanner;
    scanner.setVst3Paths({dir.path()});
    scanner.setVst1Paths({});
    scanner.setVst2Paths({});
    const auto found = scanner.scan();
    REQUIRE(found.size() >= 1);
    bool hit = false;
    for (const AudioPluginDesc &d : found) {
        if (d.format == PluginFormat::Vst3 && d.path.contains(QStringLiteral("Demo.vst3"))) {
            hit = true;
            REQUIRE(d.name.contains(QStringLiteral("Demo")));
        }
    }
    REQUIRE(hit);
}

TEST_CASE("Vst3Host create/process/state", "[vst3][host]")
{
    int argc = 0;
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, nullptr);

#ifdef OPENVGAS_HAS_VST3_SDK
    REQUIRE(Vst3Host::hasSdk());
#else
    REQUIRE_FALSE(Vst3Host::hasSdk());
#endif

    AudioPluginDesc desc;
    desc.name = QStringLiteral("Stub VST3");
    desc.format = PluginFormat::Vst3;
    desc.path = QStringLiteral("C:/Plugins/Stub.vst3");
    desc.id = QStringLiteral("vst3:stub");

    FxSlot slot;
    REQUIRE(Vst3Host::instance().createInstance(desc, &slot));
    REQUIRE(slot.pluginId.startsWith(QStringLiteral("vst3:")));

    Vst3Host::instance().prepare(&slot, 48000.0, 128);
    float inL[4] = {0.25f, 0.5f, 0.75f, 1.0f};
    float inR[4] = {0.1f, 0.2f, 0.3f, 0.4f};
    float outL[4] = {};
    float outR[4] = {};
    float *in[2] = {inL, inR};
    float *out[2] = {outL, outR};
    Vst3Host::instance().process(&slot, in, out, 2, 4);
    // Without a real module, process is pass-through.
    REQUIRE(outL[0] == Catch::Approx(0.25f));
    REQUIRE(outR[2] == Catch::Approx(0.3f));

    const QByteArray state("vst3-state-chunk");
    REQUIRE(Vst3Host::instance().setState(&slot, state));
    REQUIRE(Vst3Host::instance().getState(&slot) == state);

    slot.bypass = true;
    outL[0] = 0.f;
    Vst3Host::instance().process(&slot, in, out, 2, 4);
    REQUIRE(outL[0] == Catch::Approx(0.25f));

    Vst3Host::instance().releaseInstance(&slot);
}

TEST_CASE("Vst3Host process fixture module", "[vst3][host][fixture]")
{
    int argc = 0;
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, nullptr);

    if (!Vst3Host::hasSdk()) {
        SKIP("Built without OPENVGAS_HAS_VST3_SDK");
    }
    const QString path = findFixtureVst3();
    if (path.isEmpty()) {
        SKIP("No fixture .vst3 (set OPENVGAS_TEST_VST3 or add tests/fixtures/plugins/*.vst3)");
    }

    AudioPluginDesc desc;
    desc.name = QFileInfo(path).completeBaseName();
    desc.format = PluginFormat::Vst3;
    desc.path = path;
    desc.id = QStringLiteral("vst3:") + path;

    FxSlot slot;
    REQUIRE(Vst3Host::instance().createInstance(desc, &slot));
    Vst3Host::instance().prepare(&slot, 48000.0, 64);

    float inL[64];
    float inR[64];
    float outL[64] = {};
    float outR[64] = {};
    for (int i = 0; i < 64; ++i) {
        inL[i] = 0.25f;
        inR[i] = 0.25f;
    }
    float *in[2] = {inL, inR};
    float *out[2] = {outL, outR};
    Vst3Host::instance().process(&slot, in, out, 2, 64);
    // Soft check: process must not crash; output finite.
    REQUIRE(std::isfinite(outL[0]));
    REQUIRE(std::isfinite(outR[0]));

    const QByteArray st = Vst3Host::instance().getState(&slot);
    REQUIRE(Vst3Host::instance().setState(&slot, st));

    Vst3Host::instance().releaseInstance(&slot);
}

TEST_CASE("videoFxSlotFromName maps Color Corrector builtin", "[vst3][types]")
{
    const FxSlot cc = videoFxSlotFromName(QStringLiteral("Color Corrector"));
    REQUIRE(cc.format == PluginFormat::Builtin);
    REQUIRE(cc.displayName == QStringLiteral("Color Corrector"));

    const FxSlot ofx = videoFxSlotFromName(QStringLiteral("Gaussian Blur"));
    REQUIRE(ofx.format == PluginFormat::Ofx);
}
