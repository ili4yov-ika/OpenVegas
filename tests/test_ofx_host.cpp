#include "plugins/OfxHost.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

namespace {

QString fixtureGainOfx()
{
#ifdef OPENVGAS_OFX_FIXTURE_DIR
    return QDir(QString::fromUtf8(OPENVGAS_OFX_FIXTURE_DIR)).filePath(QStringLiteral("Gain.ofx"));
#else
    return {};
#endif
}

} // namespace

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

TEST_CASE("OfxHost load missing binary fails soft", "[media][ofx][plugins]")
{
    openvegas::OfxPluginDesc d;
    d.name = QStringLiteral("Any");
    d.path = QStringLiteral("C:/definitely/missing/NoSuchPlugin.ofx");
    d.hasBinary = true;
    QString err;
    REQUIRE_FALSE(openvegas::OfxHost::load(d, &err));
    REQUIRE_FALSE(err.isEmpty());
    REQUIRE_FALSE(err.contains(QStringLiteral("stub"), Qt::CaseInsensitive));
}

TEST_CASE("OfxHost stand-in renderers are off", "[media][ofx][plugins]")
{
    // OPENVEGAS_EMULATED_VIDEO_FX == 0: OpenVegas must not paint its own approximation of a
    // VEGAS effect. This used to assert that Invert flipped the channels — see the note in
    // plugins/OfxHost.h for why that behaviour was retired rather than extended.
    QImage img(4, 4, QImage::Format_ARGB32);
    img.fill(qRgb(10, 20, 30));
    const QImage before = img.copy();
    REQUIRE_FALSE(openvegas::OfxHost::processEmulated(&img, QStringLiteral("Invert"), {}));
    REQUIRE(img == before);
}

TEST_CASE("OfxHost load+process Gain.ofx fixture", "[media][ofx][plugins]")
{
    const QString path = fixtureGainOfx();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        SKIP("Gain.ofx fixture not built (OPENVGAS_OFX_FIXTURE_DIR)");
    }

    openvegas::OfxPluginDesc d = openvegas::OfxHost::describe(path);
    REQUIRE(d.hasBinary);
    QString err;
    REQUIRE(openvegas::OfxHost::load(d, &err));
    REQUIRE(err.isEmpty());

    const int id = openvegas::OfxHost::instance().createInstance(d, &err);
    REQUIRE(id > 0);

    QImage img(8, 8, QImage::Format_ARGB32);
    img.fill(qRgb(100, 100, 100));
    QVariantMap params;
    params.insert(QStringLiteral("gain"), 2.0);
    REQUIRE(openvegas::OfxHost::instance().processFrame(id, &img, 0.0, params, &err));
    REQUIRE(qRed(img.pixel(0, 0)) == 200);
    REQUIRE(qGreen(img.pixel(0, 0)) == 200);
    REQUIRE(qBlue(img.pixel(0, 0)) == 200);

    openvegas::OfxHost::instance().destroyInstance(id);
}
