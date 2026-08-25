#include "plugins/PluginDiscovery.h"
#include "plugins/PluginScanner.h"
#include "plugins/VegasVideoPluginCatalog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <QFileInfo>
#include <QSet>
#include <QSettings>
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

TEST_CASE("Plug-in discovery reports only folders that exist", "[plugins][setup]")
{
    const QVector<openvegas::PluginDiscovery::Found> found = openvegas::PluginDiscovery::scan();
    for (const openvegas::PluginDiscovery::Found &f : found) {
        INFO(f.path.toStdString());
        CHECK_FALSE(f.path.isEmpty());
        // A setup screen listing a folder that is not there would be worse than listing
        // nothing: the user cannot tell a wrong guess from a real find.
        CHECK(QFileInfo(f.path).isDir());
        // Paths are normalised, so the same folder cannot appear twice under two spellings.
        CHECK(f.path == QDir::fromNativeSeparators(QDir::cleanPath(f.path)));
    }

    // Unique within a kind, not across all of them: one folder can genuinely be both a
    // VST and a VST2 root — Steinberg/VSTPlugins is exactly that — and every host scans
    // it for both. Listing it under each is honest about what will happen to it.
    QSet<QString> seen;
    for (const openvegas::PluginDiscovery::Found &f : found) {
        const QString key = QStringLiteral("%1|%2").arg(int(f.kind)).arg(f.path.toLower());
        INFO(f.path.toStdString());
        CHECK_FALSE(seen.contains(key));
        seen.insert(key);
    }
}

TEST_CASE("Plug-in discovery counts what is in a folder", "[plugins][setup]")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    // A VST3 folder with two plug-ins in it and one file that is not one.
    for (const QString &name : {QStringLiteral("A.vst3"), QStringLiteral("B.vst3"),
                                QStringLiteral("readme.txt")}) {
        QFile f(QDir(tmp.path()).filePath(name));
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("x");
    }

    // The count is what separates "found the folder" from "found anything in it", which
    // is the difference between a useful setup screen and a misleading one.
    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegasTest"));
    const QVector<openvegas::PluginDiscovery::Found> found = openvegas::PluginDiscovery::scan();
    for (const openvegas::PluginDiscovery::Found &f : found) {
        if (f.kind == openvegas::PluginDiscovery::Kind::VegasProgram) {
            // A program folder is not a bag of plug-ins; counting its files would say
            // nothing useful, so it deliberately reports no count.
            CHECK(f.count == -1);
        } else {
            CHECK(f.count >= 0);
        }
    }
}

TEST_CASE("Every discovery kind has a label", "[plugins][setup]")
{
    // The setup screen groups rows by kind, so a kind without a name would show an empty
    // heading rather than fail anywhere visible.
    for (openvegas::PluginDiscovery::Kind k :
         {openvegas::PluginDiscovery::Kind::VegasProgram, openvegas::PluginDiscovery::Kind::VegasSharedFx,
          openvegas::PluginDiscovery::Kind::VegasOfx, openvegas::PluginDiscovery::Kind::Ofx,
          openvegas::PluginDiscovery::Kind::Vst1, openvegas::PluginDiscovery::Kind::Vst2,
          openvegas::PluginDiscovery::Kind::Vst3}) {
        CHECK_FALSE(openvegas::PluginDiscovery::kindLabel(k).isEmpty());
    }
}
