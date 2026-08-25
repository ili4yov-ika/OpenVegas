#include "plugins/PluginDiscovery.h"

#include "plugins/AudioPluginScanner.h"
#include "plugins/OfxPluginPaths.h"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSettings>

namespace openvegas {

namespace {

/** Add `path` if it exists and is not already there, keeping the given order. */
void addUnique(QStringList *out, const QString &path)
{
    if (!out || path.isEmpty()) {
        return;
    }
    const QString clean = QDir::fromNativeSeparators(QDir::cleanPath(path));
    if (clean.isEmpty() || !QFileInfo(clean).isDir()) {
        return;
    }
    for (const QString &existing : *out) {
        if (existing.compare(clean, Qt::CaseInsensitive) == 0) {
            return;
        }
    }
    out->push_back(clean);
}

QStringList programFilesRoots()
{
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QStringList roots;
    for (const char *var : {"ProgramFiles", "ProgramFiles(x86)", "ProgramW6432"}) {
        const QString v = env.value(QString::fromLatin1(var));
        if (!v.isEmpty()) {
            roots << QDir::fromNativeSeparators(v);
        }
    }
    roots.removeDuplicates();
    return roots;
}

/** How many files inside look like a plug-in of this kind. */
int countPlugins(const QString &dir, PluginDiscovery::Kind kind)
{
    QStringList filters;
    switch (kind) {
    case PluginDiscovery::Kind::VegasOfx:
    case PluginDiscovery::Kind::Ofx:
        filters << QStringLiteral("*.ofx.bundle") << QStringLiteral("*.ofx");
        break;
    case PluginDiscovery::Kind::Vst1:
    case PluginDiscovery::Kind::Vst2:
        filters << QStringLiteral("*.dll") << QStringLiteral("*.vst");
        break;
    case PluginDiscovery::Kind::Vst3:
        filters << QStringLiteral("*.vst3");
        break;
    case PluginDiscovery::Kind::VegasSharedFx:
        filters << QStringLiteral("*.dll");
        break;
    case PluginDiscovery::Kind::VegasProgram:
        return -1; // a program folder is not a bag of plug-ins; counting would mislead
    }
    QDir d(dir);
    return int(d.entryList(filters, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).size());
}

} // namespace

QString PluginDiscovery::kindLabel(Kind kind)
{
    switch (kind) {
    case Kind::VegasProgram:
        return QObject::tr("VEGAS Pro");
    case Kind::VegasSharedFx:
        return QObject::tr("Shared Plug-Ins (audio)");
    case Kind::VegasOfx:
        return QObject::tr("VEGAS OFX video plug-ins");
    case Kind::Ofx:
        return QObject::tr("OFX plug-ins");
    case Kind::Vst1:
        return QObject::tr("VST");
    case Kind::Vst2:
        return QObject::tr("VST2");
    case Kind::Vst3:
        return QObject::tr("VST3");
    }
    return QString();
}

QStringList PluginDiscovery::vegasProgramRoots()
{
    QStringList out;

#ifdef Q_OS_WIN
    // A properly installed VEGAS registers itself. Several versions can be side by side,
    // so every one is offered rather than the first found.
    for (const char *hive : {"HKEY_LOCAL_MACHINE", "HKEY_CURRENT_USER"}) {
        for (const char *vendor : {"MAGIX", "Sony Creative Software", "Sony"}) {
            const QString base =
                QStringLiteral("%1\\SOFTWARE\\%2").arg(QString::fromLatin1(hive),
                                                       QString::fromLatin1(vendor));
            QSettings reg(base, QSettings::NativeFormat);
            for (const QString &group : reg.childGroups()) {
                if (!group.contains(QStringLiteral("VEGAS"), Qt::CaseInsensitive)
                    && !group.contains(QStringLiteral("Vegas"), Qt::CaseInsensitive)) {
                    continue;
                }
                QSettings sub(base + QLatin1Char('\\') + group, QSettings::NativeFormat);
                for (const QString &key : {QStringLiteral("InstallPath"),
                                           QStringLiteral("Path"),
                                           QStringLiteral("InstallDir")}) {
                    addUnique(&out, sub.value(key).toString());
                }
                for (const QString &child : sub.childGroups()) {
                    QSettings ver(base + QLatin1Char('\\') + group + QLatin1Char('\\') + child,
                                  QSettings::NativeFormat);
                    for (const QString &key : {QStringLiteral("InstallPath"),
                                               QStringLiteral("Path"),
                                               QStringLiteral("InstallDir")}) {
                        addUnique(&out, ver.value(key).toString());
                    }
                }
            }
        }
    }
#endif

    // A copied or portable install has no registry entry, and that is exactly the case a
    // first run should still find.
    for (const QString &pf : programFilesRoots()) {
        for (const QString &vendor : {QStringLiteral("VEGAS"), QStringLiteral("MAGIX"),
                                      QStringLiteral("Sony")}) {
            QDir dir(pf + QLatin1Char('/') + vendor);
            if (!dir.exists()) {
                continue;
            }
            addUnique(&out, dir.absolutePath());
            for (const QString &child :
                 dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
                if (child.contains(QStringLiteral("VEGAS"), Qt::CaseInsensitive)) {
                    addUnique(&out, dir.absoluteFilePath(child));
                }
            }
        }
    }
    return out;
}

QStringList PluginDiscovery::sharedPluginRoots()
{
    QStringList out;
    for (const QString &pf : programFilesRoots()) {
        addUnique(&out, pf + QStringLiteral("/Sony/Shared Plug-Ins"));
        addUnique(&out, pf + QStringLiteral("/MAGIX/Shared Plug-Ins"));
        addUnique(&out, pf + QStringLiteral("/VEGAS/Shared Plug-Ins"));
        addUnique(&out, pf + QStringLiteral("/Common Files/Sony Shared/Shared Plug-Ins"));
        addUnique(&out, pf + QStringLiteral("/Common Files/MAGIX Shared/Shared Plug-Ins"));
    }
    // Alongside a VEGAS install, which is where a copied one keeps them.
    for (const QString &veg : vegasProgramRoots()) {
        addUnique(&out, veg + QStringLiteral("/Shared Plug-Ins"));
        addUnique(&out, QFileInfo(veg).absolutePath() + QStringLiteral("/Shared Plug-Ins"));
    }
    return out;
}

QStringList PluginDiscovery::vegasOfxRoots()
{
    QStringList out;
    for (const QString &veg : vegasProgramRoots()) {
        addUnique(&out, veg + QStringLiteral("/OFX Video Plug-Ins"));
        addUnique(&out, veg + QStringLiteral("/FileIO Plug-Ins"));
    }
    for (const QString &pf : programFilesRoots()) {
        addUnique(&out, pf + QStringLiteral("/Common Files/OFX/Plugins"));
    }
    return out;
}

QVector<PluginDiscovery::Found> PluginDiscovery::scan()
{
    QVector<Found> out;
    const auto push = [&out](Kind kind, const QStringList &paths) {
        for (const QString &p : paths) {
            Found f;
            f.kind = kind;
            f.path = p;
            f.count = countPlugins(p, kind);
            out.push_back(f);
        }
    };

    push(Kind::VegasProgram, vegasProgramRoots());
    push(Kind::VegasSharedFx, sharedPluginRoots());
    push(Kind::VegasOfx, vegasOfxRoots());

    // Third-party OFX roots, minus the VEGAS ones already listed above.
    QStringList ofx;
    for (const QString &p : OfxPluginPaths::standardRoots()) {
        bool already = false;
        for (const Found &f : out) {
            if (f.path.compare(QDir::fromNativeSeparators(QDir::cleanPath(p)),
                               Qt::CaseInsensitive)
                == 0) {
                already = true;
                break;
            }
        }
        if (!already) {
            addUnique(&ofx, p);
        }
    }
    push(Kind::Ofx, ofx);

    QStringList v1;
    QStringList v2;
    QStringList v3;
    for (const QString &p : AudioPluginScanner::defaultVst1Roots()) {
        addUnique(&v1, p);
    }
    for (const QString &p : AudioPluginScanner::defaultVst2Roots()) {
        addUnique(&v2, p);
    }
    for (const QString &p : AudioPluginScanner::defaultVst3Roots()) {
        addUnique(&v3, p);
    }
    push(Kind::Vst1, v1);
    push(Kind::Vst2, v2);
    push(Kind::Vst3, v3);

    // Mark the obvious pick for the two single-valued settings, so the screen can preselect
    // something sensible instead of leaving them blank when several were found.
    for (Kind kind : {Kind::VegasProgram, Kind::VegasOfx}) {
        int best = -1;
        for (int i = 0; i < out.size(); ++i) {
            if (out[i].kind != kind) {
                continue;
            }
            if (best < 0 || out[i].count > out[best].count) {
                best = i;
            }
        }
        if (best >= 0) {
            out[best].preferred = true;
        }
    }
    return out;
}

void PluginDiscovery::apply(const QVector<Found> &chosen)
{
    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));

    QStringList v1;
    QStringList v2;
    QStringList v3;
    QString vegas;
    QString ofx;
    for (const Found &f : chosen) {
        switch (f.kind) {
        case Kind::VegasProgram:
            if (vegas.isEmpty() || f.preferred) {
                vegas = f.path;
            }
            break;
        case Kind::VegasOfx:
        case Kind::Ofx:
            // One preferred OFX root is what PluginScanner reads; the rest are found
            // anyway through the standard roots, so nothing is lost by picking one.
            if (ofx.isEmpty() || f.preferred) {
                ofx = f.path;
            }
            break;
        case Kind::VegasSharedFx:
            // The Shared Plug-Ins are COM servers found through the registry, not by
            // path, so there is nothing to store — listing them tells the user they were
            // seen, which is the point of showing them.
            break;
        case Kind::Vst1:
            v1 << f.path;
            break;
        case Kind::Vst2:
            v2 << f.path;
            break;
        case Kind::Vst3:
            v3 << f.path;
            break;
        }
    }

    if (!vegas.isEmpty()) {
        s.setValue(QStringLiteral("plugins/vegasProPath"), vegas);
    }
    if (!ofx.isEmpty()) {
        s.setValue(QStringLiteral("plugins/ofxPath"), ofx);
        s.setValue(QStringLiteral("plugins/useVegasOfx"), true);
    }
    if (!v1.isEmpty()) {
        s.setValue(QStringLiteral("plugins/vst1Paths"), v1);
    }
    if (!v2.isEmpty()) {
        s.setValue(QStringLiteral("plugins/vst2Paths"), v2);
    }
    if (!v3.isEmpty()) {
        s.setValue(QStringLiteral("plugins/vst3Paths"), v3);
    }
}

bool PluginDiscovery::needsFirstRun()
{
    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    return !s.value(QStringLiteral("setup/firstRunDone"), false).toBool();
}

void PluginDiscovery::markFirstRunDone()
{
    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    s.setValue(QStringLiteral("setup/firstRunDone"), true);
}

} // namespace openvegas
