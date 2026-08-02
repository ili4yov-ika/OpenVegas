#include "io/MediaMime.h"
#include "io/MediaProbe.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMimeData>
#include <QUrl>

namespace openvegas {

QString MediaMime::mimeType()
{
    return QStringLiteral("application/x-openvegas-media");
}

bool MediaMime::isMediaFile(const QString &path)
{
    static const QStringList exts = {
        QStringLiteral("mp4"),  QStringLiteral("mov"),  QStringLiteral("mkv"),
        QStringLiteral("avi"),  QStringLiteral("m4v"),  QStringLiteral("wmv"),
        QStringLiteral("webm"), QStringLiteral("mpg"),  QStringLiteral("mpeg"),
        QStringLiteral("mts"),  QStringLiteral("m2ts"), QStringLiteral("mxf"),
        QStringLiteral("mp3"),  QStringLiteral("wav"),  QStringLiteral("flac"),
        QStringLiteral("aif"),  QStringLiteral("aiff"), QStringLiteral("ogg"),
        QStringLiteral("m4a"),  QStringLiteral("bwf"),  QStringLiteral("wma"),
        QStringLiteral("jpg"),  QStringLiteral("jpeg"), QStringLiteral("png"),
        QStringLiteral("tga"),  QStringLiteral("bmp"),  QStringLiteral("tif"),
        QStringLiteral("tiff"), QStringLiteral("gif"),  QStringLiteral("webp"),
    };
    return exts.contains(QFileInfo(path).suffix().toLower());
}

QString MediaMime::guessKind(const QString &pathOrName)
{
    const QString lower = pathOrName.toLower();
    const QString suf = QFileInfo(lower).suffix();
    if (suf == QLatin1String("wav") || suf == QLatin1String("mp3") || suf == QLatin1String("flac")
        || suf == QLatin1String("aif") || suf == QLatin1String("aiff") || suf == QLatin1String("ogg")
        || suf == QLatin1String("m4a") || suf == QLatin1String("bwf") || suf == QLatin1String("wma")
        || lower.contains(QLatin1String("audio"))) {
        return QStringLiteral("audio");
    }
    if (suf == QLatin1String("png") || suf == QLatin1String("jpg") || suf == QLatin1String("jpeg")
        || suf == QLatin1String("tga") || suf == QLatin1String("bmp") || suf == QLatin1String("tif")
        || suf == QLatin1String("tiff") || suf == QLatin1String("gif") || suf == QLatin1String("webp")) {
        return QStringLiteral("still");
    }
    return QStringLiteral("video");
}

QStringList MediaMime::expandToMediaFiles(const QStringList &paths)
{
    QStringList out;
    auto addFile = [&](const QString &p) {
        const QString clean = QDir::cleanPath(p);
        if (clean.isEmpty() || !QFileInfo::exists(clean) || !isMediaFile(clean)) {
            return;
        }
        if (!out.contains(clean, Qt::CaseInsensitive)) {
            out.push_back(clean);
        }
    };

    for (const QString &raw : paths) {
        const QFileInfo fi(raw);
        if (!fi.exists()) {
            continue;
        }
        if (fi.isDir()) {
            const QDir dir(fi.absoluteFilePath());
            const auto entries = dir.entryInfoList(QDir::Files | QDir::Readable, QDir::Name);
            for (const QFileInfo &e : entries) {
                addFile(e.absoluteFilePath());
            }
        } else {
            addFile(fi.absoluteFilePath());
        }
    }
    return out;
}

QMimeData *MediaMime::fromLocalPaths(const QStringList &paths)
{
    const QStringList media = expandToMediaFiles(paths);
    if (media.isEmpty()) {
        return nullptr;
    }

    auto *md = new QMimeData;
    QStringList blocks;
    QStringList names;
    QList<QUrl> urls;
    for (const QString &path : media) {
        const QString name = QFileInfo(path).fileName();
        const QString kind = guessKind(path);
        const double len = MediaProbe::lengthForInsert(path, kind);
        blocks << QStringLiteral("%1\n%2\n%3\n%4").arg(kind, name, path).arg(len);
        names << name;
        urls << QUrl::fromLocalFile(path);
    }
    md->setData(mimeType(), blocks.join(QStringLiteral("\n---\n")).toUtf8());
    md->setText(names.join(QLatin1Char('\n')));
    md->setUrls(urls);
    return md;
}

void MediaMime::parse(const QMimeData *md, QStringList *names, QStringList *kinds, QStringList *paths,
                      QVector<double> *lengths)
{
    if (!md || !names) {
        return;
    }
    names->clear();
    if (kinds) {
        kinds->clear();
    }
    if (paths) {
        paths->clear();
    }
    if (lengths) {
        lengths->clear();
    }

    QStringList rawPaths;

    if (md->hasFormat(mimeType())) {
        const QString payload = QString::fromUtf8(md->data(mimeType()));
        const QStringList blocks = payload.split(QStringLiteral("\n---\n"), Qt::SkipEmptyParts);
        for (const QString &block : blocks) {
            const QStringList parts = block.split(QLatin1Char('\n'));
            QString kind;
            QString name;
            QString path;
            double len = 0.0;
            if (parts.size() >= 2) {
                kind = parts[0].trimmed();
                name = parts[1].trimmed();
            }
            if (parts.size() >= 3) {
                path = parts[2].trimmed();
            }
            if (parts.size() >= 4) {
                len = parts[3].trimmed().toDouble();
            }
            if (!path.isEmpty()) {
                rawPaths << path;
                // Keep explicit kind/len associated after expand via parallel rebuild below
                Q_UNUSED(kind);
                Q_UNUSED(name);
                Q_UNUSED(len);
            } else if (!name.isEmpty()) {
                // Name-only card (no path) — keep as synthetic entry
                names->push_back(name);
                if (kinds) {
                    kinds->push_back(kind.isEmpty() ? guessKind(name) : kind);
                }
                if (paths) {
                    paths->push_back(QString());
                }
                if (lengths) {
                    lengths->push_back(len);
                }
            }
        }
    }

    if (md->hasUrls()) {
        for (const QUrl &url : md->urls()) {
            if (url.isLocalFile()) {
                rawPaths << url.toLocalFile();
            }
        }
    }

    if (rawPaths.isEmpty() && md->hasText() && names->isEmpty()) {
        for (const QString &line : md->text().split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            const QString t = line.trimmed();
            if (t.isEmpty()) {
                continue;
            }
            if (QFileInfo::exists(t)) {
                rawPaths << t;
            } else {
                names->push_back(QFileInfo(t).fileName().isEmpty() ? t : QFileInfo(t).fileName());
                if (kinds) {
                    kinds->push_back(guessKind(t));
                }
                if (paths) {
                    paths->push_back(QString());
                }
                if (lengths) {
                    lengths->push_back(0.0);
                }
            }
        }
    }

    // Preserve lengths from custom MIME blocks when path matches.
    QHash<QString, double> lenByPath;
    if (md->hasFormat(mimeType())) {
        const QString payload = QString::fromUtf8(md->data(mimeType()));
        for (const QString &block : payload.split(QStringLiteral("\n---\n"), Qt::SkipEmptyParts)) {
            const QStringList parts = block.split(QLatin1Char('\n'));
            if (parts.size() >= 4) {
                const QString p = QDir::cleanPath(parts[2].trimmed());
                const double len = parts[3].trimmed().toDouble();
                if (!p.isEmpty() && len > 0.05) {
                    lenByPath.insert(p, len);
                }
            }
        }
    }

    const QStringList media = expandToMediaFiles(rawPaths);
    for (const QString &path : media) {
        names->push_back(QFileInfo(path).fileName());
        if (kinds) {
            kinds->push_back(guessKind(path));
        }
        if (paths) {
            paths->push_back(path);
        }
        if (lengths) {
            lengths->push_back(lenByPath.value(QDir::cleanPath(path), 0.0));
        }
    }
}

bool MediaMime::hasMediaPayload(const QMimeData *md)
{
    if (!md) {
        return false;
    }
    if (md->hasFormat(mimeType())) {
        return true;
    }
    if (md->hasUrls()) {
        QStringList paths;
        for (const QUrl &url : md->urls()) {
            if (url.isLocalFile()) {
                paths << url.toLocalFile();
            }
        }
        return !expandToMediaFiles(paths).isEmpty();
    }
    if (md->hasText()) {
        const QString t = md->text().trimmed();
        if (t.isEmpty()) {
            return false;
        }
        // Allow Project Media text-only cards and local paths
        return true;
    }
    return false;
}

} // namespace openvegas
