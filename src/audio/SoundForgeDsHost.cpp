#include "audio/SoundForgeDsHost.h"

#include "plugins/SoundForgeHost.h"

#include <QByteArray>
#include <QDebug>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>

#include <control.h>
#include <mmreg.h>
#include <objbase.h>
#include <ocidl.h>
#include <olectl.h>
#include <strmif.h>
#include <uuids.h>
#include <vfwmsgs.h>

#ifndef WAVE_FORMAT_IEEE_FLOAT
#define WAVE_FORMAT_IEEE_FLOAT 0x0003
#endif
#endif // Q_OS_WIN

namespace openvegas {

// The "sfds:{CLSID}" scheme belongs to the discovery layer, which is what mints the ids
// in the first place; these just forward so there is one definition of the format.
QString SoundForgeDsHost::makePluginId(const QString &clsid)
{
    return SoundForgeHost::pluginId(clsid);
}

QString SoundForgeDsHost::clsidFromPluginId(const QString &pluginId)
{
    return SoundForgeHost::clsidFromPluginId(pluginId);
}

// ===========================================================================
#ifndef Q_OS_WIN
// ---------------------------------------------------------------------------
// Non-Windows: the effects are COM in-process servers, so there is nothing to
// host. Everything fails cleanly and callers fall back to builtin substitutes.
// ---------------------------------------------------------------------------

struct SoundForgeDsHost::Instance {};

SoundForgeDsHost &SoundForgeDsHost::instance()
{
    static SoundForgeDsHost host;
    return host;
}

bool SoundForgeDsHost::isAvailable()
{
    return false;
}

std::shared_ptr<SoundForgeDsHost::Instance> SoundForgeDsHost::lookup(const FxSlot *) const
{
    return {};
}

bool SoundForgeDsHost::hasInstance(const FxSlot &) const
{
    return false;
}

bool SoundForgeDsHost::createInstance(const AudioPluginDesc &, FxSlot *)
{
    return false;
}

void SoundForgeDsHost::releaseInstance(FxSlot *) {}
void SoundForgeDsHost::prepare(FxSlot *, double, int) {}
void SoundForgeDsHost::reset(FxSlot *) {}

void SoundForgeDsHost::process(FxSlot *, float **in, float **out, int channels, int frames)
{
    if (!in || !out) {
        return;
    }
    for (int c = 0; c < channels; ++c) {
        if (in[c] && out[c] && in[c] != out[c]) {
            std::memcpy(out[c], in[c], size_t(frames) * sizeof(float));
        }
    }
}

bool SoundForgeDsHost::openEditor(FxSlot *, QWidget *)
{
    return false;
}

QByteArray SoundForgeDsHost::getState(const FxSlot *slot) const
{
    return slot ? slot->state : QByteArray{};
}

bool SoundForgeDsHost::setState(FxSlot *slot, const QByteArray &state)
{
    if (!slot) {
        return false;
    }
    slot->state = state;
    return true;
}

bool SoundForgeDsHost::probeProcess(const QString &, double *, QString *errorOut)
{
    if (errorOut) {
        *errorOut = QStringLiteral("Sound Forge Shared Plug-Ins are COM servers (Windows only)");
    }
    return false;
}

QStringList SoundForgeDsHost::propertyPageTitles(const QString &)
{
    return {};
}

#else // Q_OS_WIN
// ---------------------------------------------------------------------------
// Windows: real DirectShow hosting.
// ---------------------------------------------------------------------------

namespace {

/** CoInitialize this thread once; harmless when it is already initialised. */
void ensureComOnThread()
{
    static thread_local bool done = false;
    if (!done) {
        // RPC_E_CHANGED_MODE just means the thread is already an STA (the Qt GUI
        // thread is). The effects register ThreadingModel=Both, so they work either
        // way; we only need *some* apartment to exist before CoCreateInstance.
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        done = true;
    }
}

QString hrText(HRESULT hr)
{
    switch (static_cast<unsigned long>(hr)) {
    case 0x8004025fUL: return QStringLiteral("VFW_E_TYPE_NOT_ACCEPTED");
    case 0x80040207UL: return QStringLiteral("VFW_E_NO_ACCEPTABLE_TYPES");
    case 0x80040231UL: return QStringLiteral("VFW_E_NO_TRANSPORT");
    case 0x80040233UL: return QStringLiteral("VFW_E_CIRCULAR_GRAPH");
    case 0x8004022aUL: return QStringLiteral("VFW_E_INVALID_MEDIA_TYPE");
    case 0x80040211UL: return QStringLiteral("VFW_E_NOT_COMMITTED");
    case 0x80004002UL: return QStringLiteral("E_NOINTERFACE");
    default: break;
    }
    return QStringLiteral("0x%1").arg(static_cast<unsigned long>(hr), 8, 16, QLatin1Char('0'));
}

// --------------------------------------------------------------- pin enumerator
class OnePinEnum : public IEnumPins {
public:
    explicit OnePinEnum(IPin *pin) : m_pin(pin) {}
    // Release() deletes through this pointer, so the destructor has to be virtual.
    virtual ~OnePinEnum() = default;

    STDMETHODIMP QueryInterface(REFIID riid, void **out) override
    {
        if (!out) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == IID_IEnumPins) {
            *out = static_cast<IEnumPins *>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_ref); }
    STDMETHODIMP_(ULONG) Release() override
    {
        const LONG left = InterlockedDecrement(&m_ref);
        if (left == 0) {
            delete this;
        }
        return left;
    }
    STDMETHODIMP Next(ULONG count, IPin **out, ULONG *fetched) override
    {
        ULONG n = 0;
        if (count > 0 && m_pos == 0 && m_pin) {
            out[0] = m_pin;
            m_pin->AddRef();
            m_pos = 1;
            n = 1;
        }
        if (fetched) {
            *fetched = n;
        }
        return n == count ? S_OK : S_FALSE;
    }
    STDMETHODIMP Skip(ULONG count) override
    {
        m_pos += int(count);
        return m_pos <= 1 ? S_OK : S_FALSE;
    }
    STDMETHODIMP Reset() override
    {
        m_pos = 0;
        return S_OK;
    }
    STDMETHODIMP Clone(IEnumPins **out) override
    {
        if (!out) {
            return E_POINTER;
        }
        auto *copy = new OnePinEnum(m_pin);
        copy->m_pos = m_pos;
        *out = copy;
        return S_OK;
    }

private:
    LONG m_ref = 1;
    IPin *m_pin = nullptr;
    int m_pos = 0;
};

// --------------------------------------------------------------- host filter shell
class HostFilterBase : public IBaseFilter {
public:
    virtual IPin *hostedPin() = 0;
    virtual const wchar_t *hostedPinId() const = 0;

