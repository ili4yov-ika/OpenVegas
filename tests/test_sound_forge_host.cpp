#include "plugins/SoundForgeHost.h"

#include <QCoreApplication>
#include <QElapsedTimer>

#include <QSet>

#include <catch2/catch_test_macros.hpp>

using openvegas::SoundForgeClass;
using openvegas::SoundForgeHost;

TEST_CASE("Sound Forge discovery is honest about availability", "[plugins][soundforge]")
{
    const QVector<SoundForgeClass> all = SoundForgeHost::discoverRegistered();
    const QVector<SoundForgeClass> fx = SoundForgeHost::discoverEffects();

#ifndef Q_OS_WIN
    // COM registration is a Windows concept and the packs ship x64 PE DLLs only.
    REQUIRE(all.isEmpty());
    REQUIRE(fx.isEmpty());
    return;
#else
    if (all.isEmpty()) {
        SKIP("no VEGAS / Sound Forge Shared Plug-Ins registered on this machine");
    }

    for (const SoundForgeClass &c : all) {
        INFO(c.name.toStdString() << " " << c.clsid.toStdString());
        // A CLSID key name is always the braced GUID form.
        REQUIRE(c.clsid.startsWith(QLatin1Char('{')));
        REQUIRE(c.clsid.endsWith(QLatin1Char('}')));
        REQUIRE(c.clsid.size() == 38);
        REQUIRE_FALSE(c.dllFileName.isEmpty());
        REQUIRE(c.dllPath.contains(QLatin1String("Shared Plug-Ins"), Qt::CaseInsensitive));
    }

    // Property pages are companions of an effect, not effects: they must not reach the
    // effect list, or the chooser fills up with "SfReverb Property Page" entries.
    REQUIRE(fx.size() < all.size());
    for (const SoundForgeClass &c : fx) {
        REQUIRE_FALSE(c.name.contains(QLatin1String("Property Page"), Qt::CaseInsensitive));
    }

    // Sorted by name, so the chooser order is stable between runs.
    for (int i = 1; i < fx.size(); ++i) {
        REQUIRE(QString::localeAwareCompare(fx[i - 1].name, fx[i].name) <= 0);
    }
#endif
}

TEST_CASE("Sound Forge discovery caches its registry scan", "[plugins][soundforge]")
{
    SoundForgeHost::invalidateCache();
    QElapsedTimer clock;
    clock.start();
    const int first = SoundForgeHost::discoverRegistered().size();
    const qint64 cold = clock.elapsed();

    clock.restart();
    const int second = SoundForgeHost::discoverRegistered().size();
    const qint64 warm = clock.elapsed();

    REQUIRE(first == second);
    // The scan walks every class in HKCR\CLSID, so repeating it per chooser open would be
    // felt. The cache must make the second call effectively free.
    REQUIRE(warm <= cold);
    REQUIRE(warm < 50);
}

#include "plugins/VegasSharedAudioCatalog.h"

TEST_CASE("Curated Sound Forge names match the real COM registration",
          "[plugins][soundforge]")
{
    // The hand-written half of the catalogue names effects that must exist for its
    // builtin substitutions to line up with what a user sees in VEGAS. Those names had
    // drifted — "Wave Hammer" is registered as "Wave Hammer Surround", "Flange" as
    // "Flange/Wah-wah" — and nothing caught it. This does.
    const QVector<openvegas::SoundForgeClass> registered = SoundForgeHost::discoverEffects();
    if (registered.isEmpty()) {
        SKIP("no VEGAS / Sound Forge Shared Plug-Ins registered on this machine");
    }
    QSet<QString> realNames;
    for (const openvegas::SoundForgeClass &c : registered) {
        realNames.insert(c.name.toLower());
    }

    for (const openvegas::VegasSharedFxEntry &e :
         openvegas::VegasSharedAudioCatalog::catalog()) {
        if (e.openvegasBuiltinName.isEmpty()) {
            continue; // Unmapped rows include the runtime DLL and registry-sourced extras
        }
        INFO("curated name: " << e.vegasFxName.toStdString());
        REQUIRE(realNames.contains(e.vegasFxName.toLower()));
    }
}

TEST_CASE("Registered effects missing from the curated map still reach the catalogue",
          "[plugins][soundforge]")
{
    const QVector<openvegas::SoundForgeClass> registered = SoundForgeHost::discoverEffects();
    if (registered.isEmpty()) {
        SKIP("no VEGAS / Sound Forge Shared Plug-Ins registered on this machine");
    }
    QSet<QString> catalogNames;
    for (const openvegas::VegasSharedFxEntry &e :
         openvegas::VegasSharedAudioCatalog::catalog()) {
        catalogNames.insert(e.vegasFxName.toLower());
    }
    // Completeness: the inventory is the registration, not a hand-kept list.
    for (const openvegas::SoundForgeClass &c : registered) {
        INFO("registered effect: " << c.name.toStdString());
        REQUIRE(catalogNames.contains(c.name.toLower()));
    }
}
