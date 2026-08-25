#include "io/VegRiffWriter.h"

#include <QtEndian>

namespace openvegas {

namespace {

constexpr int kChunkHeader = 24; // 16-byte id + int64 size
constexpr int kListHeader = 40;  // …and a form type for the two container ids

/** The 16 raw bytes of an id written as 32 hex characters, or empty when it is not one. */
QByteArray guidBytes(const QString &hexId)
{
    if (hexId.size() != 32) {
        return {};
    }
    const QByteArray raw = QByteArray::fromHex(hexId.toLatin1());
    return raw.size() == 16 ? raw : QByteArray();
}

/** Total bytes this chunk occupies, header included — which is what its size field holds. */
qint64 chunkSize(const VegWriteChunk &chunk)
{
    if (!chunk.isList()) {
        return kChunkHeader + chunk.payload.size();
    }
    qint64 total = kListHeader;
    for (const VegWriteChunk &child : chunk.children) {
        total += chunkSize(child);
    }
    return total + chunk.tail.size();
}

bool appendChunk(const VegWriteChunk &chunk, QByteArray *out)
{
    const QByteArray id = guidBytes(chunk.id);
    if (id.isEmpty()) {
        return false;
    }
    QByteArray sizeField(8, '\0');
    qToLittleEndian<qint64>(chunkSize(chunk),
                            reinterpret_cast<uchar *>(sizeField.data()));
    out->append(id);
    out->append(sizeField);

    if (!chunk.isList()) {
        out->append(chunk.payload);
        return true;
    }
    const QByteArray form = guidBytes(chunk.listType);
    if (form.isEmpty()) {
        return false;
    }
    out->append(form);
    for (const VegWriteChunk &child : chunk.children) {
        if (!appendChunk(child, out)) {
            return false;
        }
    }
    out->append(chunk.tail);
    return true;
}

/** Rebuild the subtree of `chunks[i]`, advancing `i` past everything it owns. */
VegWriteChunk buildSubtree(const QByteArray &data, const QVector<VegChunk> &chunks, int *i)
{
    const VegChunk &c = chunks[*i];
    ++(*i);

    VegWriteChunk out;
    out.id = c.id;
    if (!c.isList) {
        out.payload = data.mid(c.payload, c.end - c.payload);
        return out;
    }
    out.listType = c.listType;

    int covered = c.payload;
    while (*i < chunks.size() && chunks[*i].offset < c.end) {
        const int childEnd = chunks[*i].end;
        out.children.push_back(buildSubtree(data, chunks, i));
        covered = childEnd;
    }
    // Anything inside the list the walker did not read as a chunk. Kept verbatim so a file
    // that carries something unexpected still comes back the same rather than quietly
    // losing it — and so a round trip that fails says the model is wrong, not the file.
    if (covered < c.end) {
        out.tail = data.mid(covered, c.end - covered);
    }
    return out;
}

} // namespace

QByteArray vegRiffWrite(const VegWriteChunk &root)
{
    QByteArray out;
    if (!appendChunk(root, &out)) {
        return {};
    }
    return out;
}

VegWriteChunk vegRiffTree(const QByteArray &data)
{
    const QVector<VegChunk> chunks = vegRiffChunks(data);
    if (chunks.isEmpty()) {
        return {};
    }
    int i = 0;
    VegWriteChunk root = buildSubtree(data, chunks, &i);
    // Bytes after the root chunk. A well-formed project has none; keeping them means the
    // round trip measures the container model rather than the file's tidiness.
    if (root.isList() && chunks.first().end < data.size()) {
        root.tail.append(data.mid(chunks.first().end));
    }
    return root;
}

} // namespace openvegas