    STDMETHODIMP QueryInterface(REFIID riid, void **out) override
    {
        if (!out) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == IID_IPersist || riid == IID_IMediaFilter
            || riid == IID_IBaseFilter) {
            *out = static_cast<IBaseFilter *>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    // The graph holds these filters for exactly as long as the owning Instance does,
    // so reference counting only has to be well-behaved, not drive destruction.
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_ref); }
    STDMETHODIMP_(ULONG) Release() override { return InterlockedDecrement(&m_ref); }

    STDMETHODIMP GetClassID(CLSID *out) override
    {
        if (out) {
            *out = GUID_NULL;
        }
        return S_OK;
    }
    STDMETHODIMP Stop() override
    {
        m_state = State_Stopped;
        return S_OK;
    }
    STDMETHODIMP Pause() override
    {
        m_state = State_Paused;
        return S_OK;
    }
    STDMETHODIMP Run(REFERENCE_TIME) override
    {
        m_state = State_Running;
        return S_OK;
    }
    STDMETHODIMP GetState(DWORD, FILTER_STATE *out) override
    {
        if (out) {
            *out = m_state;
        }
        return S_OK;
    }
    STDMETHODIMP SetSyncSource(IReferenceClock *) override { return S_OK; }
    STDMETHODIMP GetSyncSource(IReferenceClock **out) override
    {
        if (out) {
            *out = nullptr;
        }
        return S_OK;
    }
    STDMETHODIMP EnumPins(IEnumPins **out) override
    {
        if (!out) {
            return E_POINTER;
        }
        *out = new OnePinEnum(hostedPin());
        return S_OK;
    }
    STDMETHODIMP FindPin(LPCWSTR id, IPin **out) override
    {
        if (!out) {
            return E_POINTER;
        }
        if (id && wcscmp(id, hostedPinId()) == 0) {
            *out = hostedPin();
            (*out)->AddRef();
            return S_OK;
        }
        *out = nullptr;
        return VFW_E_NOT_FOUND;
    }
    STDMETHODIMP QueryFilterInfo(FILTER_INFO *out) override
    {
        if (!out) {
            return E_POINTER;
        }
        ZeroMemory(out, sizeof(*out));
        wcsncpy(out->achName, m_name, 63);
        out->pGraph = m_graph;
        if (m_graph) {
            m_graph->AddRef();
        }
        return S_OK;
    }
    STDMETHODIMP JoinFilterGraph(IFilterGraph *graph, LPCWSTR name) override
    {
        m_graph = graph;
        if (name) {
            wcsncpy(m_name, name, 63);
            m_name[63] = 0;
        }
        return S_OK;
    }
    STDMETHODIMP QueryVendorInfo(LPWSTR *out) override
    {
        if (out) {
            *out = nullptr;
        }
        return E_NOTIMPL;
    }

protected:
    LONG m_ref = 1;
    FILTER_STATE m_state = State_Stopped;
    IFilterGraph *m_graph = nullptr;
    WCHAR m_name[64] = L"OpenVegas";
};

// --------------------------------------------------------------- source pin
class HostSourcePin : public IPin {
public:
    HostFilterBase *owner = nullptr;
    IPin *peer = nullptr;
    AM_MEDIA_TYPE mt{};

    STDMETHODIMP QueryInterface(REFIID riid, void **out) override
    {
        if (!out) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == IID_IPin) {
            *out = static_cast<IPin *>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_ref); }
    STDMETHODIMP_(ULONG) Release() override { return InterlockedDecrement(&m_ref); }

    STDMETHODIMP Connect(IPin *pin, const AM_MEDIA_TYPE *pmt) override
    {
        if (!pin) {
            return E_POINTER;
        }
        const HRESULT hr = pin->ReceiveConnection(static_cast<IPin *>(this), pmt ? pmt : &mt);
        if (SUCCEEDED(hr)) {
            peer = pin;
        }
        return hr;
    }
    STDMETHODIMP ReceiveConnection(IPin *, const AM_MEDIA_TYPE *) override { return E_UNEXPECTED; }
    STDMETHODIMP Disconnect() override
    {
        peer = nullptr;
        return S_OK;
    }
    STDMETHODIMP ConnectedTo(IPin **out) override
    {
        if (!out) {
            return E_POINTER;
        }
        *out = peer;
        if (peer) {
            peer->AddRef();
        }
        return peer ? S_OK : VFW_E_NOT_CONNECTED;
    }
    STDMETHODIMP ConnectionMediaType(AM_MEDIA_TYPE *out) override
    {
        if (!out) {
            return E_POINTER;
        }
        if (!peer) {
            return VFW_E_NOT_CONNECTED;
        }
        *out = mt;
        if (mt.cbFormat && mt.pbFormat) {
            out->pbFormat = static_cast<BYTE *>(CoTaskMemAlloc(mt.cbFormat));
            std::memcpy(out->pbFormat, mt.pbFormat, mt.cbFormat);
        }
        out->pUnk = nullptr;
        return S_OK;
    }
    STDMETHODIMP QueryPinInfo(PIN_INFO *out) override
    {
        if (!out) {
            return E_POINTER;
        }
        ZeroMemory(out, sizeof(*out));
        out->dir = PINDIR_OUTPUT;
        wcscpy(out->achName, L"Out");
        out->pFilter = static_cast<IBaseFilter *>(owner);
        if (out->pFilter) {
            out->pFilter->AddRef();
        }
        return S_OK;
    }
    STDMETHODIMP QueryDirection(PIN_DIRECTION *out) override
    {
        if (!out) {
            return E_POINTER;
        }
        *out = PINDIR_OUTPUT;
        return S_OK;
    }
    STDMETHODIMP QueryId(LPWSTR *out) override
    {
        if (!out) {
            return E_POINTER;
        }
        *out = static_cast<LPWSTR>(CoTaskMemAlloc(sizeof(WCHAR) * 4));
        wcscpy(*out, L"Out");
        return S_OK;
    }
    STDMETHODIMP QueryAccept(const AM_MEDIA_TYPE *) override { return S_OK; }
    STDMETHODIMP EnumMediaTypes(IEnumMediaTypes **out) override
    {
        if (out) {
            *out = nullptr;
        }
        return E_NOTIMPL;
    }
    STDMETHODIMP QueryInternalConnections(IPin **, ULONG *) override { return E_NOTIMPL; }
    STDMETHODIMP EndOfStream() override { return S_OK; }
    STDMETHODIMP BeginFlush() override { return S_OK; }
    STDMETHODIMP EndFlush() override { return S_OK; }
    STDMETHODIMP NewSegment(REFERENCE_TIME, REFERENCE_TIME, double) override { return S_OK; }

