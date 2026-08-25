#include "plugins/OfxHost.h"
#include "plugins/OfxPluginPaths.h"
#include "plugins/OfxTrace.h"
#include "plugins/OfxVegasExtensions.h"
#include "plugins/VegasVideoPluginCatalog.h"

#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QIODevice>
#include <QLibrary>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdlib>
#include <array>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#endif

extern "C" {
#include <ofxCore.h>
#include <ofxImageEffect.h>
#include <ofxInteract.h>
#include <ofxMemory.h>
#include <ofxMessage.h>
#include <ofxMultiThread.h>
#include <ofxParam.h>
#include <ofxParametricParam.h>
#include <ofxProgress.h>
#include <ofxProperty.h>
#include <ofxTimeLine.h>
}

namespace openvegas {
namespace {

// ---------------------------------------------------------------------------
// DLL search path — Vegas OFX binaries import sibling runtime DLLs (e.g.
// sharedk.dll, OpenColorIO_2_0.dll) that live in the VEGAS install root, not
// next to the .ofx binary itself. Plain LoadLibrary() only searches the
// *calling exe's* directory for dependencies, so it fails with "module not
// found" unless the install root is added to the search path first.
// ---------------------------------------------------------------------------

/** Walk up from a bundled .ofx binary to find the VEGAS install root (parent of "OFX Video Plug-Ins"). */
QString ofxInstallRootForBinary(const QString &binaryPath)
{
    QDir dir = QFileInfo(binaryPath).dir();
    for (int i = 0; i < 8 && dir.exists(); ++i) {
        if (dir.dirName().compare(QStringLiteral("OFX Video Plug-Ins"), Qt::CaseInsensitive) == 0) {
            QDir parent = dir;
            return parent.cdUp() ? parent.absolutePath() : QString();
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return {};
}

/** Temporarily add a directory to the process DLL search path for the duration of a LoadLibrary call. */
class ScopedOfxDllDirectory {
public:
    explicit ScopedOfxDllDirectory(const QString &dir)
    {
#ifdef _WIN32
        if (dir.isEmpty()) {
            return;
        }
        wchar_t prevBuf[MAX_PATH] = {};
        const DWORD prevLen = ::GetDllDirectoryW(MAX_PATH, prevBuf);
        m_prev = (prevLen > 0 && prevLen < MAX_PATH) ? QString::fromWCharArray(prevBuf) : QString();
        m_active = ::SetDllDirectoryW(reinterpret_cast<const wchar_t *>(dir.utf16())) != 0;
#else
        Q_UNUSED(dir);
#endif
    }

    ~ScopedOfxDllDirectory()
    {
#ifdef _WIN32
        if (m_active) {
            ::SetDllDirectoryW(m_prev.isEmpty() ? nullptr
                                                : reinterpret_cast<const wchar_t *>(m_prev.utf16()));
        }
#endif
    }

    ScopedOfxDllDirectory(const ScopedOfxDllDirectory &) = delete;
    ScopedOfxDllDirectory &operator=(const ScopedOfxDllDirectory &) = delete;

private:
#ifdef _WIN32
    QString m_prev;
    bool m_active = false;
#endif
};

/**
 * ABI directory a binary sits in, or empty when it is not inside a bundle layout.
 *
 * Callers hand us `.../Something.ofx.bundle/Contents/Win64/Something.ofx` directly as
 * often as they hand us the bundle root, so the ABI has to be recoverable from the
 * binary path alone — that is the one place every load funnels through.
 */
QString archFolderFromBinaryPath(const QString &binaryPath)
{
    const QString parent = QFileInfo(binaryPath).dir().dirName();
    for (const QString &known : OfxPluginPaths::knownArchFolderNames()) {
        if (parent.compare(known, Qt::CaseInsensitive) == 0) {
            return known;
        }
    }
    return {};
}

/**
 * Guard every dlopen/LoadLibrary: refuse a binary built for another platform instead of
 * letting the OS loader fail with an unhelpful message (or, worse, on some systems,
 * partially map it). Returns false and fills `errorOut` when the load must not happen.
 */
bool checkArchLoadable(const QString &binaryPath, QString *errorOut)
{
    const QString arch = archFolderFromBinaryPath(binaryPath);
    const QString reason = OfxPluginPaths::archIncompatibilityReason(arch);
    if (reason.isEmpty()) {
        return true;
    }
    OPENVEGAS_OFX_TRACE(
        QStringLiteral("skip \"%1\": %2").arg(binaryPath, reason));
    if (errorOut) {
        *errorOut = QStringLiteral("OFX plug-in \"%1\" cannot run here — %2")
                        .arg(QFileInfo(binaryPath).fileName(), reason);
    }
    return false;
}

QString tidyNameFromBundle(const QString &bundleName)
{
    QString n = bundleName;
    if (n.endsWith(QStringLiteral(".ofx.bundle"), Qt::CaseInsensitive)) {
        n.chop(QStringLiteral(".ofx.bundle").size());
    } else if (n.endsWith(QStringLiteral(".bundle"), Qt::CaseInsensitive)) {
        n.chop(QStringLiteral(".bundle").size());
    }
    return n;
}

/**
 * Pick the binary inside an `.ofx.bundle`.
 *
 * Prefers a directory this build can actually load; only if there is none does it fall
 * back to whatever the bundle does ship, so the caller can still name the plug-in and
 * explain that it is the wrong platform rather than silently losing it.
 */
QString findOfxBinaryInBundle(const QString &bundlePath, QString *archOut)
{
    const QDir contents(QDir(bundlePath).filePath(QStringLiteral("Contents")));
    if (!contents.exists()) {
        const QFileInfoList files =
            QDir(bundlePath).entryInfoList({QStringLiteral("*.ofx")}, QDir::Files);
        if (!files.isEmpty()) {
            if (archOut) {
                *archOut = QString();
            }
            return files.first().absoluteFilePath();
        }
        return {};
    }

    // Native ABI first.
    for (const QString &arch : OfxPluginPaths::loadableArchFolderNames()) {
        const QDir archDir(contents.filePath(arch));
        if (!archDir.exists()) {
            continue;
        }
        const QFileInfoList files = archDir.entryInfoList({QStringLiteral("*.ofx")}, QDir::Files);
        if (!files.isEmpty()) {
            if (archOut) {
                *archOut = arch;
            }
            return files.first().absoluteFilePath();
        }
    }

    // Foreign ABI — reported, not loaded.
    const auto entries = contents.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &d : entries) {
        const QFileInfoList files =
            QDir(d.absoluteFilePath()).entryInfoList({QStringLiteral("*.ofx")}, QDir::Files);
        if (!files.isEmpty()) {
            if (archOut) {
                *archOut = d.fileName();
            }
            return files.first().absoluteFilePath();
        }
    }
    return {};
}

OfxPluginDesc describeBundleOrFile(const QFileInfo &fi)
{
    OfxPluginDesc d;
    if (fi.isDir()) {
        d.bundlePath = fi.absoluteFilePath();
        d.name = tidyNameFromBundle(fi.fileName());
        QString arch;
        const QString bin = findOfxBinaryInBundle(fi.absoluteFilePath(), &arch);
        d.archHint = arch;
        d.archLoadable = OfxPluginPaths::isArchLoadable(arch);
        d.archNote = OfxPluginPaths::archIncompatibilityReason(arch);
        if (!bin.isEmpty()) {
            d.path = bin;
            d.hasBinary = true;
        } else {
            d.path = fi.absoluteFilePath();
            d.hasBinary = false;
        }
        return d;
    }
    d.name = fi.completeBaseName();
    d.path = fi.absoluteFilePath();
    d.hasBinary = fi.suffix().compare(QStringLiteral("ofx"), Qt::CaseInsensitive) == 0;
    // A loose .ofx file outside a bundle carries no ABI marker; the loader is the only
    // thing that can judge it, and it fails soft.
    return d;
}

QVariantMap loadSlotParams(const FxSlot &slot)
{
    QVariantMap m;
    if (slot.state.isEmpty()) {
        return m;
    }
    QDataStream in(slot.state);
    in.setVersion(QDataStream::Qt_6_0);
    in >> m;
    return m;
}

// ---------------------------------------------------------------------------
// OpenVegas's own stand-in renderers for VEGAS video effects.
//
// Compiled out (OPENVEGAS_EMULATED_VIDEO_FX == 0) and kept only so the switch can be
// flipped back while working on the .veg OFX parameter decoder. Do not extend these —
// see the note in plugins/OfxHost.h.
// ---------------------------------------------------------------------------
#if OPENVEGAS_EMULATED_VIDEO_FX

double mapGet(const QVariantMap &m, const QString &key, double def)
{
    const auto it = m.constFind(key);
    if (it == m.cend()) {
        return def;
    }
    return it->toDouble();
}

inline double clamp01(double v)
{
    return std::clamp(v, 0.0, 1.0);
}

void ensureArgb32(QImage *img)
{
    if (!img || img->isNull()) {
        return;
    }
    if (img->format() != QImage::Format_ARGB32
        && img->format() != QImage::Format_ARGB32_Premultiplied) {
        *img = img->convertToFormat(QImage::Format_ARGB32);
    } else if (img->format() == QImage::Format_ARGB32_Premultiplied) {
        *img = img->convertToFormat(QImage::Format_ARGB32);
    }
}

void applyBoxBlur(QImage *img, int radius)
{
    if (!img || img->isNull() || radius <= 0) {
        return;
    }
    ensureArgb32(img);
    const int w = img->width();
    const int h = img->height();
    QImage src = img->copy();
    const int diam = radius * 2 + 1;
    const int area = diam * diam;
    for (int y = 0; y < h; ++y) {
        QRgb *dst = reinterpret_cast<QRgb *>(img->scanLine(y));
        for (int x = 0; x < w; ++x) {
            int r = 0, g = 0, b = 0, a = 0;
            for (int dy = -radius; dy <= radius; ++dy) {
                const int yy = std::clamp(y + dy, 0, h - 1);
                const QRgb *sline = reinterpret_cast<const QRgb *>(src.constScanLine(yy));
                for (int dx = -radius; dx <= radius; ++dx) {
                    const int xx = std::clamp(x + dx, 0, w - 1);
                    const QRgb px = sline[xx];
                    r += qRed(px);
                    g += qGreen(px);
                    b += qBlue(px);
                    a += qAlpha(px);
                }
            }
            dst[x] = qRgba(r / area, g / area, b / area, a / area);
        }
    }
}

void applyInvert(QImage *img)
{
    if (!img || img->isNull()) {
        return;
    }
    ensureArgb32(img);
    for (int y = 0; y < img->height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img->scanLine(y));
        for (int x = 0; x < img->width(); ++x) {
            const QRgb px = line[x];
            line[x] = qRgba(255 - qRed(px), 255 - qGreen(px), 255 - qBlue(px), qAlpha(px));
        }
    }
}

void applySepia(QImage *img)
{
    if (!img || img->isNull()) {
        return;
    }
    ensureArgb32(img);
    for (int y = 0; y < img->height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img->scanLine(y));
        for (int x = 0; x < img->width(); ++x) {
            const QRgb px = line[x];
            const int r = qRed(px);
            const int g = qGreen(px);
            const int b = qBlue(px);
            const int nr = std::clamp(int(0.393 * r + 0.769 * g + 0.189 * b), 0, 255);
            const int ng = std::clamp(int(0.349 * r + 0.686 * g + 0.168 * b), 0, 255);
            const int nb = std::clamp(int(0.272 * r + 0.534 * g + 0.131 * b), 0, 255);
            line[x] = qRgba(nr, ng, nb, qAlpha(px));
        }
    }
}

void applyBrightnessContrast(QImage *img, double brightness, double contrast)
{
    if (!img || img->isNull()) {
        return;
    }
    if (std::abs(brightness) < 1e-6 && std::abs(contrast - 1.0) < 1e-6) {
        return;
    }
    ensureArgb32(img);
    const double bright = brightness * 0.5;
    for (int y = 0; y < img->height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img->scanLine(y));
        for (int x = 0; x < img->width(); ++x) {
            const QRgb px = line[x];
            const int a = qAlpha(px);
            if (a == 0) {
                continue;
            }
            double r = qRed(px) / 255.0;
            double g = qGreen(px) / 255.0;
            double b = qBlue(px) / 255.0;
            r = clamp01((r - 0.5) * contrast + 0.5 + bright);
            g = clamp01((g - 0.5) * contrast + 0.5 + bright);
            b = clamp01((b - 0.5) * contrast + 0.5 + bright);
            line[x] = qRgba(int(std::lround(r * 255.0)), int(std::lround(g * 255.0)),
                            int(std::lround(b * 255.0)), a);
        }
    }
}

void applyGain(QImage *img, double gain)
{
    if (!img || img->isNull() || std::abs(gain - 1.0) < 1e-6) {
        return;
    }
    ensureArgb32(img);
    for (int y = 0; y < img->height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img->scanLine(y));
        for (int x = 0; x < img->width(); ++x) {
            const QRgb px = line[x];
            const int a = qAlpha(px);
            if (a == 0) {
                continue;
            }
            const int r = std::clamp(int(std::lround(qRed(px) * gain)), 0, 255);
            const int g = std::clamp(int(std::lround(qGreen(px) * gain)), 0, 255);
            const int b = std::clamp(int(std::lround(qBlue(px) * gain)), 0, 255);
            line[x] = qRgba(r, g, b, a);
        }
    }
}

#endif // OPENVEGAS_EMULATED_VIDEO_FX

// ---------------------------------------------------------------------------
// Minimal OFX property / effect host
// ---------------------------------------------------------------------------

struct PropValue {
    enum Kind { None, Pointer, String, Double, Int } kind = None;
    std::vector<void *> pointers;
    std::vector<std::string> strings;
    std::vector<double> doubles;
    std::vector<int> ints;
};

struct PropSet {
    std::map<std::string, PropValue> props;
};

struct ParamRec {
    std::string name;
    std::string type;
    double value = 0.0;
    /**
     * Per-component values for a parameter that has more than one.
     *
     * Empty means every component takes `value`, which is right for the many parameters
     * whose components genuinely move together and wrong for the ones that do not: a
     * colour is three numbers, and giving red's value to green and blue turns every colour
     * into a shade of grey.
     */
    std::vector<double> components;
    PropSet props;
    /** Parametric ("curve") params: curve index -> sorted control points (position, value). */
    std::map<int, std::vector<std::pair<double, double>>> curves;
};

struct ClipRec {
    std::string name;
    PropSet props;
    PropSet *activeImage = nullptr; // during render
    /** Region of definition in pixels — what clipGetRegionOfDefinition must report. */
    double rodWidth = 0.0;
    double rodHeight = 0.0;
};

struct EffectRec;

struct ModuleRec {
    QString path;
    std::unique_ptr<QLibrary> lib;
    OfxPlugin *plugin = nullptr;
    int pluginIndex = 0;
    PropSet descriptorProps;
    std::map<std::string, ClipRec> descriptorClips;
    std::map<std::string, ParamRec> descriptorParams;
    bool loaded = false;
    bool described = false;
    bool describedInContext = false;
    /** Context DescribeInContext actually accepted — instances must be created in it. */
    std::string describedContext;
};

struct EffectRec {
    ModuleRec *module = nullptr;
    PropSet props;
    std::map<std::string, ClipRec> clips;
    std::map<std::string, ParamRec> params;
    QImage *frameImage = nullptr; // during processFrame
    int width = 0;
    int height = 0;
    PropSet sourceImageProps;
    PropSet outputImageProps;
    /**
     * The incoming clip of a transition, set for the duration of one render.
     *
     * A transition has two source clips, and without this both would be bound to the same
     * image — the plug-in would dissolve a picture into itself and look like it did
     * nothing. Null for an ordinary filter, which has one source.
     */
    const QImage *transitionTo = nullptr;
    PropSet transitionToImageProps;
};

PropSet g_hostProps;
using OfxHostC = ::OfxHost;
OfxHostC g_hostC{};

// Cast helpers
PropSet *asProp(OfxPropertySetHandle h)
{
    return reinterpret_cast<PropSet *>(h);
}
EffectRec *asEffect(OfxImageEffectHandle h)
{
    return reinterpret_cast<EffectRec *>(h);
}
ClipRec *asClip(OfxImageClipHandle h)
{
    return reinterpret_cast<ClipRec *>(h);
}
ParamRec *asParam(OfxParamHandle h)
{
    return reinterpret_cast<ParamRec *>(h);
}

