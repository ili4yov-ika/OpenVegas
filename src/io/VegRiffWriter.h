#pragma once

#include "io/VegRiff.h"

#include <QByteArray>
#include <QString>
#include <QVector>

namespace openvegas {

/**
 * A chunk to be written: an id, and either a payload or children.
 *
 * The mirror image of what `vegRiffChunks()` reads. A leaf carries bytes; a list carries a
 * form type and the chunks inside it, and its size is worked out rather than given, because
 * a size written by hand is a size that can disagree with what follows it.
 */
struct VegWriteChunk {
    /** Chunk id as 32 lowercase hex characters, the same spelling `vegRiffId()` produces. */
    QString id;
    /** Form type for the two container ids; empty for a leaf. */
    QString listType;
    /** Payload of a leaf. Ignored when `children` is used. */
    QByteArray payload;
    QVector<VegWriteChunk> children;
    /**
     * Bytes inside a list that are not chunks, kept verbatim.
     *
     * A well-formed project has none. Carrying them anyway means a file with something
     * unexpected in it still comes back byte for byte, so a failed round trip accuses
     * the model rather than the file.
     */
    QByteArray tail;

    bool isList() const { return !listType.isEmpty(); }
};

/**
 * Serialise a chunk tree into `.veg` container bytes.
 *
 * Sizes are computed from the tree: a chunk's size counts its own header, which is the one
 * thing about this format that a reader written from the RIFF description alone gets wrong.
 * Returns an empty array when an id is not 32 hex characters, since a mistyped id would
 * produce a file that is structurally valid and means nothing.
 */
QByteArray vegRiffWrite(const VegWriteChunk &root);

/**
 * Rebuild the chunk tree of a project that has already been read.
 *
 * Every leaf keeps its bytes exactly, so writing the result back reproduces the file it came
 * from. That round trip is the point: it is the only check available that the container
 * model — where a header ends, what a size counts, which chunks carry a form type — is right
 * in every case the file contains, rather than in the ones that were looked at by hand.
 *
 * Empty when `data` does not open as a container.
 */
VegWriteChunk vegRiffTree(const QByteArray &data);

} // namespace openvegas