private:
    LONG m_ref = 1;
};

// --------------------------------------------------------------- sink pin
class HostSinkPin : public IPin, public IMemInputPin {
public:
    HostFilterBase *owner = nullptr;
    IPin *peer = nullptr;
    AM_MEDIA_TYPE mt{};
    /** Interleaved float output collected from the effect, drained by process(). */
    std::vector<float> fifo;

    STDMETHODIMP QueryInterface(REFIID riid, void **out) override
    {
        if (!out) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == IID_IPin) {
            *out = static_cast<IPin *>(this);
            AddRef();
            return S_OK;
        }
        if (riid == IID_IMemInputPin) {
            *out = static_cast<IMemInputPin *>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_ref); }
    STDMETHODIMP_(ULONG) Release() override { return InterlockedDecrement(&m_ref); }

    STDMETHODIMP Connect(IPin *, const AM_MEDIA_TYPE *) override { return E_UNEXPECTED; }
    STDMETHODIMP ReceiveConnection(IPin *connector, const AM_MEDIA_TYPE *pmt) override
    {
        peer = connector;
        if (pmt) {
            mt = *pmt;
            if (pmt->cbFormat && pmt->pbFormat) {
                mt.pbFormat = static_cast<BYTE *>(CoTaskMemAlloc(pmt->cbFormat));
                std::memcpy(mt.pbFormat, pmt->pbFormat, pmt->cbFormat);
            }
            mt.pUnk = nullptr;
        }
        return S_OK;
    }
    STDMETHODIMP Disconnect() override
    {
        peer = nullptr;
        return S_OK;
    }
    STDMETHODIMP ConnectedTo(IPin **out) override
    {
        if (!out) {
            return E_POINTER;
        }
        *out = peer;
        if (peer) {
            peer->AddRef();
        }
        return peer ? S_OK : VFW_E_NOT_CONNECTED;
    }
    STDMETHODIMP ConnectionMediaType(AM_MEDIA_TYPE *out) override
    {
        if (!out) {
            return E_POINTER;
        }
        if (!peer) {
            return VFW_E_NOT_CONNECTED;
        }
        *out = mt;
        if (mt.cbFormat && mt.pbFormat) {
            out->pbFormat = static_cast<BYTE *>(CoTaskMemAlloc(mt.cbFormat));
            std::memcpy(out->pbFormat, mt.pbFormat, mt.cbFormat);
        }
        out->pUnk = nullptr;
        return S_OK;
    }
    STDMETHODIMP QueryPinInfo(PIN_INFO *out) override
    {
        if (!out) {
            return E_POINTER;
        }
        ZeroMemory(out, sizeof(*out));
        out->dir = PINDIR_INPUT;
        wcscpy(out->achName, L"In");
        out->pFilter = static_cast<IBaseFilter *>(owner);
        if (out->pFilter) {
            out->pFilter->AddRef();
        }
        return S_OK;
    }
    STDMETHODIMP QueryDirection(PIN_DIRECTION *out) override
    {
        if (!out) {
            return E_POINTER;
        }
        *out = PINDIR_INPUT;
        return S_OK;
    }
    STDMETHODIMP QueryId(LPWSTR *out) override
    {
        if (!out) {
            return E_POINTER;
        }
        *out = static_cast<LPWSTR>(CoTaskMemAlloc(sizeof(WCHAR) * 3));
        wcscpy(*out, L"In");
        return S_OK;
    }
    STDMETHODIMP QueryAccept(const AM_MEDIA_TYPE *) override { return S_OK; }
    STDMETHODIMP EnumMediaTypes(IEnumMediaTypes **out) override
    {
        if (out) {
            *out = nullptr;
        }
        return E_NOTIMPL;
    }
    STDMETHODIMP QueryInternalConnections(IPin **, ULONG *) override { return E_NOTIMPL; }
    STDMETHODIMP EndOfStream() override { return S_OK; }
    STDMETHODIMP BeginFlush() override { return S_OK; }
    STDMETHODIMP EndFlush() override { return S_OK; }
    STDMETHODIMP NewSegment(REFERENCE_TIME, REFERENCE_TIME, double) override { return S_OK; }

    // IMemInputPin — the effect delivers finished audio here.
    STDMETHODIMP GetAllocator(IMemAllocator **out) override
    {
        if (!out) {
            return E_POINTER;
        }
        *out = nullptr;
        return VFW_E_NO_ALLOCATOR;
    }
    STDMETHODIMP NotifyAllocator(IMemAllocator *, BOOL) override { return S_OK; }
    STDMETHODIMP GetAllocatorRequirements(ALLOCATOR_PROPERTIES *) override { return E_NOTIMPL; }
    STDMETHODIMP Receive(IMediaSample *sample) override
    {
        if (!sample) {
            return E_POINTER;
        }
        BYTE *data = nullptr;
        if (FAILED(sample->GetPointer(&data)) || !data) {
            return S_OK;
        }
        const long bytes = sample->GetActualDataLength();
        if (bytes <= 0) {
            return S_OK;
        }
        const auto *samples = reinterpret_cast<const float *>(data);
        fifo.insert(fifo.end(), samples, samples + bytes / long(sizeof(float)));
        return S_OK;
    }
    STDMETHODIMP ReceiveMultiple(IMediaSample **samples, long count, long *done) override
    {
        long n = 0;
        for (long i = 0; i < count; ++i) {
            if (FAILED(Receive(samples[i]))) {
                break;
            }
            ++n;
        }
        if (done) {
            *done = n;
        }
        return S_OK;
    }
    STDMETHODIMP ReceiveCanBlock() override { return S_FALSE; }

private:
    LONG m_ref = 1;
};

