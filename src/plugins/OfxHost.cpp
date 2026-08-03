#include "plugins/OfxHost.h"

#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QIODevice>
#include <QLibrary>
#include <QMutex>
#include <QMutexLocker>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

extern "C" {
#include <ofxCore.h>
#include <ofxImageEffect.h>
#include <ofxMemory.h>
#include <ofxMessage.h>
#include <ofxParam.h>
#include <ofxProperty.h>
}

namespace openvegas {
namespace {

// ---------------------------------------------------------------------------
// Discover helpers (unchanged behaviour)
// ---------------------------------------------------------------------------

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

    for (const QString &arch : OfxHost::supportedArchFolderNames()) {
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

QString ofxPathFromPluginId(const QString &pluginId)
{
    if (pluginId.startsWith(QStringLiteral("ofx:"), Qt::CaseInsensitive)) {
        return pluginId.mid(4);
    }
    return {};
}

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
    PropSet props;
};

struct ClipRec {
    std::string name;
    PropSet props;
    PropSet *activeImage = nullptr; // during render
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

void appendString(PropSet *ps, const char *key, const char *value)
{
    if (!ps || !key) {
        return;
    }
    auto &v = ps->props[key];
    v.kind = PropValue::String;
    v.strings.push_back(value ? value : "");
}

OfxStatus propSetPointer(OfxPropertySetHandle properties, const char *property, int index,
                         void *value)
{
    PropSet *ps = asProp(properties);
    if (!ps) {
        return kOfxStatErrBadHandle;
    }
    setPointer(ps, property, index, value);
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
    return kOfxStatOK;
}

OfxStatus propSetInt(OfxPropertySetHandle properties, const char *property, int index, int value)
{
    PropSet *ps = asProp(properties);
    if (!ps) {
        return kOfxStatErrBadHandle;
    }
    setInt(ps, property, index, value);
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
        return kOfxStatErrUnknown;
    }
    *value = it->second.pointers[size_t(index)];
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
        return kOfxStatErrUnknown;
    }
    *value = const_cast<char *>(it->second.strings[size_t(index)].c_str());
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
        return kOfxStatErrUnknown;
    }
    *value = it->second.doubles[size_t(index)];
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
        return kOfxStatErrUnknown;
    }
    *value = it->second.ints[size_t(index)];
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
    ParamRec &p = fx->params[name ? name : ""];
    p.name = name ? name : "";
    p.type = paramType ? paramType : "";
    p.value = 0.0;
    setString(&p.props, kOfxPropName, 0, p.name.c_str());
    setString(&p.props, kOfxPropType, 0, kOfxTypeParameter);
    setString(&p.props, kOfxParamPropType, 0, p.type.c_str());
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