/**
 * Name a property set for the trace log. Plug-ins hold opaque handles, so the only
 * way to tell "host props" from "the Source clip's props" in a log is to look the
 * pointer back up against the sets we handed out; kOfxPropName/kOfxPropType do that
 * for everything except the host set itself.
 */
QString traceSetName(OfxPropertySetHandle h)
{
    PropSet *ps = asProp(h);
    if (!ps) {
        return QStringLiteral("<null>");
    }
    if (ps == &g_hostProps) {
        return QStringLiteral("host");
    }
    QString type;
    QString name;
    const auto typeIt = ps->props.find(kOfxPropType);
    if (typeIt != ps->props.end() && !typeIt->second.strings.empty()) {
        type = QString::fromStdString(typeIt->second.strings.front());
    }
    const auto nameIt = ps->props.find(kOfxPropName);
    if (nameIt != ps->props.end() && !nameIt->second.strings.empty()) {
        name = QString::fromStdString(nameIt->second.strings.front());
    }
    if (type.isEmpty() && name.isEmpty()) {
        return QStringLiteral("props@%1").arg(reinterpret_cast<quintptr>(ps), 0, 16);
    }
    return name.isEmpty() ? type : QStringLiteral("%1:%2").arg(type, name);
}

/** One line per host callback: who was asked, for what, and what we answered. */
void tracePropCall(const char *op, OfxPropertySetHandle set, const char *property, int index,
                   const QString &value, OfxStatus status)
{
    if (!ofx::Trace::enabled()) {
        return;
    }
    ofx::Trace::write(QStringLiteral("  %1 %2[%3][%4]%5 -> %6")
                          .arg(QString::fromLatin1(op), traceSetName(set),
                               QString::fromUtf8(property ? property : "(null)"))
                          .arg(index)
                          .arg(value.isEmpty() ? QString() : QStringLiteral(" = ") + value)
                          .arg(status));
}

void setString(PropSet *ps, const char *key, int index, const char *value)
{
    if (!ps || !key) {
        return;
    }
    auto &v = ps->props[key];
    v.kind = PropValue::String;
    if (static_cast<int>(v.strings.size()) <= index) {
        v.strings.resize(size_t(index) + 1);
    }
    v.strings[size_t(index)] = value ? value : "";
}

void setInt(PropSet *ps, const char *key, int index, int value)
{
    if (!ps || !key) {
        return;
    }
    auto &v = ps->props[key];
    v.kind = PropValue::Int;
    if (static_cast<int>(v.ints.size()) <= index) {
        v.ints.resize(size_t(index) + 1);
    }
    v.ints[size_t(index)] = value;
}

void setDouble(PropSet *ps, const char *key, int index, double value)
{
    if (!ps || !key) {
        return;
    }
    auto &v = ps->props[key];
    v.kind = PropValue::Double;
    if (static_cast<int>(v.doubles.size()) <= index) {
        v.doubles.resize(size_t(index) + 1);
    }
    v.doubles[size_t(index)] = value;
}

void setPointer(PropSet *ps, const char *key, int index, void *value)
{
    if (!ps || !key) {
        return;
    }
    auto &v = ps->props[key];
    v.kind = PropValue::Pointer;
    if (static_cast<int>(v.pointers.size()) <= index) {
        v.pointers.resize(size_t(index) + 1);
    }
    v.pointers[size_t(index)] = value;
}

/**
 * Declare a multi-value property as present but empty.
 *
 * This is not cosmetic. The OFX C++ support library grows list properties with
 *     n = propGetDimension(prop); propSetString(prop, value, n);
 * so a property the host has never heard of makes propGetDimension fail, and
 * OFX::throwPropertyException turns *any* failure there into
 * OFX::Exception::HostInadequate — which reaches the host as
 * kOfxStatErrMissingHostFeature, with nothing to say which property was at fault.
 * Every list property a plug-in may append to therefore has to exist up front.
 */
void declareList(PropSet *ps, const char *key, PropValue::Kind kind)
{
    if (!ps || !key) {
        return;
    }
    auto &v = ps->props[key];
    if (v.kind == PropValue::None) {
        v.kind = kind;
    }
}

void appendString(PropSet *ps, const char *key, const char *value)
{
    if (!ps || !key) {
        return;
    }
    auto &v = ps->props[key];
    v.kind = PropValue::String;
    v.strings.push_back(value ? value : "");
}

// ---------------------------------------------------------------------------
// Descriptor property defaults
//
// OFX requires the host to hand back a *fully populated* property set for every
// descriptor it creates: the plug-in is entitled to read any spec-defined property
// before it has written it. Handing out a near-empty set is what made real VEGAS
// bundles abort DescribeInContext with kOfxStatErrMissingHostFeature right after
// their first defineClip() — the support library was reading the dimension of
// kOfxImageEffectPropSupportedComponents in order to append to it.
// ---------------------------------------------------------------------------

void seedClipDescriptorProps(PropSet *ps, const std::string &name)
{
    setString(ps, kOfxPropLabel, 0, name.c_str());
    setString(ps, kOfxPropShortLabel, 0, name.c_str());
    setString(ps, kOfxPropLongLabel, 0, name.c_str());
    declareList(ps, kOfxImageEffectPropSupportedComponents, PropValue::String);
    setInt(ps, kOfxImageEffectPropTemporalClipAccess, 0, 0);
    setInt(ps, kOfxImageClipPropOptional, 0, 0);
    setInt(ps, kOfxImageClipPropIsMask, 0, 0);
    // We hand out whole frames only, never tiles — must match the host property, or a
    // plug-in will ask for a sub-rectangle and then address the returned buffer as if it
    // really were that sub-rectangle.
    setInt(ps, kOfxImageEffectPropSupportsTiles, 0, 0);
    setString(ps, kOfxImageClipPropFieldExtraction, 0, kOfxImageFieldDoubled);
}

void seedEffectDescriptorProps(PropSet *ps)
{
    declareList(ps, kOfxImageEffectPropSupportedContexts, PropValue::String);
    declareList(ps, kOfxImageEffectPropSupportedPixelDepths, PropValue::String);
    declareList(ps, kOfxImageEffectPropClipPreferencesSlaveParam, PropValue::String);
    declareList(ps, kOfxPluginPropParamPageOrder, PropValue::String);
    declareList(ps, kOfxPropIcon, PropValue::String);
    setString(ps, kOfxPropLabel, 0, "");
    setString(ps, kOfxPropShortLabel, 0, "");
    setString(ps, kOfxPropLongLabel, 0, "");
    setString(ps, kOfxPropPluginDescription, 0, "");
    setString(ps, kOfxImageEffectPluginPropGrouping, 0, "");
    setString(ps, kOfxPluginPropFilePath, 0, "");
    setString(ps, kOfxImageEffectPluginRenderThreadSafety, 0, kOfxImageEffectRenderInstanceSafe);
    setInt(ps, kOfxImageEffectPluginPropSingleInstance, 0, 0);
    setInt(ps, kOfxImageEffectPluginPropHostFrameThreading, 0, 1);
    setInt(ps, kOfxImageEffectPluginPropFieldRenderTwiceAlways, 0, 1);
    setInt(ps, kOfxImageEffectPropSupportsMultipleClipDepths, 0, 0);
    setInt(ps, kOfxImageEffectPropSupportsMultipleClipPARs, 0, 0);
    setInt(ps, kOfxImageEffectPropSupportsTiles, 0, 0);
    setInt(ps, kOfxImageEffectPropSupportsMultiResolution, 0, 0);
    setInt(ps, kOfxImageEffectPropTemporalClipAccess, 0, 0);
    setString(ps, "OfxImageEffectPropOpenGLRenderSupported", 0, "false");
    // VEGAS extensions a bundle may read straight back off its own descriptor.
    setString(ps, kOfxImageEffectPropVegasContext, 0, kOfxImageEffectPropVegasContextEvent);
    setString(ps, kOfxImageEffectPropVegasUpliftGUID, 0, "");
    setInt(ps, kOfxImageEffectPropVegasPrePanCrop, 0, 0);
    setInt(ps, kOfxImageEffectPropVegasUiOpened, 0, 0);
}

/** Default project geometry an instance reports until the compositor overrides it. */
constexpr double kDefaultProjectWidth = 1920.0;
constexpr double kDefaultProjectHeight = 1080.0;
constexpr double kDefaultFrameRate = 25.0;
constexpr double kDefaultDurationFrames = 100.0;

/**
 * Properties an *instance* (as opposed to a descriptor) must carry. The support
 * library's ImageEffect constructor reads kOfxImageEffectPropContext straight away,
 * so an instance created without it dies the same way DescribeInContext used to.
 */
void seedEffectInstanceProps(PropSet *ps, const std::string &context, double width, double height)
{
    setString(ps, kOfxPropType, 0, kOfxTypeImageEffectInstance);
    setString(ps, kOfxImageEffectPropContext, 0,
              context.empty() ? kOfxImageEffectContextFilter : context.c_str());
    setPointer(ps, kOfxPropInstanceData, 0, nullptr);
    setInt(ps, kOfxPropIsInteractive, 0, 0);
    setInt(ps, kOfxImageEffectPropSequentialRenderStatus, 0, 0);
    setInt(ps, kOfxImageEffectPropInteractiveRenderStatus, 0, 0);
    setDouble(ps, kOfxImageEffectPropProjectSize, 0, width);
    setDouble(ps, kOfxImageEffectPropProjectSize, 1, height);
    setDouble(ps, kOfxImageEffectPropProjectExtent, 0, width);
    setDouble(ps, kOfxImageEffectPropProjectExtent, 1, height);
    setDouble(ps, kOfxImageEffectPropProjectOffset, 0, 0.0);
    setDouble(ps, kOfxImageEffectPropProjectOffset, 1, 0.0);
    setDouble(ps, kOfxImageEffectPropProjectPixelAspectRatio, 0, 1.0);
    setDouble(ps, kOfxImageEffectPropFrameRate, 0, kDefaultFrameRate);
    setDouble(ps, kOfxImageEffectInstancePropEffectDuration, 0, kDefaultDurationFrames);
    setDouble(ps, kOfxImageEffectPropFrameRange, 0, 0.0);
    setDouble(ps, kOfxImageEffectPropFrameRange, 1, kDefaultDurationFrames);
}

/** Clip *instance* properties, i.e. what the clip currently carries rather than accepts. */
void seedClipInstanceProps(PropSet *ps, double width, double height)
{
    setInt(ps, kOfxImageClipPropConnected, 0, 1);
    setString(ps, kOfxImageEffectPropComponents, 0, kOfxImageComponentRGBA);
    setString(ps, kOfxImageEffectPropPixelDepth, 0, kOfxBitDepthByte);
    setString(ps, kOfxImageEffectPropPreMultiplication, 0, kOfxImageUnPreMultiplied);
    setString(ps, kOfxImageClipPropUnmappedComponents, 0, kOfxImageComponentRGBA);
    setString(ps, kOfxImageClipPropUnmappedPixelDepth, 0, kOfxBitDepthByte);
    setString(ps, kOfxImageClipPropFieldOrder, 0, kOfxImageFieldNone);
    setInt(ps, kOfxImageClipPropContinuousSamples, 0, 0);
    setDouble(ps, kOfxImagePropPixelAspectRatio, 0, 1.0);
    setDouble(ps, kOfxImageEffectPropFrameRate, 0, kDefaultFrameRate);
    setDouble(ps, kOfxImageEffectPropUnmappedFrameRate, 0, kDefaultFrameRate);
    setDouble(ps, kOfxImageEffectPropFrameRange, 0, 0.0);
    setDouble(ps, kOfxImageEffectPropFrameRange, 1, kDefaultDurationFrames);
    setDouble(ps, kOfxImageEffectPropUnmappedFrameRange, 0, 0.0);
    setDouble(ps, kOfxImageEffectPropUnmappedFrameRange, 1, kDefaultDurationFrames);
    setDouble(ps, kOfxImageEffectPropRegionOfDefinition, 0, 0.0);
    setDouble(ps, kOfxImageEffectPropRegionOfDefinition, 1, 0.0);
    setDouble(ps, kOfxImageEffectPropRegionOfDefinition, 2, width);
    setDouble(ps, kOfxImageEffectPropRegionOfDefinition, 3, height);
}

void seedParamDescriptorProps(PropSet *ps, const std::string &name, const std::string &type)
{
    setString(ps, kOfxPropLabel, 0, name.c_str());
    setString(ps, kOfxPropShortLabel, 0, name.c_str());
    setString(ps, kOfxPropLongLabel, 0, name.c_str());
    setString(ps, kOfxParamPropScriptName, 0, name.c_str());
    setString(ps, kOfxParamPropHint, 0, "");
    setString(ps, kOfxParamPropParent, 0, "");
    setString(ps, kOfxParamPropCacheInvalidation, 0, kOfxParamInvalidateValueChange);
    declareList(ps, kOfxParamPropChoiceOption, PropValue::String);
    declareList(ps, kOfxParamPropPageChild, PropValue::String);
    declareList(ps, kOfxParamPropDimensionLabel, PropValue::String);
    setPointer(ps, kOfxParamPropDataPtr, 0, nullptr);
    setInt(ps, kOfxParamPropSecret, 0, 0);
    setInt(ps, kOfxParamPropEnabled, 0, 1);
    setInt(ps, kOfxParamPropCanUndo, 0, 1);
    setInt(ps, kOfxParamPropAnimates, 0, 1);
    setInt(ps, kOfxParamPropIsAnimating, 0, 0);
    setInt(ps, kOfxParamPropIsAutoKeying, 0, 0);
    setInt(ps, kOfxParamPropPersistant, 0, 1);
    setInt(ps, kOfxParamPropEvaluateOnChange, 0, 1);

    const bool numeric = type == kOfxParamTypeDouble || type == kOfxParamTypeDouble2D
                         || type == kOfxParamTypeDouble3D || type == kOfxParamTypeInteger
                         || type == kOfxParamTypeInteger2D || type == kOfxParamTypeInteger3D;
    if (numeric) {
        setDouble(ps, kOfxParamPropMin, 0, 0.0);
        setDouble(ps, kOfxParamPropMax, 0, 1.0);
        setDouble(ps, kOfxParamPropDisplayMin, 0, 0.0);
        setDouble(ps, kOfxParamPropDisplayMax, 0, 1.0);
        setDouble(ps, kOfxParamPropDefault, 0, 0.0);
        setDouble(ps, kOfxParamPropIncrement, 0, 0.01);
        setInt(ps, kOfxParamPropDigits, 0, 2);
        setString(ps, kOfxParamPropDoubleType, 0, kOfxParamDoubleTypePlain);
    } else if (type == kOfxParamTypeString) {
        setString(ps, kOfxParamPropDefault, 0, "");
        setString(ps, kOfxParamPropStringMode, 0, kOfxParamStringIsSingleLine);
        setInt(ps, kOfxParamPropStringFilePathExists, 0, 1);
    } else if (type == kOfxParamTypeBoolean || type == kOfxParamTypeChoice) {
        setInt(ps, kOfxParamPropDefault, 0, 0);
    } else if (type == kOfxParamTypeParametric) {
        setInt(ps, kOfxParamPropParametricDimension, 0, 1);
        setDouble(ps, kOfxParamPropParametricRange, 0, 0.0);
        setDouble(ps, kOfxParamPropParametricRange, 1, 1.0);
        declareList(ps, kOfxParamPropParametricUIColour, PropValue::Double);
    }
}


OfxStatus propSetPointer(OfxPropertySetHandle properties, const char *property, int index,
                         void *value)
{
    PropSet *ps = asProp(properties);
    if (!ps) {
        return kOfxStatErrBadHandle;
    }
    setPointer(ps, property, index, value);
    tracePropCall("setPointer", properties, property, index,
                  QStringLiteral("0x%1").arg(reinterpret_cast<quintptr>(value), 0, 16), kOfxStatOK);
    return kOfxStatOK;
}

OfxStatus propSetString(OfxPropertySetHandle properties, const char *property, int index,
                        const char *value)
{
    PropSet *ps = asProp(properties);
    if (!ps) {
        return kOfxStatErrBadHandle;
    }
    setString(ps, property, index, value);
    tracePropCall("setString", properties, property, index,
                  QString::fromUtf8(value ? value : ""), kOfxStatOK);
    return kOfxStatOK;
}