/**
 * Minimal IPropertyPageSite. The effects' own dialogs are ordinary OLE property pages,
 * so they can be activated straight into a Qt widget instead of the modal frame
 * OleCreatePropertyFrame would put up.
 *
 * A page reports edits through OnStatusChange; applying them there and then is what makes
 * the plug-in's dialog behave the way it does inside VEGAS, where moving a control is
 * audible immediately rather than on an OK button.
 */
class PropertyPageSite : public IPropertyPageSite {
public:
    IPropertyPage *page = nullptr;

    STDMETHODIMP QueryInterface(REFIID riid, void **out) override
    {
        if (!out) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == IID_IPropertyPageSite) {
            *out = static_cast<IPropertyPageSite *>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_ref); }
    STDMETHODIMP_(ULONG) Release() override { return InterlockedDecrement(&m_ref); }

    STDMETHODIMP OnStatusChange(DWORD flags) override
    {
        if ((flags & PROPPAGESTATUS_DIRTY) && page) {
            page->Apply();
        }
        return S_OK;
    }
    STDMETHODIMP GetLocaleID(LCID *localeId) override
    {
        if (!localeId) {
            return E_POINTER;
        }
        *localeId = GetUserDefaultLCID();
        return S_OK;
    }
    STDMETHODIMP GetPageContainer(IUnknown **out) override
    {
        if (out) {
            *out = nullptr;
        }
        return E_NOTIMPL;
    }
    STDMETHODIMP TranslateAccelerator(MSG *) override { return E_NOTIMPL; }

private:
    LONG m_ref = 1;
};

class HostSourceFilter : public HostFilterBase {
public:
    HostSourcePin pin;
    HostSourceFilter() { pin.owner = this; }
    IPin *hostedPin() override { return static_cast<IPin *>(&pin); }
    const wchar_t *hostedPinId() const override { return L"Out"; }
};

class HostSinkFilter : public HostFilterBase {
public:
    HostSinkPin pin;
    HostSinkFilter() { pin.owner = this; }
    IPin *hostedPin() override { return static_cast<IPin *>(&pin); }
    const wchar_t *hostedPinId() const override { return L"In"; }
};

/**
 * One built graph: source -> effect -> sink, wired and running.
 * Everything here is plain DirectShow; nothing is guessed about the effect.
 */
struct Graph {
    IGraphBuilder *builder = nullptr;
    IBaseFilter *effect = nullptr;
    IPin *effectIn = nullptr;
    IPin *effectOut = nullptr;
    IMemInputPin *effectMem = nullptr;
    IMemAllocator *allocator = nullptr;
    IMediaControl *control = nullptr;
    HostSourceFilter source;
    HostSinkFilter sink;

    WAVEFORMATEX format{};
    AM_MEDIA_TYPE mediaType{};
    int channels = 0;
    int blockFrames = 0;
    REFERENCE_TIME streamTime = 0;
    bool running = false;

    ~Graph() { teardown(); }

    void teardown()
    {
        if (control) {
            control->Stop();
            control->Release();
            control = nullptr;
        }
        if (allocator) {
            allocator->Decommit();
            allocator->Release();
            allocator = nullptr;
        }
        if (effectMem) {
            effectMem->Release();
            effectMem = nullptr;
        }
        if (effectOut) {
            effectOut->Disconnect();
            effectOut->Release();
            effectOut = nullptr;
        }
        if (effectIn) {
            effectIn->Disconnect();
            effectIn->Release();
            effectIn = nullptr;
        }
        if (builder) {
            builder->RemoveFilter(static_cast<IBaseFilter *>(&source));
            builder->RemoveFilter(static_cast<IBaseFilter *>(&sink));
            if (effect) {
                builder->RemoveFilter(effect);
            }
        }
        if (effect) {
            effect->Release();
            effect = nullptr;
        }
        if (builder) {
            builder->Release();
            builder = nullptr;
        }
        if (mediaType.pbFormat && mediaType.pbFormat != reinterpret_cast<BYTE *>(&format)) {
            CoTaskMemFree(mediaType.pbFormat);
        }
        ZeroMemory(&mediaType, sizeof(mediaType));
        // The sink pin copies the format handed to ReceiveConnection, so that copy is
        // ours to free — otherwise every graph rebuild would leak it.
        if (sink.pin.mt.pbFormat && sink.pin.mt.pbFormat != reinterpret_cast<BYTE *>(&format)) {
            CoTaskMemFree(sink.pin.mt.pbFormat);
        }
        ZeroMemory(&sink.pin.mt, sizeof(sink.pin.mt));
        ZeroMemory(&source.pin.mt, sizeof(source.pin.mt));
        sink.pin.fifo.clear();
        running = false;
    }

