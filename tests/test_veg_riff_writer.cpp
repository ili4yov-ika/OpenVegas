#include "io/SamplePaths.h"
#include "io/VegRiffWriter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtEndian>

#include <catch2/catch_test_macros.hpp>

using namespace openvegas;

namespace {

QStringList sampleProjects()
{
    const QString dir = SamplePaths::vegProjectDir();
    if (dir.isEmpty()) {
        return {};
    }
    QStringList out;
    for (const QString &name : QDir(dir).entryList({QStringLiteral("*.veg")}, QDir::Files)) {
        out << QDir(dir).filePath(name);
    }
    return out;
}

/** Number of chunks in a tree, so a round trip cannot pass by writing nothing. */
int countChunks(const VegWriteChunk &chunk)
{
    int n = 1;
    for (const VegWriteChunk &child : chunk.children) {
        n += countChunks(child);
    }
    return n;
}

} // namespace

TEST_CASE("Every sample project survives a trip through the container writer",
          "[veg][riff]")
{
    const QStringList projects = sampleProjects();
    if (projects.isEmpty()) {
        SKIP("SAMPLES/veg_project not available");
    }

    int checked = 0;
    for (const QString &path : projects) {
        QFile f(path);
        REQUIRE(f.open(QIODevice::ReadOnly));
        const QByteArray original = f.readAll();
        f.close();

        const VegWriteChunk tree = vegRiffTree(original);
        if (tree.id.isEmpty()) {
            continue; // not a container; the reader has a scanning path for those
        }
        INFO(QFileInfo(path).fileName().toStdString());

        // A real project is deeply nested — timeline, track, event list, event, plug-in
        // chain — so a tree of one chunk would mean the walk stopped at the door.
        CHECK(countChunks(tree) > 10);

        const QByteArray rebuilt = vegRiffWrite(tree);
        REQUIRE(rebuilt.size() == original.size());

        // Byte for byte. This is the only check available that the container model is
        // right everywhere the file goes rather than in the places that were read by
        // hand: where a header ends, that a size counts its own header, which ids carry a
        // form type, and that nothing is padded between chunks. Any of those wrong by one
        // byte and the file comes back different.
        CHECK(rebuilt == original);
        ++checked;
    }
    // Every sample opens as a container, so a run that quietly skipped some would
    // be measuring less than it looks.
    CHECK(checked == projects.size());
}

TEST_CASE("A chunk's size counts its own header", "[veg][riff]")
{
    // The one thing a reader written from the RIFF description alone gets wrong, and the
    // reason VEGAS's own riff64_ReadChunkHeader subtracts 24 before handing a size back.
    VegWriteChunk leaf;
    leaf.id = QStringLiteral("00112233445566778899aabbccddeeff");
    leaf.payload = QByteArray(10, 'x');

    const QByteArray bytes = vegRiffWrite(leaf);
    REQUIRE(bytes.size() == 34); // 24 of header, 10 of payload
    const qint64 stored =
        qFromLittleEndian<qint64>(reinterpret_cast<const uchar *>(bytes.constData()) + 16);
    CHECK(stored == 34);

    // A list spends sixteen more bytes on its form type, and that is inside the size too.
    VegWriteChunk list;
    list.id = QStringLiteral("7473696c912fcf11a5d628db04c10000");
    list.listType = QStringLiteral("00112233445566778899aabbccddeeff");
    list.children.push_back(leaf);
    const QByteArray listBytes = vegRiffWrite(list);
    REQUIRE(listBytes.size() == 40 + 34);
    const qint64 listStored =
        qFromLittleEndian<qint64>(reinterpret_cast<const uchar *>(listBytes.constData()) + 16);
    CHECK(listStored == 74);

    // An id that is not a GUID produces nothing rather than a structurally valid file
    // that means something else.
    VegWriteChunk broken;
    broken.id = QStringLiteral("not-a-guid");
    CHECK(vegRiffWrite(broken).isEmpty());
}
