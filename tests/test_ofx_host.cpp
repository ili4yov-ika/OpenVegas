#include "plugins/OfxHost.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("OfxHost describe bundle with Win64 binary", "[media][ofx]")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString bundle =
        QDir(tmp.path()).filePath(QStringLiteral("DemoFx.ofx.bundle"));
    const QString win64 = QDir(bundle).filePath(QStringLiteral("Contents/Win64"));
    REQUIRE(QDir().mkpath(win64));
    const QString ofxBin = QDir(win64).filePath(QStringLiteral("DemoFx.ofx"));
    {
        QFile f(ofxBin);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("stub");
    }

    const openvegas::OfxPluginDesc d = openvegas::OfxHost::describe(bundle);
    REQUIRE(d.name == QStringLiteral("DemoFx"));
    REQUIRE(d.hasBinary);
    REQUIRE(d.archHint == QStringLiteral("Win64"));
    REQUIRE(QFileInfo(d.path).fileName() == QStringLiteral("DemoFx.ofx"));
}

TEST_CASE("OfxHost discoverInRoot finds OFX Video Plug-Ins", "[media][ofx]")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString root = tmp.path();
    const QString ofxRoot = QDir(root).filePath(QStringLiteral("OFX Video Plug-Ins"));
    const QString bundle = QDir(ofxRoot).filePath(QStringLiteral("Blur.ofx.bundle"));
    REQUIRE(QDir().mkpath(QDir(bundle).filePath(QStringLiteral("Contents/Win64"))));
    {
        QFile f(QDir(bundle).filePath(QStringLiteral("Contents/Win64/Blur.ofx")));
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("x");
    }

    const auto list = openvegas::OfxHost::discoverInRoot(root);
    REQUIRE(list.size() >= 1);
    REQUIRE(list.first().name == QStringLiteral("Blur"));
    REQUIRE(list.first().hasBinary);
}

TEST_CASE("OfxHost load is stub failure", "[media][ofx]")
{
    openvegas::OfxPluginDesc d;
    d.name = QStringLiteral("Any");
    d.hasBinary = true;
    QString err;
    REQUIRE_FALSE(openvegas::OfxHost::load(d, &err));
    REQUIRE(err.contains(QStringLiteral("stub"), Qt::CaseInsensitive));
}