OfxStatus paramGetValue(OfxParamHandle paramHandle, ...)
{
    ParamRec *p = asParam(paramHandle);
    if (!p) {
        return kOfxStatErrBadHandle;
    }
    va_list ap;
    va_start(ap, paramHandle);
    if (p->type == kOfxParamTypeDouble) {
        double *out = va_arg(ap, double *);
        if (out) {
            *out = p->value;
        }
    }
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
    if (p->type == kOfxParamTypeDouble) {
        double *out = va_arg(ap, double *);
        if (out) {
            *out = p->value;
        }
    }
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
    ClipRec &c = fx->clips[name ? name : ""];
    c.name = name ? name : "";
    setString(&c.props, kOfxPropName, 0, c.name.c_str());
    setString(&c.props, kOfxPropType, 0, kOfxTypeClip);
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

OfxStatus clipGetImage(OfxImageClipHandle clip, OfxTime, const OfxRectD *,
                       OfxPropertySetHandle *imageHandle)
{
    ClipRec *c = asClip(clip);
    if (!c || !imageHandle) {
        return kOfxStatErrBadHandle;
    }
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
    bounds->x1 = 0;
    bounds->y1 = 0;
    bounds->x2 = 1;
    bounds->y2 = 1;
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

const void *fetchSuite(OfxPropertySetHandle, const char *suiteName, int suiteVersion)
{
    if (!suiteName) {
        return nullptr;
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
    if (std::strcmp(suiteName, kOfxParameterSuite) == 0 && suiteVersion == 1) {
        return &g_paramSuite;
    }
    if (std::strcmp(suiteName, kOfxImageEffectSuite) == 0 && suiteVersion == 1) {
        return &g_imageEffectSuite;
    }
    return nullptr;
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
    setInt(&g_hostProps, kOfxImageEffectHostPropIsBackground, 0, 0);
    setInt(&g_hostProps, kOfxImageEffectPropSupportsTiles, 0, 0);
    setInt(&g_hostProps, kOfxImageEffectPropSupportsMultiResolution, 0, 0);
    setInt(&g_hostProps, kOfxImageEffectPropTemporalClipAccess, 0, 0);
    setInt(&g_hostProps, "OfxImageEffectPropMultipleClipDepths", 0, 0);
    setInt(&g_hostProps, "OfxImageEffectPropSupportsMultipleClipPARs", 0, 0);
    setInt(&g_hostProps, "OfxImageEffectPropSetableFrameRate", 0, 0);
    setInt(&g_hostProps, "OfxImageEffectPropSetableFielding", 0, 0);
    setInt(&g_hostProps, kOfxParamHostPropSupportsCustomInteract, 0, 0);
    setInt(&g_hostProps, kOfxParamHostPropMaxParameters, 0, 1000);
    setInt(&g_hostProps, kOfxParamHostPropMaxPages, 0, 10);
    setInt(&g_hostProps, kOfxParamHostPropPageRowColumnCount, 0, 10);
    setInt(&g_hostProps, kOfxParamHostPropPageRowColumnCount, 1, 1);
    appendString(&g_hostProps, kOfxImageEffectPropSupportedComponents, kOfxImageComponentRGBA);
    appendString(&g_hostProps, kOfxImageEffectPropSupportedContexts, kOfxImageEffectContextFilter);
    appendString(&g_hostProps, kOfxImageEffectPropSupportedPixelDepths, kOfxBitDepthByte);

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

} // namespace

struct OfxHost::Impl {
    QMutex mutex;
    QHash<QString, std::shared_ptr<ModuleRec>> modules; // path -> module
    QHash<int, std::shared_ptr<EffectRec>> instances;
    QHash<QString, int> instanceKeyToId; // path|slot -> id
    int nextId = 1;

    std::shared_ptr<ModuleRec> ensureModule(const OfxPluginDesc &desc, QString *errorOut)
    {
        ensureHostC();
        const QString path = desc.path;
        if (path.isEmpty() || !QFileInfo::exists(path)) {
            if (errorOut) {
                *errorOut = QStringLiteral("OFX binary not found: \"%1\"")
                                .arg(desc.name.isEmpty() ? path : desc.name);
            }
            return {};
        }
        if (modules.contains(path) && modules[path] && modules[path]->loaded) {
            return modules[path];
        }

        auto mod = std::make_shared<ModuleRec>();
        mod->path = path;
        mod->lib = std::make_unique<QLibrary>(path);
        if (!mod->lib->load()) {
            if (errorOut) {
                *errorOut = QStringLiteral("Failed to load OFX \"%1\": %2")
                                .arg(path, mod->lib->errorString());
            }
            return {};
        }

        using GetNumFn = int (*)();
        using GetPluginFn = OfxPlugin *(*)(int);
        auto getNum = reinterpret_cast<GetNumFn>(mod->lib->resolve("OfxGetNumberOfPlugins"));
        auto getPlugin = reinterpret_cast<GetPluginFn>(mod->lib->resolve("OfxGetPlugin"));
        if (!getNum || !getPlugin) {
            if (errorOut) {
                *errorOut = QStringLiteral(
                    "OFX entry points OfxGetNumberOfPlugins / OfxGetPlugin not found in \"%1\"")
                                .arg(path);
            }
            mod->lib->unload();
            return {};
        }

        try {
            const int n = getNum();
            if (n <= 0) {
                if (errorOut) {
                    *errorOut = QStringLiteral("OFX reports zero plugins: \"%1\"").arg(path);
                }
                return {};
            }
            OfxPlugin *plug = getPlugin(0);
            if (!plug || !plug->setHost || !plug->mainEntry) {
                if (errorOut) {
                    *errorOut = QStringLiteral("Invalid OfxPlugin from \"%1\"").arg(path);
                }
                return {};
            }
            plug->setHost(&g_hostC);

            OfxStatus st = plug->mainEntry(kOfxActionLoad, nullptr, nullptr, nullptr);
            if (!statusOk(st)) {
                if (errorOut) {
                    *errorOut = QStringLiteral("OFX Load failed (status %1) for \"%2\"")
                                    .arg(st)
                                    .arg(path);
                }
                return {};
            }
            mod->plugin = plug;
            mod->pluginIndex = 0;
            mod->loaded = true;

            // Describe into a temporary effect descriptor
            EffectRec descFx;
            descFx.module = mod.get();
            setString(&descFx.props, kOfxPropType, 0, kOfxTypeImageEffect);
            setString(&descFx.props, kOfxPropName, 0,
                      plug->pluginIdentifier ? plug->pluginIdentifier : "plugin");
            st = plug->mainEntry(kOfxActionDescribe,
                                 reinterpret_cast<OfxImageEffectHandle>(&descFx), nullptr, nullptr);
            if (!statusOk(st)) {
                if (errorOut) {
                    *errorOut = QStringLiteral("OFX Describe failed (status %1) for \"%2\"")
                                    .arg(st)
                                    .arg(path);
                }
                return {};
            }
            mod->descriptorProps = descFx.props;
            mod->descriptorClips = descFx.clips;
            mod->descriptorParams = descFx.params;
            mod->described = true;

            // DescribeInContext(Filter)
            PropSet inArgs;
            setString(&inArgs, kOfxImageEffectPropContext, 0, kOfxImageEffectContextFilter);
            EffectRec ctxFx;
            ctxFx.module = mod.get();
            ctxFx.props = mod->descriptorProps;
            ctxFx.params = mod->descriptorParams;
            st = plug->mainEntry(kOfxImageEffectActionDescribeInContext,
                                 reinterpret_cast<OfxImageEffectHandle>(&ctxFx),
                                 reinterpret_cast<OfxPropertySetHandle>(&inArgs), nullptr);
            if (!statusOk(st)) {
                // Soft: some plugs may still process via emulation
                if (errorOut) {
                    *errorOut = QStringLiteral(
                                    "OFX DescribeInContext failed (status %1) for \"%2\" — "
                                    "load accepted for entry points only")
                                    .arg(st)
                                    .arg(path);
                }
                // Still consider load success if Load+Describe worked
            } else {
                mod->descriptorClips = ctxFx.clips;
                mod->descriptorParams = ctxFx.params;
                mod->describedInContext = true;
            }

            modules[path] = mod;
            if (errorOut && errorOut->contains(QStringLiteral("DescribeInContext"))) {
                // Keep warning but load succeeded
            } else if (errorOut) {
                errorOut->clear();
            }
            return mod;
        } catch (const std::exception &ex) {
            if (errorOut) {
                *errorOut = QStringLiteral("OFX load exception: %1").arg(QString::fromUtf8(ex.what()));
            }
            return {};
        } catch (...) {
            if (errorOut) {
                *errorOut = QStringLiteral("OFX load crashed or threw unknown exception");
            }
            return {};
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
    return {
        QStringLiteral("Win64"),
        QStringLiteral("Win32"),
        QStringLiteral("MacOS"),
        QStringLiteral("Linux-x86-64"),
        QStringLiteral("Linux-x86"),
    };
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
        setString(&fx->props, kOfxPropType, 0, kOfxTypeImageEffectInstance);
        for (auto &ckv : fx->clips) {
            setInt(&ckv.second.props, kOfxImageClipPropConnected, 0, 1);
            setString(&ckv.second.props, kOfxImageEffectPropComponents, 0, kOfxImageComponentRGBA);
            setString(&ckv.second.props, kOfxImageEffectPropPixelDepth, 0, kOfxBitDepthByte);
            setString(&ckv.second.props, kOfxImageEffectPropPreMultiplication, 0,
                      kOfxImageUnPreMultiplied);
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
            if (fit != fx->params.end() && fit->second.type == kOfxParamTypeDouble) {
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

        auto fillImageProps = [&](PropSet *ps) {
            ps->props.clear();
            setString(ps, kOfxPropType, 0, kOfxTypeImage);
            setPointer(ps, kOfxImagePropData, 0, rgba->bits());
            setInt(ps, kOfxImagePropRowBytes, 0, rgba->bytesPerLine());
            setInt(ps, kOfxImagePropBounds, 0, 0);
            setInt(ps, kOfxImagePropBounds, 1, 0);
            setInt(ps, kOfxImagePropBounds, 2, rgba->width());
            setInt(ps, kOfxImagePropBounds, 3, rgba->height());
            setInt(ps, kOfxImagePropRegionOfDefinition, 0, 0);
            setInt(ps, kOfxImagePropRegionOfDefinition, 1, 0);
            setInt(ps, kOfxImagePropRegionOfDefinition, 2, rgba->width());
            setInt(ps, kOfxImagePropRegionOfDefinition, 3, rgba->height());
            setString(ps, kOfxImageEffectPropComponents, 0, kOfxImageComponentRGBA);
            setString(ps, kOfxImageEffectPropPixelDepth, 0, kOfxBitDepthByte);
            setString(ps, kOfxImageEffectPropPreMultiplication, 0, kOfxImageUnPreMultiplied);
            setDouble(ps, kOfxImagePropPixelAspectRatio, 0, 1.0);
            setString(ps, kOfxImagePropField, 0, kOfxImageFieldNone);
        };

        fillImageProps(&fx->sourceImageProps);
        fillImageProps(&fx->outputImageProps);

        for (auto &ckv : fx->clips) {
            if (ckv.first == kOfxImageEffectSimpleSourceClipName
                || ckv.first == "Source") {
                ckv.second.activeImage = &fx->sourceImageProps;
            } else if (ckv.first == kOfxImageEffectOutputClipName
                       || ckv.first == "Output") {
                ckv.second.activeImage = &fx->outputImageProps;
            } else {
                ckv.second.activeImage = &fx->sourceImageProps;
            }
        }

        PropSet inArgs;
        setDouble(&inArgs, kOfxPropTime, 0, timeSec);
        setString(&inArgs, kOfxImageEffectPropFieldToRender, 0, kOfxImageFieldNone);
        setInt(&inArgs, kOfxImageEffectPropRenderWindow, 0, 0);
        setInt(&inArgs, kOfxImageEffectPropRenderWindow, 1, 0);
        setInt(&inArgs, kOfxImageEffectPropRenderWindow, 2, rgba->width());
        setInt(&inArgs, kOfxImageEffectPropRenderWindow, 3, rgba->height());
        setDouble(&inArgs, kOfxImageEffectPropRenderScale, 0, 1.0);
        setDouble(&inArgs, kOfxImageEffectPropRenderScale, 1, 1.0);

        OfxStatus st =
            fx->module->plugin->mainEntry(kOfxImageEffectActionRender,
                                          reinterpret_cast<OfxImageEffectHandle>(fx.get()),
                                          reinterpret_cast<OfxPropertySetHandle>(&inArgs),
                                          nullptr);

        for (auto &ckv : fx->clips) {
            ckv.second.activeImage = nullptr;
        }
        fx->frameImage = nullptr;

        if (!statusOk(st)) {
            // Fallback: if plugin has gain param, apply emulated gain
            double gain = 1.5;
            auto fit = fx->params.find("gain");
            if (fit != fx->params.end()) {
                gain = fit->second.value;
            } else if (params.contains(QStringLiteral("gain"))) {
                gain = params.value(QStringLiteral("gain")).toDouble();
            }
            QImage argb = rgba->convertToFormat(QImage::Format_ARGB32);
            applyGain(&argb, gain);
            *rgba = argb;
            if (errorOut) {
                *errorOut = QStringLiteral(
                    "OFX Render failed (status %1) — applied host-side gain fallback")
                                .arg(st);
            }
            return true;
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

bool OfxHost::processEmulated(QImage *rgba, const QString &displayName, const QVariantMap &params)
{
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
    return false;
}

bool OfxHost::processSlot(FxSlot &slot, QImage *rgba, double timeSec)
{
    if (!rgba || rgba->isNull() || slot.bypass) {
        return false;
    }

    const QVariantMap params = loadSlotParams(slot);
    QString path = ofxPathFromPluginId(slot.pluginId);
    if (path.isEmpty() && QFileInfo::exists(slot.pluginId)
        && slot.pluginId.endsWith(QStringLiteral(".ofx"), Qt::CaseInsensitive)) {
        path = slot.pluginId;
    }

    if (!path.isEmpty()) {
        const QString key = path + QLatin1Char('|') + slot.pluginId;
        QMutexLocker lock(&m_->mutex);
        int id = m_->instanceKeyToId.value(key, 0);
        lock.unlock();

        if (id == 0) {
            OfxPluginDesc desc = describe(path);
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

    return processEmulated(rgba, slot.displayName, params);
}

} // namespace openvegas
