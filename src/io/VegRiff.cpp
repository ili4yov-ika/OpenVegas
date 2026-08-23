#include "io/VegRiff.h"

#include <QtEndian>

#include <cstring>

namespace openvegas {

namespace {

// The two container ids, as they sit in the file. Their first four bytes spell the RIFF
// keyword they stand in for.
const unsigned char kRiffGuid[16] = {'r',  'i',  'f',  'f',  0x2e, 0x91, 0xcf, 0x11,
                                     0xa5, 0xd6, 0x28, 0xdb, 0x04, 0xc1, 0x00, 0x00};
const unsigned char kListGuid[16] = {'l',  'i',  's',  't',  0x2f, 0x91, 0xcf, 0x11,
                                     0xa5, 0xd6, 0x28, 0xdb, 0x04, 0xc1, 0x00, 0x00};

// 16-byte id + int64 size. The size counts this header, which is why the host's own
// riff64_ReadChunkHeader subtracts 0x18 from it before handing it back, and why
// riff64_ReadListHeader subtracts 0x28 — a list spends 16 more bytes on its form type.
constexpr int kChunkHeader = 24;
constexpr int kListHeader = 40;

/** Guard against a malformed file turning into unbounded recursion. */
constexpr int kMaxDepth = 32;

void walk(const QByteArray &data, int pos, int end, int depth, QVector<QString> path,
          QVector<VegChunk> *out)
{
    if (depth > kMaxDepth) {
        return;
    }
    const unsigned char *base = reinterpret_cast<const unsigned char *>(data.constData());
    while (pos + kChunkHeader <= end) {
        const qint64 size = qFromLittleEndian<qint64>(base + pos + 16);
        if (size < kChunkHeader || size > end - pos) {
            return;
        }
        const bool isRiff = std::memcmp(base + pos, kRiffGuid, 16) == 0;
        const bool isList = isRiff || std::memcmp(base + pos, kListGuid, 16) == 0;

        VegChunk chunk;
        chunk.offset = pos;
        chunk.end = pos + int(size);
        chunk.isList = isList;
        chunk.id = vegRiffId(base + pos);
        chunk.path = path;
        if (isList) {
            if (size < kListHeader) {
                return;
            }
            chunk.payload = pos + kListHeader;
            chunk.listType = vegRiffId(base + pos + kChunkHeader);
        } else {
            chunk.payload = pos + kChunkHeader;
        }
        out->push_back(chunk);

        if (isList) {
            QVector<QString> child = path;
            child.push_back(chunk.listType);
            walk(data, chunk.payload, chunk.end, depth + 1, child, out);
        }
        pos += int(size);
    }
}

} // namespace

QString vegRiffId(const unsigned char guid[16])
{
    return QString::fromLatin1(
        QByteArray(reinterpret_cast<const char *>(guid), 16).toHex());
}

QVector<VegChunk> vegRiffChunks(const QByteArray &data)
{
    QVector<VegChunk> out;
    if (data.size() < kListHeader) {
        return out;
    }
    const unsigned char *base = reinterpret_cast<const unsigned char *>(data.constData());
    if (std::memcmp(base, kRiffGuid, 16) != 0) {
        // Not a container at all. Callers that also scan should fall back rather than
        // read this as "the file holds nothing".
        return out;
    }
    walk(data, 0, data.size(), 0, {}, &out);
    return out;
}

const VegChunk *vegRiffEnclosing(const QVector<VegChunk> &chunks, int offset,
                                 const QString &wantedId)
{
    const VegChunk *best = nullptr;
    for (const VegChunk &c : chunks) {
        if (offset < c.offset || offset >= c.end) {
            continue;
        }
        const QString &key = c.isList ? c.listType : c.id;
        if (key != wantedId) {
            continue;
        }
        // Innermost wins: a chunk that starts later and still contains the offset is
        // nested inside the earlier one.
        if (!best || c.offset > best->offset) {
            best = &c;
        }
    }
    return best;
}

} // namespace openvegas