OfxStatus propSetDouble(OfxPropertySetHandle properties, const char *property, int index,
                        double value)
{
    PropSet *ps = asProp(properties);
    if (!ps) {
        return kOfxStatErrBadHandle;
    }
    setDouble(ps, property, index, value);
    tracePropCall("setDouble", properties, property, index, QString::number(value), kOfxStatOK);
    return kOfxStatOK;
}

OfxStatus propSetInt(OfxPropertySetHandle properties, const char *property, int index, int value)
{
    PropSet *ps = asProp(properties);
    if (!ps) {
        return kOfxStatErrBadHandle;
    }
    setInt(ps, property, index, value);
    tracePropCall("setInt", properties, property, index, QString::number(value), kOfxStatOK);
    return kOfxStatOK;
}

OfxStatus propSetPointerN(OfxPropertySetHandle properties, const char *property, int count,
                          void *const *value)
{
    for (int i = 0; i < count; ++i) {
        propSetPointer(properties, property, i, value ? value[i] : nullptr);
    }
    return kOfxStatOK;
}

OfxStatus propSetStringN(OfxPropertySetHandle properties, const char *property, int count,
                         const char *const *value)
{
    for (int i = 0; i < count; ++i) {
        propSetString(properties, property, i, value ? value[i] : nullptr);
    }
    return kOfxStatOK;
}

OfxStatus propSetDoubleN(OfxPropertySetHandle properties, const char *property, int count,
                         const double *value)
{
    for (int i = 0; i < count; ++i) {
        propSetDouble(properties, property, i, value ? value[i] : 0.0);
    }
    return kOfxStatOK;
}

OfxStatus propSetIntN(OfxPropertySetHandle properties, const char *property, int count,
                      const int *value)
{
    for (int i = 0; i < count; ++i) {
        propSetInt(properties, property, i, value ? value[i] : 0);
    }
    return kOfxStatOK;
}

OfxStatus propGetPointer(OfxPropertySetHandle properties, const char *property, int index,
                         void **value)
{
    PropSet *ps = asProp(properties);
    if (!ps || !value) {
        return kOfxStatErrBadHandle;
    }
    auto it = ps->props.find(property ? property : "");
    if (it == ps->props.end() || it->second.kind != PropValue::Pointer
        || index < 0 || index >= int(it->second.pointers.size())) {
        *value = nullptr;
        tracePropCall("getPointer", properties, property, index, QStringLiteral("<missing>"),
                      kOfxStatErrUnknown);
        return kOfxStatErrUnknown;
    }
    *value = it->second.pointers[size_t(index)];
    tracePropCall("getPointer", properties, property, index, QString(), kOfxStatOK);
    return kOfxStatOK;
}

OfxStatus propGetString(OfxPropertySetHandle properties, const char *property, int index,
                        char **value)
{
    PropSet *ps = asProp(properties);
    if (!ps || !value) {
        return kOfxStatErrBadHandle;
    }
    auto it = ps->props.find(property ? property : "");
    if (it == ps->props.end() || it->second.kind != PropValue::String
        || index < 0 || index >= int(it->second.strings.size())) {
        *value = nullptr;
        tracePropCall("getString", properties, property, index, QStringLiteral("<missing>"),
                      kOfxStatErrUnknown);
        return kOfxStatErrUnknown;
    }
    *value = const_cast<char *>(it->second.strings[size_t(index)].c_str());
    tracePropCall("getString", properties, property, index, QString::fromUtf8(*value), kOfxStatOK);
    return kOfxStatOK;
}

OfxStatus propGetDouble(OfxPropertySetHandle properties, const char *property, int index,
                        double *value)
{
    PropSet *ps = asProp(properties);
    if (!ps || !value) {
        return kOfxStatErrBadHandle;
    }
    auto it = ps->props.find(property ? property : "");
    if (it == ps->props.end() || it->second.kind != PropValue::Double
        || index < 0 || index >= int(it->second.doubles.size())) {
        *value = 0.0;
        tracePropCall("getDouble", properties, property, index, QStringLiteral("<missing>"),
                      kOfxStatErrUnknown);
        return kOfxStatErrUnknown;
    }
    *value = it->second.doubles[size_t(index)];
    tracePropCall("getDouble", properties, property, index, QString::number(*value), kOfxStatOK);
    return kOfxStatOK;
}

OfxStatus propGetInt(OfxPropertySetHandle properties, const char *property, int index, int *value)
{
    PropSet *ps = asProp(properties);
    if (!ps || !value) {
        return kOfxStatErrBadHandle;
    }
    auto it = ps->props.find(property ? property : "");
    if (it == ps->props.end() || it->second.kind != PropValue::Int
        || index < 0 || index >= int(it->second.ints.size())) {
        *value = 0;
        tracePropCall("getInt", properties, property, index, QStringLiteral("<missing>"),
                      kOfxStatErrUnknown);
        return kOfxStatErrUnknown;
    }
    *value = it->second.ints[size_t(index)];
    tracePropCall("getInt", properties, property, index, QString::number(*value), kOfxStatOK);
    return kOfxStatOK;
}

OfxStatus propGetPointerN(OfxPropertySetHandle properties, const char *property, int count,
                          void **value)
{
    for (int i = 0; i < count; ++i) {
        propGetPointer(properties, property, i, value ? &value[i] : nullptr);
    }
    return kOfxStatOK;
}

OfxStatus propGetStringN(OfxPropertySetHandle properties, const char *property, int count,
                         char **value)
{
    for (int i = 0; i < count; ++i) {
        propGetString(properties, property, i, value ? &value[i] : nullptr);
    }
    return kOfxStatOK;
}

OfxStatus propGetDoubleN(OfxPropertySetHandle properties, const char *property, int count,
                         double *value)
{
    for (int i = 0; i < count; ++i) {
        propGetDouble(properties, property, i, value ? &value[i] : nullptr);
    }
    return kOfxStatOK;
}

OfxStatus propGetIntN(OfxPropertySetHandle properties, const char *property, int count, int *value)
{
    for (int i = 0; i < count; ++i) {
        propGetInt(properties, property, i, value ? &value[i] : nullptr);
    }
    return kOfxStatOK;
}

OfxStatus propReset(OfxPropertySetHandle, const char *)
{
    return kOfxStatOK;
}

OfxStatus propGetDimension(OfxPropertySetHandle properties, const char *property, int *count)
{
    PropSet *ps = asProp(properties);
    if (!ps || !count) {
        return kOfxStatErrBadHandle;
    }
    auto it = ps->props.find(property ? property : "");
    if (it == ps->props.end()) {
        *count = 0;
        tracePropCall("getDimension", properties, property, 0, QStringLiteral("<missing>"),
                      kOfxStatErrUnknown);
        return kOfxStatErrUnknown;
    }
    switch (it->second.kind) {
    case PropValue::Pointer:
        *count = int(it->second.pointers.size());
        break;
    case PropValue::String:
        *count = int(it->second.strings.size());
        break;
    case PropValue::Double:
        *count = int(it->second.doubles.size());
        break;
    case PropValue::Int:
        *count = int(it->second.ints.size());
        break;
    default:
        *count = 0;
        break;
    }
    tracePropCall("getDimension", properties, property, 0, QString::number(*count), kOfxStatOK);
    return kOfxStatOK;
}

OfxPropertySuiteV1 g_propertySuite = {
    propSetPointer, propSetString,   propSetDouble,   propSetInt,    propSetPointerN,
    propSetStringN, propSetDoubleN,  propSetIntN,     propGetPointer, propGetString,
    propGetDouble,  propGetInt,      propGetPointerN, propGetStringN, propGetDoubleN,
    propGetIntN,    propReset,       propGetDimension};

OfxStatus memoryAlloc(void *, size_t nBytes, void **allocatedData)
{
    if (!allocatedData) {
        return kOfxStatErrBadHandle;
    }
    *allocatedData = std::malloc(nBytes);
    return *allocatedData ? kOfxStatOK : kOfxStatErrMemory;
}

OfxStatus memoryFree(void *allocatedData)
{
    std::free(allocatedData);
    return kOfxStatOK;
}

OfxMemorySuiteV1 g_memorySuite = {memoryAlloc, memoryFree};

OfxStatus messageFn(void *, const char *, const char *, const char *, ...)
{
    return kOfxStatOK;
}

OfxMessageSuiteV1 g_messageSuite = {messageFn};

// ---------------------------------------------------------------------------
// Multi-thread suite — real plug-ins (Vegas's OFX bundles included) treat this
// as a required feature at kOfxActionLoad and bail with
// kOfxStatErrMissingHostFeature if fetchSuite("OfxMultiThreadSuite", 1)
// returns null. A straightforward std::thread-backed implementation.
// ---------------------------------------------------------------------------

thread_local bool t_isSpawnedThread = false;
thread_local unsigned int t_threadIndex = 0;
std::atomic<bool> g_multiThreadBusy{false};

/**
 * Joins the process COM apartment for the lifetime of a render worker (Windows only).
 *
 * VEGAS's own OFX bundles pull in COM-based runtime DLLs (sharedk.dll, OpenColorIO)
 * from the VEGAS install, and VEGAS calls their render entry points from threads that
 * are already in an apartment. On a bare std::thread they are not, and the plug-in
 * faults inside its worker instead of returning an error. A no-op everywhere else.
 */
class ScopedComApartment {
public:
    ScopedComApartment()
    {
#ifdef _WIN32
        const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        m_owned = SUCCEEDED(hr);
#endif
    }

    ~ScopedComApartment()
    {
#ifdef _WIN32
        if (m_owned) {
            ::CoUninitialize();
        }
#endif
    }

    ScopedComApartment(const ScopedComApartment &) = delete;
    ScopedComApartment &operator=(const ScopedComApartment &) = delete;

private:
#ifdef _WIN32
    bool m_owned = false;
#endif
};

unsigned int mtCpuCount()
{
    return std::max(1u, std::thread::hardware_concurrency());
}

/** Height of the frame currently being rendered, published for the thread-count cap. */
std::atomic<int> g_currentRenderHeight{0};

/**
 * Fewest image rows we will let one render thread be given.
 *
 * Plug-ins split the render window by `threadIndex`/`threadMax` themselves, and a
 * spatial effect needs a halo of neighbouring rows around its band. Real VEGAS bundles
 * turn out to corrupt the heap once the band gets comparable to their kernel radius:
 * VEGAS Chroma Blur at radius 8 renders a 512-row frame across 16 threads perfectly,
 * but faults on a 64-row frame at 4 threads (16-row bands) and survives at 2 (32-row
 * bands). The host cannot see a plug-in's kernel size, so it keeps the bands
 * comfortably large instead; on any real frame size this still saturates the CPU
 * (1080p / 64 = 16 bands).
 */
constexpr int kMinRowsPerRenderThread = 64;

/**
 * Upper bound on threads we will spawn for a plug-in's multiThread() call.
 *
 * `OPENVEGAS_OFX_THREADS=1` forces single-threaded rendering — the first thing to try
 * when a third-party plug-in crashes or produces torn output, and what the trace log
 * asks a bug reporter to attach alongside.
 */
unsigned int ofxMaxRenderThreads()
{
    static const unsigned int limit = [] {
        bool ok = false;
        const int v = qEnvironmentVariableIntValue("OPENVEGAS_OFX_THREADS", &ok);
        if (ok && v > 0) {
            return std::min(unsigned(v), mtCpuCount());
        }
        return mtCpuCount();
    }();
    return limit;
}

OfxStatus mtMultiThread(OfxThreadFunctionV1 func, unsigned int nThreads, void *customArg)
{
    if (!func) {
        return kOfxStatFailed;
    }
    bool expected = false;
    if (!g_multiThreadBusy.compare_exchange_strong(expected, true)) {
        return kOfxStatErrExists; // multiThread cannot be called recursively (OFX spec)
    }
    unsigned int cap = ofxMaxRenderThreads();
    const int height = g_currentRenderHeight.load();
    if (height > 0) {
        cap = std::min(cap, unsigned(std::max(1, height / kMinRowsPerRenderThread)));
    }
    const unsigned int actual = std::clamp(nThreads, 1u, cap);
    OPENVEGAS_OFX_TRACE(QStringLiteral("  multiThread(nThreads=%1 -> %2, cap %3 for %4 rows)")
                            .arg(nThreads)
                            .arg(actual)
                            .arg(cap)
                            .arg(height));
    auto invoke = [func, actual, customArg](unsigned int i) {
        ScopedComApartment com;
        t_isSpawnedThread = true;
        t_threadIndex = i;
        func(i, actual, customArg);
    };
    try {
        if (actual == 1) {
            // Fast path: skip OS thread creation for the common single-thread case.
            const bool prevSpawned = t_isSpawnedThread;
            const unsigned int prevIndex = t_threadIndex;
            invoke(0);
            t_isSpawnedThread = prevSpawned;
            t_threadIndex = prevIndex;
        } else {
            std::vector<std::thread> workers;
            workers.reserve(actual);
            for (unsigned int i = 0; i < actual; ++i) {
                workers.emplace_back(invoke, i);
            }
            for (std::thread &t : workers) {
                t.join();
            }
        }
    } catch (...) {
        g_multiThreadBusy = false;
        return kOfxStatFailed;
    }
    g_multiThreadBusy = false;
    return kOfxStatOK;
}

OfxStatus mtNumCPUs(unsigned int *nCPUs)
{
    if (!nCPUs) {
        return kOfxStatFailed;
    }
    *nCPUs = mtCpuCount();
    return kOfxStatOK;
}

OfxStatus mtThreadIndex(unsigned int *threadIndex)
{
    if (!threadIndex) {
        return kOfxStatFailed;
    }
    *threadIndex = t_threadIndex;
    return kOfxStatOK;
}

int mtIsSpawnedThread()
{
    return t_isSpawnedThread ? 1 : 0;
}

OfxStatus mtMutexCreate(OfxMutexHandle *mutex, int lockCount)
{
    if (!mutex) {
        return kOfxStatErrBadHandle;
    }
    auto *m = new QMutex();
    for (int i = 0; i < lockCount; ++i) {
        m->lock();
    }
    *mutex = reinterpret_cast<OfxMutexHandle>(m);
    return kOfxStatOK;
}

OfxStatus mtMutexDestroy(const OfxMutexHandle mutex)
{
    if (!mutex) {
        return kOfxStatErrBadHandle;
    }
    delete reinterpret_cast<QMutex *>(mutex);
    return kOfxStatOK;
}

OfxStatus mtMutexLock(const OfxMutexHandle mutex)
{
    if (!mutex) {
        return kOfxStatErrBadHandle;
    }
    reinterpret_cast<QMutex *>(mutex)->lock();
    return kOfxStatOK;
}

OfxStatus mtMutexUnLock(const OfxMutexHandle mutex)
{
    if (!mutex) {
        return kOfxStatErrBadHandle;
    }
    reinterpret_cast<QMutex *>(mutex)->unlock();
    return kOfxStatOK;
}

OfxStatus mtMutexTryLock(const OfxMutexHandle mutex)
{
    if (!mutex) {
        return kOfxStatErrBadHandle;
    }
    return reinterpret_cast<QMutex *>(mutex)->tryLock() ? kOfxStatOK : kOfxStatFailed;
}

OfxMultiThreadSuiteV1 g_multiThreadSuite = {
    mtMultiThread, mtNumCPUs, mtThreadIndex, mtIsSpawnedThread,
    mtMutexCreate, mtMutexDestroy, mtMutexLock, mtMutexUnLock, mtMutexTryLock};

// ---------------------------------------------------------------------------
// Suites a plug-in may treat as mandatory even though we have nothing useful to
// do behind them. The OFX C++ support library fetches these during
// kOfxActionLoad and throws OFX::Exception::HostInadequate — surfacing as
// kOfxStatErrMissingHostFeature — the moment one comes back null, so answering
// with a working no-op is the difference between a plug-in loading and not.
// ---------------------------------------------------------------------------

OfxStatus messageV2SetPersistent(void *, const char *, const char *, const char *, ...)
{
    return kOfxStatOK;
}

OfxStatus messageV2ClearPersistent(void *)
{
    return kOfxStatOK;
}

OfxMessageSuiteV2 g_messageSuiteV2 = {messageFn, messageV2SetPersistent, messageV2ClearPersistent};

OfxStatus interactSwapBuffers(OfxInteractHandle)
{
    return kOfxStatOK;
}

OfxStatus interactRedraw(OfxInteractHandle)
{
    return kOfxStatOK;
}

