#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace openvegas {

/**
 * The container a `.veg` is made of.
 *
 * It is RIFF with 16-byte GUIDs where RIFF has four-character codes, and an int64 size
 * that counts the 24-byte header along with the payload. Two ids give the game away by
 * spelling ASCII in their first four bytes — `riff` {66666972-…} and `list` {7473696C-…}
 * — and both carry a form-type GUID directly after the size, exactly as RIFF does.
 * Everything else is a leaf.
 *
 * The host reads it through a `riff64_*` API exported by its own `sharedk.dll`
 * (`riff64_ReadChunkHeader`, `riff64_FindListChunk`, `riff64_SeekListEntry` and the rest),
 * which is where the name comes from. See MARKDOWN/VEG_CONTAINER_FORMAT.md.
 *
 * Reading a `.veg` by scanning the whole file for a known GUID works, but it cannot say
 * which event a record belongs to — that has to be guessed from byte order. Walking the
 * tree answers it outright, because the record is nested inside its owner.
 */
struct VegChunk {
    /** Offset of the 16-byte chunk id. */
    int offset = -1;
    /** First byte of the payload: `offset + 24`, or `offset + 40` for riff/list. */
    int payload = -1;
    /** One past the last byte of this chunk. */
    int end = -1;
    /** True for the two container ids, which carry a form type before their children. */
    bool isList = false;
    /** Chunk id, and for a list its form type; 32 lowercase hex characters. */
    QString id;
    QString listType;
    /** Ancestors, outermost first. Holds ids for leaves, form types for lists. */
    QVector<QString> path;
};

/**
 * Every chunk in `data`, parents before children.
 *
 * Empty when the file does not open as a container — a caller that also has a scanning
 * path should fall back to it rather than treat that as "no records".
 */
QVector<VegChunk> vegRiffChunks(const QByteArray &data);

/**
 * The innermost chunk that encloses `offset` and whose id or form type is `wantedId`,
 * or nullptr. The pointer is into `chunks` and lives as long as it does.
 *
 * This is what replaces "the nearest event header earlier in the file": an event's
 * records are nested inside it, so the enclosing chunk is the owner by construction.
 */
const VegChunk *vegRiffEnclosing(const QVector<VegChunk> &chunks, int offset,
                                 const QString &wantedId);

/** Lowercase hex id for a GUID written the way it appears in memory. */
QString vegRiffId(const unsigned char guid[16]);

// Chunk ids this project needs by name. Recovered by walking real projects and confirmed
// against the host binary, which carries every one of them in a single table in .rdata.
namespace VegChunkIds {
/** One timeline event. Its records — take, fades, plug-in chain — nest inside it. */
inline QString event()
{
    return QStringLiteral("4a076c4d1623d21186b000c04f8edb8a");
}
/** The plug-in chain attached to an event; holds one `fxRecord` per plug-in. */
inline QString fxChain()
{
    return QStringLiteral("ce3b2bfc8579d211871100c04f8edb8a");
}
/**
 * One slot in a chain.
 *
 * The payload opens with an int32 giving the length of its fixed header (0x90 in the
 * builds seen). A slot exactly that long is **empty** — an event with a fade but no
 * plug-in on it. Otherwise a plug-in record follows: an int32 length, then the CLSID,
 * then at CLSID+48 the preset name length in bytes and at CLSID+52 the name itself, with
 * the parameters straight after.
 *
 * Reading the CLSID at a fixed 0x94 happens to work only because the header is that long
 * here; the length field is what actually places it, and the empty slots have no CLSID at
 * all.
 */
inline QString fxRecord()
{
    return QStringLiteral("cf3b2bfc8579d211871100c04f8edb8a");
}
/** A timeline marker: position in 100-ns units, then a UTF-16 label. */
inline QString marker()
{
    return QStringLiteral("5662f7ab2d39d21186c700c04f8edb8a");
}
} // namespace VegChunkIds

} // namespace openvegas