    bool build(const CLSID &clsid, double sampleRate, int chans, int frames, QString *errorOut)
    {
        teardown();
        channels = std::max(1, chans);
        blockFrames = std::max(64, frames);

        ensureComOnThread();

        HRESULT hr = CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_IGraphBuilder, reinterpret_cast<void **>(&builder));
        if (FAILED(hr) || !builder) {
            if (errorOut) {
                *errorOut = QStringLiteral("filter graph unavailable (%1)").arg(hrText(hr));
            }
            return false;
        }
        hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, IID_IBaseFilter,
                              reinterpret_cast<void **>(&effect));
        if (FAILED(hr) || !effect) {
            if (errorOut) {
                *errorOut = QStringLiteral("CoCreateInstance failed (%1)").arg(hrText(hr));
            }
            teardown();
            return false;
        }

        // Source and sink must be *separate* filters: one filter owning both pins turns
        // the graph into a cycle and the output pin then refuses with VFW_E_NO_TRANSPORT.
        builder->AddFilter(static_cast<IBaseFilter *>(&source), L"OpenVegas Source");
        builder->AddFilter(effect, L"Effect");
        builder->AddFilter(static_cast<IBaseFilter *>(&sink), L"OpenVegas Sink");

        IEnumPins *pins = nullptr;
        if (SUCCEEDED(effect->EnumPins(&pins)) && pins) {
            IPin *pin = nullptr;
            ULONG fetched = 0;
            while (pins->Next(1, &pin, &fetched) == S_OK && pin) {
                PIN_DIRECTION dir;
                if (SUCCEEDED(pin->QueryDirection(&dir))) {
                    if (dir == PINDIR_INPUT && !effectIn) {
                        effectIn = pin;
                        effectIn->AddRef();
                    } else if (dir == PINDIR_OUTPUT && !effectOut) {
                        effectOut = pin;
                        effectOut->AddRef();
                    }
                }
                pin->Release();
                pin = nullptr;
            }
            pins->Release();
        }
        if (!effectIn || !effectOut) {
            if (errorOut) {
                *errorOut = QStringLiteral("effect does not expose an audio in/out pin pair");
            }
            teardown();
            return false;
        }

        // 32-bit float: these filters process it natively and losslessly.
        ZeroMemory(&format, sizeof(format));
        format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        format.nChannels = WORD(channels);
        format.nSamplesPerSec = DWORD(sampleRate > 0 ? sampleRate : 48000.0);
        format.wBitsPerSample = 32;
        format.nBlockAlign = WORD(channels * 4);
        format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
        format.cbSize = 0;

        ZeroMemory(&mediaType, sizeof(mediaType));
        mediaType.majortype = MEDIATYPE_Audio;
        mediaType.subtype = MEDIASUBTYPE_IEEE_FLOAT;
        mediaType.formattype = FORMAT_WaveFormatEx;
        mediaType.bFixedSizeSamples = TRUE;
        mediaType.bTemporalCompression = FALSE;
        mediaType.lSampleSize = format.nBlockAlign;
        mediaType.cbFormat = sizeof(WAVEFORMATEX);
        mediaType.pbFormat = reinterpret_cast<BYTE *>(&format);

        source.pin.mt = mediaType;
        sink.pin.mt = mediaType;

        hr = builder->ConnectDirect(static_cast<IPin *>(&source.pin), effectIn, &mediaType);
        if (FAILED(hr)) {
            if (errorOut) {
                *errorOut = QStringLiteral("input pin refused the format (%1)").arg(hrText(hr));
            }
            teardown();
            return false;
        }

        if (FAILED(effectIn->QueryInterface(IID_IMemInputPin,
                                            reinterpret_cast<void **>(&effectMem)))
            || !effectMem) {
            if (errorOut) {
                *errorOut = QStringLiteral("input pin has no IMemInputPin");
            }
            teardown();
            return false;
        }

        hr = CoCreateInstance(CLSID_MemoryAllocator, nullptr, CLSCTX_INPROC_SERVER,
                              IID_IMemAllocator, reinterpret_cast<void **>(&allocator));
        if (FAILED(hr) || !allocator) {
            if (errorOut) {
                *errorOut = QStringLiteral("no memory allocator (%1)").arg(hrText(hr));
            }
            teardown();
            return false;
        }
        ALLOCATOR_PROPERTIES want;
        ALLOCATOR_PROPERTIES got;
        ZeroMemory(&want, sizeof(want));
        ZeroMemory(&got, sizeof(got));
        want.cBuffers = 8;
        want.cbBuffer = long(blockFrames) * format.nBlockAlign;
        want.cbAlign = 1;
        allocator->SetProperties(&want, &got);
        effectMem->NotifyAllocator(allocator, FALSE);

        hr = builder->ConnectDirect(effectOut, static_cast<IPin *>(&sink.pin), &mediaType);
        if (FAILED(hr)) {
            if (errorOut) {
                *errorOut = QStringLiteral("output pin refused the format (%1)").arg(hrText(hr));
            }
            teardown();
            return false;
        }

        IMediaFilter *mf = nullptr;
        if (SUCCEEDED(builder->QueryInterface(IID_IMediaFilter, reinterpret_cast<void **>(&mf)))
            && mf) {
            mf->SetSyncSource(nullptr); // free-running: we drive the clock
            mf->Release();
        }
        if (SUCCEEDED(builder->QueryInterface(IID_IMediaControl,
                                              reinterpret_cast<void **>(&control)))
            && control) {
            control->Run();
        }

        // Wiring the output pin decommits the allocator; without this the first
        // GetBuffer fails with VFW_E_NOT_COMMITTED.
        allocator->Commit();

        source.pin.NewSegment(0, 0, 1.0);
        streamTime = 0;
        running = true;
        return true;
    }

    /** Push one interleaved block; output lands in sink.pin.fifo. */
    bool pushBlock(const float *interleaved, int frames)
    {
        if (!running || !allocator || !effectMem || frames <= 0) {
            return false;
        }
        IMediaSample *sample = nullptr;
        HRESULT hr = allocator->GetBuffer(&sample, nullptr, nullptr, 0);
        if (FAILED(hr) || !sample) {
            allocator->Commit();
            hr = allocator->GetBuffer(&sample, nullptr, nullptr, 0);
            if (FAILED(hr) || !sample) {
                return false;
            }
        }
        BYTE *data = nullptr;
        if (FAILED(sample->GetPointer(&data)) || !data) {
            sample->Release();
            return false;
        }
        const long bytes = long(frames) * format.nBlockAlign;
        if (bytes > sample->GetSize()) {
            sample->Release();
            return false;
        }
        std::memcpy(data, interleaved, size_t(bytes));
        sample->SetActualDataLength(bytes);

        const auto span = REFERENCE_TIME(10000000.0 * frames / double(format.nSamplesPerSec));
        REFERENCE_TIME start = streamTime;
        REFERENCE_TIME end = streamTime + span;
        sample->SetTime(&start, &end);
        sample->SetSyncPoint(TRUE);
        sample->SetDiscontinuity(streamTime == 0);
        streamTime = end;

        hr = effectMem->Receive(sample);
        sample->Release();
        return SUCCEEDED(hr);
    }

    void flush()
    {
        if (effectIn) {
            effectIn->BeginFlush();
            effectIn->EndFlush();
        }
        sink.pin.fifo.clear();
        streamTime = 0;
    }
};

} // namespace

// --------------------------------------------------------------- instance
struct SoundForgeDsHost::Instance {
    CLSID clsid{};
    QString clsidText;
    QString displayName;
    Graph graph;
    QMutex mutex;
    double sampleRate = 48000.0;
    int blockSize = 512;
    bool graphFailed = false;
    QString lastError;
    /** Pending state to apply once the effect object exists. */
    QByteArray pendingState;
    std::vector<float> scratchIn;
    std::vector<float> scratchOut;
    /**
     * Live embedded property pages, while an editor is open.
     *
     * Several effects register more than one — Wave Hammer Surround and Graphic EQ have
     * three, Track EQ and Multi-Band Dynamics two — and showing only the first hides
     * whole sections of their controls, so all of them are hosted.
     */
    QVector<IPropertyPage *> pages;
    QVector<PropertyPageSite *> sites;
    QPointer<QWidget> pageContainer;

