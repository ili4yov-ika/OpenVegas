#include "audio/Vst3Host.h"
#include "audio/BuiltinDsp.h"

#include <QByteArray>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QHash>
#include <QMessageBox>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QTimer>
#include <QWidget>
#include <QWindow>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// VeSTige VST2 ABI (LMMS aeffectx.h) — GPL-compatible substitute for Steinberg SDK
#include "aeffectx.h"

#ifdef OPENVGAS_HAS_VST3_SDK
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/vstspeaker.h"
#endif

namespace openvegas {
namespace {

void passThrough(float **in, float **out, int channels, int frames)
{
    if (!in || !out) {
        return;
    }
    for (int c = 0; c < channels; ++c) {
        if (in[c] && out[c] && in[c] != out[c]) {
            for (int i = 0; i < frames; ++i) {
                out[c][i] = in[c][i];
            }
        }
    }
}

QString pathFromPluginId(const QString &pluginId)
{
    const int colon = pluginId.indexOf(QLatin1Char(':'));
    if (colon >= 0 && colon + 1 < pluginId.size()) {
        return pluginId.mid(colon + 1);
    }
    return pluginId;
}

struct HostUserData {
    double sampleRate = 48000.0;
    int blockSize = 512;
    QPointer<QWidget> editorHost;
};

QHash<AEffect *, HostUserData *> &vstHostDataMap()
{
    static QHash<AEffect *, HostUserData *> map;
    return map;
}

#ifdef _WIN32
void fillVstEditorToParent(HWND parentHwnd)
{
    if (!parentHwnd) {
        return;
    }
    RECT rc{};
    if (!GetClientRect(parentHwnd, &rc)) {
        return;
    }
    const int w = std::max(1, int(rc.right - rc.left));
    const int h = std::max(1, int(rc.bottom - rc.top));
    HWND child = GetWindow(parentHwnd, GW_CHILD);
    if (child) {
        SetWindowPos(child, nullptr, 0, 0, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}
#endif

/** Resize native plug-in UI when the Qt host widget changes size. */
class PluginEditorResizeFilter : public QObject {
public:
    enum class Mode { FillChildHwnd, Vst3OnSize };

    using Vst3ResizeFn = std::function<void(int, int)>;

    PluginEditorResizeFilter(QWidget *host, Mode mode, Vst3ResizeFn vst3Fn = {})
        : QObject(host)
        , m_host(host)
        , m_mode(mode)
        , m_vst3Fn(std::move(vst3Fn))
    {
        if (m_host) {
            m_host->installEventFilter(this);
        }
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_host && event && event->type() == QEvent::Resize) {
            apply(m_host->width(), m_host->height());
        }
        return QObject::eventFilter(watched, event);
    }

    void apply(int w, int h)
    {
        if (w < 1 || h < 1) {
            return;
        }
        if (m_mode == Mode::Vst3OnSize) {
            if (m_vst3Fn) {
                m_vst3Fn(w, h);
            }
            return;
        }
#ifdef _WIN32
        if (m_host) {
            fillVstEditorToParent(reinterpret_cast<HWND>(m_host->winId()));
        }
#else
        Q_UNUSED(w);
        Q_UNUSED(h);
#endif
    }

private:
    QPointer<QWidget> m_host;
    Mode m_mode = Mode::FillChildHwnd;
    Vst3ResizeFn m_vst3Fn;
};

intptr_t VST_CALL_CONV hostCallback(AEffect *effect, int32_t opcode, int32_t index, intptr_t value,
                                    void *ptr, float opt)
{
    Q_UNUSED(opt);
    HostUserData *ud = effect ? vstHostDataMap().value(effect, nullptr) : nullptr;
    switch (opcode) {
    case audioMasterVersion:
        return 2400;
    case audioMasterGetSampleRate:
        return intptr_t(ud ? ud->sampleRate : 48000);
    case audioMasterGetBlockSize:
        return intptr_t(ud ? ud->blockSize : 512);
    case audioMasterGetCurrentProcessLevel:
        return 2; // realtime
    case audioMasterWantMidi:
        return 0;
    case audioMasterSizeWindow:
        if (ud && ud->editorHost) {
            const int w = int(index);
            const int h = int(value);
            QWidget *host = ud->editorHost.data();
            QMetaObject::invokeMethod(
                host,
                [host, w, h]() {
                    if (!host) {
                        return;
                    }
                    host->setMinimumSize(std::min(host->minimumWidth(), w),
                                         std::min(host->minimumHeight(), h));
                    host->resize(std::max(1, w), std::max(1, h));
#ifdef _WIN32
                    fillVstEditorToParent(reinterpret_cast<HWND>(host->winId()));
#endif
                },
                Qt::QueuedConnection);
            return 1;
        }
        return 0;
    case audioMasterGetVendorString:
        if (ptr) {
            std::snprintf(static_cast<char *>(ptr), 64, "OpenVegas");
        }
        return 1;
    case audioMasterGetProductString:
        if (ptr) {
            std::snprintf(static_cast<char *>(ptr), 64, "OpenVegas");
        }
        return 1;
    case audioMasterGetVendorVersion:
        return 1000;
    case audioMasterCanDo:
        if (ptr) {
            const char *s = static_cast<const char *>(ptr);
            if (std::strcmp(s, "supplyIdle") == 0 || std::strcmp(s, "sendVstEvents") == 0
                || std::strcmp(s, "sendVstMidiEvent") == 0 || std::strcmp(s, "receiveVstEvents") == 0
                || std::strcmp(s, "receiveVstMidiEvent") == 0 || std::strcmp(s, "sizeWindow") == 0) {
                return 1;
            }
        }
        return 0;
    default:
        return 0;
    }
}

struct Vst2Instance {
    QString path;
#ifdef _WIN32
    HMODULE module = nullptr;
#else
    void *module = nullptr;
#endif
    AEffect *effect = nullptr;
    HostUserData hostData;
    std::vector<float *> inPtrs;
    std::vector<float *> outPtrs;
    std::vector<float> silentIn;
    bool opened = false;
};

QHash<QString, std::shared_ptr<Vst2Instance>> g_vst2;

QString vst2InstanceKey(const FxSlot *slot)
{
    if (!slot) {
        return {};
    }
    if (!slot->hostKey.isEmpty()) {
        return slot->hostKey;
    }
    return slot->pluginId + QLatin1Char('\n') + slot->displayName;
}

std::shared_ptr<Vst2Instance> vst2Lookup(const FxSlot *slot)
{
    const QString key = vst2InstanceKey(slot);
    if (key.isEmpty()) {
        return {};
    }
    return g_vst2.value(key);
}

#ifdef OPENVGAS_HAS_VST3_SDK

using Steinberg::FUnknown;
using Steinberg::FIDString;
using Steinberg::IBStream;
using Steinberg::IPlugFrame;
using Steinberg::IPlugView;
using Steinberg::IPluginFactory;
using Steinberg::PClassInfo;
using Steinberg::TUID;
using Steinberg::ViewRect;
using Steinberg::int16;
using Steinberg::int32;
using Steinberg::int64;
using Steinberg::kPlatformTypeHWND;
using Steinberg::kResultFalse;
using Steinberg::kResultOk;
using Steinberg::kResultTrue;
using Steinberg::tresult;
using Steinberg::uint32;
using Steinberg::Vst::AudioBusBuffers;
using Steinberg::Vst::IAudioProcessor;
using Steinberg::Vst::IComponent;
using Steinberg::Vst::IComponentHandler;
using Steinberg::Vst::IConnectionPoint;
using Steinberg::Vst::IEditController;
using Steinberg::Vst::IHostApplication;
using Steinberg::Vst::IParamValueQueue;
using Steinberg::Vst::IParameterChanges;
using Steinberg::Vst::ParamID;
using Steinberg::Vst::ParamValue;
using Steinberg::Vst::ParameterInfo;
using Steinberg::Vst::ProcessData;
using Steinberg::Vst::ProcessSetup;
using Steinberg::Vst::SpeakerArrangement;
using Steinberg::Vst::kAudio;
using Steinberg::Vst::kInput;
using Steinberg::Vst::kOutput;
using Steinberg::Vst::kRealtime;
using Steinberg::Vst::kSample32;
using Steinberg::Vst::SpeakerArr::kStereo;

/** Minimal IBStream backed by QByteArray / growing buffer. */
class MemoryIBStream : public IBStream {
public:
    MemoryIBStream() { FUNKNOWN_CTOR }
    explicit MemoryIBStream(const QByteArray &data)
        : m_data(data)
    {
        FUNKNOWN_CTOR
    }
    ~MemoryIBStream() { FUNKNOWN_DTOR }

    DECLARE_FUNKNOWN_METHODS

    tresult PLUGIN_API read(void *buffer, int32 numBytes, int32 *numBytesRead) override
    {
        if (!buffer || numBytes < 0) {
            return Steinberg::kInvalidArgument;
        }
        const int32 avail = int32(m_data.size()) - int32(m_pos);
        const int32 n = std::min(numBytes, std::max(int32(0), avail));
        if (n > 0) {
            std::memcpy(buffer, m_data.constData() + m_pos, size_t(n));
            m_pos += n;
        }
        if (numBytesRead) {
            *numBytesRead = n;
        }
        return kResultOk;
    }

    tresult PLUGIN_API write(void *buffer, int32 numBytes, int32 *numBytesWritten) override
    {
        if (!buffer || numBytes < 0) {
            return Steinberg::kInvalidArgument;
        }
        if (m_pos + numBytes > m_data.size()) {
            m_data.resize(int(m_pos + numBytes));
        }
        std::memcpy(m_data.data() + m_pos, buffer, size_t(numBytes));
        m_pos += numBytes;
        if (numBytesWritten) {
            *numBytesWritten = numBytes;
        }
        return kResultOk;
    }

    tresult PLUGIN_API seek(int64 pos, int32 mode, int64 *result) override
    {
        int64 next = m_pos;
        switch (mode) {
        case kIBSeekSet:
            next = pos;
            break;
        case kIBSeekCur:
            next = m_pos + pos;
            break;
        case kIBSeekEnd:
            next = int64(m_data.size()) + pos;
            break;
        default:
            return Steinberg::kInvalidArgument;
        }
        if (next < 0) {
            return Steinberg::kInvalidArgument;
        }
        m_pos = next;
        if (result) {
            *result = m_pos;
        }
        return kResultOk;
    }

    tresult PLUGIN_API tell(int64 *pos) override
    {
        if (!pos) {
            return Steinberg::kInvalidArgument;
        }
        *pos = m_pos;
        return kResultOk;
    }

    QByteArray data() const { return m_data.left(int(std::max<int64>(m_pos, m_data.size()))); }
    QByteArray bytes() const { return m_data; }

private:
    QByteArray m_data;
    int64 m_pos = 0;
};

tresult PLUGIN_API MemoryIBStream::queryInterface(const Steinberg::TUID _iid, void **obj)
{
    QUERY_INTERFACE(_iid, obj, FUnknown::iid, IBStream)
    QUERY_INTERFACE(_iid, obj, IBStream::iid, IBStream)
    *obj = nullptr;
    return Steinberg::kNoInterface;
}
uint32 PLUGIN_API MemoryIBStream::addRef()
{
    return Steinberg::FUnknownPrivate::atomicAdd(__funknownRefCount, 1);
}
uint32 PLUGIN_API MemoryIBStream::release()
{
    // Stack / host-owned — never delete via COM release.
    return Steinberg::FUnknownPrivate::atomicAdd(__funknownRefCount, -1);
}

/** Single-point parameter queue for UI → process transfer. */
class HostParamQueue : public IParamValueQueue {
public:
    HostParamQueue() { FUNKNOWN_CTOR }
    ~HostParamQueue() { FUNKNOWN_DTOR }
    DECLARE_FUNKNOWN_METHODS

    ParamID id = 0;
    ParamValue value = 0;

    ParamID PLUGIN_API getParameterId() override { return id; }
    int32 PLUGIN_API getPointCount() override { return 1; }
    tresult PLUGIN_API getPoint(int32 index, int32 &sampleOffset, ParamValue &v) override
    {
        if (index != 0) {
            return Steinberg::kInvalidArgument;
        }
        sampleOffset = 0;
        v = value;
        return kResultOk;
    }
    tresult PLUGIN_API addPoint(int32 /*sampleOffset*/, ParamValue v, int32 &index) override
    {
        value = v;
        index = 0;
        return kResultOk;
    }
};

tresult PLUGIN_API HostParamQueue::queryInterface(const Steinberg::TUID _iid, void **obj)
{
    QUERY_INTERFACE(_iid, obj, FUnknown::iid, IParamValueQueue)
    QUERY_INTERFACE(_iid, obj, IParamValueQueue::iid, IParamValueQueue)
    *obj = nullptr;
    return Steinberg::kNoInterface;
}
uint32 PLUGIN_API HostParamQueue::addRef()
{
    return Steinberg::FUnknownPrivate::atomicAdd(__funknownRefCount, 1);
}
uint32 PLUGIN_API HostParamQueue::release()
{
    return Steinberg::FUnknownPrivate::atomicAdd(__funknownRefCount, -1);
}

class HostParameterChanges : public IParameterChanges {
public:
    HostParameterChanges() { FUNKNOWN_CTOR }
    ~HostParameterChanges() { FUNKNOWN_DTOR }
    DECLARE_FUNKNOWN_METHODS

    std::vector<std::unique_ptr<HostParamQueue>> queues;

    void clear() { queues.clear(); }
    void setFromMap(const QHash<ParamID, ParamValue> &m)
    {
        queues.clear();
        queues.reserve(size_t(m.size()));
        for (auto it = m.constBegin(); it != m.constEnd(); ++it) {
            auto q = std::make_unique<HostParamQueue>();
            q->id = it.key();
            q->value = it.value();
            queues.push_back(std::move(q));
        }
    }

    int32 PLUGIN_API getParameterCount() override { return int32(queues.size()); }
    IParamValueQueue *PLUGIN_API getParameterData(int32 index) override
    {
        if (index < 0 || index >= int32(queues.size())) {
            return nullptr;
        }
        return queues[size_t(index)].get();
    }
    IParamValueQueue *PLUGIN_API addParameterData(const ParamID &id, int32 &index) override
    {
        auto q = std::make_unique<HostParamQueue>();
        q->id = id;
        queues.push_back(std::move(q));
        index = int32(queues.size()) - 1;
        return queues.back().get();
    }
};

tresult PLUGIN_API HostParameterChanges::queryInterface(const Steinberg::TUID _iid, void **obj)
{
    QUERY_INTERFACE(_iid, obj, FUnknown::iid, IParameterChanges)
    QUERY_INTERFACE(_iid, obj, IParameterChanges::iid, IParameterChanges)
    *obj = nullptr;
    return Steinberg::kNoInterface;
}
uint32 PLUGIN_API HostParameterChanges::addRef()
{
    return Steinberg::FUnknownPrivate::atomicAdd(__funknownRefCount, 1);
}
uint32 PLUGIN_API HostParameterChanges::release()
{
    return Steinberg::FUnknownPrivate::atomicAdd(__funknownRefCount, -1);
}

struct Vst3Runtime;

class OpenVegasComponentHandler : public IComponentHandler {
public:
    explicit OpenVegasComponentHandler(Vst3Runtime *rt)
        : m_rt(rt)
    {
        FUNKNOWN_CTOR
    }
    ~OpenVegasComponentHandler() { FUNKNOWN_DTOR }
    DECLARE_FUNKNOWN_METHODS

    tresult PLUGIN_API beginEdit(ParamID /*id*/) override { return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID id, ParamValue valueNormalized) override;
    tresult PLUGIN_API endEdit(ParamID /*id*/) override { return kResultOk; }
    tresult PLUGIN_API restartComponent(int32 /*flags*/) override { return kResultOk; }

    Vst3Runtime *m_rt = nullptr;
};

class OpenVegasPlugFrame : public IPlugFrame {
public:
    OpenVegasPlugFrame() { FUNKNOWN_CTOR }
    ~OpenVegasPlugFrame() { FUNKNOWN_DTOR }
    DECLARE_FUNKNOWN_METHODS

    QPointer<QWidget> hostWidget;
    IPlugView *view = nullptr;

    tresult PLUGIN_API resizeView(IPlugView *v, ViewRect *newSize) override
    {
        if (!v || !newSize || !hostWidget) {
            return Steinberg::kInvalidArgument;
        }
        const int w = std::max(1, int(newSize->getWidth()));
        const int h = std::max(1, int(newSize->getHeight()));
        hostWidget->setMinimumSize(w, h);
        hostWidget->resize(w, h);
        return v->onSize(newSize);
    }
};

tresult PLUGIN_API OpenVegasPlugFrame::queryInterface(const Steinberg::TUID _iid, void **obj)
{
    QUERY_INTERFACE(_iid, obj, FUnknown::iid, IPlugFrame)
    QUERY_INTERFACE(_iid, obj, IPlugFrame::iid, IPlugFrame)
    *obj = nullptr;
    return Steinberg::kNoInterface;
}
uint32 PLUGIN_API OpenVegasPlugFrame::addRef()
{
    return Steinberg::FUnknownPrivate::atomicAdd(__funknownRefCount, 1);
}
uint32 PLUGIN_API OpenVegasPlugFrame::release()
{
    return Steinberg::FUnknownPrivate::atomicAdd(__funknownRefCount, -1);
}

tresult PLUGIN_API OpenVegasComponentHandler::queryInterface(const Steinberg::TUID _iid, void **obj)
{
    QUERY_INTERFACE(_iid, obj, FUnknown::iid, IComponentHandler)
    QUERY_INTERFACE(_iid, obj, IComponentHandler::iid, IComponentHandler)
    *obj = nullptr;
    return Steinberg::kNoInterface;
}
uint32 PLUGIN_API OpenVegasComponentHandler::addRef()
{
    return Steinberg::FUnknownPrivate::atomicAdd(__funknownRefCount, 1);
}
uint32 PLUGIN_API OpenVegasComponentHandler::release()
{
    return Steinberg::FUnknownPrivate::atomicAdd(__funknownRefCount, -1);
}

/** Minimal host context for IComponent::initialize (process-lifetime singleton). */
class OpenVegasHostApp : public IHostApplication {
public:
    OpenVegasHostApp() { FUNKNOWN_CTOR }
    ~OpenVegasHostApp() { FUNKNOWN_DTOR }

    DECLARE_FUNKNOWN_METHODS

    tresult PLUGIN_API getName(Steinberg::Vst::String128 name) override
    {
        const char16_t *src = u"OpenVegas";
        int i = 0;
        for (; src[i] && i < 127; ++i) {
            name[i] = src[i];
        }
        name[i] = 0;
        return kResultOk;
    }

    tresult PLUGIN_API createInstance(TUID /*cid*/, TUID /*_iid*/, void **obj) override
    {
        if (obj) {
            *obj = nullptr;
        }
        return Steinberg::kNotImplemented;
    }

    static OpenVegasHostApp &instance()
    {
        static OpenVegasHostApp app;
        return app;
    }
};

tresult PLUGIN_API OpenVegasHostApp::queryInterface(const Steinberg::TUID _iid, void **obj)
{
    QUERY_INTERFACE(_iid, obj, FUnknown::iid, IHostApplication)
    QUERY_INTERFACE(_iid, obj, IHostApplication::iid, IHostApplication)
    *obj = nullptr;
    return Steinberg::kNoInterface;
}
uint32 PLUGIN_API OpenVegasHostApp::addRef() { return 1; }
uint32 PLUGIN_API OpenVegasHostApp::release() { return 1; }

QString resolveVst3ModulePath(const QString &bundleOrDll)
{
    if (bundleOrDll.isEmpty()) {
        return {};
    }
    const QFileInfo fi(bundleOrDll);
    if (fi.isFile()) {
        return fi.absoluteFilePath();
    }
    // Bundle: Contents/<arch>/*.vst3 (Windows DLL packaged as .vst3)
    const QString contents = fi.absoluteFilePath() + QStringLiteral("/Contents");
#ifdef _WIN64
    const QStringList arches = {QStringLiteral("x86_64-win"), QStringLiteral("arm64-win")};
#else
    const QStringList arches = {QStringLiteral("x86-win"), QStringLiteral("x86_64-win")};
#endif
    for (const QString &arch : arches) {
        const QDir dir(contents + QLatin1Char('/') + arch);
        if (!dir.exists()) {
            continue;
        }
        const QStringList dlls =
            dir.entryList({QStringLiteral("*.vst3"), QStringLiteral("*.dll")}, QDir::Files);
        if (!dlls.isEmpty()) {
            return dir.absoluteFilePath(dlls.first());
        }
    }
    return {};
}

struct Vst3Runtime {
    QString path;
    QString modulePath;
#ifdef _WIN32
    HMODULE module = nullptr;
#else
    void *module = nullptr;
#endif
    IPluginFactory *factory = nullptr;
    IComponent *component = nullptr;
    IAudioProcessor *processor = nullptr;
    mutable IEditController *controller = nullptr;
    mutable bool controllerOwned = false;
    mutable bool handlerInstalled = false;
    std::unique_ptr<OpenVegasComponentHandler> componentHandler;
    std::unique_ptr<OpenVegasPlugFrame> plugFrame;
    IPlugView *plugView = nullptr;
    QPointer<QWidget> editorHost;
    QMutex pendingMutex;
    QHash<ParamID, ParamValue> pendingParams;
    HostParameterChanges processChanges;
    double sampleRate = 48000.0;
    int blockSize = 512;
    bool active = false;
    bool processing = false;
    int numInCh = 2;
    int numOutCh = 2;
    std::vector<float *> inPtrs;
    std::vector<float *> outPtrs;
    std::vector<float> silent;
    AudioBusBuffers inBus{};
    AudioBusBuffers outBus{};

    ~Vst3Runtime() { teardown(); }

    void disconnectController()
    {
        if (!component || !controller) {
            return;
        }
        IConnectionPoint *compCp = nullptr;
        IConnectionPoint *ctrlCp = nullptr;
        component->queryInterface(IConnectionPoint::iid, (void **)&compCp);
        controller->queryInterface(IConnectionPoint::iid, (void **)&ctrlCp);
        if (compCp && ctrlCp) {
            compCp->disconnect(ctrlCp);
            ctrlCp->disconnect(compCp);
        }
        if (compCp) {
            compCp->release();
        }
        if (ctrlCp) {
            ctrlCp->release();
        }
    }

    void closePlugView()
    {
        if (plugView) {
            plugView->setFrame(nullptr);
            plugView->removed();
            plugView->release();
            plugView = nullptr;
        }
        if (plugFrame) {
            plugFrame->view = nullptr;
            plugFrame->hostWidget.clear();
        }
        editorHost.clear();
    }

    void teardown()
    {
        closePlugView();
        if (processor && processing) {
            processor->setProcessing(false);
            processing = false;
        }
        if (component && active) {
            component->setActive(false);
            active = false;
        }
        disconnectController();
        if (controller) {
            if (handlerInstalled) {
                controller->setComponentHandler(nullptr);
                handlerInstalled = false;
            }
            if (controllerOwned) {
                controller->terminate();
            }
            controller->release();
            controller = nullptr;
            controllerOwned = false;
        }
        componentHandler.reset();
        plugFrame.reset();
        if (processor) {
            processor->release();
            processor = nullptr;
        }
        if (component) {
            component->terminate();
            component->release();
            component = nullptr;
        }
        factory = nullptr;
#ifdef _WIN32
        if (module) {
            using ExitDllFn = bool(PLUGIN_API *)();
            if (auto exitDll = reinterpret_cast<ExitDllFn>(GetProcAddress(module, "ExitDll"))) {
                exitDll();
            }
            FreeLibrary(module);
            module = nullptr;
        }
#endif
    }

    bool connectController()
    {
        if (!component || !controller) {
            return false;
        }
        IConnectionPoint *compCp = nullptr;
        IConnectionPoint *ctrlCp = nullptr;
        component->queryInterface(IConnectionPoint::iid, (void **)&compCp);
        controller->queryInterface(IConnectionPoint::iid, (void **)&ctrlCp);
        if (compCp && ctrlCp) {
            compCp->connect(ctrlCp);
            ctrlCp->connect(compCp);
        }
        if (compCp) {
            compCp->release();
        }
        if (ctrlCp) {
            ctrlCp->release();
        }
        return true;
    }

    bool ensureController() const
    {
        if (controller) {
            return true;
        }
        if (!component || !factory) {
            return false;
        }
        // Same-class component+controller.
        auto *self = const_cast<Vst3Runtime *>(this);
        if (self->component->queryInterface(IEditController::iid, (void **)&self->controller)
                == kResultOk
            && self->controller) {
            self->controllerOwned = false;
            self->connectController();
            self->installComponentHandler();
            self->syncControllerFromComponent();
            return true;
        }
        self->controller = nullptr;
        TUID cid{};
        if (self->component->getControllerClassId(cid) != kResultOk) {
            return false;
        }
        void *obj = nullptr;
        if (self->factory->createInstance(cid, IEditController::iid, &obj) != kResultOk || !obj) {
            return false;
        }
        self->controller = static_cast<IEditController *>(obj);
        self->controllerOwned = true;
        if (self->controller->initialize(&OpenVegasHostApp::instance()) != kResultOk) {
            self->controller->release();
            self->controller = nullptr;
            self->controllerOwned = false;
            return false;
        }
        self->connectController();
        self->installComponentHandler();
        self->syncControllerFromComponent();
        return true;
    }

    void installComponentHandler()
    {
        if (!controller || handlerInstalled) {
            return;
        }
        if (!componentHandler) {
            componentHandler = std::make_unique<OpenVegasComponentHandler>(this);
        }
        if (controller->setComponentHandler(componentHandler.get()) == kResultOk) {
            handlerInstalled = true;
        }
    }

    void syncControllerFromComponent()
    {
        if (!component || !controller) {
            return;
        }
        MemoryIBStream stream;
        if (component->getState(&stream) != kResultOk) {
            return;
        }
        MemoryIBStream forCtrl(stream.bytes());
        controller->setComponentState(&forCtrl);
    }

    bool openPlugView(QWidget *parent)
    {
        if (!parent || !ensureController() || !controller) {
            return false;
        }
        installComponentHandler();
        syncControllerFromComponent();
        closePlugView();

        IPlugView *view = controller->createView(Steinberg::Vst::ViewType::kEditor);
        if (!view) {
            return false;
        }
#ifdef _WIN32
        if (view->isPlatformTypeSupported(kPlatformTypeHWND) != kResultTrue) {
            view->release();
            return false;
        }
#else
        view->release();
        return false;
#endif
        if (!plugFrame) {
            plugFrame = std::make_unique<OpenVegasPlugFrame>();
        }
        plugFrame->hostWidget = parent;
        plugFrame->view = view;
        view->setFrame(plugFrame.get());

        ViewRect vr{};
        int prefW = 400;
        int prefH = 280;
        if (view->getSize(&vr) == kResultOk) {
            prefW = std::max(100, int(vr.getWidth()));
            prefH = std::max(80, int(vr.getHeight()));
        }

        parent->setAttribute(Qt::WA_NativeWindow, true);
        parent->winId();
#ifdef _WIN32
        const HWND hwnd = reinterpret_cast<HWND>(parent->winId());
        if (view->attached(hwnd, kPlatformTypeHWND) != kResultOk) {
            view->setFrame(nullptr);
            view->release();
            plugFrame->view = nullptr;
            return false;
        }
#endif
        plugView = view;
        editorHost = parent;

        const bool canResize = (view->canResize() == kResultTrue);
        if (canResize) {
            parent->setMinimumSize(std::min(prefW, 320), std::min(prefH, 240));
            parent->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
            parent->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            auto *filter = new PluginEditorResizeFilter(
                parent, PluginEditorResizeFilter::Mode::Vst3OnSize,
                [this](int w, int h) {
                    if (!plugView) {
                        return;
                    }
                    ViewRect r{0, 0, w, h};
                    plugView->checkSizeConstraint(&r);
                    plugView->onSize(&r);
                });
            // Stretch to current host size once layout settles.
            QTimer::singleShot(0, parent, [parent, filter]() {
                if (parent && filter) {
                    filter->apply(parent->width(), parent->height());
                }
            });
        } else {
            parent->setFixedSize(prefW, prefH);
        }

        QObject::connect(parent, &QObject::destroyed, parent, [this]() {
            closePlugView();
        });
        return true;
    }

    bool loadAndCreate()
    {
        modulePath = resolveVst3ModulePath(path);
        if (modulePath.isEmpty() || !QFileInfo::exists(modulePath)) {
            return false;
        }
#ifdef _WIN32
        module = LoadLibraryW(reinterpret_cast<LPCWSTR>(modulePath.utf16()));
        if (!module) {
            return false;
        }
        using InitDllFn = bool(PLUGIN_API *)();
        using GetFactoryFn = IPluginFactory *(PLUGIN_API *)();
        if (auto initDll = reinterpret_cast<InitDllFn>(GetProcAddress(module, "InitDll"))) {
            if (!initDll()) {
                FreeLibrary(module);
                module = nullptr;
                return false;
            }
        }
        auto getFactory = reinterpret_cast<GetFactoryFn>(GetProcAddress(module, "GetPluginFactory"));
        if (!getFactory) {
            FreeLibrary(module);
            module = nullptr;
            return false;
        }
        factory = getFactory();
        if (!factory) {
            FreeLibrary(module);
            module = nullptr;
            return false;
        }

        const int32 count = factory->countClasses();
        TUID classId{};
        bool found = false;
        for (int32 i = 0; i < count; ++i) {
            PClassInfo ci{};
            if (factory->getClassInfo(i, &ci) != kResultOk) {
                continue;
            }
            if (std::strcmp(ci.category, kVstAudioEffectClass) != 0) {
                continue;
            }
            std::memcpy(classId, ci.cid, sizeof(TUID));
            found = true;
            break;
        }
        if (!found) {
            return false;
        }

        void *obj = nullptr;
        if (factory->createInstance(classId, IComponent::iid, &obj) != kResultOk || !obj) {
            return false;
        }
        component = static_cast<IComponent *>(obj);
        if (component->initialize(&OpenVegasHostApp::instance()) != kResultOk) {
            component->release();
            component = nullptr;
            return false;
        }
        if (component->queryInterface(IAudioProcessor::iid, (void **)&processor) != kResultOk
            || !processor) {
            component->terminate();
            component->release();
            component = nullptr;
            return false;
        }
        ensureController();
        return true;
#else
        Q_UNUSED(path);
        return false;
#endif
    }

    bool activateBusesStereo()
    {
        if (!component || !processor) {
            return false;
        }
        SpeakerArrangement inArrs[8] = {};
        SpeakerArrangement outArrs[8] = {};
        const int32 nIn = component->getBusCount(kAudio, kInput);
        const int32 nOut = component->getBusCount(kAudio, kOutput);
        for (int32 i = 0; i < nIn && i < 8; ++i) {
            inArrs[i] = kStereo;
            component->activateBus(kAudio, kInput, i, true);
        }
        for (int32 i = 0; i < nOut && i < 8; ++i) {
            outArrs[i] = kStereo;
            component->activateBus(kAudio, kOutput, i, true);
        }
        // Best-effort stereo; plugins may renegotiate.
        if (nIn > 0 || nOut > 0) {
            processor->setBusArrangements(nIn > 0 ? inArrs : nullptr, nIn,
                                          nOut > 0 ? outArrs : nullptr, nOut);
        }
        SpeakerArrangement gotIn = kStereo;
        SpeakerArrangement gotOut = kStereo;
        if (nIn > 0) {
            processor->getBusArrangement(kInput, 0, gotIn);
        }
        if (nOut > 0) {
            processor->getBusArrangement(kOutput, 0, gotOut);
        }
        numInCh = std::max(1, int(Steinberg::Vst::SpeakerArr::getChannelCount(gotIn)));
        numOutCh = std::max(1, int(Steinberg::Vst::SpeakerArr::getChannelCount(gotOut)));
        return true;
    }

    bool setup(double sr, int block)
    {
        if (!processor || !component) {
            return false;
        }
        sampleRate = sr;
        blockSize = block;
        if (processing) {
            processor->setProcessing(false);
            processing = false;
        }
        if (active) {
            component->setActive(false);
            active = false;
        }
        activateBusesStereo();
        ProcessSetup setup{};
        setup.processMode = kRealtime;
        setup.symbolicSampleSize = kSample32;
        setup.maxSamplesPerBlock = blockSize;
        setup.sampleRate = sampleRate;
        if (processor->setupProcessing(setup) != kResultOk) {
            return false;
        }
        if (component->setActive(true) != kResultOk) {
            return false;
        }
        active = true;
        if (processor->setProcessing(true) != kResultOk) {
            return false;
        }
        processing = true;
        inPtrs.assign(size_t(std::max(2, numInCh)), nullptr);
        outPtrs.assign(size_t(std::max(2, numOutCh)), nullptr);
        silent.assign(size_t(blockSize), 0.f);
        return true;
    }
};

tresult PLUGIN_API OpenVegasComponentHandler::performEdit(ParamID id, ParamValue valueNormalized)
{
    if (!m_rt) {
        return Steinberg::kInvalidArgument;
    }
    if (m_rt->controller) {
        m_rt->controller->setParamNormalized(id, valueNormalized);
    }
    {
        QMutexLocker lock(&m_rt->pendingMutex);
        m_rt->pendingParams.insert(id, valueNormalized);
    }
    return kResultOk;
}

#endif // OPENVGAS_HAS_VST3_SDK

} // namespace

struct Vst3Host::Instance {
    QString path;
    double sampleRate = 48000.0;
    int blockSize = 512;
    bool loaded = false;
#ifdef OPENVGAS_HAS_VST3_SDK
    std::shared_ptr<Vst3Runtime> rt;
#endif
};

Vst3Host &Vst3Host::instance()
{
    static Vst3Host h;
    return h;
}

bool Vst3Host::hasSdk()
{
#ifdef OPENVGAS_HAS_VST3_SDK
    return true;
#else
    return false;
#endif
}

QString Vst3Host::instanceKey(const FxSlot *slot)
{
    if (!slot) {
        return {};
    }
    if (!slot->hostKey.isEmpty()) {
        return slot->hostKey;
    }
    return slot->pluginId + QLatin1Char('\n') + slot->displayName;
}

bool Vst3Host::createInstance(const AudioPluginDesc &desc, FxSlot *slot)
{
    if (!slot) {
        return false;
    }
    ensureFxHostKey(slot);
    const QString key = instanceKey(slot);
    const QByteArray prevState = slot->state;
    const bool prevBypass = slot->bypass;
    const QString prevName = slot->displayName;
    const QString prevKey = slot->hostKey;

    if (auto existing = m_instances.value(key)) {
        if (existing->loaded || !desc.path.isEmpty()) {
            *slot = fxSlotFromDesc(desc);
            slot->hostKey = prevKey;
            slot->state = prevState;
            slot->bypass = prevBypass;
            if (!prevName.isEmpty()) {
                slot->displayName = prevName;
            }
            if (!desc.path.isEmpty()) {
                slot->pluginId = QStringLiteral("vst3:") + desc.path;
            }
            if (existing->loaded) {
                return true;
            }
        }
    }

    *slot = fxSlotFromDesc(desc);
    slot->hostKey = prevKey;
    slot->state = prevState;
    slot->bypass = prevBypass;
    if (!prevName.isEmpty()) {
        slot->displayName = prevName;
    }
    if (!desc.path.isEmpty()) {
        slot->pluginId = QStringLiteral("vst3:") + desc.path;
    } else if (slot->pluginId.isEmpty()) {
        slot->pluginId = QStringLiteral("vst3:") + slot->displayName;
    }
    auto inst = std::make_shared<Instance>();
    inst->path = desc.path.isEmpty() ? pathFromPluginId(slot->pluginId) : desc.path;
    inst->loaded = false;
#ifdef OPENVGAS_HAS_VST3_SDK
    auto rt = std::make_shared<Vst3Runtime>();
    rt->path = inst->path;
    if (rt->loadAndCreate()) {
        inst->rt = rt;
        inst->loaded = true;
        QByteArray blob = fxStateChunk(*slot);
        if (blob.isEmpty()) {
            blob = slot->state;
        }
        if (!blob.isEmpty()) {
            MemoryIBStream stream(blob);
            rt->component->setState(&stream);
        }
    }
#endif
    m_instances.insert(key, inst);
    return inst->loaded || !inst->path.isEmpty();
}

void Vst3Host::releaseInstance(FxSlot *slot)
{
#ifdef OPENVGAS_HAS_VST3_SDK
    auto it = m_instances.find(instanceKey(slot));
    if (it != m_instances.end() && it.value() && it.value()->rt) {
        it.value()->rt->teardown();
        it.value()->rt.reset();
    }
#endif
    m_instances.remove(instanceKey(slot));
    if (slot) {
        slot->state.clear();
    }
}

void Vst3Host::prepare(FxSlot *slot, double sampleRate, int blockSize)
{
    auto it = m_instances.find(instanceKey(slot));
    if (it == m_instances.end() || !it.value()) {
        return;
    }
    it.value()->sampleRate = sampleRate;
    it.value()->blockSize = blockSize;
#ifdef OPENVGAS_HAS_VST3_SDK
    if (it.value()->rt) {
        it.value()->rt->setup(sampleRate, blockSize);
    }
#endif
}

void Vst3Host::reset(FxSlot *slot)
{
#ifdef OPENVGAS_HAS_VST3_SDK
    auto it = m_instances.find(instanceKey(slot));
    if (it == m_instances.end() || !it.value() || !it.value()->rt || !it.value()->rt->processor) {
        return;
    }
    auto &rt = *it.value()->rt;
    if (rt.processing) {
        rt.processor->setProcessing(false);
        rt.processor->setProcessing(true);
    }
#else
    Q_UNUSED(slot);
#endif
}

void Vst3Host::process(FxSlot *slot, float **in, float **out, int channels, int frames)
{
    if (!slot || !in || !out || channels < 1 || frames <= 0) {
        return;
    }
    if (slot->bypass) {
        passThrough(in, out, channels, frames);
        return;
    }
#ifdef OPENVGAS_HAS_VST3_SDK
    auto it = m_instances.find(instanceKey(slot));
    if (it != m_instances.end() && it.value() && it.value()->rt && it.value()->rt->processor
        && it.value()->rt->processing) {
        auto &rt = *it.value()->rt;
        if (int(rt.silent.size()) < frames) {
            rt.silent.assign(size_t(frames), 0.f);
        }
        for (int c = 0; c < int(rt.inPtrs.size()); ++c) {
            if (c < channels && in[c]) {
                rt.inPtrs[size_t(c)] = in[c];
            } else {
                rt.inPtrs[size_t(c)] = rt.silent.data();
            }
        }
        // Process in-place into out; seed outs from ins when distinct.
        passThrough(in, out, channels, frames);
        for (int c = 0; c < int(rt.outPtrs.size()); ++c) {
            if (c < channels && out[c]) {
                rt.outPtrs[size_t(c)] = out[c];
            } else if (!rt.outPtrs.empty()) {
                rt.outPtrs[size_t(c)] = rt.outPtrs[0] ? rt.outPtrs[0] : rt.silent.data();
            }
        }
        rt.inBus.numChannels = int32(rt.inPtrs.size());
        rt.inBus.channelBuffers32 = rt.inPtrs.data();
        rt.inBus.silenceFlags = 0;
        rt.outBus.numChannels = int32(rt.outPtrs.size());
        rt.outBus.channelBuffers32 = rt.outPtrs.data();
        rt.outBus.silenceFlags = 0;

        ProcessData data{};
        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = frames;
        data.numInputs = 1;
        data.numOutputs = 1;
        data.inputs = &rt.inBus;
        data.outputs = &rt.outBus;
        {
            QHash<ParamID, ParamValue> snap;
            {
                QMutexLocker lock(&rt.pendingMutex);
                snap.swap(rt.pendingParams);
            }
            if (!snap.isEmpty()) {
                rt.processChanges.setFromMap(snap);
                data.inputParameterChanges = &rt.processChanges;
            } else {
                data.inputParameterChanges = nullptr;
            }
        }
        rt.processor->process(data);
        return;
    }
#endif
    passThrough(in, out, channels, frames);
}

bool Vst3Host::openEditor(FxSlot *slot, QWidget *parent)
{
    if (!slot || !parent) {
        return false;
    }
#ifdef OPENVGAS_HAS_VST3_SDK
    auto it = m_instances.find(instanceKey(slot));
    if (it == m_instances.end() || !it.value() || !it.value()->rt) {
        return false;
    }
    auto &rt = *it.value()->rt;
    if (!rt.component) {
        return false;
    }
    return rt.openPlugView(parent);
#else
    Q_UNUSED(parent);
    return false;
#endif
}

int Vst3Host::parameterCount(const FxSlot *slot) const
{
#ifdef OPENVGAS_HAS_VST3_SDK
    auto it = m_instances.find(instanceKey(slot));
    if (it == m_instances.end() || !it.value() || !it.value()->rt) {
        return 0;
    }
    auto &rt = *it.value()->rt;
    if (!rt.ensureController() || !rt.controller) {
        return 0;
    }
    return int(rt.controller->getParameterCount());
#else
    Q_UNUSED(slot);
    return 0;
#endif
}

bool Vst3Host::parameterInfo(const FxSlot *slot, int index, QString *name, float *min, float *max,
                             float *step) const
{
#ifdef OPENVGAS_HAS_VST3_SDK
    auto it = m_instances.find(instanceKey(slot));
    if (it == m_instances.end() || !it.value() || !it.value()->rt) {
        return false;
    }
    auto &rt = *it.value()->rt;
    if (!rt.ensureController() || !rt.controller) {
        return false;
    }
    if (index < 0 || index >= int(rt.controller->getParameterCount())) {
        return false;
    }
    ParameterInfo info{};
    if (rt.controller->getParameterInfo(index, info) != kResultOk) {
        return false;
    }
    if (name) {
        *name = QString::fromUtf16(reinterpret_cast<const char16_t *>(info.title));
        if (name->isEmpty()) {
            *name = QString::fromUtf16(reinterpret_cast<const char16_t *>(info.shortTitle));
        }
        if (name->isEmpty()) {
            *name = QStringLiteral("Param %1").arg(index);
        }
    }
    if (min) {
        *min = 0.f;
    }
    if (max) {
        *max = 1.f;
    }
    if (step) {
        *step = (info.stepCount > 0) ? (1.f / float(info.stepCount)) : 0.f;
    }
    return true;
#else
    Q_UNUSED(slot);
    Q_UNUSED(index);
    Q_UNUSED(name);
    Q_UNUSED(min);
    Q_UNUSED(max);
    Q_UNUSED(step);
    return false;
#endif
}

float Vst3Host::getParameter(const FxSlot *slot, int index) const
{
#ifdef OPENVGAS_HAS_VST3_SDK
    auto it = m_instances.find(instanceKey(slot));
    if (it == m_instances.end() || !it.value() || !it.value()->rt) {
        return 0.f;
    }
    auto &rt = *it.value()->rt;
    if (!rt.ensureController() || !rt.controller) {
        return 0.f;
    }
    if (index < 0 || index >= int(rt.controller->getParameterCount())) {
        return 0.f;
    }
    ParameterInfo info{};
    if (rt.controller->getParameterInfo(index, info) != kResultOk) {
        return 0.f;
    }
    return float(rt.controller->getParamNormalized(info.id));
#else
    Q_UNUSED(slot);
    Q_UNUSED(index);
    return 0.f;
#endif
}

void Vst3Host::setParameter(FxSlot *slot, int index, float value)
{
#ifdef OPENVGAS_HAS_VST3_SDK
    auto it = m_instances.find(instanceKey(slot));
    if (it == m_instances.end() || !it.value() || !it.value()->rt) {
        return;
    }
    auto &rt = *it.value()->rt;
    if (!rt.ensureController() || !rt.controller) {
        return;
    }
    if (index < 0 || index >= int(rt.controller->getParameterCount())) {
        return;
    }
    ParameterInfo info{};
    if (rt.controller->getParameterInfo(index, info) != kResultOk) {
        return;
    }
    const ParamValue v = std::clamp(ParamValue(value), ParamValue(0), ParamValue(1));
    rt.controller->setParamNormalized(info.id, v);
#else
    Q_UNUSED(slot);
    Q_UNUSED(index);
    Q_UNUSED(value);
#endif
}

QByteArray Vst3Host::getState(const FxSlot *slot) const
{
#ifdef OPENVGAS_HAS_VST3_SDK
    auto it = m_instances.find(instanceKey(slot));
    if (it != m_instances.end() && it.value() && it.value()->rt && it.value()->rt->component) {
        MemoryIBStream stream;
        if (it.value()->rt->component->getState(&stream) == kResultOk) {
            const QByteArray ba = stream.bytes();
            if (slot) {
                setFxStateChunk(const_cast<FxSlot *>(slot), ba);
            }
            return ba;
        }
    }
#endif
    if (!slot) {
        return {};
    }
    const QByteArray chunk = fxStateChunk(*slot);
    return chunk.isEmpty() ? slot->state : chunk;
}

bool Vst3Host::setState(FxSlot *slot, const QByteArray &state)
{
    if (!slot) {
        return false;
    }
    setFxStateChunk(slot, state);
#ifdef OPENVGAS_HAS_VST3_SDK
    auto it = m_instances.find(instanceKey(slot));
    if (it != m_instances.end() && it.value() && it.value()->rt && it.value()->rt->component
        && !state.isEmpty()) {
        MemoryIBStream stream(state);
        it.value()->rt->component->setState(&stream);
    }
#endif
    return true;
}

// ---------------- VST2 / VST1 (VeSTige + LoadLibrary) ----------------

Vst2Host &Vst2Host::instance()
{
    static Vst2Host h;
    return h;
}

bool Vst2Host::createInstance(const AudioPluginDesc &desc, FxSlot *slot)
{
    if (!slot) {
        return false;
    }
    ensureFxHostKey(slot);
    const QString key = vst2InstanceKey(slot);
    const QByteArray prevState = slot->state;
    const bool prevBypass = slot->bypass;
    const QString prevName = slot->displayName;
    const QString prevKey = slot->hostKey;

    // Reuse already-loaded module for this hostKey (graph copy / UI share one instance).
    if (auto existing = g_vst2.value(key)) {
        if (existing->effect) {
            *slot = fxSlotFromDesc(desc);
            slot->hostKey = prevKey;
            slot->state = prevState;
            slot->bypass = prevBypass;
            if (!prevName.isEmpty()) {
                slot->displayName = prevName;
            }
            if (!desc.path.isEmpty()) {
                const QString prefix = (desc.format == PluginFormat::Vst1) ? QStringLiteral("vst1:")
                                                                          : QStringLiteral("vst2:");
                slot->pluginId = prefix + desc.path;
            }
            return true;
        }
    }

    *slot = fxSlotFromDesc(desc);
    slot->hostKey = prevKey;
    slot->state = prevState;
    slot->bypass = prevBypass;
    if (!prevName.isEmpty()) {
        slot->displayName = prevName;
    }
    const QString path = desc.path.isEmpty() ? pathFromPluginId(slot->pluginId) : desc.path;
    if (!path.isEmpty()) {
        const QString prefix =
            (desc.format == PluginFormat::Vst1) ? QStringLiteral("vst1:") : QStringLiteral("vst2:");
        slot->pluginId = prefix + path;
    }

    auto inst = std::make_shared<Vst2Instance>();
    inst->path = path;
#ifdef _WIN32
    if (!path.isEmpty() && QFileInfo::exists(path)) {
        inst->module = LoadLibraryW(reinterpret_cast<LPCWSTR>(path.utf16()));
        if (inst->module) {
            using MainProc = AEffect *(VST_CALL_CONV *)(audioMasterCallback);
            auto mainProc = reinterpret_cast<MainProc>(GetProcAddress(inst->module, "VSTPluginMain"));
            if (!mainProc) {
                mainProc = reinterpret_cast<MainProc>(GetProcAddress(inst->module, "main"));
            }
            if (mainProc) {
                inst->effect = mainProc(hostCallback);
                if (inst->effect && inst->effect->magic == kEffectMagic) {
                    vstHostDataMap().insert(inst->effect, &inst->hostData);
                    inst->effect->dispatcher(inst->effect, effOpen, 0, 0, nullptr, 0.f);
                    inst->opened = true;
                } else {
                    inst->effect = nullptr;
                }
            }
        }
    }
#else
    Q_UNUSED(path);
#endif
    g_vst2.insert(key, inst);
    return inst->effect != nullptr;
}

void Vst2Host::releaseInstance(FxSlot *slot)
{
    const QString key = vst2InstanceKey(slot);
    auto it = g_vst2.find(key);
    if (it != g_vst2.end() && it.value()) {
        auto &inst = *it.value();
        if (inst.effect && inst.opened) {
            inst.effect->dispatcher(inst.effect, effMainsChanged, 0, 0, nullptr, 0.f);
            inst.effect->dispatcher(inst.effect, effClose, 0, 0, nullptr, 0.f);
            vstHostDataMap().remove(inst.effect);
            inst.effect = nullptr;
        }
#ifdef _WIN32
        if (inst.module) {
            FreeLibrary(inst.module);
            inst.module = nullptr;
        }
#endif
    }
    g_vst2.remove(key);
    if (slot) {
        slot->state.clear();
    }
}

void Vst2Host::prepare(FxSlot *slot, double sampleRate, int blockSize)
{
    auto instPtr = vst2Lookup(slot);
    if (!instPtr || !instPtr->effect) {
        return;
    }
    auto &inst = *instPtr;
    inst.hostData.sampleRate = sampleRate;
    inst.hostData.blockSize = blockSize;
    inst.effect->dispatcher(inst.effect, effSetSampleRate, 0, 0, nullptr, float(sampleRate));
    inst.effect->dispatcher(inst.effect, effSetBlockSize, 0, intptr_t(blockSize), nullptr, 0.f);
    inst.effect->dispatcher(inst.effect, effMainsChanged, 0, 1, nullptr, 0.f);
    const int nIn = std::max(2, int(inst.effect->numInputs));
    const int nOut = std::max(2, int(inst.effect->numOutputs));
    inst.inPtrs.assign(size_t(nIn), nullptr);
    inst.outPtrs.assign(size_t(nOut), nullptr);
    inst.silentIn.assign(size_t(blockSize), 0.f);
}

void Vst2Host::reset(FxSlot *slot)
{
    auto instPtr = vst2Lookup(slot);
    if (!instPtr || !instPtr->effect) {
        return;
    }
    instPtr->effect->dispatcher(instPtr->effect, effMainsChanged, 0, 0, nullptr, 0.f);
    instPtr->effect->dispatcher(instPtr->effect, effMainsChanged, 0, 1, nullptr, 0.f);
}

void Vst2Host::process(FxSlot *slot, float **in, float **out, int channels, int frames)
{
    if (!slot || !in || !out || channels < 1 || frames <= 0) {
        return;
    }
    if (slot->bypass) {
        passThrough(in, out, channels, frames);
        return;
    }
    auto instPtr = vst2Lookup(slot);
    if (!instPtr || !instPtr->effect) {
        passThrough(in, out, channels, frames);
        return;
    }
    auto &inst = *instPtr;
    AEffect *fx = inst.effect;
    passThrough(in, out, channels, frames);

    if (inst.inPtrs.empty()) {
        prepare(slot, inst.hostData.sampleRate, std::max(frames, inst.hostData.blockSize));
    }

    const int nIn = int(inst.inPtrs.size());
    const int nOut = int(inst.outPtrs.size());
    for (int c = 0; c < nIn; ++c) {
        if (c < channels && in[c]) {
            inst.inPtrs[size_t(c)] = in[c];
        } else {
            if (int(inst.silentIn.size()) < frames) {
                inst.silentIn.assign(size_t(frames), 0.f);
            }
            inst.inPtrs[size_t(c)] = inst.silentIn.data();
        }
    }
    for (int c = 0; c < nOut; ++c) {
        if (c < channels && out[c]) {
            inst.outPtrs[size_t(c)] = out[c];
        } else if (!inst.outPtrs.empty()) {
            inst.outPtrs[size_t(c)] = inst.outPtrs[0];
        }
    }

    if (fx->flags & effFlagsCanReplacing && fx->processReplacing) {
        fx->processReplacing(fx, inst.inPtrs.data(), inst.outPtrs.data(), frames);
    } else if (fx->process) {
        fx->process(fx, inst.inPtrs.data(), inst.outPtrs.data(), frames);
    }
}

bool Vst2Host::openEditor(FxSlot *slot, QWidget *parent)
{
    if (!slot) {
        return false;
    }
    auto instPtr = vst2Lookup(slot);
    if (!instPtr || !instPtr->effect) {
        QMessageBox::information(parent, QObject::tr("VST"),
                                 QObject::tr("%1\n\nCould not load plug-in DLL:\n%2\n\n"
                                             "Add the VST folder in Preferences → Plug-Ins.")
                                     .arg(slot->displayName)
                                     .arg(pathFromPluginId(slot->pluginId)));
        return false;
    }
    AEffect *fx = instPtr->effect;
    if (!(fx->flags & effFlagsHasEditor)) {
        return false;
    }
#ifdef _WIN32
    QWidget *hostWidget = parent;
    QWidget *ownedWin = nullptr;
    if (!hostWidget) {
        ownedWin = new QWidget(nullptr, Qt::Window);
        ownedWin->setAttribute(Qt::WA_DeleteOnClose);
        ownedWin->setWindowTitle(slot->displayName);
        ownedWin->resize(500, 400);
        hostWidget = ownedWin;
    }
    hostWidget->setAttribute(Qt::WA_NativeWindow, true);
    hostWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    hostWidget->setMinimumSize(320, 240);
    hostWidget->show();
    hostWidget->winId();
    WId wid = hostWidget->winId();
    fx->dispatcher(fx, effEditOpen, 0, 0, reinterpret_cast<void *>(wid), 0.f);
    struct VstERect {
        short top, left, bottom, right;
    };
    VstERect *er = nullptr;
    fx->dispatcher(fx, effEditGetRect, 0, 0, &er, 0.f);
    int prefW = 400;
    int prefH = 280;
    if (er) {
        prefW = std::max(100, int(er->right - er->left));
        prefH = std::max(80, int(er->bottom - er->top));
        hostWidget->setMinimumSize(std::min(prefW, 320), std::min(prefH, 240));
    }
    // Allow the host pane to grow; stretch the native child HWND to fill it.
    hostWidget->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    instPtr->hostData.editorHost = hostWidget;
    auto *filter =
        new PluginEditorResizeFilter(hostWidget, PluginEditorResizeFilter::Mode::FillChildHwnd);
    fillVstEditorToParent(reinterpret_cast<HWND>(wid));
    QTimer::singleShot(0, hostWidget, [hostWidget, filter]() {
        if (hostWidget && filter) {
            filter->apply(hostWidget->width(), hostWidget->height());
        }
    });
    QObject::connect(hostWidget, &QWidget::destroyed, hostWidget, [fx, instPtr]() {
        if (instPtr) {
            instPtr->hostData.editorHost.clear();
        }
        fx->dispatcher(fx, effEditClose, 0, 0, nullptr, 0.f);
    });
    if (ownedWin) {
        ownedWin->show();
    }
    return true;
#else
    Q_UNUSED(parent);
    return false;
#endif
}

int Vst2Host::parameterCount(const FxSlot *slot) const
{
    auto it = g_vst2.find(vst2InstanceKey(slot));
    if (it == g_vst2.end() || !it.value() || !it.value()->effect) {
        return 0;
    }
    return int(it.value()->effect->numParams);
}

bool Vst2Host::parameterInfo(const FxSlot *slot, int index, QString *name, float *min, float *max,
                             float *step) const
{
    auto it = g_vst2.find(vst2InstanceKey(slot));
    if (it == g_vst2.end() || !it.value() || !it.value()->effect) {
        return false;
    }
    if (index < 0 || index >= it.value()->effect->numParams) {
        return false;
    }
    char buf[64] = {};
    it.value()->effect->dispatcher(it.value()->effect, effGetParamName, index, 0, buf, 0.f);
    if (name) {
        *name = QString::fromUtf8(buf);
    }
    if (min) {
        *min = 0.f;
    }
    if (max) {
        *max = 1.f;
    }
    if (step) {
        *step = 0.01f;
    }
    return true;
}

float Vst2Host::getParameter(const FxSlot *slot, int index) const
{
    auto it = g_vst2.find(vst2InstanceKey(slot));
    if (it == g_vst2.end() || !it.value() || !it.value()->effect || !it.value()->effect->getParameter) {
        return 0.f;
    }
    return it.value()->effect->getParameter(it.value()->effect, index);
}

void Vst2Host::setParameter(FxSlot *slot, int index, float value)
{
    auto it = g_vst2.find(vst2InstanceKey(slot));
    if (it == g_vst2.end() || !it.value() || !it.value()->effect || !it.value()->effect->setParameter) {
        return;
    }
    it.value()->effect->setParameter(it.value()->effect, index, value);
}

QByteArray Vst2Host::getState(const FxSlot *slot) const
{
    auto it = g_vst2.find(vst2InstanceKey(slot));
    if (it != g_vst2.end() && it.value() && it.value()->effect) {
        void *data = nullptr;
        const intptr_t sz =
            it.value()->effect->dispatcher(it.value()->effect, effGetChunk, 0, 0, &data, 0.f);
        if (sz > 0 && data) {
            return QByteArray(static_cast<const char *>(data), int(sz));
        }
    }
    return slot ? slot->state : QByteArray{};
}

bool Vst2Host::setState(FxSlot *slot, const QByteArray &state)
{
    if (!slot) {
        return false;
    }
    slot->state = state;
    auto it = g_vst2.find(vst2InstanceKey(slot));
    if (it != g_vst2.end() && it.value() && it.value()->effect && !state.isEmpty()) {
        it.value()->effect->dispatcher(it.value()->effect, effSetChunk, 0, state.size(),
                                       const_cast<char *>(state.data()), 0.f);
    }
    return true;
}

} // namespace openvegas
