// Frame-by-frame comparison of our Pan/Crop with VEGAS Pro's own render of the same project.
//
// Not part of the regular run: it needs the 4K source clip and frames pulled out of the
// reference render first. Tagged [.reference] so Catch2 skips it unless asked for.
//
//   frames.txt + ref_01.png … in $OPENVGAS_PANCROP_REF, made with
//   ffmpeg -i SAMPLES/veg_projects/project_big--buck-bunny_pan-crop_mask-720p60-vegas_pro.mp4
//          -vf "select='eq(n\,60)+eq(n\,180)+…',scale=in_color_matrix=bt709:in_range=tv:
//               out_range=pc,format=rgb24" -fps_mode passthrough ref_%02d.png
//   and frames.txt listing the same frame indices, space-separated. The render carries no
//   colour tags, and without in_color_matrix=bt709 ffmpeg decodes it as BT.601: the frames
//   then differ from ours by a tint that is the extraction's, not the renderer's (it put
//   0.3 of 255 on the average).
//
// For every frame it prints the mean absolute difference (0..255, over R, G and B) and
// writes ours_NN.png and diff_NN.png (difference ×4) beside the reference.

#include "io/SamplePaths.h"
#include "io/VegReader.h"
#include "model/ProjectModel.h"
#include "video/VideoCompositor.h"
#include "video/VideoKeyframeEval.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QThread>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>

using namespace openvegas;

TEST_CASE("Pan/Crop against VEGAS Pro's render, frame by frame", "[.reference][pancrop]")
{
    const QString dir = QString::fromLocal8Bit(qgetenv("OPENVGAS_PANCROP_REF"));
    if (dir.isEmpty()) {
        SKIP("OPENVGAS_PANCROP_REF not set");
    }
    QFile list(QDir(dir).filePath(QStringLiteral("frames.txt")));
    REQUIRE(list.open(QIODevice::ReadOnly));
    const QStringList frames = QString::fromLatin1(list.readAll()).simplified().split(QLatin1Char(' '));

    const QString project = SamplePaths::resolveProjectPath(
        QStringLiteral("SAMPLES/veg_projects/project_big--buck-bunny_pan-crop_mask.veg"));
    QString err;
    const VegOpenResult veg = VegReader::open(project, &err);
    REQUIRE(err.isEmpty());
    ProjectModel model;
    model.applyVegImport(veg, project, false);

    double total = 0.0;
    for (int i = 0; i < frames.size(); ++i) {
        const int n = frames[i].toInt();
        const double t = n * 1001.0 / 60000.0;
        const QImage ref = QImage(QDir(dir).filePath(QStringLiteral("ref_%1.png").arg(i + 1, 2, 10, QLatin1Char('0'))))
                               .convertToFormat(QImage::Format_RGB32);
        REQUIRE(!ref.isNull());

        // compose() hands back a null image while the source frame is still decoding.
        QImage ours;
        QElapsedTimer clock;
        clock.start();
        while (ours.isNull() && clock.elapsed() < 60000) {
            ours = VideoCompositor::compose(model, t, ref.size());
            if (ours.isNull()) {
                QThread::msleep(50);
            }
        }
        REQUIRE(!ours.isNull());
        ours = ours.convertToFormat(QImage::Format_RGB32);

        QImage diff(ref.size(), QImage::Format_RGB32);
        double sum = 0.0;
        for (int y = 0; y < ref.height(); ++y) {
            const QRgb *a = reinterpret_cast<const QRgb *>(ref.constScanLine(y));
            const QRgb *b = reinterpret_cast<const QRgb *>(ours.constScanLine(y));
            QRgb *d = reinterpret_cast<QRgb *>(diff.scanLine(y));
            for (int x = 0; x < ref.width(); ++x) {
                const int dr = std::abs(qRed(a[x]) - qRed(b[x]));
                const int dg = std::abs(qGreen(a[x]) - qGreen(b[x]));
                const int db = std::abs(qBlue(a[x]) - qBlue(b[x]));
                sum += (dr + dg + db) / 3.0;
                d[x] = qRgb(std::min(255, dr * 4), std::min(255, dg * 4), std::min(255, db * 4));
            }
        }
        const double mean = sum / (double(ref.width()) * ref.height());
        total += mean;
        std::printf("frame %5d  t=%7.3f  mean %6.2f\n", n, t, mean);
        const QString tag = QStringLiteral("%1").arg(i + 1, 2, 10, QLatin1Char('0'));
        ours.save(QDir(dir).filePath(QStringLiteral("ours_%1.png").arg(tag)));
        diff.save(QDir(dir).filePath(QStringLiteral("diff_%1.png").arg(tag)));
    }
    std::printf("average over %lld frames: %.2f\n", static_cast<long long>(frames.size()),
                total / frames.size());
}

TEST_CASE("The reference project's ellipse moves between keyframes that differ in anchors",
          "[video][kf][mask]")
{
    // project_big--buck-bunny_pan-crop_mask.veg: the ellipse has four anchors at 20.75 s
    // and three at 32.57 s. VEGAS's own render has it two thirds of the way up at 27 s; it
    // used to stand still until 32.57 s here.
    const QString path = SamplePaths::resolveProjectPath(
        QStringLiteral("SAMPLES/veg_projects/project_big--buck-bunny_pan-crop_mask.veg"));
    if (!QFileInfo::exists(path)) {
        SKIP("sample project not available");
    }
    const VegOpenResult veg = VegReader::open(path);
    ProjectModel model;
    model.applyVegImport(veg, path, false);
    const TrackEvent *ev = nullptr;
    for (const Track &t : model.tracks()) {
        for (const TrackEvent &e : t.events) {
            if (!e.panCrop.maskKeyframes.isEmpty()) ev = &e;
        }
    }
    REQUIRE(ev);
    MaskKeyframe at27;
    REQUIRE(maskAt(ev->panCrop, 27.0 - ev->startSec, &at27));
    const MaskPath *ellipse = nullptr;
    for (const MaskPath &p : at27.paths) {
        if (p.id == 170) ellipse = &p;
    }
    REQUIRE(ellipse);
    double cy = 0.0;
    for (const MaskAnchor &a : ellipse->anchors) cy += a.y;
    cy /= ellipse->anchors.size();
    // 1466 at 20.75 s, 710 at 32.57 s; VEGAS's frame puts it near 1050 at 27 s.
    CHECK(cy > 980.0);
    CHECK(cy < 1150.0);
}