    void closePage()
    {
        for (IPropertyPage *page : pages) {
            if (!page) {
                continue;
            }
            page->Deactivate();
            page->SetPageSite(nullptr);
            page->SetObjects(0, nullptr);
            page->Release();
        }
        pages.clear();
        qDeleteAll(sites);
        sites.clear();
        // Deactivate first, then the widgets whose HWNDs the pages were drawing into.
        if (pageContainer) {
            delete pageContainer.data();
        }
        pageContainer = nullptr;
    }

    /**
     * Settings live inside the effect object, so anything that replaces it has to carry
     * them across — otherwise changing the project sample rate would quietly reset the
     * plug-in to its defaults. An open property page is bound to that same object and
     * must go down with it.
     */
    void retireEffect()
    {
        if (!graph.effect) {
            return;
        }
        const QByteArray live = captureState();
        if (!live.isEmpty()) {
            pendingState = live;
        }
        closePage();
    }

    bool ensureGraph(int channels)
    {
        if (graph.running && graph.channels == channels
            && graph.format.nSamplesPerSec == DWORD(sampleRate)) {
            return true;
        }
        if (graphFailed) {
            return false;
        }
        retireEffect();
        QString error;
        if (!graph.build(clsid, sampleRate, channels, blockSize, &error)) {
            graphFailed = true;
            lastError = error;
            qWarning().noquote() << "[plugins][soundforge]" << displayName
                                 << "could not be hosted:" << error;
            return false;
        }
        if (!pendingState.isEmpty()) {
            applyState(pendingState);
        }
        return true;
    }

    bool applyState(const QByteArray &blob)
    {
        if (!graph.effect || blob.isEmpty()) {
            return false;
        }
        IPersistStream *persist = nullptr;
        if (FAILED(graph.effect->QueryInterface(IID_IPersistStream,
                                                reinterpret_cast<void **>(&persist)))
            || !persist) {
            return false;
        }
        bool ok = false;
        HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, SIZE_T(blob.size()));
        if (mem) {
            if (void *dst = GlobalLock(mem)) {
                std::memcpy(dst, blob.constData(), size_t(blob.size()));
                GlobalUnlock(mem);
                IStream *stream = nullptr;
                if (SUCCEEDED(CreateStreamOnHGlobal(mem, TRUE, &stream)) && stream) {
                    ok = SUCCEEDED(persist->Load(stream));
                    stream->Release();
                    mem = nullptr; // owned by the stream now
                }
            }
            if (mem) {
                GlobalFree(mem);
            }
        }
        persist->Release();
        return ok;
    }

    QByteArray captureState() const
    {
        if (!graph.effect) {
            return pendingState;
        }
        IPersistStream *persist = nullptr;
        if (FAILED(graph.effect->QueryInterface(IID_IPersistStream,
                                                reinterpret_cast<void **>(&persist)))
            || !persist) {
            return pendingState;
        }
        QByteArray blob;
        IStream *stream = nullptr;
        if (SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)) && stream) {
            if (SUCCEEDED(persist->Save(stream, TRUE))) {
                HGLOBAL mem = nullptr;
                if (SUCCEEDED(GetHGlobalFromStream(stream, &mem)) && mem) {
                    const SIZE_T size = GlobalSize(mem);
                    if (const void *src = GlobalLock(mem)) {
                        blob = QByteArray(static_cast<const char *>(src), int(size));
                        GlobalUnlock(mem);
                    }
                }
            }
            stream->Release();
        }
        persist->Release();
        return blob;
    }
};

SoundForgeDsHost &SoundForgeDsHost::instance()
{
    static SoundForgeDsHost host;
    return host;
}

bool SoundForgeDsHost::isAvailable()
{
    return SoundForgeHost::anyRegistered();
}

std::shared_ptr<SoundForgeDsHost::Instance> SoundForgeDsHost::lookup(const FxSlot *slot) const
{
    if (!slot || slot->hostKey.isEmpty()) {
        return {};
    }
    return m_instances.value(slot->hostKey);
}

bool SoundForgeDsHost::hasInstance(const FxSlot &slot) const
{
    return !slot.hostKey.isEmpty() && m_instances.contains(slot.hostKey);
}

bool SoundForgeDsHost::createInstance(const AudioPluginDesc &desc, FxSlot *slot)
{
    if (!slot) {
        return false;
    }
    const QString clsidText = clsidFromPluginId(desc.id.isEmpty() ? slot->pluginId : desc.id);
    if (clsidText.isEmpty()) {
        return false;
    }
    CLSID clsid{};
    if (FAILED(CLSIDFromString(reinterpret_cast<LPCOLESTR>(clsidText.utf16()), &clsid))) {
        return false;
    }

    ensureFxHostKey(slot);
    auto inst = std::make_shared<Instance>();
    inst->clsid = clsid;
    inst->clsidText = clsidText;
    inst->displayName = desc.name.isEmpty() ? slot->displayName : desc.name;
    inst->pendingState = slot->state;

    m_instances.insert(slot->hostKey, inst);
    slot->format = PluginFormat::DirectShow;
    slot->pluginId = makePluginId(clsidText);
    if (slot->displayName.isEmpty()) {
        slot->displayName = inst->displayName;
    }
    return true;
}

void SoundForgeDsHost::releaseInstance(FxSlot *slot)
{
    if (!slot || slot->hostKey.isEmpty()) {
        return;
    }
    if (auto inst = m_instances.value(slot->hostKey)) {
        QMutexLocker lock(&inst->mutex);
        inst->closePage();
    }
    m_instances.remove(slot->hostKey);
}

void SoundForgeDsHost::prepare(FxSlot *slot, double sampleRate, int blockSize)
{
    auto inst = lookup(slot);
    if (!inst) {
        return;
    }
    QMutexLocker lock(&inst->mutex);
    const bool rateChanged = sampleRate > 0 && !qFuzzyCompare(inst->sampleRate, sampleRate);
    const bool blockChanged = blockSize > 0 && blockSize != inst->blockSize;
    if (!rateChanged && !blockChanged) {
        return;
    }
    inst->retireEffect();
    if (rateChanged) {
        inst->sampleRate = sampleRate;
    }
    if (blockChanged) {
        inst->blockSize = blockSize;
    }
    inst->graph.teardown();
    inst->graphFailed = false;
}

