#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "audio/BuiltinDsp.h"
#include "plugins/VegasSharedAudioCatalog.h"

#include <QFileInfo>
#include <QSet>

#include <cmath>
#include <vector>

using namespace openvegas;

TEST_CASE("Vegas Shared catalog has TrackFX / XFX / ExpressFX entries", "[vegas-shared]")
{
    const auto cat = VegasSharedAudioCatalog::catalog();
    REQUIRE(cat.size() >= 15);

    bool hasTrackEq = false;
    bool hasChorus = false;
    bool hasApluginsk = false;
    for (const VegasSharedFxEntry &e : cat) {
        if (e.vegasFxName == QLatin1String("Track EQ")) {
            hasTrackEq = true;
            REQUIRE(e.status == VegasSharedReplacementStatus::Implemented);
            REQUIRE(e.openvegasBuiltinName == QLatin1String("Track EQ"));
            REQUIRE(e.dllFileName.contains(QLatin1String("sftrkfx1"), Qt::CaseInsensitive));
        }
        if (e.vegasFxName == QLatin1String("Chorus")) {
            hasChorus = true;
            REQUIRE(e.status == VegasSharedReplacementStatus::Implemented);
        }
        if (e.dllFileName.compare(QLatin1String("apluginsk.dll"), Qt::CaseInsensitive) == 0) {
            hasApluginsk = true;
            REQUIRE(e.status == VegasSharedReplacementStatus::Unmapped);
        }
    }
    REQUIRE(hasTrackEq);
    REQUIRE(hasChorus);
    REQUIRE(hasApluginsk);
}

TEST_CASE("Vegas Shared resolveBuiltinName aliases", "[vegas-shared]")
{
    REQUIRE(VegasSharedAudioCatalog::resolveBuiltinName(QStringLiteral("VEGAS Track EQ"))
            == QLatin1String("Track EQ"));
    REQUIRE(VegasSharedAudioCatalog::resolveBuiltinName(QStringLiteral("TrackEQ"))
            == QLatin1String("Track EQ"));
    REQUIRE(VegasSharedAudioCatalog::resolveBuiltinName(QStringLiteral("ExpressFX Reverb"))
            == QLatin1String("Reverb"));
    REQUIRE(VegasSharedAudioCatalog::resolveBuiltinName(QStringLiteral("Resonant Filter")).isEmpty());
}

TEST_CASE("Vegas Shared replacementSlot yields processable builtins", "[vegas-shared][dsp]")
{
    for (const VegasSharedFxEntry &e : VegasSharedAudioCatalog::implementedEntries()) {
        FxSlot slot = VegasSharedAudioCatalog::replacementSlot(e.vegasFxName);
        REQUIRE_FALSE(slot.displayName.isEmpty());
        REQUIRE(slot.format == PluginFormat::Builtin);
        REQUIRE(slot.pluginId.startsWith(QLatin1String("builtin:")));

        BuiltinDspState st;
        st.prepare(48000.0);
        constexpr int N = 512;
        std::vector<float> L(N, 0.1f), R(N, 0.1f);
        // Must not crash; Implemented names are wired in BuiltinDsp.
        processBuiltinFx(&slot, &st, L.data(), R.data(), N);
        float peak = 0.f;
        for (float v : L) {
            peak = std::max(peak, std::abs(v));
        }
        REQUIRE(std::isfinite(peak));
    }
}

TEST_CASE("Vegas Shared discoverInstalled optional", "[vegas-shared][discovery]")
{
    const auto packs = VegasSharedAudioCatalog::discoverInstalled();
    if (packs.isEmpty()) {
        WARN("No VEGAS/Sony Shared Plug-Ins Audio_x64 DLLs on this machine — discovery SKIP");
        return;
    }
    INFO("Installed Shared packs: " << packs.size());
    bool hasTrackFx = false;
    for (const VegasSharedInstalledPack &p : packs) {
        REQUIRE(QFileInfo::exists(p.absolutePath));
        REQUIRE(p.absolutePath.contains(QLatin1String("Audio_x64"), Qt::CaseInsensitive));
        if (p.dllFileName.contains(QLatin1String("sftrkfx1"), Qt::CaseInsensitive)) {
            hasTrackFx = true;
        }
    }
    // On a typical Vegas Pro 22 machine TrackFX pack is present.
    CHECK(hasTrackFx);
}

TEST_CASE("Vegas Shared chooserDescriptors unique builtin ids", "[vegas-shared]")
{
    const auto descs = VegasSharedAudioCatalog::chooserDescriptors(false);
    REQUIRE_FALSE(descs.isEmpty());
    QSet<QString> ids;
    for (const AudioPluginDesc &d : descs) {
        REQUIRE(d.category == QLatin1String("VEGAS Shared"));
        REQUIRE(d.format == PluginFormat::Builtin);
        REQUIRE_FALSE(ids.contains(d.id));
        ids.insert(d.id);
    }
}