OfxStatus interactGetPropertySet(OfxInteractHandle interactInstance,
                                 OfxPropertySetHandle *property)
{
    if (!property) {
        return kOfxStatErrBadHandle;
    }
    // We never create interacts, so the only handle a plug-in can pass back is one
    // it invented. Hand out its own property set rather than a dangling one.
    *property = reinterpret_cast<OfxPropertySetHandle>(interactInstance);
    return interactInstance ? kOfxStatOK : kOfxStatErrBadHandle;
}

OfxInteractSuiteV1 g_interactSuite = {interactSwapBuffers, interactRedraw,
                                      interactGetPropertySet};

OfxStatus progressStart(void *, const char *)
{
    return kOfxStatOK;
}

OfxStatus progressUpdate(void *, double)
{
    return kOfxStatOK; // kOfxStatReplyNo would ask the plug-in to abort
}

OfxStatus progressEnd(void *)
{
    return kOfxStatOK;
}

OfxProgressSuiteV1 g_progressSuite = {progressStart, progressUpdate, progressEnd};

/** Current timeline position, published by the compositor before each process call. */
std::atomic<double> g_timelineTime{0.0};

OfxStatus timeLineGetTime(void *, double *time)
{
    if (!time) {
        return kOfxStatErrBadHandle;
    }
    *time = g_timelineTime.load();
    return kOfxStatOK;
}

OfxStatus timeLineGotoTime(void *, double)
{
    // A plug-in must not drive OpenVegas's transport.
    return kOfxStatFailed;
}

OfxStatus timeLineGetTimeBounds(void *, double *firstTime, double *lastTime)
{
    if (!firstTime || !lastTime) {
        return kOfxStatErrBadHandle;
    }
    *firstTime = 0.0;
    *lastTime = g_timelineTime.load();
    return kOfxStatOK;
}

OfxTimeLineSuiteV1 g_timeLineSuite = {timeLineGetTime, timeLineGotoTime, timeLineGetTimeBounds};

// Parameter suite
OfxStatus paramDefine(OfxParamSetHandle paramSet, const char *paramType, const char *name,
                      OfxPropertySetHandle *propertySet)
{
    EffectRec *fx = asEffect(reinterpret_cast<OfxImageEffectHandle>(paramSet));
    ModuleRec *mod = nullptr;
    // During describe, paramSet is the descriptor effect (we reuse EffectRec for descriptor)
    if (!fx) {
        return kOfxStatErrBadHandle;
    }
    OPENVEGAS_OFX_TRACE(QStringLiteral("  paramDefine(%1, \"%2\")")
                            .arg(QString::fromUtf8(paramType ? paramType : ""),
                                 QString::fromUtf8(name ? name : "")));
    ParamRec &p = fx->params[name ? name : ""];
    p.name = name ? name : "";
    p.type = paramType ? paramType : "";
    p.value = 0.0;
    setString(&p.props, kOfxPropName, 0, p.name.c_str());
    setString(&p.props, kOfxPropType, 0, kOfxTypeParameter);
    setString(&p.props, kOfxParamPropType, 0, p.type.c_str());
    seedParamDescriptorProps(&p.props, p.name, p.type);
    if (propertySet) {
        *propertySet = reinterpret_cast<OfxPropertySetHandle>(&p.props);
    }
    Q_UNUSED(mod);
    return kOfxStatOK;
}

OfxStatus paramGetHandle(OfxParamSetHandle paramSet, const char *name, OfxParamHandle *param,
                         OfxPropertySetHandle *propertySet)
{
    EffectRec *fx = asEffect(reinterpret_cast<OfxImageEffectHandle>(paramSet));
    if (!fx || !param) {
        return kOfxStatErrBadHandle;
    }
    auto it = fx->params.find(name ? name : "");
    if (it == fx->params.end()) {
        return kOfxStatErrUnknown;
    }
    *param = reinterpret_cast<OfxParamHandle>(&it->second);
    if (propertySet) {
        *propertySet = reinterpret_cast<OfxPropertySetHandle>(&it->second.props);
    }
    return kOfxStatOK;
}

OfxStatus paramSetGetPropertySet(OfxParamSetHandle paramSet, OfxPropertySetHandle *propHandle)
{
    EffectRec *fx = asEffect(reinterpret_cast<OfxImageEffectHandle>(paramSet));
    if (!fx || !propHandle) {
        return kOfxStatErrBadHandle;
    }
    *propHandle = reinterpret_cast<OfxPropertySetHandle>(&fx->props);
    return kOfxStatOK;
}

OfxStatus paramGetPropertySet(OfxParamHandle param, OfxPropertySetHandle *propHandle)
{
    ParamRec *p = asParam(param);
    if (!p || !propHandle) {
        return kOfxStatErrBadHandle;
    }
    *propHandle = reinterpret_cast<OfxPropertySetHandle>(&p->props);
    return kOfxStatOK;
}

/**
 * Write one parameter's value into the caller's out-pointer, by declared type.
 *
 * Getting the type wrong is not a no-op: the plug-in passed a pointer expecting to be
 * written, so leaving it alone hands it whatever was on the stack. Booleans and choices
 * used to fall through here, which is why a preset's `Monochromatic=true` never reached
 * VEGAS Add Noise and its preview came out colour-noisy.
 */
void writeParamValue(const ParamRec &p, va_list &ap)
{
    // Strings first: leaving this out-pointer untouched handed the plug-in whatever was on
    // the stack. VEGAS AutoLooks then passed that garbage straight to its DIB loader and
    // faulted inside Vfx1.ofx — a crash in our host's clothing.
    if (p.type == kOfxParamTypeString || p.type == kOfxParamTypeCustom) {
        if (char **out = va_arg(ap, char **)) {
            static const char kEmpty[] = "";
            const auto it = p.props.props.find(kOfxParamPropDefault);
            *out = (it != p.props.props.end() && it->second.kind == PropValue::String
                    && !it->second.strings.empty())
                       ? const_cast<char *>(it->second.strings.front().c_str())
                       : const_cast<char *>(kEmpty);
        }
        return;
    }
    if (p.type == kOfxParamTypeDouble || p.type == kOfxParamTypeParametric) {
        if (double *out = va_arg(ap, double *)) {
            *out = p.value;
        }
        return;
    }
    if (p.type == kOfxParamTypeBoolean || p.type == kOfxParamTypeChoice
        || p.type == kOfxParamTypeInteger) {
        if (int *out = va_arg(ap, int *)) {
            *out = int(std::lround(p.value));
        }
        return;
    }
    // Component `i` of a multi-part parameter: its own value when one was set, otherwise
    // the flat one, which is right for the many parameters whose parts move together.
    const auto component = [&p](int i) {
        return i < int(p.components.size()) ? p.components[size_t(i)] : p.value;
    };
    if (p.type == kOfxParamTypeDouble2D || p.type == kOfxParamTypeDouble3D) {
        const int n = p.type == kOfxParamTypeDouble2D ? 2 : 3;
        for (int i = 0; i < n; ++i) {
            if (double *out = va_arg(ap, double *)) {
                *out = component(i);
            }
        }
        return;
    }
    if (p.type == kOfxParamTypeInteger2D || p.type == kOfxParamTypeInteger3D) {
        const int n = p.type == kOfxParamTypeInteger2D ? 2 : 3;
        for (int i = 0; i < n; ++i) {
            if (int *out = va_arg(ap, int *)) {
                *out = int(std::lround(component(i)));
            }
        }
        return;
    }
    if (p.type == kOfxParamTypeRGB || p.type == kOfxParamTypeRGBA) {
        const int n = p.type == kOfxParamTypeRGB ? 3 : 4;
        for (int i = 0; i < n; ++i) {
            if (double *out = va_arg(ap, double *)) {
                *out = component(i);
            }
        }
    }
}

OfxStatus paramGetValue(OfxParamHandle paramHandle, ...)
{
    ParamRec *p = asParam(paramHandle);
    if (!p) {
        return kOfxStatErrBadHandle;
    }
    va_list ap;
    va_start(ap, paramHandle);
    writeParamValue(*p, ap);
    va_end(ap);
    return kOfxStatOK;
}

OfxStatus paramGetValueAtTimeFixed(OfxParamHandle paramHandle, OfxTime time, ...)
{
    Q_UNUSED(time);
    ParamRec *p = asParam(paramHandle);
    if (!p) {
        return kOfxStatErrBadHandle;
    }
    va_list ap;
    va_start(ap, time);
    writeParamValue(*p, ap);
    va_end(ap);
    return kOfxStatOK;
}

OfxStatus paramGetDerivative(OfxParamHandle, OfxTime, ...)
{
    return kOfxStatErrUnsupported;
}
OfxStatus paramGetIntegral(OfxParamHandle, OfxTime, OfxTime, ...)
{
    return kOfxStatErrUnsupported;
}
OfxStatus paramSetValue(OfxParamHandle paramHandle, ...)
{
    ParamRec *p = asParam(paramHandle);
    if (!p) {
        return kOfxStatErrBadHandle;
    }
    va_list ap;
    va_start(ap, paramHandle);
    if (p->type == kOfxParamTypeDouble) {
        p->value = va_arg(ap, double);
    }
    va_end(ap);
    return kOfxStatOK;
}
OfxStatus paramSetValueAtTimeFixed(OfxParamHandle paramHandle, OfxTime time, ...)
{
    Q_UNUSED(time);
    ParamRec *p = asParam(paramHandle);
    if (!p) {
        return kOfxStatErrBadHandle;
    }
    va_list ap;
    va_start(ap, time);
    if (p->type == kOfxParamTypeDouble) {
        p->value = va_arg(ap, double);
    }
    va_end(ap);
    return kOfxStatOK;
}
OfxStatus paramGetNumKeys(OfxParamHandle, unsigned int *n)
{
    if (n) {
        *n = 0;
    }
    return kOfxStatOK;
}
OfxStatus paramGetKeyTime(OfxParamHandle, unsigned int, OfxTime *)
{
    return kOfxStatErrBadIndex;
}
OfxStatus paramGetKeyIndex(OfxParamHandle, OfxTime, int, int *index)
{
    if (index) {
        *index = -1;
    }
    return kOfxStatFailed;
}
OfxStatus paramDeleteKey(OfxParamHandle, OfxTime)
{
    return kOfxStatErrBadIndex;
}
OfxStatus paramDeleteAllKeys(OfxParamHandle)
{
    return kOfxStatOK;
}
OfxStatus paramCopy(OfxParamHandle, OfxParamHandle, OfxTime, const OfxRangeD *)
{
    return kOfxStatErrUnsupported;
}
OfxStatus paramEditBegin(OfxParamSetHandle, const char *)
{
    return kOfxStatOK;
}
OfxStatus paramEditEnd(OfxParamSetHandle)
{
    return kOfxStatOK;
}

OfxParameterSuiteV1 g_paramSuite = {
    paramDefine,
    paramGetHandle,
    paramSetGetPropertySet,
    paramGetPropertySet,
    paramGetValue,
    paramGetValueAtTimeFixed,
    paramGetDerivative,
    paramGetIntegral,
    paramSetValue,
    paramSetValueAtTimeFixed,
    paramGetNumKeys,
    paramGetKeyTime,
    paramGetKeyIndex,
    paramDeleteKey,
    paramDeleteAllKeys,
    paramCopy,
    paramEditBegin,
    paramEditEnd};

// ---------------------------------------------------------------------------
// Parametric parameter suite — curve params (Color Curves and friends). Stored
// in-memory per param so a plug-in that defines one can round-trip its own
// control points; nothing here is persisted to the project yet.
// ---------------------------------------------------------------------------

/** Piecewise-linear evaluation of a control-point curve; flat outside the end points. */
double evalCurve(const std::vector<std::pair<double, double>> &pts, double position)
{
    if (pts.empty()) {
        return 0.0;
    }
    if (position <= pts.front().first) {
        return pts.front().second;
    }
    if (position >= pts.back().first) {
        return pts.back().second;
    }
    for (size_t i = 1; i < pts.size(); ++i) {
        if (position <= pts[i].first) {
            const double span = pts[i].first - pts[i - 1].first;
            const double t = span > 0.0 ? (position - pts[i - 1].first) / span : 0.0;
            return pts[i - 1].second + t * (pts[i].second - pts[i - 1].second);
        }
    }
    return pts.back().second;
}

void sortCurve(std::vector<std::pair<double, double>> *pts)
{
    std::sort(pts->begin(), pts->end(),
              [](const std::pair<double, double> &a, const std::pair<double, double> &b) {
                  return a.first < b.first;
              });
}

OfxStatus parametricGetValue(OfxParamHandle param, int curveIndex, OfxTime, double parametricPosition,
                             double *returnValue)
{
    ParamRec *p = asParam(param);
    if (!p || !returnValue) {
        return kOfxStatErrBadHandle;
    }
    const auto it = p->curves.find(curveIndex);
    *returnValue = it == p->curves.end() ? 0.0 : evalCurve(it->second, parametricPosition);
    return kOfxStatOK;
}

OfxStatus parametricGetNControlPoints(OfxParamHandle param, int curveIndex, OfxTime,
                                      int *returnValue)
{
    ParamRec *p = asParam(param);
    if (!p || !returnValue) {
        return kOfxStatErrBadHandle;
    }
    const auto it = p->curves.find(curveIndex);
    *returnValue = it == p->curves.end() ? 0 : int(it->second.size());
    return kOfxStatOK;
}

OfxStatus parametricGetNthControlPoint(OfxParamHandle param, int curveIndex, OfxTime, int nthCtl,
                                       double *key, double *value)
{
    ParamRec *p = asParam(param);
    if (!p || !key || !value) {
        return kOfxStatErrBadHandle;
    }
    const auto it = p->curves.find(curveIndex);
    if (it == p->curves.end() || nthCtl < 0 || nthCtl >= int(it->second.size())) {
        return kOfxStatErrBadIndex;
    }
    *key = it->second[size_t(nthCtl)].first;
    *value = it->second[size_t(nthCtl)].second;
    return kOfxStatOK;
}

OfxStatus parametricSetNthControlPoint(OfxParamHandle param, int curveIndex, OfxTime, int nthCtl,
                                       double key, double value, bool)
{
    ParamRec *p = asParam(param);
    if (!p) {
        return kOfxStatErrBadHandle;
    }
    auto &pts = p->curves[curveIndex];
    if (nthCtl < 0 || nthCtl >= int(pts.size())) {
        return kOfxStatErrBadIndex;
    }
    pts[size_t(nthCtl)] = {key, value};
    sortCurve(&pts);
    return kOfxStatOK;
}

OfxStatus parametricAddControlPoint(OfxParamHandle param, int curveIndex, OfxTime, double key,
                                    double value, bool)
{
    ParamRec *p = asParam(param);
    if (!p) {
        return kOfxStatErrBadHandle;
    }
    auto &pts = p->curves[curveIndex];
    pts.emplace_back(key, value);
    sortCurve(&pts);
    return kOfxStatOK;
}

OfxStatus parametricDeleteControlPoint(OfxParamHandle param, int curveIndex, int nthCtl)
{
    ParamRec *p = asParam(param);
    if (!p) {
        return kOfxStatErrBadHandle;
    }
    auto it = p->curves.find(curveIndex);
    if (it == p->curves.end() || nthCtl < 0 || nthCtl >= int(it->second.size())) {
        return kOfxStatErrBadIndex;
    }
    it->second.erase(it->second.begin() + nthCtl);
    return kOfxStatOK;
}

OfxStatus parametricDeleteAllControlPoints(OfxParamHandle param, int curveIndex)
{
    ParamRec *p = asParam(param);
    if (!p) {
        return kOfxStatErrBadHandle;
    }
    p->curves[curveIndex].clear();
    return kOfxStatOK;
}

OfxParametricParameterSuiteV1 g_parametricSuite = {
    parametricGetValue,          parametricGetNControlPoints,   parametricGetNthControlPoint,
    parametricSetNthControlPoint, parametricAddControlPoint,     parametricDeleteControlPoint,
    parametricDeleteAllControlPoints};

// Image effect suite
OfxStatus getPropertySet(OfxImageEffectHandle imageEffect, OfxPropertySetHandle *propHandle)
{
    EffectRec *fx = asEffect(imageEffect);
    if (!fx || !propHandle) {
        return kOfxStatErrBadHandle;
    }
    *propHandle = reinterpret_cast<OfxPropertySetHandle>(&fx->props);
    return kOfxStatOK;
}