void SoundForgeDsHost::reset(FxSlot *slot)
{
    auto inst = lookup(slot);
    if (!inst) {
        return;
    }
    QMutexLocker lock(&inst->mutex);
    inst->graph.flush();
}

void SoundForgeDsHost::process(FxSlot *slot, float **in, float **out, int channels, int frames)
{
    if (!in || !out || channels <= 0 || frames <= 0) {
        return;
    }
    auto passThrough = [&]() {
        for (int c = 0; c < channels; ++c) {
            if (in[c] && out[c] && in[c] != out[c]) {
                std::memcpy(out[c], in[c], size_t(frames) * sizeof(float));
            }
        }
    };

    auto inst = lookup(slot);
    if (!inst) {
        passThrough();
        return;
    }
    QMutexLocker lock(&inst->mutex);
    if (!inst->ensureGraph(channels)) {
        passThrough();
        return;
    }

    const size_t needed = size_t(frames) * size_t(channels);
    inst->scratchIn.resize(needed);
    for (int c = 0; c < channels; ++c) {
        const float *src = in[c];
        for (int i = 0; i < frames; ++i) {
            inst->scratchIn[size_t(i) * size_t(channels) + size_t(c)] = src ? src[i] : 0.0f;
        }
    }

    // Feed in allocator-sized chunks; the effect answers synchronously into the FIFO.
    int offset = 0;
    bool ok = true;
    while (offset < frames && ok) {
        const int chunk = std::min(frames - offset, inst->graph.blockFrames);
        ok = inst->graph.pushBlock(inst->scratchIn.data() + size_t(offset) * size_t(channels),
                                   chunk);
        offset += chunk;
    }
    if (!ok) {
        passThrough();
        return;
    }

    std::vector<float> &fifo = inst->graph.sink.pin.fifo;
    if (fifo.size() < needed) {
        // Effect is still priming: emit what exists and pad the rest with silence
        // rather than handing back a half-filled buffer.
        const size_t have = fifo.size();
        for (int c = 0; c < channels; ++c) {
            if (!out[c]) {
                continue;
            }
            for (int i = 0; i < frames; ++i) {
                const size_t idx = size_t(i) * size_t(channels) + size_t(c);
                out[c][i] = idx < have ? fifo[idx] : 0.0f;
            }
        }
        fifo.clear();
        return;
    }
    for (int c = 0; c < channels; ++c) {
        if (!out[c]) {
            continue;
        }
        for (int i = 0; i < frames; ++i) {
            out[c][i] = fifo[size_t(i) * size_t(channels) + size_t(c)];
        }
    }
    fifo.erase(fifo.begin(), fifo.begin() + long(needed));

    // Measured behaviour is one output block per input block, but an effect that
    // returned more would otherwise grow this buffer — and the added latency — without
    // bound. Keep a few blocks of slack and drop the oldest beyond that.
    const size_t cap = needed * 4;
    if (fifo.size() > cap) {
        fifo.erase(fifo.begin(), fifo.begin() + long(fifo.size() - cap));
    }
}

bool SoundForgeDsHost::openEditor(FxSlot *slot, QWidget *parent)
{
    auto inst = lookup(slot);
    if (!inst) {
        return false;
    }
    QMutexLocker lock(&inst->mutex);
    if (!parent) {
        return false;
    }
    // Reuse the layout the audio path already negotiated; rebuilding it just to open a
    // dialog would replace the very object the page is about to attach to.
    if (!inst->ensureGraph(inst->graph.running ? inst->graph.channels : 2)) {
        return false;
    }
    inst->closePage();

    ISpecifyPropertyPages *spec = nullptr;
    if (FAILED(inst->graph.effect->QueryInterface(IID_ISpecifyPropertyPages,
                                                  reinterpret_cast<void **>(&spec)))
        || !spec) {
        return false;
    }
    CAUUID pages{};
    const bool gotPages = SUCCEEDED(spec->GetPages(&pages)) && pages.cElems > 0;
    spec->Release();
    if (!gotPages) {
        return false;
    }

    // Host every page the effect offers. One page fills the widget; several become tabs,
    // which is how VEGAS presents these same dialogs.
    QWidget *container = nullptr;
    QTabWidget *tabs = nullptr;
    if (!parent->layout()) {
        auto *lay = new QVBoxLayout(parent);
        lay->setContentsMargins(0, 0, 0, 0);
    }
    if (pages.cElems > 1) {
        tabs = new QTabWidget(parent);
        container = tabs;
    }

    IUnknown *unk = nullptr;
    inst->graph.effect->QueryInterface(IID_IUnknown, reinterpret_cast<void **>(&unk));

    int widest = 0;
    int tallest = 0;
    for (ULONG i = 0; i < pages.cElems; ++i) {
        IPropertyPage *page = nullptr;
        if (FAILED(CoCreateInstance(pages.pElems[i], nullptr, CLSCTX_INPROC_SERVER,
                                    IID_IPropertyPage, reinterpret_cast<void **>(&page)))
            || !page) {
            continue; // a page that will not instantiate simply does not appear
        }

        auto *site = new PropertyPageSite;
        site->page = page;
        page->SetPageSite(static_cast<IPropertyPageSite *>(site));
        if (FAILED(page->SetObjects(1, &unk))) {
            page->SetPageSite(nullptr);
            page->Release();
            delete site;
            continue;
        }

        PROPPAGEINFO info{};
        info.cb = sizeof(info);
        QSize pageSize(360, 240);
        QString title = QStringLiteral("Page %1").arg(i + 1);
        if (SUCCEEDED(page->GetPageInfo(&info))) {
            if (info.size.cx > 0 && info.size.cy > 0) {
                pageSize = QSize(int(info.size.cx), int(info.size.cy));
            }
            if (info.pszTitle) {
                title = QString::fromWCharArray(info.pszTitle);
                CoTaskMemFree(info.pszTitle);
            }
            if (info.pszDocString) {
                CoTaskMemFree(info.pszDocString);
            }
            if (info.pszHelpFile) {
                CoTaskMemFree(info.pszHelpFile);
            }
        }
        widest = std::max(widest, pageSize.width());
        tallest = std::max(tallest, pageSize.height());

        // Each page needs its own native window to draw into.
        auto *host = new QWidget(tabs ? static_cast<QWidget *>(tabs) : parent);
        host->setMinimumSize(pageSize);
        host->setAttribute(Qt::WA_NativeWindow);
        const HWND hwnd = reinterpret_cast<HWND>(host->winId());

        RECT rect{0, 0, pageSize.width(), pageSize.height()};
        if (FAILED(page->Activate(hwnd, &rect, FALSE))) {
            page->SetPageSite(nullptr);
            page->SetObjects(0, nullptr);
            page->Release();
            delete site;
            delete host;
            continue;
        }
        page->Show(SW_SHOW);

        inst->pages.push_back(page);
        inst->sites.push_back(site);
        if (tabs) {
            tabs->addTab(host, title);
        } else {
            container = host;
        }
    }
    if (unk) {
        unk->Release();
    }
    CoTaskMemFree(pages.pElems);

    if (inst->pages.isEmpty()) {
        delete container;
        return false;
    }

    if (widest > 0 && tallest > 0) {
        // Property pages are fixed-size dialogs; give the surrounding widget the room
        // they expect rather than letting the layout clip them.
        const int chromeH = tabs ? 32 : 0;
        parent->setMinimumSize(widest, tallest + chromeH);
    }
    parent->layout()->addWidget(container);
    container->show();
    inst->pageContainer = container;

    // The page draws into the widget's HWND, so it must go down with the widget.
    QObject::connect(parent, &QObject::destroyed, parent, [inst]() {
        QMutexLocker guard(&inst->mutex);
        inst->closePage();
    });

    if (slot) {
        slot->state = inst->captureState();
    }
    return true;
}

