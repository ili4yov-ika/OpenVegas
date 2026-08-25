#include "plugins/OfxTransitionSource.h"

#include "plugins/OfxHost.h"
#include "plugins/PluginScanner.h"
#include "video/TransitionPluginHook.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QStringList>
#include <QMutex>
#include <QMutexLocker>

namespace openvegas {

namespace {

/** Roots to try before the plug-in search, set by install(). */
QStringList g_preferredRoots;
/** Bundles found by the search; reset with the provider so a new install re-scans. */
QStringList g_binaries;
bool g_binariesScanned = false;

/**
 * Every Vfx1 this machine has, in the order the plug-in search offers them.
 *
 * The search is the one the effect catalog already does — no second idea of where VEGAS
 * is — and it can turn up more than one install. That matters: a 2013 VEGAS has a Vfx1
 * too, and it does not carry the transition effects at all. Taking the first bundle found
 * would hand every transition to the oldest VEGAS on the machine.
 */
QStringList transitionBinaries()
{
    if (g_binariesScanned) {
        return g_binaries;
    }
    g_binariesScanned = true;
    QStringList roots = g_preferredRoots;
    roots += PluginScanner().candidateRoots();
    for (const QString &root : roots) {
        for (const QString &rel :
             {QStringLiteral("OFX Video Plug-Ins/Vfx1.ofx.bundle/Contents/Win64/Vfx1.ofx"),
              // A root that already points inside "OFX Video Plug-Ins".
              QStringLiteral("Vfx1.ofx.bundle/Contents/Win64/Vfx1.ofx")}) {
            const QString candidate = QDir(root).filePath(rel);
            if (QFileInfo::exists(candidate) && !g_binaries.contains(candidate)) {
                g_binaries << candidate;
            }
        }
    }
    return g_binaries;
}

QMutex g_mutex;
/** group key -> instance id; 0 means "asked and there is none", so it is asked once. */
QHash<QString, int> g_instances;

int instanceFor(const QString &groupKey)
{
    QMutexLocker lock(&g_mutex);
    const auto it = g_instances.constFind(groupKey);
    if (it != g_instances.constEnd()) {
        return it.value();
    }

    int id = 0;
    const QString effectId = QStringLiteral("com.vegascreativesoftware:") + groupKey;
    for (const QString &binary : transitionBinaries()) {
        // A bundle that does not declare this effect is the wrong VEGAS for it, not a
        // failure — keep looking rather than falling back to our own geometry.
        const QHash<QString, int> index = OfxHost::effectIndexMap(binary);
        if (!index.contains(effectId)) {
            continue;
        }
        OfxPluginDesc desc;
        desc.path = binary;
        desc.effectId = effectId;
        desc.pluginIndex = index.value(effectId);
        desc.hasBinary = true;
        id = OfxHost::instance().createInstance(desc, nullptr);
        if (id > 0) {
            break;
        }
    }
    // Remembered either way: a group no installed VEGAS carries must not be looked up
    // again on every frame of a transition.
    g_instances.insert(groupKey, id);
    return id;
}

/**
 * Preset values under the names the plug-in declares.
 *
 * The preset package spells them lower-camel (`peelAngle`); the plug-in declares them
 * capitalised (`PeelAngle`). Both are set — writing a parameter that does not exist is
 * ignored — so neither spelling has to be guessed correctly.
 *
 * The light colour does not carry: the plug-in has one RGB parameter where the package
 * stores `lightColorRed`/`Green`/`Blue` as three numbers, and the host sets parameters as
 * single doubles. Such a transition renders with the plug-in's own default light.
 */
QVariantMap pluginParams(const QVariantMap &presetParams)
{
    QVariantMap out;
    for (auto it = presetParams.constBegin(); it != presetParams.constEnd(); ++it) {
        out.insert(it.key(), it.value());
        QString capitalised = it.key();
        if (!capitalised.isEmpty()) {
            capitalised[0] = capitalised[0].toUpper();
            out.insert(capitalised, it.value());
        }
    }
    return out;
}

bool renderThroughPlugin(const QString &groupKey, const QImage &from, const QImage &to,
                         double progress, const QVariantMap &params, QImage *out)
{
    const int id = instanceFor(groupKey);
    if (id <= 0) {
        return false;
    }
    return OfxHost::instance().processTransition(id, from, to, out, progress,
                                                 pluginParams(params), nullptr);
}

} // namespace

void OfxTransitionSource::install(const QStringList &preferredRoots)
{
    g_preferredRoots = preferredRoots;
    setTransitionPluginProvider(&renderThroughPlugin);
}

void OfxTransitionSource::uninstall()
{
    setTransitionPluginProvider(nullptr);
    QMutexLocker lock(&g_mutex);
    for (auto it = g_instances.constBegin(); it != g_instances.constEnd(); ++it) {
        if (it.value() > 0) {
            OfxHost::instance().destroyInstance(it.value());
        }
    }
    g_instances.clear();
    g_preferredRoots.clear();
    g_binaries.clear();
    g_binariesScanned = false;
}

} // namespace openvegas