OfxStatus getParamSet(OfxImageEffectHandle imageEffect, OfxParamSetHandle *paramSet)
{
    EffectRec *fx = asEffect(imageEffect);
    if (!fx || !paramSet) {
        return kOfxStatErrBadHandle;
    }
    *paramSet = reinterpret_cast<OfxParamSetHandle>(fx);
    return kOfxStatOK;
}

OfxStatus clipDefine(OfxImageEffectHandle imageEffect, const char *name,
                     OfxPropertySetHandle *propertySet)
{
    EffectRec *fx = asEffect(imageEffect);
    if (!fx) {
        return kOfxStatErrBadHandle;
    }
    OPENVEGAS_OFX_TRACE(
        QStringLiteral("  clipDefine(\"%1\")").arg(QString::fromUtf8(name ? name : "")));
    ClipRec &c = fx->clips[name ? name : ""];
    c.name = name ? name : "";
    setString(&c.props, kOfxPropName, 0, c.name.c_str());
    setString(&c.props, kOfxPropType, 0, kOfxTypeClip);
    seedClipDescriptorProps(&c.props, c.name);
    if (propertySet) {
        *propertySet = reinterpret_cast<OfxPropertySetHandle>(&c.props);
    }
    return kOfxStatOK;
}

OfxStatus clipGetHandle(OfxImageEffectHandle imageEffect, const char *name, OfxImageClipHandle *clip,
                        OfxPropertySetHandle *propertySet)
{
    EffectRec *fx = asEffect(imageEffect);
    if (!fx || !clip) {
        return kOfxStatErrBadHandle;
    }
    auto it = fx->clips.find(name ? name : "");
    if (it == fx->clips.end()) {
        return kOfxStatErrUnknown;
    }
    *clip = reinterpret_cast<OfxImageClipHandle>(&it->second);
    if (propertySet) {
        *propertySet = reinterpret_cast<OfxPropertySetHandle>(&it->second.props);
    }
    return kOfxStatOK;
}

OfxStatus clipGetPropertySet(OfxImageClipHandle clip, OfxPropertySetHandle *propHandle)
{
    ClipRec *c = asClip(clip);
    if (!c || !propHandle) {
        return kOfxStatErrBadHandle;
    }
    *propHandle = reinterpret_cast<OfxPropertySetHandle>(&c->props);
    return kOfxStatOK;
}

OfxStatus clipGetImage(OfxImageClipHandle clip, OfxTime time, const OfxRectD *region,
                       OfxPropertySetHandle *imageHandle)
{
    ClipRec *c = asClip(clip);
    if (!c || !imageHandle) {
        return kOfxStatErrBadHandle;
    }
    OPENVEGAS_OFX_TRACE(QStringLiteral("  clipGetImage(\"%1\", t=%2, region=%3)")
                            .arg(QString::fromStdString(c->name))
                            .arg(time)
                            .arg(region ? QStringLiteral("%1,%2..%3,%4")
                                              .arg(region->x1)
                                              .arg(region->y1)
                                              .arg(region->x2)
                                              .arg(region->y2)
                                        : QStringLiteral("full")));
    if (!c->activeImage) {
        return kOfxStatFailed;
    }
    *imageHandle = reinterpret_cast<OfxPropertySetHandle>(c->activeImage);
    return kOfxStatOK;
}

OfxStatus clipReleaseImage(OfxPropertySetHandle)
{
    return kOfxStatOK;
}

OfxStatus clipGetRegionOfDefinition(OfxImageClipHandle clip, OfxTime, OfxRectD *bounds)
{
    ClipRec *c = asClip(clip);
    if (!c || !bounds) {
        return kOfxStatErrBadHandle;
    }
    // Must be the real frame extent: a spatial effect sizes its sampling window from
    // this, and the stub 1x1 rectangle this used to return made every such plug-in
    // compute nonsense.
    bounds->x1 = 0;
    bounds->y1 = 0;
    bounds->x2 = c->rodWidth > 0.0 ? c->rodWidth : kDefaultProjectWidth;
    bounds->y2 = c->rodHeight > 0.0 ? c->rodHeight : kDefaultProjectHeight;
    return kOfxStatOK;
}

int abortFn(OfxImageEffectHandle)
{
    return 0;
}

OfxStatus imageMemoryAlloc(OfxImageEffectHandle, size_t nBytes, OfxImageMemoryHandle *memoryHandle)
{
    void *p = std::malloc(nBytes);
    if (!p) {
        return kOfxStatErrMemory;
    }
    *memoryHandle = reinterpret_cast<OfxImageMemoryHandle>(p);
    return kOfxStatOK;
}
OfxStatus imageMemoryFree(OfxImageMemoryHandle memoryHandle)
{
    std::free(memoryHandle);
    return kOfxStatOK;
}
OfxStatus imageMemoryLock(OfxImageMemoryHandle memoryHandle, void **returnedPtr)
{
    if (!returnedPtr) {
        return kOfxStatErrBadHandle;
    }
    *returnedPtr = memoryHandle;
    return kOfxStatOK;
}
OfxStatus imageMemoryUnlock(OfxImageMemoryHandle)
{
    return kOfxStatOK;
}

OfxImageEffectSuiteV1 g_imageEffectSuite = {
    getPropertySet,    getParamSet,         clipDefine,       clipGetHandle,
    clipGetPropertySet, clipGetImage,       clipReleaseImage, clipGetRegionOfDefinition,
    abortFn,           imageMemoryAlloc,    imageMemoryFree,  imageMemoryLock,
    imageMemoryUnlock};

// ---------------------------------------------------------------------------
// Native plug-in UI: OfxHWndInteractSuite.
//
// VEGAS plug-ins that ship their own panel (only Color Curves does, in the whole
// VEGAS Pro 22 catalogue) draw it into an HWND and talk back to the host through this
// suite. Its layout is not published; the two slots below were recovered from
// Vfx1.ofx by decompilation and are documented in
// MARKDOWN/RE_OFX_HWND_INTERACT_REPORT.md:
//
//   +0x00  getPropertySet(handle, OfxPropertySetHandle *out)
//          Proven, not inferred: the plug-in immediately wraps the returned handle in
//          OFX::PropertySet and reads "OfxPropInstanceData" / "OfxPropEffectInstance"
//          out of it. Called first on every action.
//   +0x08  redraw(handle)                     — one argument
//          Called straight after the plug-in InvalidateRect()s its own window, i.e.
//          "my contents changed". Mirrors OfxInteractSuiteV1::interactRedraw.
//
// Beyond those two the plug-in indexes nothing, so a short struct is enough; extra
// slots would simply never be reached.
//
// Off unless OPENVEGAS_OFX_INTERACT is set — it changes what the host claims to
// support, which affects every plug-in, not just the one under investigation.
// `OPENVEGAS_OFX_INTERACT=probe` swaps the real suite for the measuring harness that
// found this layout, so the same trick can be repeated on another bundle.
// See MARKDOWN/PLAN_OFX_HWND_INTERACT_RE.md.
// ---------------------------------------------------------------------------

bool ofxInteractProbeEnabled()
{
    static const bool on = qEnvironmentVariableIsSet("OPENVEGAS_OFX_INTERACT")
                           && qEnvironmentVariable("OPENVEGAS_OFX_INTERACT") != QLatin1String("0");
    return on;
}

/** `OPENVEGAS_OFX_INTERACT=probe` — hand out the measuring stubs instead of the suite. */
bool ofxInteractProbeOnly()
{
    static const bool probe =
        qEnvironmentVariable("OPENVEGAS_OFX_INTERACT") == QLatin1String("probe");
    return probe;
}

/**
 * One interact instance. The plug-in reaches its own C++ object through
 * kOfxPropInstanceData on this property set, and the host publishes the owning effect
 * as kOfxPropEffectInstance.
 */
struct InteractRec {
    PropSet props;
    EffectRec *effect = nullptr;
};

OfxStatus hwndInteractGetPropertySet(void *interactHandle, OfxPropertySetHandle *out)
{
    auto *rec = reinterpret_cast<InteractRec *>(interactHandle);
    if (!rec || !out) {
        return kOfxStatErrBadHandle;
    }
    *out = reinterpret_cast<OfxPropertySetHandle>(&rec->props);
    OPENVEGAS_OFX_TRACE(QStringLiteral("  hwndInteract getPropertySet"));
    return kOfxStatOK;
}

OfxStatus hwndInteractRedraw(void *interactHandle)
{
    // The plug-in has already repainted its own HWND; nothing for a host that does not
    // composite the panel itself to do beyond noting it.
    OPENVEGAS_OFX_TRACE(QStringLiteral("  hwndInteract redraw"));
    return interactHandle ? kOfxStatOK : kOfxStatErrBadHandle;
}

/** Recovered layout — see the note above. */
struct OfxHWndInteractSuiteV1 {
    OfxStatus (*interactGetPropertySet)(void *, OfxPropertySetHandle *);
    OfxStatus (*interactRedraw)(void *);
};

OfxHWndInteractSuiteV1 g_hwndInteractSuite = {hwndInteractGetPropertySet, hwndInteractRedraw};

/** Widest plausible suite; the real one is far smaller, spare slots simply never fire. */
constexpr int kInteractProbeSlots = 32;

/**
 * One probe slot.
 *
 * Six pointer-sized parameters cover any real signature: on the Windows x64 ABI the caller
 * cleans up, so reading more parameters than were passed is harmless as long as the values
 * are only printed, never dereferenced.
 */
template <int Slot>
OfxStatus interactProbe(void *a0, void *a1, void *a2, void *a3, void *a4, void *a5)
{
    OPENVEGAS_OFX_TRACE(QStringLiteral("  PROBE OfxHWndInteractSuite slot %1"
                                       " args=%2 %3 %4 %5 %6 %7")
                            .arg(Slot)
                            .arg(reinterpret_cast<quintptr>(a0), 0, 16)
                            .arg(reinterpret_cast<quintptr>(a1), 0, 16)
                            .arg(reinterpret_cast<quintptr>(a2), 0, 16)
                            .arg(reinterpret_cast<quintptr>(a3), 0, 16)
                            .arg(reinterpret_cast<quintptr>(a4), 0, 16)
                            .arg(reinterpret_cast<quintptr>(a5), 0, 16));
    // Stage 1: refuse rather than pretend. Returning success without filling an out-param
    // would hand the plug-in stack garbage.
    return kOfxStatFailed;
}

using InteractProbeFn = OfxStatus (*)(void *, void *, void *, void *, void *, void *);

template <int... Slots>
constexpr std::array<InteractProbeFn, sizeof...(Slots)> makeProbeTable(
    std::integer_sequence<int, Slots...>)
{
    return {{&interactProbe<Slots>...}};
}

std::array<InteractProbeFn, kInteractProbeSlots> g_interactProbeSuite =
    makeProbeTable(std::make_integer_sequence<int, kInteractProbeSlots>{});

const void *resolveSuite(const char *suiteName, int suiteVersion)
{
    if (ofxInteractProbeEnabled()
        && (std::strcmp(suiteName, kOfxHWndInteractSuite) == 0
            || std::strcmp(suiteName, kOfxHWndOverlayInteractSuite) == 0)) {
        if (ofxInteractProbeOnly()) {
            return g_interactProbeSuite.data();
        }
        // The overlay variant's layout has not been recovered yet (no VEGAS bundle in
        // Vfx1.ofx uses it; Titles & Text does, and is a separate binary), so only the
        // panel suite is answered for real.
        if (std::strcmp(suiteName, kOfxHWndInteractSuite) == 0) {
            return &g_hwndInteractSuite;
        }
        return g_interactProbeSuite.data();
    }
    if (std::strcmp(suiteName, kOfxPropertySuite) == 0 && suiteVersion == 1) {
        return &g_propertySuite;
    }
    if (std::strcmp(suiteName, kOfxMemorySuite) == 0 && suiteVersion == 1) {
        return &g_memorySuite;
    }
    if (std::strcmp(suiteName, kOfxMessageSuite) == 0 && suiteVersion == 1) {
        return &g_messageSuite;
    }
    if (std::strcmp(suiteName, kOfxMessageSuite) == 0 && suiteVersion == 2) {
        return &g_messageSuiteV2;
    }
    if (std::strcmp(suiteName, kOfxParameterSuite) == 0 && suiteVersion == 1) {
        return &g_paramSuite;
    }
    if (std::strcmp(suiteName, kOfxImageEffectSuite) == 0 && suiteVersion == 1) {
        return &g_imageEffectSuite;
    }
    if (std::strcmp(suiteName, kOfxMultiThreadSuite) == 0 && suiteVersion == 1) {
        return &g_multiThreadSuite;
    }
    if (std::strcmp(suiteName, kOfxInteractSuite) == 0 && suiteVersion == 1) {
        return &g_interactSuite;
    }
    if (std::strcmp(suiteName, kOfxProgressSuite) == 0 && suiteVersion == 1) {
        return &g_progressSuite;
    }
    if (std::strcmp(suiteName, kOfxTimeLineSuite) == 0 && suiteVersion == 1) {
        return &g_timeLineSuite;
    }
    if (std::strcmp(suiteName, kOfxParametricParameterSuite) == 0 && suiteVersion == 1) {
        return &g_parametricSuite;
    }
    // Everything else — including VEGAS's private OfxVegas*Suite family, whose struct
    // layouts are unpublished — is answered honestly with null. Guessing a layout would
    // hand the plug-in function pointers of the wrong arity and crash the app.
    return nullptr;
}

const void *fetchSuite(OfxPropertySetHandle, const char *suiteName, int suiteVersion)
{
    if (!suiteName) {
        return nullptr;
    }
    const void *suite = resolveSuite(suiteName, suiteVersion);
    OPENVEGAS_OFX_TRACE(QStringLiteral("  fetchSuite(\"%1\", v%2) -> %3")
                            .arg(QString::fromUtf8(suiteName))
                            .arg(suiteVersion)
                            .arg(suite ? QStringLiteral("ok") : QStringLiteral("NULL")));
    return suite;
}

/**
 * Writable per-user directory a plug-in may use for caches/licences
 * (kOfxPropVegasHostAppDataDirectory). Created on demand; falls back to the temp dir
 * so the property is never an unusable path.
 */
QString vegasHostAppDataDirectory()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty()) {
        dir = QDir::temp().filePath(QStringLiteral("OpenVegas"));
    }
    QDir().mkpath(dir);
    return QDir::toNativeSeparators(dir);
}

