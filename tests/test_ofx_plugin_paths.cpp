#include "plugins/OfxHost.h"
#include "plugins/OfxPluginPaths.h"
#include "plugins/PluginScanner.h"
#include "plugins/VegasVideoPluginCatalog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

using openvegas::OfxHost;
using openvegas::OfxPluginPaths;

namespace {

void ensureQtApp(int &argc, char **argv)
{
    if (!QCoreApplication::instance()) {
        static QCoreApplication app(argc, argv);
        Q_UNUSED(app);
    }
}

/** Arch directory name that is definitely *not* the one this build can load. */
QString foreignArchFolderName()
{
    for (const QString &known : OfxPluginPaths::knownArchFolderNames()) {
        if (!OfxPluginPaths::isArchLoadable(known)) {
            return known;
        }
    }
    return {};
}

/** Create `<root>/<name>.ofx.bundle/Contents/<arch>/<name>.ofx` with dummy content. */
QString makeBundle(const QString &root, const QString &name, const QString &arch)
{
    const QString bundle = QDir(root).filePath(name + QStringLiteral(".ofx.bundle"));
    const QString archDir = QDir(bundle).filePath(QStringLiteral("Contents/") + arch);
    REQUIRE(QDir().mkpath(archDir));
    QFile f(QDir(archDir).filePath(name + QStringLiteral(".ofx")));
    REQUIRE(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write("not a real binary");
    f.close();
    return bundle;
}

QString fixtureGainOfx()
{
#ifdef OPENVGAS_OFX_FIXTURE_DIR
    return QDir(QString::fromUtf8(OPENVGAS_OFX_FIXTURE_DIR)).filePath(QStringLiteral("Gain.ofx"));
#else
    return {};
#endif
}

} // namespace

TEST_CASE("OFX_PLUGIN_PATH entries come first in the standard roots", "[media][ofx][paths]")
{
    int argc = 1;
    char arg0[] = "test";
    char *argv[] = {arg0, nullptr};
    ensureQtApp(argc, argv);

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString a = QDir(tmp.path()).filePath(QStringLiteral("ofx-a"));
    const QString b = QDir(tmp.path()).filePath(QStringLiteral("ofx-b"));
    REQUIRE(QDir().mkpath(a));
    REQUIRE(QDir().mkpath(b));

#ifdef Q_OS_WIN
    const QString sep = QStringLiteral(";");
#else
    const QString sep = QStringLiteral(":");
#endif
    qputenv("OFX_PLUGIN_PATH", (a + sep + b).toUtf8());
    const QStringList roots = OfxPluginPaths::standardRoots();
    qunsetenv("OFX_PLUGIN_PATH");

    REQUIRE(roots.size() >= 2);
    REQUIRE(QDir(roots.at(0)).absolutePath() == QDir(a).absolutePath());
    REQUIRE(QDir(roots.at(1)).absolutePath() == QDir(b).absolutePath());
}

TEST_CASE("Only the running platform's ABI is loadable", "[media][ofx][paths]")
{
    const QStringList loadable = OfxPluginPaths::loadableArchFolderNames();
    REQUIRE_FALSE(loadable.isEmpty());
    for (const QString &arch : loadable) {
        REQUIRE(OfxPluginPaths::isArchLoadable(arch));
        REQUIRE(OfxPluginPaths::archIncompatibilityReason(arch).isEmpty());
    }

#if defined(Q_OS_WIN)
    REQUIRE(loadable.contains(QStringLiteral("Win64")));
    REQUIRE_FALSE(OfxPluginPaths::isArchLoadable(QStringLiteral("Linux-x86-64")));
    REQUIRE_FALSE(OfxPluginPaths::isArchLoadable(QStringLiteral("MacOS")));
#elif defined(Q_OS_MACOS)
    REQUIRE(loadable.contains(QStringLiteral("MacOS")));
    REQUIRE_FALSE(OfxPluginPaths::isArchLoadable(QStringLiteral("Win64")));
#else
    REQUIRE(loadable.contains(QStringLiteral("Linux-x86-64"))
            || loadable.contains(QStringLiteral("Linux-arm-64"))
            || loadable.contains(QStringLiteral("Linux-x86"))
            || loadable.contains(QStringLiteral("Linux-arm-32")));
    REQUIRE_FALSE(OfxPluginPaths::isArchLoadable(QStringLiteral("Win64")));
#endif

    // A flat bundle with no Contents/<arch> layout carries no ABI claim; only the loader
    // can judge it, so it must not be pre-emptively rejected.
    REQUIRE(OfxPluginPaths::isArchLoadable(QString()));
}

TEST_CASE("A bundle built for another platform is described but never loaded",
          "[media][ofx][paths]")
{
    int argc = 1;
    char arg0[] = "test";
    char *argv[] = {arg0, nullptr};
    ensureQtApp(argc, argv);

    const QString foreign = foreignArchFolderName();
    REQUIRE_FALSE(foreign.isEmpty());

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString bundle = makeBundle(tmp.path(), QStringLiteral("Foreign"), foreign);

    const openvegas::OfxPluginDesc d = OfxHost::describe(bundle);
    // Still discoverable and nameable — a project referencing it must keep opening.
    REQUIRE(d.name == QStringLiteral("Foreign"));
    REQUIRE(d.hasBinary);
    REQUIRE(d.archHint == foreign);
    REQUIRE_FALSE(d.archLoadable);
    REQUIRE(d.archNote.contains(foreign));

    // …but the loader must refuse rather than hand a foreign binary to dlopen.
    QString err;
    REQUIRE_FALSE(OfxHost::load(d, &err));
    REQUIRE(err.contains(foreign));
    REQUIRE(OfxHost::effectIndexMap(d.path).isEmpty());
    REQUIRE(OfxHost::enumerateEffects(d.path).isEmpty());
}

TEST_CASE("A native-ABI bundle is preferred over a foreign one in the same bundle",
          "[media][ofx][paths]")
{
    const QString foreign = foreignArchFolderName();
    const QString native = OfxPluginPaths::hostArchFolderName();
    REQUIRE_FALSE(foreign.isEmpty());
    REQUIRE_FALSE(native.isEmpty());

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString bundle = makeBundle(tmp.path(), QStringLiteral("Fat"), foreign);
    const QString nativeDir = QDir(bundle).filePath(QStringLiteral("Contents/") + native);
    REQUIRE(QDir().mkpath(nativeDir));
    QFile f(QDir(nativeDir).filePath(QStringLiteral("Fat.ofx")));
    REQUIRE(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write("x");
    f.close();

    const openvegas::OfxPluginDesc d = OfxHost::describe(bundle);
    REQUIRE(d.archHint == native);
    REQUIRE(d.archLoadable);
}

TEST_CASE("PluginScanner includes the standard OFX roots", "[media][ofx][paths]")
{
    int argc = 1;
    char arg0[] = "test";
    char *argv[] = {arg0, nullptr};
    ensureQtApp(argc, argv);

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString root = QDir(tmp.path()).filePath(QStringLiteral("std-ofx"));
    REQUIRE(QDir().mkpath(root));
    qputenv("OFX_PLUGIN_PATH", root.toUtf8());

    const QStringList roots = openvegas::PluginScanner().candidateRoots();
    qunsetenv("OFX_PLUGIN_PATH");

    bool found = false;
    for (const QString &r : roots) {
        if (QDir(r).absolutePath() == QDir(root).absolutePath()) {
            found = true;
            break;
        }
    }
    // Without this, a Linux or macOS build has nowhere to find OFX plug-ins at all:
    // every other candidate root is a VEGAS installation guess.
    REQUIRE(found);
}

TEST_CASE("enumerateEffects reads identity straight out of the binary",
          "[media][ofx][paths][plugins]")
{
    int argc = 1;
    char arg0[] = "test";
    char *argv[] = {arg0, nullptr};
    ensureQtApp(argc, argv);

    const QString path = fixtureGainOfx();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        SKIP("Gain.ofx fixture not built (OPENVGAS_OFX_FIXTURE_DIR)");
    }

    const QVector<openvegas::OfxEffectSummary> effects = OfxHost::enumerateEffects(path);
    REQUIRE_FALSE(effects.isEmpty());
    REQUIRE_FALSE(effects.first().effectId.isEmpty());
    // Label must never be empty — it is what a chooser shows for a plug-in that has no
    // VEGAS resource manifest, which is every non-VEGAS plug-in.
    REQUIRE_FALSE(effects.first().label.isEmpty());
    REQUIRE(effects.first().pluginIndex == 0);
}

TEST_CASE("A bundle with no VEGAS manifest still reaches the catalog",
          "[media][ofx][paths][plugins]")
{
    int argc = 1;
    char arg0[] = "test";
    char *argv[] = {arg0, nullptr};
    ensureQtApp(argc, argv);

    const QString path = fixtureGainOfx();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        SKIP("Gain.ofx fixture not built (OPENVGAS_OFX_FIXTURE_DIR)");
    }

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString bundle = QDir(tmp.path()).filePath(QStringLiteral("Gain.ofx.bundle"));
    const QString archDir =
        QDir(bundle).filePath(QStringLiteral("Contents/") + OfxPluginPaths::hostArchFolderName());
    REQUIRE(QDir().mkpath(archDir));
    REQUIRE(QFile::copy(path, QDir(archDir).filePath(QStringLiteral("Gain.ofx"))));

    openvegas::VegasVideoPluginCatalog::invalidateCache();
    const auto entries = openvegas::VegasVideoPluginCatalog::discover({tmp.path()});
    openvegas::VegasVideoPluginCatalog::invalidateCache();

    REQUIRE_FALSE(entries.isEmpty());
    REQUIRE(entries.first().hasBinary);
    REQUIRE_FALSE(entries.first().effectId.isEmpty());
    REQUIRE_FALSE(entries.first().displayName.isEmpty());
}
