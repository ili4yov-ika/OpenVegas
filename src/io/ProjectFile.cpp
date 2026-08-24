#include "io/ProjectFile.h"

#include "io/ProjectInterchange.h"
#include "model/ProjectModel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>

namespace openvegas {
namespace ProjectFile {

namespace {

/**
 * The smallest ZIP writer that produces a file every reader accepts.
 *
 * Entries are stored, not deflated: a project is a few tens of kilobytes of JSON, so
 * compressing it buys almost nothing, and storing keeps this free of a compression
 * dependency. Media put inside is already compressed anyway.
 */
class ZipWriter {
public:
    explicit ZipWriter(QFile *out)
        : m_out(out)
    {
    }

    void add(const QString &name, const QByteArray &data)
    {
        const QByteArray utf8 = name.toUtf8();
        Entry e;
        e.name = utf8;
        e.crc = crc32(data);
        e.size = quint32(data.size());
        e.offset = quint32(m_out->pos());
        m_entries.push_back(e);

        QByteArray hdr;
        appendU32(hdr, 0x04034b50); // local file header
        appendU16(hdr, 20);         // version needed
        appendU16(hdr, 0x0800);     // UTF-8 names
        appendU16(hdr, 0);          // stored
        appendU16(hdr, 0);          // time
        appendU16(hdr, 0);          // date
        appendU32(hdr, e.crc);
        appendU32(hdr, e.size);
        appendU32(hdr, e.size);
        appendU16(hdr, quint16(utf8.size()));
        appendU16(hdr, 0);
        m_out->write(hdr);
        m_out->write(utf8);
        m_out->write(data);
    }

    void finish()
    {
        const quint32 dirStart = quint32(m_out->pos());
        for (const Entry &e : m_entries) {
            QByteArray c;
            appendU32(c, 0x02014b50); // central directory header
            appendU16(c, 20);         // version made by
            appendU16(c, 20);         // version needed
            appendU16(c, 0x0800);
            appendU16(c, 0);
            appendU16(c, 0);
            appendU16(c, 0);
            appendU32(c, e.crc);
            appendU32(c, e.size);
            appendU32(c, e.size);
            appendU16(c, quint16(e.name.size()));
            appendU16(c, 0); // extra
            appendU16(c, 0); // comment
            appendU16(c, 0); // disk
            appendU16(c, 0); // internal attrs
            appendU32(c, 0); // external attrs
            appendU32(c, e.offset);
            m_out->write(c);
            m_out->write(e.name);
        }
        const quint32 dirSize = quint32(m_out->pos()) - dirStart;

        QByteArray end;
        appendU32(end, 0x06054b50); // end of central directory
        appendU16(end, 0);
        appendU16(end, 0);
        appendU16(end, quint16(m_entries.size()));
        appendU16(end, quint16(m_entries.size()));
        appendU32(end, dirSize);
        appendU32(end, dirStart);
        appendU16(end, 0);
        m_out->write(end);
    }

private:
    struct Entry {
        QByteArray name;
        quint32 crc = 0;
        quint32 size = 0;
        quint32 offset = 0;
    };

    static void appendU16(QByteArray &b, quint16 v)
    {
        char raw[2];
        qToLittleEndian(v, raw);
        b.append(raw, 2);
    }
    static void appendU32(QByteArray &b, quint32 v)
    {
        char raw[4];
        qToLittleEndian(v, raw);
        b.append(raw, 4);
    }

    static quint32 crc32(const QByteArray &data)
    {
        static quint32 table[256];
        static bool ready = false;
        if (!ready) {
            for (quint32 i = 0; i < 256; ++i) {
                quint32 c = i;
                for (int k = 0; k < 8; ++k) {
                    c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                }
                table[i] = c;
            }
            ready = true;
        }
        quint32 c = 0xFFFFFFFFu;
        for (char ch : data) {
            c = table[(c ^ quint8(ch)) & 0xFF] ^ (c >> 8);
        }
        return c ^ 0xFFFFFFFFu;
    }

    QFile *m_out = nullptr;
    QVector<Entry> m_entries;
};

/** Read every stored entry of a ZIP. Deflated entries are reported, not guessed at. */
bool readZip(const QString &path, QHash<QString, QByteArray> *out, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("Cannot open %1").arg(path);
        }
        return false;
    }
    const QByteArray all = f.readAll();
    f.close();