void initHostProps()
{
    static bool once = false;
    if (once) {
        return;
    }
    once = true;
    setString(&g_hostProps, kOfxPropType, 0, kOfxTypeImageEffectHost);
    setString(&g_hostProps, kOfxPropName, 0, "OpenVegas");
    setString(&g_hostProps, kOfxPropLabel, 0, "OpenVegas");
    setInt(&g_hostProps, kOfxPropAPIVersion, 0, 1);
    setInt(&g_hostProps, kOfxPropAPIVersion, 1, 4);
    setInt(&g_hostProps, kOfxPropVersion, 0, 1);
    setInt(&g_hostProps, kOfxPropVersion, 1, 0);
    setInt(&g_hostProps, kOfxPropVersion, 2, 0);
    setString(&g_hostProps, kOfxPropVersionLabel, 0, "1.0");
    setInt(&g_hostProps, kOfxImageEffectHostPropIsBackground, 0, 0);
    // Stage 0 of the native-UI investigation: VEGAS plug-ins gate registering their own
    // interact on these two flags, and with "no" they never publish
    // kOfxImageEffectPluginPropHWndInteractV1 at all — so we had never seen the code path.
    setInt(&g_hostProps, kOfxImageEffectPropSupportsOverlays, 0,
           ofxInteractProbeEnabled() ? 1 : 0);
    setInt(&g_hostProps, kOfxImageEffectPropSupportsTiles, 0, 0);
    setInt(&g_hostProps, kOfxImageEffectPropSupportsMultiResolution, 0, 0);
    setInt(&g_hostProps, kOfxImageEffectPropTemporalClipAccess, 0, 0);
    setInt(&g_hostProps, "OfxImageEffectPropMultipleClipDepths", 0, 0);
    setInt(&g_hostProps, "OfxImageEffectPropSupportsMultipleClipPARs", 0, 0);
    setInt(&g_hostProps, "OfxImageEffectPropSetableFrameRate", 0, 0);
    setInt(&g_hostProps, "OfxImageEffectPropSetableFielding", 0, 0);
    setInt(&g_hostProps, kOfxImageEffectInstancePropSequentialRender, 0, 1);
    setInt(&g_hostProps, kOfxImageEffectPropRenderQualityDraft, 0, 0);
    setInt(&g_hostProps, kOfxParamHostPropSupportsCustomInteract, 0,
           ofxInteractProbeEnabled() ? 1 : 0);
    setInt(&g_hostProps, kOfxParamHostPropSupportsStringAnimation, 0, 0);
    setInt(&g_hostProps, kOfxParamHostPropSupportsBooleanAnimation, 0, 0);
    setInt(&g_hostProps, kOfxParamHostPropSupportsChoiceAnimation, 0, 0);
    setInt(&g_hostProps, kOfxParamHostPropSupportsCustomAnimation, 0, 0);
    setInt(&g_hostProps, kOfxParamHostPropSupportsParametricAnimation, 0, 0);
    setInt(&g_hostProps, kOfxParamHostPropMaxParameters, 0, 1000);
    setInt(&g_hostProps, kOfxParamHostPropMaxPages, 0, 10);
    setInt(&g_hostProps, kOfxParamHostPropPageRowColumnCount, 0, 10);
    setInt(&g_hostProps, kOfxParamHostPropPageRowColumnCount, 1, 1);
    appendString(&g_hostProps, kOfxImageEffectPropSupportedComponents, kOfxImageComponentRGBA);
    appendString(&g_hostProps, kOfxImageEffectPropSupportedContexts, kOfxImageEffectContextFilter);
    appendString(&g_hostProps, kOfxImageEffectPropSupportedContexts,
                 kOfxImageEffectContextTransition);
    appendString(&g_hostProps, kOfxImageEffectPropSupportedContexts, kOfxImageEffectContextGeneral);
    appendString(&g_hostProps, kOfxImageEffectPropSupportedContexts,
                 kOfxImageEffectContextGenerator);
    appendString(&g_hostProps, kOfxImageEffectPropSupportedPixelDepths, kOfxBitDepthByte);

    // VEGAS extensions. VEGAS's own OFX bundles are built against a fork of the OFX
    // support library that reads these unconditionally; a host that omits them cannot
    // get such a bundle past DescribeInContext. See plugins/OfxVegasExtensions.h.
    setString(&g_hostProps, kOfxImageEffectHostPropNativeOrigin, 0,
              kOfxImageEffectHostPropNativeOriginTopLeft); // QImage rows run top-down
    setString(&g_hostProps, kOfxPropVegasHostAppDataDirectory, 0,
              vegasHostAppDataDirectory().toUtf8().constData());
    setPointer(&g_hostProps, kOfxPropVegasHostHWnd, 0, nullptr);
    setPointer(&g_hostProps, kOfxPropHostOSHandle, 0, nullptr);

    g_hostC.host = reinterpret_cast<OfxPropertySetHandle>(&g_hostProps);
    g_hostC.fetchSuite = fetchSuite;
}

void ensureHostC()
{
    initHostProps();
}

bool statusOk(OfxStatus st)
{
    return st == kOfxStatOK || st == kOfxStatReplyDefault;
}

/**
 * Index of `effectId` inside an already-loaded binary.
 *
 * Returns `requestedIdx` unchanged when no effectId is known (plain "load whatever is
 * at this index"), the matching index when the requested one points elsewhere, and -1
 * when the binary does not contain the effect at all. Never silently substitutes a
 * different effect: a bundle like Vfx1.ofx holds dozens, and rendering the wrong one
 * looks like a broken effect rather than a lookup failure.
 */
int resolvePluginIndex(int (*getNum)(), OfxPlugin *(*getPlugin)(int), int requestedIdx,
                       const QString &effectId)
{
    if (effectId.isEmpty()) {
        return requestedIdx;
    }
    auto identifierAt = [getPlugin](int i) -> QString {
        OfxPlugin *p = getPlugin(i);
        return (p && p->pluginIdentifier) ? QString::fromUtf8(p->pluginIdentifier) : QString();
    };
    if (identifierAt(requestedIdx).compare(effectId, Qt::CaseInsensitive) == 0) {
        return requestedIdx;
    }
    const int n = getNum();
    for (int i = 0; i < n; ++i) {
        if (identifierAt(i).compare(effectId, Qt::CaseInsensitive) == 0) {
            OPENVEGAS_OFX_TRACE(QStringLiteral("resolvePluginIndex: \"%1\" is #%2, not #%3")
                                    .arg(effectId)
                                    .arg(i)
                                    .arg(requestedIdx));
            return i;
        }
    }
    return -1;
}

} // namespace

OfxPluginIdParts OfxHost::parsePluginId(const QString &pluginId)
{
    OfxPluginIdParts p;
    QString rest = pluginId.trimmed();
    if (rest.startsWith(QStringLiteral("ofx-id:"), Qt::CaseInsensitive)) {
        p.effectId = rest.mid(7).trimmed();
        return p;
    }
    if (rest.startsWith(QStringLiteral("ofx:"), Qt::CaseInsensitive)) {
        rest = rest.mid(4);
    }
    const int hash1 = rest.indexOf(QLatin1Char('#'));
    if (hash1 >= 0) {
        p.path = rest.left(hash1);
        const int hash2 = rest.indexOf(QLatin1Char('#'), hash1 + 1);
        if (hash2 >= 0) {
            p.index = rest.mid(hash1 + 1, hash2 - hash1 - 1).toInt();
            p.effectId = rest.mid(hash2 + 1);
        } else {
            p.index = rest.mid(hash1 + 1).toInt();
        }
    } else if (!rest.isEmpty()) {
        p.path = rest;
    }
    return p;
}

QHash<QString, int> OfxHost::effectIndexMap(const QString &binaryPath)
{
    QHash<QString, int> out;
    if (binaryPath.isEmpty() || !QFileInfo::exists(binaryPath)) {
        return out;
    }
    if (!checkArchLoadable(binaryPath, nullptr)) {
        return out;
    }
    ScopedOfxDllDirectory dllDirGuard(ofxInstallRootForBinary(binaryPath));
    QLibrary lib(binaryPath);
    if (!lib.load()) {
        return out;
    }
    using GetNumFn = int (*)();
    using GetPluginFn = OfxPlugin *(*)(int);
    auto getNum = reinterpret_cast<GetNumFn>(lib.resolve("OfxGetNumberOfPlugins"));
    auto getPlugin = reinterpret_cast<GetPluginFn>(lib.resolve("OfxGetPlugin"));
    if (!getNum || !getPlugin) {
        lib.unload();
        return out;
    }
    try {
        const int n = getNum();
        for (int i = 0; i < n; ++i) {
            OfxPlugin *plug = getPlugin(i);
            if (plug && plug->pluginIdentifier) {
                out.insert(QString::fromUtf8(plug->pluginIdentifier).toLower(), i);
            }
        }
    } catch (...) {
    }
    lib.unload();
    return out;
}

/**
 * The VEGAS context that goes with an OFX one.
 *
 * VEGAS's fork of the OFX support library maps this in-arg to its own enum before the
 * plug-in sees anything, and treats an absent or unmappable value as fatal — which is why
 * VEGAS bundles used to stop dead after their first clipDefine(). A transition lives on a
 * fade, and that is the VEGAS context it belongs to.
 */
const char *vegasContextFor(const std::string &ofxContext)
{
    if (ofxContext == kOfxImageEffectContextGenerator) {
        return kOfxImageEffectPropVegasContextGenerator;
    }
    if (ofxContext == kOfxImageEffectContextTransition) {
        return kOfxImageEffectPropVegasContextEventFadeIn;
    }
    return kOfxImageEffectPropVegasContextEvent;
}

QVector<OfxEffectSummary> OfxHost::enumerateEffects(const QString &binaryPath)
{
    QVector<OfxEffectSummary> out;
    if (binaryPath.isEmpty() || !QFileInfo::exists(binaryPath)) {
        return out;
    }
    if (!checkArchLoadable(binaryPath, nullptr)) {
        return out;
    }
    ensureHostC();

    ScopedOfxDllDirectory dllDirGuard(ofxInstallRootForBinary(binaryPath));
    QLibrary lib(binaryPath);
    if (!lib.load()) {
        OPENVEGAS_OFX_TRACE(
            QStringLiteral("enumerateEffects: load failed for \"%1\": %2")
                .arg(binaryPath, lib.errorString()));
        return out;
    }
    using GetNumFn = int (*)();
    using GetPluginFn = OfxPlugin *(*)(int);
    auto getNum = reinterpret_cast<GetNumFn>(lib.resolve("OfxGetNumberOfPlugins"));
    auto getPlugin = reinterpret_cast<GetPluginFn>(lib.resolve("OfxGetPlugin"));
    if (!getNum || !getPlugin) {
        lib.unload();
        return out;
    }

    auto propString = [](const PropSet &props, const char *key) -> QString {
        const auto it = props.props.find(key);
        if (it != props.props.end() && it->second.kind == PropValue::String
            && !it->second.strings.empty()) {
            return QString::fromStdString(it->second.strings.front());
        }
        return {};
    };

    try {
        const int n = getNum();
        for (int i = 0; i < n; ++i) {
            OfxPlugin *plug = getPlugin(i);
            if (!plug || !plug->pluginIdentifier || !plug->setHost || !plug->mainEntry) {
                continue;
            }
            OfxEffectSummary s;
            s.pluginIndex = i;
            s.effectId = QString::fromUtf8(plug->pluginIdentifier);

            // Describe is the only way to learn the label; a plug-in that refuses still
            // gets listed under its identifier rather than being dropped.
            plug->setHost(&g_hostC);
            if (statusOk(plug->mainEntry(kOfxActionLoad, nullptr, nullptr, nullptr))) {
                EffectRec descFx;
                setString(&descFx.props, kOfxPropType, 0, kOfxTypeImageEffect);
                setString(&descFx.props, kOfxPropName, 0, plug->pluginIdentifier);
                seedEffectDescriptorProps(&descFx.props);
                if (statusOk(plug->mainEntry(kOfxActionDescribe,
                                             reinterpret_cast<OfxImageEffectHandle>(&descFx),
                                             nullptr, nullptr))) {
                    s.label = propString(descFx.props, kOfxPropLabel);
                    s.grouping = propString(descFx.props, kOfxImageEffectPluginPropGrouping);
                }
            }
            if (s.label.isEmpty()) {
                s.label = s.effectId.section(QLatin1Char(':'), -1);
            }
            out.push_back(s);
        }
    } catch (...) {
        OPENVEGAS_OFX_TRACE(
            QStringLiteral("enumerateEffects: exception in \"%1\"").arg(binaryPath));
    }
    lib.unload();
    return out;
}

QVector<OfxContextReport> OfxHost::describeContexts(const QString &binaryPath, int pluginIndex)
{
    QVector<OfxContextReport> out;
    if (binaryPath.isEmpty() || !QFileInfo::exists(binaryPath)) {
        return out;
    }
    if (!checkArchLoadable(binaryPath, nullptr)) {
        return out;
    }
    ensureHostC();

    ScopedOfxDllDirectory dllDirGuard(ofxInstallRootForBinary(binaryPath));
    QLibrary lib(binaryPath);
    if (!lib.load()) {
        return out;
    }
    using GetNumFn = int (*)();
    using GetPluginFn = OfxPlugin *(*)(int);
    auto getNum = reinterpret_cast<GetNumFn>(lib.resolve("OfxGetNumberOfPlugins"));
    auto getPlugin = reinterpret_cast<GetPluginFn>(lib.resolve("OfxGetPlugin"));
    if (!getNum || !getPlugin) {
        lib.unload();
        return out;
    }

    try {
        if (pluginIndex < 0 || pluginIndex >= getNum()) {
            lib.unload();
            return out;
        }
        OfxPlugin *plug = getPlugin(pluginIndex);
        if (!plug || !plug->setHost || !plug->mainEntry) {
            lib.unload();
            return out;
        }
        plug->setHost(&g_hostC);
        if (!statusOk(plug->mainEntry(kOfxActionLoad, nullptr, nullptr, nullptr))) {
            lib.unload();
            return out;
        }

        EffectRec descFx;
        setString(&descFx.props, kOfxPropType, 0, kOfxTypeImageEffect);
        setString(&descFx.props, kOfxPropName, 0, plug->pluginIdentifier);
        seedEffectDescriptorProps(&descFx.props);
        if (!statusOk(plug->mainEntry(kOfxActionDescribe,
                                      reinterpret_cast<OfxImageEffectHandle>(&descFx), nullptr,
                                      nullptr))) {
            lib.unload();
            return out;
        }

        // Every context this host advertises, asked for one at a time on a descriptor of
        // its own — the point is to find out what the plug-in will accept, so one context
        // being refused must not spoil the next.
        const std::vector<std::string> candidates = {
            kOfxImageEffectContextFilter, kOfxImageEffectContextTransition,
            kOfxImageEffectContextGeneral, kOfxImageEffectContextGenerator};
        for (const std::string &context : candidates) {
            PropSet inArgs;
            setString(&inArgs, kOfxImageEffectPropContext, 0, context.c_str());
            setString(&inArgs, kOfxImageEffectPropVegasContext, 0,
                      vegasContextFor(context.c_str()));
            EffectRec ctxFx;
            ctxFx.props = descFx.props;
            ctxFx.params = descFx.params;
            OfxContextReport rep;
            rep.context = QString::fromStdString(context);
            rep.status = plug->mainEntry(kOfxImageEffectActionDescribeInContext,
                                         reinterpret_cast<OfxImageEffectHandle>(&ctxFx),
                                         reinterpret_cast<OfxPropertySetHandle>(&inArgs), nullptr);
            rep.accepted = statusOk(rep.status);
            for (const auto &ckv : ctxFx.clips) {
                rep.clips << QString::fromStdString(ckv.first);
            }
            for (const auto &pkv : ctxFx.params) {
                rep.params << QString::fromStdString(pkv.first);
            }
            rep.clips.sort();
            rep.params.sort();
            out.push_back(rep);
        }
    } catch (...) {
        OPENVEGAS_OFX_TRACE(
            QStringLiteral("describeContexts: exception in \"%1\"").arg(binaryPath));
    }
    lib.unload();
    return out;
}

struct OfxHost::Impl {
    QMutex mutex;
    QHash<QString, std::shared_ptr<ModuleRec>> modules; // path -> module
    /**
     * Modules that already failed, with the reason. Preview calls processSlot() once per
     * frame, and without this a plug-in that cannot load would be LoadLibrary'd, probed
     * and thrown away sixty times a second behind an emulated fallback.
     */
    QHash<QString, QString> failedModules;
    QHash<int, std::shared_ptr<EffectRec>> instances;
    QHash<QString, int> instanceKeyToId; // path|slot -> id
    int nextId = 1;

