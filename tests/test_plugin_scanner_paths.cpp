#include "plugins/PluginScanner.h"
#include "plugins/VegasVideoPluginCatalog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
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
    const QString bundlePath = QDir(ofxDir).filePath(QStringLiteral("DummyOfxPlugin.ofx.bundle"));
    REQUIRE(QDir().mkpath(bundlePath));
    const QString resDir = QDir(bundlePath).filePath(QStringLiteral("Contents/Resources"));
    REQUIRE(QDir().mkpath(resDir));
    QFile xml(QDir(resDir).filePath(QStringLiteral("DummyOfxPlugin.xml")));
    REQUIRE(xml.open(QIODevice::WriteOnly | QIODevice::Truncate));
    xml.write(R"(<?xml version="1.0" encoding="utf-8"?>
<OfxImageEffectResource>
  <OfxPlugin name="com.example:dummy">
    <OfxResourceSet ofxHost="default">
      <OfxPropLabel>VEGAS Dummy Filter</OfxPropLabel>
      <OfxImageEffectPluginPropGrouping>VEGAS\Utility</OfxImageEffectPluginPropGrouping>
    </OfxResourceSet>
  </OfxPlugin>
</OfxImageEffectResource>
)");
    xml.close();

    scan.setVegasProPath(vegasRoot);
    scan.setPreferredPath(QString());
    openvegas::VegasVideoPluginCatalog::invalidateCache();

    const QStringList roots = scan.candidateRoots();
    REQUIRE_FALSE(roots.isEmpty());
    REQUIRE(QDir(roots.first()).absolutePath() == vegasRoot);

    const auto plugins = scan.scanOfx();
    REQUIRE_FALSE(plugins.isEmpty());
    REQUIRE(plugins.first().name == QLatin1String("Dummy Filter"));
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
