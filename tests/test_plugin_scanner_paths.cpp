#include "plugins/PluginScanner.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

namespace {

void ensureQtApp(int &argc, char **argv)
{
    if (!QCoreApplication::instance()) {
        static QCoreApplication app(argc, argv);
        Q_UNUSED(app);
    }
}

} // namespace

TEST_CASE("PluginScanner candidateRoots prefers vegasProPath", "[media][plugins]")
{
    int argc = 1;
    char arg0[] = "openvegas_media_tests";
    char *argv[] = {arg0, nullptr};
    ensureQtApp(argc, argv);

    openvegas::PluginScanner scan;
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString vegasRoot = QDir(tmp.path()).absolutePath();
    const QString ofxDir = QDir(vegasRoot).filePath(QStringLiteral("OFX Video Plug-Ins"));
    REQUIRE(QDir().mkpath(ofxDir));
    REQUIRE(QDir().mkpath(QDir(ofxDir).filePath(QStringLiteral("DummyOfxPlugin.ofx.bundle"))));

    scan.setVegasProPath(vegasRoot);
    scan.setPreferredPath(QString());

    const QStringList roots = scan.candidateRoots();
    REQUIRE_FALSE(roots.isEmpty());
    REQUIRE(QDir(roots.first()).absolutePath() == vegasRoot);

    const auto plugins = scan.scanOfx();
    REQUIRE_FALSE(plugins.isEmpty());
    REQUIRE(QDir(scan.resolvedSource()).absolutePath() == vegasRoot);
}

TEST_CASE("PluginScanner sampleVegasProPath is empty or existing dir", "[media][plugins]")
{
    int argc = 1;
    char arg0[] = "openvegas_media_tests";
    char *argv[] = {arg0, nullptr};
    ensureQtApp(argc, argv);

    const QString sample = openvegas::PluginScanner::sampleVegasProPath();
    if (!sample.isEmpty()) {
        REQUIRE(QDir(sample).exists());
    }
}
