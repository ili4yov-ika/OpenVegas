#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "audio/Vst3Host.h"
#include "plugins/AudioPluginScanner.h"
#include "plugins/AudioPluginTypes.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

using namespace openvegas;

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

TEST_CASE("Vst3Host stub create/process/state round-trip", "[vst3][host]")
{
    int argc = 0;
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, nullptr);

    AudioPluginDesc desc;
    desc.name = QStringLiteral("Stub VST3");
    desc.format = PluginFormat::Vst3;
    desc.path = QStringLiteral("C:/Plugins/Stub.vst3");
    desc.id = QStringLiteral("vst3:stub");

    FxSlot slot;
    REQUIRE(Vst3Host::instance().createInstance(desc, &slot));
    REQUIRE(slot.pluginId.startsWith(QStringLiteral("vst3:")));
    REQUIRE_FALSE(Vst3Host::hasSdk()); // CI / default builds without Steinberg SDK

    Vst3Host::instance().prepare(&slot, 48000.0, 128);
    float inL[4] = {0.25f, 0.5f, 0.75f, 1.0f};
    float inR[4] = {0.1f, 0.2f, 0.3f, 0.4f};
    float outL[4] = {};
    float outR[4] = {};
    float *in[2] = {inL, inR};
    float *out[2] = {outL, outR};
    Vst3Host::instance().process(&slot, in, out, 2, 4);
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

TEST_CASE("videoFxSlotFromName maps Color Corrector builtin", "[vst3][types]")
{
    const FxSlot cc = videoFxSlotFromName(QStringLiteral("Color Corrector"));
    REQUIRE(cc.format == PluginFormat::Builtin);
    REQUIRE(cc.displayName == QStringLiteral("Color Corrector"));

    const FxSlot ofx = videoFxSlotFromName(QStringLiteral("Gaussian Blur"));
    REQUIRE(ofx.format == PluginFormat::Ofx);
}
