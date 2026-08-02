#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace openvegas {

struct RenderTemplate {
    QString name;
    bool audioOnly = false;
    QString extension; // ".wav", ".mp4", …
    int sampleRate = 0;
    int channels = 0;
    int bitDepth = 0;
    int bitrateKbps = 0; // audio (or total) bitrate for size estimate
    int width = 0;
    int height = 0;
    double fps = 0.0;
    double pixelAspect = 1.0;
    QString videoSummary; // e.g. "YUV (10 bit)" / Progressive
    QString info;         // Template Info (multi-line OK)
};

struct RenderFormat {
    QString name;
    bool audioOnly = false;
    QString extension;
    QString aboutTitle; // "AAC Audio File Format Plug-In"
    QString aboutExtra; // optional copyright / notes
    QVector<RenderTemplate> templates;
};

/** Vegas-style render format / template catalog (UI + metadata; encode is separate). */
class RenderTemplateCatalog {
public:
    static QVector<RenderFormat> formats();
    static QStringList formatNames();
    static const RenderFormat *findFormat(const QString &name);
    static QString defaultExtensionFor(const QString &formatName);
    static bool isFavorite(const QString &formatName, const QString &templateName);
    static void setFavorite(const QString &formatName, const QString &templateName, bool on);
    static QString favoriteKey(const QString &formatName, const QString &templateName);

    /** Rough output size in bytes for durationSec (0 if unknown). */
    static qint64 estimateBytes(const RenderTemplate &tpl, double durationSec);
};

} // namespace openvegas
