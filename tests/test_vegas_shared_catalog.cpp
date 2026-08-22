#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "audio/BuiltinDsp.h"
#include "plugins/SoundForgeHost.h"
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
    // This asks specifically about the substitutes, so pin the policy rather than
    // inherit whatever the machine happens to be set to.
    const VegasSharedSubstitution original = VegasSharedAudioCatalog::substitutionPolicy();
    struct Restore {
        VegasSharedSubstitution value;
        ~Restore() { VegasSharedAudioCatalog::setSubstitutionPolicy(value); }
    } restore{original};
    VegasSharedAudioCatalog::setSubstitutionPolicy(VegasSharedSubstitution::ReplaceWithBuiltin);

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

TEST_CASE("Vegas Shared chooserDescriptors unique ids", "[vegas-shared]")
{
    const auto descs = VegasSharedAudioCatalog::chooserDescriptors(false);
    REQUIRE_FALSE(descs.isEmpty());
    QSet<QString> ids;
    for (const AudioPluginDesc &d : descs) {
        INFO(d.name.toStdString() << " / " << d.id.toStdString());
        REQUIRE(d.category == QLatin1String("VEGAS Shared"));
        // Two kinds live here now: DirectShow entries are the registered Shared
        // Plug-Ins hosted for real, Builtin entries are OpenVegas substitutes for the
        // names no registered plug-in covers (or for machines without VEGAS at all).
        REQUIRE((d.format == PluginFormat::Builtin || d.format == PluginFormat::DirectShow));
        if (d.format == PluginFormat::DirectShow) {
            REQUIRE(d.id.startsWith(QLatin1String("sfds:{")));
        } else {
            REQUIRE(d.id.startsWith(QLatin1String("builtin:")));
        }
        // Ids address either a builtin or a COM class; a duplicate would make two
        // chooser rows resolve to the same instance.
        REQUIRE_FALSE(ids.contains(d.id));
        ids.insert(d.id);
    }
}

TEST_CASE("Substitution policy decides who owns a shared FX name", "[vegas-shared][policy]")
{
    // This writes a real user setting, so put it back whatever the test does.
    const VegasSharedSubstitution original = VegasSharedAudioCatalog::substitutionPolicy();
    struct Restore {
        VegasSharedSubstitution value;
        ~Restore() { VegasSharedAudioCatalog::setSubstitutionPolicy(value); }
    } restore{original};

    auto namesFor = [](PluginFormat format) {
        QSet<QString> names;
        for (const AudioPluginDesc &d : VegasSharedAudioCatalog::chooserDescriptors(false)) {
            if (d.format == format) {
                names.insert(normalizeVegasPluginKey(d.name));
            }
        }
        return names;
    };

    VegasSharedAudioCatalog::setSubstitutionPolicy(VegasSharedSubstitution::ReplaceWithBuiltin);
    REQUIRE(VegasSharedAudioCatalog::substitutionPolicy()
            == VegasSharedSubstitution::ReplaceWithBuiltin);
    const QSet<QString> builtinWhenReplacing = namesFor(PluginFormat::Builtin);
    const QSet<QString> hostedWhenReplacing = namesFor(PluginFormat::DirectShow);

    VegasSharedAudioCatalog::setSubstitutionPolicy(VegasSharedSubstitution::UseOriginal);
    REQUIRE(VegasSharedAudioCatalog::substitutionPolicy() == VegasSharedSubstitution::UseOriginal);
    const QSet<QString> builtinWhenOriginal = namesFor(PluginFormat::Builtin);
    const QSet<QString> hostedWhenOriginal = namesFor(PluginFormat::DirectShow);

    // Whichever way round, a name is offered exactly once — two rows reading the same
    // would be indistinguishable in the chooser while behaving differently once inserted.
    CHECK((builtinWhenReplacing & hostedWhenReplacing).isEmpty());
    CHECK((builtinWhenOriginal & hostedWhenOriginal).isEmpty());

    if (!SoundForgeHost::anyRegistered()) {
        WARN("No Shared Plug-Ins registered — the two modes cannot differ here");
        return;
    }

    // A name OpenVegas implements must swap sides with the policy, and only that side.
    const QSet<QString> contested = builtinWhenReplacing & hostedWhenOriginal;
    INFO("names implemented on both sides: " << contested.size());
    CHECK_FALSE(contested.isEmpty());
    for (const QString &name : contested) {
        INFO(name.toStdString() << " should come from the plug-in when substitution is off");
        CHECK_FALSE(builtinWhenOriginal.contains(name));
    }
    // Effects OpenVegas has no implementation for come from the plug-in either way,
    // otherwise turning substitution on would lose most of the catalog.
    CHECK_FALSE(hostedWhenReplacing.isEmpty());
    CHECK(hostedWhenOriginal.size() > hostedWhenReplacing.size());
}