QByteArray SoundForgeDsHost::getState(const FxSlot *slot) const
{
    auto inst = lookup(slot);
    if (!inst) {
        return slot ? slot->state : QByteArray{};
    }
    QMutexLocker lock(&inst->mutex);
    const QByteArray blob = inst->captureState();
    return blob.isEmpty() && slot ? slot->state : blob;
}

bool SoundForgeDsHost::setState(FxSlot *slot, const QByteArray &state)
{
    if (!slot) {
        return false;
    }
    slot->state = state;
    auto inst = lookup(slot);
    if (!inst) {
        return true;
    }
    QMutexLocker lock(&inst->mutex);
    inst->pendingState = state;
    if (inst->graph.effect) {
        inst->applyState(state);
    }
    return true;
}

QStringList SoundForgeDsHost::propertyPageTitles(const QString &clsidText)
{
    const QString text = clsidText.startsWith(QLatin1Char('{'))
                             ? clsidText
                             : clsidFromPluginId(clsidText);
    CLSID clsid{};
    if (text.isEmpty()
        || FAILED(CLSIDFromString(reinterpret_cast<LPCOLESTR>(text.utf16()), &clsid))) {
        return {};
    }
    ensureComOnThread();

    IBaseFilter *effect = nullptr;
    if (FAILED(CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, IID_IBaseFilter,
                                reinterpret_cast<void **>(&effect)))
        || !effect) {
        return {};
    }
    QStringList titles;
    ISpecifyPropertyPages *spec = nullptr;
    if (SUCCEEDED(effect->QueryInterface(IID_ISpecifyPropertyPages,
                                         reinterpret_cast<void **>(&spec)))
        && spec) {
        CAUUID pages{};
        if (SUCCEEDED(spec->GetPages(&pages))) {
            for (ULONG i = 0; i < pages.cElems; ++i) {
                IPropertyPage *page = nullptr;
                if (FAILED(CoCreateInstance(pages.pElems[i], nullptr, CLSCTX_INPROC_SERVER,
                                            IID_IPropertyPage,
                                            reinterpret_cast<void **>(&page)))
                    || !page) {
                    continue;
                }
                PROPPAGEINFO info{};
                info.cb = sizeof(info);
                if (SUCCEEDED(page->GetPageInfo(&info))) {
                    if (info.pszTitle) {
                        titles << QString::fromWCharArray(info.pszTitle);
                        CoTaskMemFree(info.pszTitle);
                    }
                    if (info.pszDocString) {
                        CoTaskMemFree(info.pszDocString);
                    }
                    if (info.pszHelpFile) {
                        CoTaskMemFree(info.pszHelpFile);
                    }
                }
                page->Release();
            }
            CoTaskMemFree(pages.pElems);
        }
        spec->Release();
    }
    effect->Release();
    return titles;
}

bool SoundForgeDsHost::probeProcess(const QString &clsidText, double *meanDiffOut,
                                    QString *errorOut)
{
    CLSID clsid{};
    const QString text = clsidText.startsWith(QLatin1Char('{')) ? clsidText
                                                                : clsidFromPluginId(clsidText);
    if (text.isEmpty()
        || FAILED(CLSIDFromString(reinterpret_cast<LPCOLESTR>(text.utf16()), &clsid))) {
        if (errorOut) {
            *errorOut = QStringLiteral("not a CLSID: %1").arg(clsidText);
        }
        return false;
    }

    ensureComOnThread();
    Graph graph;
    const int frames = 4096;
    const int channels = 2;
    if (!graph.build(clsid, 44100.0, channels, frames, errorOut)) {
        return false;
    }

    std::vector<float> input(size_t(frames) * channels);
    double phase = 0.0;
    for (int i = 0; i < frames; ++i) {
        const auto value = float(0.5 * std::sin(phase));
        phase += 2.0 * M_PI * 440.0 / 44100.0;
        for (int c = 0; c < channels; ++c) {
            input[size_t(i) * channels + size_t(c)] = value;
        }
    }
    // A few blocks: one is not always enough for an effect with internal latency.
    for (int block = 0; block < 4; ++block) {
        if (!graph.pushBlock(input.data(), frames)) {
            if (errorOut) {
                *errorOut = QStringLiteral("effect rejected the sample buffer");
            }
            return false;
        }
    }

    const std::vector<float> &fifo = graph.sink.pin.fifo;
    if (fifo.empty()) {
        if (errorOut) {
            *errorOut = QStringLiteral("effect produced no output");
        }
        return false;
    }
    const size_t n = std::min(fifo.size(), input.size());
    double diff = 0.0;
    for (size_t i = 0; i < n; ++i) {
        diff += std::fabs(double(fifo[i]) - double(input[i]));
    }
    if (meanDiffOut) {
        *meanDiffOut = n ? diff / double(n) : 0.0;
    }
    return true;
}

#endif // Q_OS_WIN

} // namespace openvegas