    std::shared_ptr<ModuleRec> ensureModule(const OfxPluginDesc &desc, QString *errorOut)
    {
        ensureHostC();
        const QString path = desc.path;
        const int plugIdx = std::max(0, desc.pluginIndex);
        // Key by effectId when the caller knows it: the same effect can be asked for
        // under a stale index, and keying by index would then load and kOfxActionLoad
        // the very same OfxPlugin twice.
        const QString modKey = path + QLatin1Char('#')
                               + (desc.effectId.isEmpty() ? QString::number(plugIdx)
                                                          : desc.effectId);
        if (path.isEmpty() || !QFileInfo::exists(path)) {
            if (errorOut) {
                *errorOut = QStringLiteral("OFX binary not found: \"%1\"")
                                .arg(desc.name.isEmpty() ? path : desc.name);
            }
            return {};
        }
        if (modules.contains(modKey) && modules[modKey] && modules[modKey]->loaded) {
            return modules[modKey];
        }
        const auto failed = failedModules.constFind(modKey);
        if (failed != failedModules.cend()) {
            if (errorOut) {
                *errorOut = *failed;
            }
            return {};
        }
        /** Remember why this module is unusable so the next frame does not retry it. */
        auto fail = [&](const QString &reason) -> std::shared_ptr<ModuleRec> {
            failedModules.insert(modKey, reason);
            if (errorOut) {
                *errorOut = reason;
            }
            return {};
        };
        if (!checkArchLoadable(path, errorOut)) {
            return fail(errorOut && !errorOut->isEmpty()
                            ? *errorOut
                            : QStringLiteral("OFX plug-in \"%1\" is built for another platform")
                                  .arg(path));
        }

        auto mod = std::make_shared<ModuleRec>();
        mod->path = path;
        mod->pluginIndex = plugIdx;
        mod->lib = std::make_unique<QLibrary>(path);
        ScopedOfxDllDirectory dllDirGuard(ofxInstallRootForBinary(path));
        if (!mod->lib->load()) {
            return fail(QStringLiteral("Failed to load OFX \"%1\": %2")
                            .arg(path, mod->lib->errorString()));
        }

        using GetNumFn = int (*)();
        using GetPluginFn = OfxPlugin *(*)(int);
        auto getNum = reinterpret_cast<GetNumFn>(mod->lib->resolve("OfxGetNumberOfPlugins"));
        auto getPlugin = reinterpret_cast<GetPluginFn>(mod->lib->resolve("OfxGetPlugin"));
        if (!getNum || !getPlugin) {
            mod->lib->unload();
            return fail(QStringLiteral(
                            "OFX entry points OfxGetNumberOfPlugins / OfxGetPlugin not found in \"%1\"")
                            .arg(path));
        }

        try {
            const int n = getNum();
            if (n <= 0 || plugIdx >= n) {
                return fail(QStringLiteral("OFX plugin index %1 out of range (%2) in \"%3\"")
                                .arg(plugIdx)
                                .arg(n)
                                .arg(path));
            }
            // A bundle holds many effects (Vfx1.ofx alone has dozens), so an index that
            // was never resolved silently means "index 0" — a completely different
            // effect. Whenever the caller knows which effectId it wants, trust that over
            // the index and re-resolve.
            const int resolvedIdx = resolvePluginIndex(getNum, getPlugin, plugIdx, desc.effectId);
            if (resolvedIdx < 0) {
                return fail(QStringLiteral("OFX effect \"%1\" not found in \"%2\"")
                                .arg(desc.effectId, path));
            }
            if (resolvedIdx != plugIdx) {
                mod->pluginIndex = resolvedIdx;
            }
            OfxPlugin *plug = getPlugin(resolvedIdx);
            if (!plug || !plug->setHost || !plug->mainEntry) {
                return fail(QStringLiteral("Invalid OfxPlugin from \"%1\"").arg(path));
            }
            plug->setHost(&g_hostC);

            OPENVEGAS_OFX_TRACE(QStringLiteral("=== %1 [#%2 %3] ===")
                                    .arg(path)
                                    .arg(plugIdx)
                                    .arg(QString::fromUtf8(plug->pluginIdentifier
                                                               ? plug->pluginIdentifier
                                                               : "")));
            OPENVEGAS_OFX_TRACE(QStringLiteral("Load begin"));
            OfxStatus st = plug->mainEntry(kOfxActionLoad, nullptr, nullptr, nullptr);
            OPENVEGAS_OFX_TRACE(QStringLiteral("Load -> status %1").arg(st));
            if (!statusOk(st)) {
                return fail(
                    QStringLiteral("OFX Load failed (status %1) for \"%2\"").arg(st).arg(path));
            }
            mod->plugin = plug;
            mod->loaded = true;

            // Describe into a temporary effect descriptor
            EffectRec descFx;
            descFx.module = mod.get();
            setString(&descFx.props, kOfxPropType, 0, kOfxTypeImageEffect);
            setString(&descFx.props, kOfxPropName, 0,
                      plug->pluginIdentifier ? plug->pluginIdentifier : "plugin");
            seedEffectDescriptorProps(&descFx.props);
            OPENVEGAS_OFX_TRACE(QStringLiteral("Describe begin"));
            st = plug->mainEntry(kOfxActionDescribe,
                                 reinterpret_cast<OfxImageEffectHandle>(&descFx), nullptr, nullptr);
            OPENVEGAS_OFX_TRACE(QStringLiteral("Describe -> status %1").arg(st));
            if (!statusOk(st)) {
                return fail(
                    QStringLiteral("OFX Describe failed (status %1) for \"%2\"").arg(st).arg(path));
            }
            mod->descriptorProps = descFx.props;
            mod->descriptorClips = descFx.clips;
            mod->descriptorParams = descFx.params;
            mod->described = true;

            // DescribeInContext — try the contexts the plugin itself declared via
            // kOfxImageEffectPropSupportedContexts (populated during kOfxActionDescribe)
            // first. Vegas's own OFX bundles leave that property unset during plain
            // Describe, so fall back to the contexts Vegas's own resource manifests
            // (Contents/Resources/*.xml) are known to use, in order, when it's empty.
            std::vector<std::string> contextsToTry;
            const auto supportedContextsIt =
                descFx.props.props.find(kOfxImageEffectPropSupportedContexts);
            if (supportedContextsIt != descFx.props.props.end()
                && supportedContextsIt->second.kind == PropValue::String) {
                for (const std::string &context : supportedContextsIt->second.strings) {
                    if (!context.empty()) {
                        contextsToTry.push_back(context);
                    }
                }
            }
            if (contextsToTry.empty()) {
                contextsToTry = {kOfxImageEffectContextFilter, kOfxImageEffectContextGeneral,
                                 kOfxImageEffectContextGenerator};
            }
            for (const std::string &context : contextsToTry) {
                PropSet inArgs;
                setString(&inArgs, kOfxImageEffectPropContext, 0, context.c_str());
                // VEGAS's fork of the OFX support library maps this in-arg to its
                // VegasContextEnum *before* handing control to the plug-in, and treats an
                // absent/unmappable value as fatal — that is why VEGAS bundles used to
                // stop dead after their first clipDefine(). Standard OFX plug-ins ignore
                // the extra property. See plugins/OfxVegasExtensions.h.
                setString(&inArgs, kOfxImageEffectPropVegasContext, 0,
                          vegasContextFor(context.c_str()));
                EffectRec ctxFx;
                ctxFx.module = mod.get();
                ctxFx.props = mod->descriptorProps;
                ctxFx.params = mod->descriptorParams;
                OPENVEGAS_OFX_TRACE(QStringLiteral("DescribeInContext(%1) begin")
                                        .arg(QString::fromStdString(context)));
                st = plug->mainEntry(kOfxImageEffectActionDescribeInContext,
                                     reinterpret_cast<OfxImageEffectHandle>(&ctxFx),
                                     reinterpret_cast<OfxPropertySetHandle>(&inArgs), nullptr);
                OPENVEGAS_OFX_TRACE(QStringLiteral("DescribeInContext(%1) -> status %2, "
                                                   "%3 clip(s), %4 param(s)")
                                        .arg(QString::fromStdString(context))
                                        .arg(st)
                                        .arg(ctxFx.clips.size())
                                        .arg(ctxFx.params.size()));
                if (statusOk(st)) {
                    mod->descriptorClips = ctxFx.clips;
                    mod->descriptorParams = ctxFx.params;
                    mod->descriptorProps = ctxFx.props;
                    mod->describedInContext = true;
                    mod->describedContext = context;
                    break;
                }
                // Soft: some plugs may still process via emulation
                if (errorOut) {
                    *errorOut = QStringLiteral(
                                    "OFX DescribeInContext(%1) failed (status %2) for \"%3\" — "
                                    "load accepted for entry points only")
                                    .arg(QString::fromStdString(context))
                                    .arg(st)
                                    .arg(path);
                }
                // Still consider load success if Load+Describe worked
            }

            modules[modKey] = mod;
            if (errorOut && errorOut->contains(QStringLiteral("DescribeInContext"))) {
                // Keep warning but load succeeded
            } else if (errorOut) {
                errorOut->clear();
            }
            return mod;
        } catch (const std::exception &ex) {
            return fail(
                QStringLiteral("OFX load exception: %1").arg(QString::fromUtf8(ex.what())));
        } catch (...) {
            return fail(QStringLiteral("OFX load crashed or threw unknown exception"));
        }
    }
};

OfxHost &OfxHost::instance()
{
    static OfxHost host;
    return host;
}

OfxHost::OfxHost()
    : m_(new Impl)
{
}

OfxHost::~OfxHost()
{
    delete m_;
    m_ = nullptr;
}

QStringList OfxHost::supportedArchFolderNames()
{
    // Native ABI first so bundle scanning prefers a loadable binary, then the rest so a
    // foreign-platform bundle is still found and can be reported instead of vanishing.
    QStringList out = OfxPluginPaths::loadableArchFolderNames();
    for (const QString &known : OfxPluginPaths::knownArchFolderNames()) {
        if (!out.contains(known, Qt::CaseInsensitive)) {
            out << known;
        }
    }
    return out;
}

OfxPluginDesc OfxHost::describe(const QString &path)
{
    const QFileInfo fi(path);
    if (!fi.exists()) {
        OfxPluginDesc d;
        d.name = fi.fileName();
        d.path = path;
        return d;
    }
    return describeBundleOrFile(fi);
}

QVector<OfxPluginDesc> OfxHost::discoverInRoot(const QString &root)
{
    QVector<OfxPluginDesc> out;
    if (root.isEmpty()) {
        return out;
    }

    QDir ofx(QDir(root).filePath(QStringLiteral("OFX Video Plug-Ins")));
    if (!ofx.exists()) {
        ofx = QDir(root);
    }
    if (!ofx.exists()) {
        return out;
    }

    const QFileInfoList entries =
        ofx.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &fi : entries) {
        const QString name = fi.fileName();
        const bool looksBundle = name.contains(QStringLiteral(".ofx"), Qt::CaseInsensitive)
                                 || fi.isDir();
        if (!looksBundle && !name.endsWith(QStringLiteral(".ofx"), Qt::CaseInsensitive)) {
            continue;
        }
        if (name.startsWith(QLatin1Char('.'))) {
            continue;
        }
        OfxPluginDesc d = describeBundleOrFile(fi);
        if (d.name.isEmpty()) {
            continue;
        }
        out.push_back(d);
    }
    return out;
}

QVector<OfxPluginDesc> OfxHost::discover(const PluginScanner &scanner)
{
    QVector<OfxPluginDesc> out;
    QStringList seen;
    for (const QString &root : scanner.candidateRoots()) {
        const QVector<OfxPluginDesc> batch = discoverInRoot(root);
        if (batch.isEmpty()) {
            continue;
        }
        for (const OfxPluginDesc &d : batch) {
            const QString key = d.path.isEmpty() ? d.bundlePath : d.path;
            if (seen.contains(key)) {
                continue;
            }
            seen << key;
            out.push_back(d);
        }
        if (!out.isEmpty()) {
            break;
        }
    }
    return out;
}

bool OfxHost::load(const OfxPluginDesc &desc, QString *errorOut)
{
    QMutexLocker lock(&instance().m_->mutex);
    auto mod = instance().m_->ensureModule(desc, errorOut);
    return mod && mod->loaded;
}

int OfxHost::createInstance(const OfxPluginDesc &desc, QString *errorOut)
{
    QMutexLocker lock(&m_->mutex);
    auto mod = m_->ensureModule(desc, errorOut);
    if (!mod || !mod->plugin) {
        return 0;
    }

    try {
        auto fx = std::make_shared<EffectRec>();
        fx->module = mod.get();
        fx->props = mod->descriptorProps;
        fx->clips = mod->descriptorClips;
        fx->params = mod->descriptorParams;
        // Apply defaults from descriptor param props
        for (auto &kv : fx->params) {
            auto it = kv.second.props.props.find(kOfxParamPropDefault);
            if (it != kv.second.props.props.end() && it->second.kind == PropValue::Double
                && !it->second.doubles.empty()) {
                kv.second.value = it->second.doubles[0];
            } else if (kv.second.name == "gain") {
                kv.second.value = 1.5;
            }
        }
        seedEffectInstanceProps(&fx->props, mod->describedContext, kDefaultProjectWidth,
                                kDefaultProjectHeight);
        for (auto &ckv : fx->clips) {
            seedClipInstanceProps(&ckv.second.props, kDefaultProjectWidth, kDefaultProjectHeight);
            ckv.second.rodWidth = kDefaultProjectWidth;
            ckv.second.rodHeight = kDefaultProjectHeight;
        }

        OfxStatus st =
            mod->plugin->mainEntry(kOfxActionCreateInstance,
                                   reinterpret_cast<OfxImageEffectHandle>(fx.get()), nullptr,
                                   nullptr);
        if (!statusOk(st)) {
            if (errorOut) {
                *errorOut = QStringLiteral("OFX CreateInstance failed (status %1)").arg(st);
            }
            return 0;
        }

        const int id = m_->nextId++;
        m_->instances.insert(id, fx);
        if (errorOut) {
            errorOut->clear();
        }
        return id;
    } catch (...) {
        if (errorOut) {
            *errorOut = QStringLiteral("OFX createInstance exception");
        }
        return 0;
    }
}

void OfxHost::destroyInstance(int id)
{
    QMutexLocker lock(&m_->mutex);
    auto it = m_->instances.find(id);
    if (it == m_->instances.end() || !it.value()) {
        return;
    }
    auto fx = it.value();
    if (fx->module && fx->module->plugin) {
        try {
            fx->module->plugin->mainEntry(kOfxActionDestroyInstance,
                                          reinterpret_cast<OfxImageEffectHandle>(fx.get()),
                                          nullptr, nullptr);
        } catch (...) {
        }
    }
    m_->instances.erase(it);
    for (auto kit = m_->instanceKeyToId.begin(); kit != m_->instanceKeyToId.end();) {
        if (kit.value() == id) {
            kit = m_->instanceKeyToId.erase(kit);
        } else {
            ++kit;
        }
    }
}