    // Walk the local headers rather than the central directory: this only ever reads what
    // saveOzp() wrote, and the headers carry everything needed.
    int pos = 0;
    while (pos + 30 <= all.size()) {
        const quint32 sig = qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar *>(all.constData() + pos));
        if (sig != 0x04034b50) {
            break; // reached the central directory
        }
        const auto u16 = [&](int at) {
            return qFromLittleEndian<quint16>(
                reinterpret_cast<const uchar *>(all.constData() + pos + at));
        };
        const auto u32 = [&](int at) {
            return qFromLittleEndian<quint32>(
                reinterpret_cast<const uchar *>(all.constData() + pos + at));
        };
        const quint16 method = u16(8);
        const quint32 size = u32(18);
        const quint16 nameLen = u16(26);
        const quint16 extraLen = u16(28);
        const int dataAt = pos + 30 + nameLen + extraLen;
        if (dataAt + int(size) > all.size()) {
            break;
        }
        const QString name = QString::fromUtf8(all.mid(pos + 30, nameLen));
        if (method != 0) {
            if (error) {
                *error = QStringLiteral("%1 is compressed; only stored entries are read")
                             .arg(name);
            }
            return false;
        }
        out->insert(name, all.mid(dataAt, int(size)));
        pos = dataAt + int(size);
    }
    return !out->isEmpty();
}

} // namespace

bool looksLikeZip(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return false;
    }
    return f.read(4) == QByteArray("PK\x03\x04", 4);
}

bool saveOvp(const ProjectModel &model, const QString &path, QString *error)
{
    const QJsonObject root = ProjectInterchange::projectToJson(model);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("Cannot write %1").arg(path);
        }
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

bool loadOvp(const QString &path, ProjectModel *model, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("Cannot open %1").arg(path);
        }
        return false;
    }
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) {
            *error = QStringLiteral("Invalid project file: %1").arg(perr.errorString());
        }
        return false;
    }
    // No base directory: a single file carries no media copies to resolve against.
    return ProjectInterchange::projectFromJson(doc.object(), QString(), model, error);
}

bool saveOzp(const ProjectModel &model, const QString &path, bool includeMedia, QString *error)
{
    QStringList mediaLines;
    QJsonObject root = ProjectInterchange::projectToJson(model, &mediaLines);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = QStringLiteral("Cannot write %1").arg(path);
        }
        return false;
    }
    ZipWriter zip(&f);

    if (includeMedia) {
        // Record where each file went before the JSON is written, so the archive resolves
        // to its own copies rather than to wherever the media happened to live.
        QJsonArray media = root.value(QStringLiteral("media")).toArray();
        for (int i = 0; i < media.size(); ++i) {
            QJsonObject mo = media[i].toObject();
            const QString src = mo.value(QStringLiteral("path")).toString();
            if (src.isEmpty() || !QFileInfo::exists(src)) {
                continue;
            }
            const QString inside =
                QStringLiteral("Media/") + QFileInfo(src).fileName();
            QFile in(src);
            if (!in.open(QIODevice::ReadOnly)) {
                continue;
            }
            zip.add(inside, in.readAll());
            mo.insert(QStringLiteral("archivedPath"), inside);
            media[i] = mo;
        }
        root.insert(QStringLiteral("media"), media);
    }

    zip.add(QStringLiteral("project.json"), QJsonDocument(root).toJson(QJsonDocument::Indented));
    QByteArray list = "# kind\tname\tpath\n";
    for (const QString &line : mediaLines) {
        list += line.toUtf8() + "\n";
    }
    zip.add(QStringLiteral("media_list.txt"), list);
    zip.finish();
    return true;
}

bool loadOzp(const QString &path, ProjectModel *model, QString *error)
{
    QHash<QString, QByteArray> entries;
    if (!readZip(path, &entries, error)) {
        return false;
    }
    if (!entries.contains(QStringLiteral("project.json"))) {
        if (error) {
            *error = QStringLiteral("%1 holds no project.json").arg(path);
        }
        return false;
    }
    QJsonParseError perr;
    const QJsonDocument doc =
        QJsonDocument::fromJson(entries.value(QStringLiteral("project.json")), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) {
            *error = QStringLiteral("Invalid project.json: %1").arg(perr.errorString());
        }
        return false;
    }

    // The model refers to media by path, so anything carried inside has to become a real
    // file again. It is unpacked beside the archive rather than into a temporary folder,
    // which would vanish under the project the next time the machine tidies up.
    QString baseDir;
    bool anyMedia = false;
    for (auto it = entries.cbegin(); it != entries.cend(); ++it) {
        if (!it.key().startsWith(QStringLiteral("Media/"))) {
            continue;
        }
        anyMedia = true;
        break;
    }
    if (anyMedia) {
        const QFileInfo fi(path);
        baseDir = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName()
                  + QStringLiteral("_media");
        QDir().mkpath(baseDir + QStringLiteral("/Media"));
        for (auto it = entries.cbegin(); it != entries.cend(); ++it) {
            if (!it.key().startsWith(QStringLiteral("Media/"))) {
                continue;
            }
            QFile out(baseDir + QLatin1Char('/') + it.key());
            if (out.open(QIODevice::WriteOnly)) {
                out.write(it.value());
            }
        }
    }
    return ProjectInterchange::projectFromJson(doc.object(), baseDir, model, error);
}

} // namespace ProjectFile
} // namespace openvegas
