#include "plugins/AudioPluginScanner.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#  include <QProcessEnvironment>
#endif

namespace openvegas {
namespace {

bool looksLikeSystemDll(const QString &baseName)
{
    static const QStringList skip = {
        QStringLiteral("msvc"), QStringLiteral("vcruntime"), QStringLiteral("ucrtbase"),
        QStringLiteral("api-ms-"), QStringLiteral("concrt"),  QStringLiteral("opengl"),
    };
    const QString lower = baseName.toLower();
    for (const QString &s : skip) {
        if (lower.contains(s)) {
            return true;
        }
    }
    return false;
}

/**
 * Instrument or effect, judged only by the folder the installer chose.
 *
 * The real answer lives in the binary (VST2's synth flag, VST3's class category), and
 * scanning deliberately never loads plug-ins. A directory literally named "VSTi" or
 * "Instruments" is the installer stating its own intent, which is a strong enough signal
 * to badge with; anything vaguer is left alone, because mislabelling an effect as an
 * instrument is the more confusing of the two errors in an effects chooser.
 */
bool looksLikeInstrumentPath(const QString &absolutePath)
{
    const QStringList parts =
        QDir::fromNativeSeparators(absolutePath).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    // Skip the file itself — only the folders it sits in count.
    for (int i = 0; i + 1 < parts.size(); ++i) {
        const QString part = parts.at(i).trimmed();
        if (part.compare(QLatin1String("VSTi"), Qt::CaseInsensitive) == 0
            || part.compare(QLatin1String("VST3i"), Qt::CaseInsensitive) == 0
            || part.compare(QLatin1String("Instruments"), Qt::CaseInsensitive) == 0
            || part.compare(QLatin1String("VST Instruments"), Qt::CaseInsensitive) == 0
            || part.compare(QLatin1String("VST3 Instruments"), Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

QStringList AudioPluginScanner::defaultVst1Roots()
{
    QStringList roots;
#ifdef Q_OS_WIN
    const QString pf = QProcessEnvironment::systemEnvironment().value(QStringLiteral("ProgramFiles"));
    const QString pf86 =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("ProgramFiles(x86)"));
    if (!pf.isEmpty()) {
        roots << pf + QStringLiteral("/VSTPlugins");
        roots << pf + QStringLiteral("/Steinberg/VSTPlugins");
        roots << pf + QStringLiteral("/Common Files/VST");
        roots << pf + QStringLiteral("/Common Files/VST1");
    }
    if (!pf86.isEmpty()) {
        roots << pf86 + QStringLiteral("/VSTPlugins");
        roots << pf86 + QStringLiteral("/Steinberg/VSTPlugins");
        roots << pf86 + QStringLiteral("/Common Files/VST");
    }
    roots << QDir::homePath() + QStringLiteral("/Documents/VST1");
    roots << QDir::homePath() + QStringLiteral("/Documents/VST");
#else
    roots << QDir::homePath() + QStringLiteral("/.vst");
    roots << QDir::homePath() + QStringLiteral("/.vst1");
    roots << QStringLiteral("/usr/lib/vst");
    roots << QStringLiteral("/usr/local/lib/vst");
#endif
    roots.removeDuplicates();
    return roots;
}

QStringList AudioPluginScanner::defaultVst2Roots()
{
    QStringList roots;
#ifdef Q_OS_WIN
    const QString pf = QProcessEnvironment::systemEnvironment().value(QStringLiteral("ProgramFiles"));
    const QString pf86 =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("ProgramFiles(x86)"));
    if (!pf.isEmpty()) {
        roots << pf + QStringLiteral("/Steinberg/VSTPlugins");
        roots << pf + QStringLiteral("/VSTPlugins");
        roots << pf + QStringLiteral("/Common Files/VST2");
    }
    if (!pf86.isEmpty()) {
        roots << pf86 + QStringLiteral("/Steinberg/VSTPlugins");
        roots << pf86 + QStringLiteral("/VSTPlugins");
        roots << pf86 + QStringLiteral("/Common Files/VST2");
    }
    roots << QDir::homePath() + QStringLiteral("/Documents/VST2");
    roots << QDir::homePath() + QStringLiteral("/Documents/VSTPlugins");
#else
    roots << QDir::homePath() + QStringLiteral("/.vst");
    roots << QDir::homePath() + QStringLiteral("/.vst2");
    roots << QStringLiteral("/usr/lib/vst");
    roots << QStringLiteral("/usr/local/lib/vst");
#endif
    roots.removeDuplicates();
    return roots;
}

QStringList AudioPluginScanner::defaultVst3Roots()
{
    QStringList roots;
#ifdef Q_OS_WIN
    const QString pf = QProcessEnvironment::systemEnvironment().value(QStringLiteral("ProgramFiles"));
    const QString pf86 =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("ProgramFiles(x86)"));
    if (!pf.isEmpty()) {
        roots << pf + QStringLiteral("/Common Files/VST3");
    }
    if (!pf86.isEmpty()) {
        roots << pf86 + QStringLiteral("/Common Files/VST3");
    }
    roots << QDir::homePath() + QStringLiteral("/Documents/VST3");
#else
    roots << QDir::homePath() + QStringLiteral("/.vst3");
    roots << QStringLiteral("/usr/lib/vst3");
    roots << QStringLiteral("/usr/local/lib/vst3");
#endif
    roots.removeDuplicates();
    return roots;
}

void AudioPluginScanner::loadPathsFromSettings(QStringList *vst1, QStringList *vst2,
                                               QStringList *vst3)
{
    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    if (vst1) {
        *vst1 = s.value(QStringLiteral("plugins/vst1Paths")).toStringList();
        if (vst1->isEmpty()) {
            *vst1 = defaultVst1Roots();
        }
    }
    if (vst2) {
        *vst2 = s.value(QStringLiteral("plugins/vst2Paths")).toStringList();
        if (vst2->isEmpty()) {
            *vst2 = defaultVst2Roots();
        }
    }
    if (vst3) {
        *vst3 = s.value(QStringLiteral("plugins/vst3Paths")).toStringList();
        if (vst3->isEmpty()) {
            *vst3 = defaultVst3Roots();
        }
    }
}

void AudioPluginScanner::savePathsToSettings(const QStringList &vst1, const QStringList &vst2,
                                             const QStringList &vst3)
{
    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    s.setValue(QStringLiteral("plugins/vst1Paths"), vst1);
    s.setValue(QStringLiteral("plugins/vst2Paths"), vst2);
    s.setValue(QStringLiteral("plugins/vst3Paths"), vst3);
}

void AudioPluginScanner::scanDllDir(const QString &root, PluginFormat format,
                                    QVector<AudioPluginDesc> *out, QSet<QString> *seenPaths) const
{
    if (!out || root.isEmpty() || !QDir(root).exists()) {
        return;
    }
    const QString prefix = (format == PluginFormat::Vst1) ? QStringLiteral("vst1:")
                                                          : QStringLiteral("vst2:");
    QDirIterator it(root, {QStringLiteral("*.dll")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo fi(path);
        const QString abs = QDir::toNativeSeparators(fi.absoluteFilePath());
        if (seenPaths && seenPaths->contains(abs.toLower())) {
            continue;
        }
        if (looksLikeSystemDll(fi.completeBaseName())) {
            continue;
        }
        if (fi.size() < 8 * 1024) {
            continue;
        }
        AudioPluginDesc d;
        d.id = prefix + path;
        d.name = fi.completeBaseName();
        d.vendor = QStringLiteral("Third Party");
        d.format = format;
        d.path = abs;
        d.category = QStringLiteral("VST");
        d.automatable = true;
        d.isInstrument = looksLikeInstrumentPath(abs);
        out->push_back(d);
        if (seenPaths) {
            seenPaths->insert(abs.toLower());
        }
    }
}

void AudioPluginScanner::scanVst3Dir(const QString &root, QVector<AudioPluginDesc> *out,
                                     QSet<QString> *seenPaths) const
{
    if (!out || root.isEmpty() || !QDir(root).exists()) {
        return;
    }
    auto add = [&](const QFileInfo &fi) {
        const QString abs = QDir::toNativeSeparators(fi.absoluteFilePath());
        if (seenPaths && seenPaths->contains(abs.toLower())) {
            return;
        }
        AudioPluginDesc d;
        d.id = QStringLiteral("vst3:") + fi.absoluteFilePath();
        d.name = fi.completeBaseName();
        d.vendor = QStringLiteral("Third Party");
        d.format = PluginFormat::Vst3;
        d.path = abs;
        d.category = QStringLiteral("VST");
        d.automatable = true;
        d.isInstrument = looksLikeInstrumentPath(abs);
        out->push_back(d);
        if (seenPaths) {
            seenPaths->insert(abs.toLower());
        }
    };

    QDirIterator dirs(root, {QStringLiteral("*.vst3")}, QDir::Dirs | QDir::NoDotAndDotDot,
                      QDirIterator::Subdirectories);
    while (dirs.hasNext()) {
        add(QFileInfo(dirs.next()));
    }
    QDirIterator files(root, {QStringLiteral("*.vst3")}, QDir::Files,
                       QDirIterator::Subdirectories);
    while (files.hasNext()) {
        const QFileInfo fi(files.next());
        if (!fi.isDir()) {
            add(fi);
        }
    }
}

QVector<AudioPluginDesc> AudioPluginScanner::scan() const
{
    QVector<AudioPluginDesc> out;
    QStringList sources;
    QSet<QString> seen;

    QStringList v1 = m_vst1Paths;
    QStringList v2 = m_vst2Paths;
    QStringList v3 = m_vst3Paths;
    if (v1.isEmpty() && v2.isEmpty() && v3.isEmpty()) {
        loadPathsFromSettings(&v1, &v2, &v3);
    }

    for (const QString &r : v1) {
        const int before = out.size();
        scanDllDir(r, PluginFormat::Vst1, &out, &seen);
        if (out.size() > before) {
            sources << r;
        }
    }
    for (const QString &r : v2) {
        const int before = out.size();
        scanDllDir(r, PluginFormat::Vst2, &out, &seen);
        if (out.size() > before) {
            sources << r;
        }
    }
    for (const QString &r : v3) {
        const int before = out.size();
        scanVst3Dir(r, &out, &seen);
        if (out.size() > before) {
            sources << r;
        }
    }

    m_lastSource = sources.isEmpty()
                       ? QStringLiteral("(no VST plug-ins found on disk)")
                       : sources.join(QStringLiteral("; "));
    return out;
}

} // namespace openvegas