bool OfxHost::processFrame(int instanceId, QImage *rgba, double timeSec, const QVariantMap &params,
                           QString *errorOut)
{
    if (!rgba || rgba->isNull()) {
        if (errorOut) {
            *errorOut = QStringLiteral("OFX processFrame: null image");
        }
        return false;
    }

    QMutexLocker lock(&m_->mutex);
    auto it = m_->instances.find(instanceId);
    if (it == m_->instances.end() || !it.value() || !it.value()->module
        || !it.value()->module->plugin) {
        if (errorOut) {
            *errorOut = QStringLiteral("OFX processFrame: invalid instance");
        }
        return false;
    }

    auto fx = it.value();
    try {
        // Apply params
        for (auto pit = params.constBegin(); pit != params.constEnd(); ++pit) {
            auto fit = fx->params.find(pit.key().toStdString());
            if (fit == fx->params.end()) {
                continue;
            }
            // Booleans and choices carry as 0/1 doubles all the way through; restricting
            // this to Double params meant a preset could never switch one on.
            if (fit->second.type == kOfxParamTypeGroup || fit->second.type == kOfxParamTypePage
                || fit->second.type == kOfxParamTypeString) {
                continue;
            }
            // A list carries one value per component — a colour, a position. Anything else
            // is a single number that every component shares.
            if (pit.value().typeId() == QMetaType::QVariantList) {
                const QVariantList parts = pit.value().toList();
                fit->second.components.clear();
                fit->second.components.reserve(size_t(parts.size()));
                for (const QVariant &part : parts) {
                    fit->second.components.push_back(part.toDouble());
                }
                fit->second.value = parts.isEmpty() ? 0.0 : parts.first().toDouble();
            } else {
                fit->second.components.clear();
                fit->second.value = pit.value().toDouble();
            }
        }
        // Common alias
        if (params.contains(QStringLiteral("gain"))) {
            auto fit = fx->params.find("gain");
            if (fit != fx->params.end()) {
                fit->second.value = params.value(QStringLiteral("gain")).toDouble();
            }
        }

        *rgba = rgba->convertToFormat(QImage::Format_RGBA8888);
        fx->frameImage = rgba;
        fx->width = rgba->width();
        fx->height = rgba->height();

        // Source and output must be distinct buffers: a filter is entitled to read any
        // source pixel while writing any output pixel, and a blur that reads its own
        // partially written output produces smeared garbage.
        QImage sourceFrame = rgba->copy();
        // The incoming side of a transition, in the same format and size as the outgoing
        // one: a plug-in reads both clips over the same render window.
        QImage toFrame;
        if (fx->transitionTo && !fx->transitionTo->isNull()) {
            toFrame = fx->transitionTo->convertToFormat(QImage::Format_RGBA8888);
            if (toFrame.size() != rgba->size()) {
                toFrame = toFrame.scaled(rgba->size(), Qt::IgnoreAspectRatio,
                                         Qt::SmoothTransformation);
            }
        }

        auto fillImageProps = [&](PropSet *ps, const char *role, QImage *img) {
            ps->props.clear();
            setString(ps, kOfxPropType, 0, kOfxTypeImage);
            // Plug-ins key their internal caches off this; it must be stable for the same
            // pixels and differ once anything about them changes.
            setString(ps, kOfxImagePropUniqueIdentifier, 0,
                      QStringLiteral("%1:%2:%3x%4:%5")
                          .arg(QString::fromLatin1(role))
                          .arg(instanceId)
                          .arg(img->width())
                          .arg(img->height())
                          .arg(timeSec, 0, 'g', 10)
                          .toUtf8()
                          .constData());
            setPointer(ps, kOfxImagePropData, 0, img->bits());
            setInt(ps, kOfxImagePropRowBytes, 0, img->bytesPerLine());
            setDouble(ps, kOfxImageEffectPropRenderScale, 0, 1.0);
            setDouble(ps, kOfxImageEffectPropRenderScale, 1, 1.0);
            setInt(ps, kOfxImagePropBounds, 0, 0);
            setInt(ps, kOfxImagePropBounds, 1, 0);
            setInt(ps, kOfxImagePropBounds, 2, img->width());
            setInt(ps, kOfxImagePropBounds, 3, img->height());
            setInt(ps, kOfxImagePropRegionOfDefinition, 0, 0);
            setInt(ps, kOfxImagePropRegionOfDefinition, 1, 0);
            setInt(ps, kOfxImagePropRegionOfDefinition, 2, img->width());
            setInt(ps, kOfxImagePropRegionOfDefinition, 3, img->height());
            setString(ps, kOfxImageEffectPropComponents, 0, kOfxImageComponentRGBA);
            setString(ps, kOfxImageEffectPropPixelDepth, 0, kOfxBitDepthByte);
            setString(ps, kOfxImageEffectPropPreMultiplication, 0, kOfxImageUnPreMultiplied);
            setDouble(ps, kOfxImagePropPixelAspectRatio, 0, 1.0);
            setString(ps, kOfxImagePropField, 0, kOfxImageFieldNone);
        };

        fillImageProps(&fx->sourceImageProps, "source", &sourceFrame);
        fillImageProps(&fx->outputImageProps, "output", rgba);
        if (!toFrame.isNull()) {
            fillImageProps(&fx->transitionToImageProps, "sourceTo", &toFrame);
        }

        // The effect instance was created against the project frame size; this render is
        // for the frame actually in hand, so re-publish the real geometry.
        setDouble(&fx->props, kOfxImageEffectPropProjectSize, 0, rgba->width());
        setDouble(&fx->props, kOfxImageEffectPropProjectSize, 1, rgba->height());
        setDouble(&fx->props, kOfxImageEffectPropProjectExtent, 0, rgba->width());
        setDouble(&fx->props, kOfxImageEffectPropProjectExtent, 1, rgba->height());
        for (auto &ckv : fx->clips) {
            ckv.second.rodWidth = rgba->width();
            ckv.second.rodHeight = rgba->height();
            setDouble(&ckv.second.props, kOfxImageEffectPropRegionOfDefinition, 2, rgba->width());
            setDouble(&ckv.second.props, kOfxImageEffectPropRegionOfDefinition, 3, rgba->height());
        }

        for (auto &ckv : fx->clips) {
            if (ckv.first == kOfxImageEffectSimpleSourceClipName
                || ckv.first == "Source") {
                ckv.second.activeImage = &fx->sourceImageProps;
            } else if (ckv.first == kOfxImageEffectOutputClipName
                       || ckv.first == "Output") {
                ckv.second.activeImage = &fx->outputImageProps;
            } else if (ckv.first == kOfxImageEffectTransitionSourceToClipName
                       && !toFrame.isNull()) {
                // The clip the transition is going to. Everything else — including
                // SourceFrom — reads the outgoing frame, which is what a filter's single
                // source is too.
                ckv.second.activeImage = &fx->transitionToImageProps;
            } else {
                ckv.second.activeImage = &fx->sourceImageProps;
            }
        }

        g_timelineTime.store(timeSec);
        g_currentRenderHeight.store(rgba->height());

        PropSet inArgs;
        setDouble(&inArgs, kOfxPropTime, 0, timeSec);
        setString(&inArgs, kOfxImageEffectPropFieldToRender, 0, kOfxImageFieldNone);
        setInt(&inArgs, kOfxImageEffectPropRenderWindow, 0, 0);
        setInt(&inArgs, kOfxImageEffectPropRenderWindow, 1, 0);
        setInt(&inArgs, kOfxImageEffectPropRenderWindow, 2, rgba->width());
        setInt(&inArgs, kOfxImageEffectPropRenderWindow, 3, rgba->height());
        setDouble(&inArgs, kOfxImageEffectPropRenderScale, 0, 1.0);
        setDouble(&inArgs, kOfxImageEffectPropRenderScale, 1, 1.0);
        setInt(&inArgs, kOfxImageEffectPropSequentialRenderStatus, 0, 0);
        setInt(&inArgs, kOfxImageEffectPropInteractiveRenderStatus, 0, 0);
        setInt(&inArgs, kOfxImageEffectPropRenderQualityDraft, 0, 0);
        // VEGAS extensions: its support library reads the timecode and the
        // stereoscopic view selection out of the render in-args unconditionally.
        // Timecode is expressed the way VEGAS does, in frames; the view arguments say
        // "one view, the left/only one", which is what a 2D compositor renders.
        setDouble(&inArgs, kOfxPropVegasTimeCode, 0, timeSec * kDefaultFrameRate);
        setInt(&inArgs, kOfxImageEffectPropViewsToRender, 0, 1);
        setInt(&inArgs, kOfxImageEffectPropRenderView, 0, 0);
        setString(&inArgs, kOfxImageEffectPropRenderQuality, 0,
                  kOfxImageEffectPropRenderQualityBest);

        OPENVEGAS_OFX_TRACE(QStringLiteral("Render begin (t=%1, %2x%3)")
                                .arg(timeSec)
                                .arg(rgba->width())
                                .arg(rgba->height()));
        OfxStatus st =
            fx->module->plugin->mainEntry(kOfxImageEffectActionRender,
                                          reinterpret_cast<OfxImageEffectHandle>(fx.get()),
                                          reinterpret_cast<OfxPropertySetHandle>(&inArgs),
                                          nullptr);
        OPENVEGAS_OFX_TRACE(QStringLiteral("Render -> status %1").arg(st));

        for (auto &ckv : fx->clips) {
            ckv.second.activeImage = nullptr;
        }
        fx->frameImage = nullptr;

        if (!statusOk(st)) {
            // Deliberately no host-side substitute: see kOpenVegasEmulatedVideoFx.
            // A failed render is reported as failed, not papered over with our own pixels.
            //
            // Disabled 2026-08-12 — was: apply applyGain() with the plug-in's "gain" param
            // (or 1.5) and return true, which made a broken plug-in look like a working one.
            if (errorOut) {
                *errorOut = QStringLiteral("OFX Render failed (status %1)").arg(st);
            }
            return false;
        }

        *rgba = rgba->convertToFormat(QImage::Format_ARGB32);
        if (errorOut) {
            errorOut->clear();
        }
        return true;
    } catch (...) {
        if (errorOut) {
            *errorOut = QStringLiteral("OFX processFrame exception");
        }
        return false;
    }
}

bool OfxHost::processTransition(int instanceId, const QImage &from, const QImage &to,
                                QImage *out, double progress, const QVariantMap &params,
                                QString *errorOut)
{
    if (!out) {
        if (errorOut) {
            *errorOut = QStringLiteral("OFX processTransition: no output image");
        }
        return false;
    }
    if (from.isNull() && to.isNull()) {
        if (errorOut) {
            *errorOut = QStringLiteral("OFX processTransition: both clips are empty");
        }
        return false;
    }

    // The outgoing clip is the output buffer's starting content, the same arrangement a
    // filter uses: the plug-in gets it as SourceFrom and writes over the output.
    const QSize size = !from.isNull() ? from.size() : to.size();
    *out = (from.isNull() ? to : from).convertToFormat(QImage::Format_RGBA8888);
    if (out->size() != size) {
        *out = out->scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    QVariantMap withProgress = params;
    // The plug-in declares this parameter itself; the host only has to fill it in. Both
    // spellings are set because a plug-in is free to name it either way and setting one
    // that does not exist is ignored.
    withProgress.insert(QString::fromLatin1(kOfxImageEffectTransitionParamName),
                        std::clamp(progress, 0.0, 1.0));
    withProgress.insert(QStringLiteral("transition"), std::clamp(progress, 0.0, 1.0));

    {
        QMutexLocker lock(&m_->mutex);
        auto it = m_->instances.find(instanceId);
        if (it == m_->instances.end() || !it.value()) {
            if (errorOut) {
                *errorOut = QStringLiteral("OFX processTransition: invalid instance");
            }
            return false;
        }
        it.value()->transitionTo = &to;
    }

    const bool ok = processFrame(instanceId, out, 0.0, withProgress, errorOut);

    {
        QMutexLocker lock(&m_->mutex);
        auto it = m_->instances.find(instanceId);
        if (it != m_->instances.end() && it.value()) {
            // Left set, the next ordinary filter render through this instance would bind
            // a dangling pointer as its second clip.
            it.value()->transitionTo = nullptr;
        }
    }
    return ok;
}

bool OfxHost::processEmulated(QImage *rgba, const QString &displayName, const QVariantMap &params)
{
    Q_UNUSED(rgba);
    Q_UNUSED(displayName);
    Q_UNUSED(params);
#if OPENVEGAS_EMULATED_VIDEO_FX
    if (!rgba || rgba->isNull()) {
        return false;
    }
    const QString n = displayName.trimmed();
    if (n.compare(QLatin1String("Soften"), Qt::CaseInsensitive) == 0
        || n.compare(QLatin1String("Blur"), Qt::CaseInsensitive) == 0
        || n.contains(QLatin1String("soften"), Qt::CaseInsensitive)
        || n.contains(QLatin1String("blur"), Qt::CaseInsensitive)
        || n.contains(QLatin1String("chromablur"), Qt::CaseInsensitive)) {
        const int radius = std::max(1, int(std::lround(mapGet(params, QStringLiteral("radius"), 1.0))));
        applyBoxBlur(rgba, radius);
        return true;
    }
    if (n.compare(QLatin1String("Invert"), Qt::CaseInsensitive) == 0
        || n.contains(QLatin1String("invert"), Qt::CaseInsensitive)) {
        applyInvert(rgba);
        return true;
    }
    if (n.compare(QLatin1String("Sepia"), Qt::CaseInsensitive) == 0
        || n.contains(QLatin1String("sepia"), Qt::CaseInsensitive)) {
        applySepia(rgba);
        return true;
    }
    if (n.compare(QLatin1String("Brightness and Contrast"), Qt::CaseInsensitive) == 0) {
        applyBrightnessContrast(rgba, mapGet(params, QStringLiteral("brightness"), 0.0),
                                mapGet(params, QStringLiteral("contrast"), 1.0));
        return true;
    }
    if (n.compare(QLatin1String("Gain"), Qt::CaseInsensitive) == 0
        || n.endsWith(QLatin1String("Gain"), Qt::CaseInsensitive)) {
        applyGain(rgba, mapGet(params, QStringLiteral("gain"), 1.5));
        return true;
    }
#endif
    return false;
}

bool OfxHost::processSlot(FxSlot &slot, QImage *rgba, double timeSec)
{
    if (!rgba || rgba->isNull() || slot.bypass) {
        return false;
    }

    // Vegas catalog resolution (effectId/displayName -> real binary + index) already
    // happened here; parts.path/parts.index are trustworthy as-is when non-empty.
    slot = VegasVideoPluginCatalog::resolveVideoFxSlot(slot);
    const QVariantMap params = loadSlotParams(slot);
    OfxPluginIdParts parts = parsePluginId(slot.pluginId);

    if (parts.path.isEmpty() && QFileInfo::exists(slot.pluginId)
        && slot.pluginId.endsWith(QStringLiteral(".ofx"), Qt::CaseInsensitive)) {
        parts.path = slot.pluginId;
    }

    if (!parts.path.isEmpty()) {
        const QString key =
            parts.path + QLatin1Char('#') + QString::number(parts.index) + QLatin1Char('|')
            + (parts.effectId.isEmpty() ? slot.pluginId : parts.effectId);
        QMutexLocker lock(&m_->mutex);
        int id = m_->instanceKeyToId.value(key, 0);
        lock.unlock();

        if (id == 0) {
            OfxPluginDesc desc = describe(parts.path);
            desc.path = parts.path;
            desc.pluginIndex = parts.index;
            desc.effectId = parts.effectId;
            QString err;
            id = createInstance(desc, &err);
            if (id > 0) {
                QMutexLocker lock2(&m_->mutex);
                m_->instanceKeyToId.insert(key, id);
            }
        }
        if (id > 0) {
            QString err;
            if (processFrame(id, rgba, timeSec, params, &err)) {
                return true;
            }
        }
    }

    // No stand-in when the real plug-in isn't there — see kOpenVegasEmulatedVideoFx.
    return processEmulated(rgba, slot.displayName, params);
}

QVector<OfxParamInfo> OfxHost::paramsForSlot(FxSlot slot)
{
    QVector<OfxParamInfo> out;
    if (slot.format != PluginFormat::Ofx || slot.bypass) {
        return out;
    }

    slot = VegasVideoPluginCatalog::resolveVideoFxSlot(slot);
    OfxPluginIdParts parts = parsePluginId(slot.pluginId);
    if (parts.path.isEmpty() && QFileInfo::exists(slot.pluginId)
        && slot.pluginId.endsWith(QStringLiteral(".ofx"), Qt::CaseInsensitive)) {
        parts.path = slot.pluginId;
    }
    if (parts.path.isEmpty()) {
        return out;
    }

    OfxPluginDesc desc = describe(parts.path);
    desc.path = parts.path;
    desc.pluginIndex = parts.index;
    desc.effectId = parts.effectId;
    QString err;
    std::shared_ptr<ModuleRec> mod = m_->ensureModule(desc, &err);
    if (!mod) {
        return out;
    }

    auto propString = [](const PropSet &props, const char *key) -> QString {
        const auto it = props.props.find(key);
        if (it != props.props.end() && it->second.kind == PropValue::String
            && !it->second.strings.empty()) {
            return QString::fromStdString(it->second.strings.front());
        }
        return {};
    };
    auto propDouble = [](const PropSet &props, const char *key, double fallback) -> double {
        const auto it = props.props.find(key);
        if (it != props.props.end() && it->second.kind == PropValue::Double
            && !it->second.doubles.empty()) {
            return it->second.doubles.front();
        }
        return fallback;
    };

    out.reserve(int(mod->descriptorParams.size()));
    for (const auto &kv : mod->descriptorParams) {
        const ParamRec &p = kv.second;
        const bool isBoolean = p.type == kOfxParamTypeBoolean;
        if (p.type != kOfxParamTypeDouble && !isBoolean) {
            continue; // Choice/int/group/page have no editor yet.
        }
        OfxParamInfo info;
        info.toggle = isBoolean;
        info.name = QString::fromStdString(p.name);
        info.label = propString(p.props, kOfxPropLabel);
        if (info.label.isEmpty()) {
            info.label = info.name;
        }
        if (isBoolean) {
            // Booleans arrive through kOfxParamPropDefault as an int, not a double.
            const auto it = p.props.props.find(kOfxParamPropDefault);
            info.defaultValue = (it != p.props.props.end() && it->second.kind == PropValue::Int
                                 && !it->second.ints.empty() && it->second.ints.front() != 0)
                                    ? 1.0
                                    : 0.0;
            info.minValue = 0.0;
            info.maxValue = 1.0;
            out.push_back(info);
            continue;
        }
        info.defaultValue = propDouble(p.props, kOfxParamPropDefault, 0.0);
        info.minValue = propDouble(p.props, kOfxParamPropDisplayMin,
                                   propDouble(p.props, kOfxParamPropMin, 0.0));
        info.maxValue = propDouble(p.props, kOfxParamPropDisplayMax,
                                   propDouble(p.props, kOfxParamPropMax, 1.0));
        if (info.maxValue <= info.minValue) {
            info.maxValue = info.minValue + 1.0;
        }
        out.push_back(info);
    }
    return out;
}

} // namespace openvegas
