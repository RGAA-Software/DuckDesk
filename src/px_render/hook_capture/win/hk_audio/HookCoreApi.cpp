#include "HookCoreApi.h"

#include <Windows.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

#include "AudioHookCommon.h"
#include "AudioShare.h"
#include "HookDirectSound.h"
#include "HookWaveOut.h"
#include "HookXAudio2.h"
#include "hk_obs/hook_manager.h"
#include "px_common_new/log.h"

namespace tc {
namespace {

struct ComVtable {
    void* func[32];
};
struct ComObj {
    ComVtable* vtbl;
};

using FuncActivate = HRESULT(STDMETHODCALLTYPE*)(IMMDevice*, REFIID, DWORD, PROPVARIANT*, void**);
using FuncGetService = HRESULT(STDMETHODCALLTYPE*)(IAudioClient*, REFIID, void**);
using FuncGetBuffer = HRESULT(STDMETHODCALLTYPE*)(IAudioRenderClient*, UINT32, BYTE**);
using FuncReleaseBuffer = HRESULT(STDMETHODCALLTYPE*)(IAudioRenderClient*, UINT32, DWORD);
using FuncInitialize = HRESULT(STDMETHODCALLTYPE*)(IAudioClient*,
                                                   AUDCLNT_SHAREMODE,
                                                   DWORD,
                                                   REFERENCE_TIME,
                                                   REFERENCE_TIME,
                                                   const WAVEFORMATEX*,
                                                   LPCGUID);
using FuncEnumEndpoints = HRESULT(STDMETHODCALLTYPE*)(IMMDeviceEnumerator*,
                                                      EDataFlow,
                                                      DWORD,
                                                      IMMDeviceCollection**);
using FuncGetDefault = HRESULT(STDMETHODCALLTYPE*)(IMMDeviceEnumerator*,
                                                   EDataFlow,
                                                   ERole,
                                                   IMMDevice**);

// Per-vtable originals. Never DetourAttach COM methods — double-hooking
// (vtable patch + Detours) was silencing the whole process audio path.
std::mutex g_vtbl_mu;
std::unordered_map<void*, FuncActivate> g_orig_activate;
std::unordered_map<void*, FuncInitialize> g_orig_initialize;
std::unordered_map<void*, FuncGetService> g_orig_getservice;
std::unordered_map<void*, FuncGetBuffer> g_orig_getbuffer;
std::unordered_map<void*, FuncReleaseBuffer> g_orig_releasebuffer;
std::unordered_set<void*> g_hooked_enum_vtbls;

// Probe fallback formats keyed by IAudioRenderClient VTABLE (the vtable is
// shared per COM class). Clients created BEFORE injection never pass through
// Hook_Initialize / Hook_GetService, so render_formats_ has nothing for them;
// the probe mix format (system mix, usually 48k/2ch/f32) is cached here as a
// last resort. Estimation risk is observable via g_rb_probe_fmt logs.
// Guarded by g_vtbl_mu. Never cleared by probe cleanup; cleared in Shutdown.
std::unordered_map<void*, HookCoreApi::StreamFormat> g_probe_fmt_by_vtbl;

FuncEnumEndpoints origin_EnumAudioEndpoints = nullptr;
FuncGetDefault origin_GetDefaultAudioEndpoint = nullptr;

std::atomic<uint64_t> g_rb_calls{0};
std::atomic<uint64_t> g_rb_posted{0};
std::atomic<uint64_t> g_gb_calls{0};
std::atomic<uint64_t> g_rb_null_buf{0};
std::atomic<uint64_t> g_rb_silent{0};
std::atomic<uint64_t> g_rb_bad_fmt{0};
std::atomic<uint64_t> g_rb_probe_fmt{0};
std::atomic<uint64_t> g_activate_count{0};
std::atomic<uint64_t> g_init_count{0};
thread_local char* g_tls_render_buf = nullptr;
thread_local IAudioRenderClient* g_tls_render_client = nullptr;

bool PatchSlot(void** slot, void* detour) {
    if (!slot || !detour || !*slot) {
        return false;
    }
    if (*slot == detour) {
        return true;
    }
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) {
        return false;
    }
    *slot = detour;
    VirtualProtect(slot, sizeof(void*), old, &old);
    return true;
}

// Undo PatchSlot: only writes back when the slot still points at our detour.
void RestoreSlot(void** slot, void* detour, void* orig) {
    if (!slot || !detour || !orig) {
        return;
    }
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) {
        return;
    }
    if (*slot == detour) {
        *slot = orig;
    }
    VirtualProtect(slot, sizeof(void*), old, &old);
}

HookCoreApi::StreamFormat ParseFormat(const WAVEFORMATEX* format) {
    HookCoreApi::StreamFormat sf{};
    if (!format) {
        return sf;
    }
    sf.samples = static_cast<int>(format->nSamplesPerSec);
    sf.channels = static_cast<int>(format->nChannels);
    sf.bits = static_cast<int>(format->wBitsPerSample);
    sf.block_align = static_cast<int>(format->nBlockAlign);
    // Unfolds WAVEFORMATEXTENSIBLE SubFormat; unsupported layouts stay invalid.
    sf.valid = ResolveWaveFormat(format, sf.format);
    if (sf.block_align <= 0 && sf.channels > 0 && sf.bits > 0) {
        sf.block_align = sf.channels * (sf.bits / 8);
    }
    return sf;
}

HRESULT STDMETHODCALLTYPE Hook_Activate(IMMDevice*, REFIID, DWORD, PROPVARIANT*, void**);
HRESULT STDMETHODCALLTYPE Hook_Initialize(IAudioClient*,
                                          AUDCLNT_SHAREMODE,
                                          DWORD,
                                          REFERENCE_TIME,
                                          REFERENCE_TIME,
                                          const WAVEFORMATEX*,
                                          LPCGUID);
HRESULT STDMETHODCALLTYPE Hook_GetService(IAudioClient*, REFIID, void**);
HRESULT STDMETHODCALLTYPE Hook_GetBuffer(IAudioRenderClient*, UINT32, BYTE**);
HRESULT STDMETHODCALLTYPE Hook_ReleaseBuffer(IAudioRenderClient*, UINT32, DWORD);

void EnsureDeviceActivateHooked(IMMDevice* device) {
    if (!device) {
        return;
    }
    auto* obj = reinterpret_cast<ComObj*>(device);
    if (!obj || !obj->vtbl) {
        return;
    }
    std::lock_guard lock(g_vtbl_mu);
    if (g_orig_activate.count(obj->vtbl)) {
        return;
    }
    auto* slot = &obj->vtbl->func[3];
    if (!*slot || *slot == reinterpret_cast<void*>(&Hook_Activate)) {
        return;
    }
    g_orig_activate[obj->vtbl] = reinterpret_cast<FuncActivate>(*slot);
    PatchSlot(slot, reinterpret_cast<void*>(&Hook_Activate));
}

void EnsureAudioClientHooked(IAudioClient* client) {
    if (!client) {
        return;
    }
    auto* obj = reinterpret_cast<ComObj*>(client);
    if (!obj || !obj->vtbl) {
        return;
    }
    std::lock_guard lock(g_vtbl_mu);
    if (!g_orig_initialize.count(obj->vtbl)) {
        auto* slot = &obj->vtbl->func[3];
        if (*slot && *slot != reinterpret_cast<void*>(&Hook_Initialize)) {
            g_orig_initialize[obj->vtbl] = reinterpret_cast<FuncInitialize>(*slot);
            PatchSlot(slot, reinterpret_cast<void*>(&Hook_Initialize));
        }
    }
    if (!g_orig_getservice.count(obj->vtbl)) {
        auto* slot = &obj->vtbl->func[14];
        if (*slot && *slot != reinterpret_cast<void*>(&Hook_GetService)) {
            g_orig_getservice[obj->vtbl] = reinterpret_cast<FuncGetService>(*slot);
            PatchSlot(slot, reinterpret_cast<void*>(&Hook_GetService));
        }
    }
}

HRESULT STDMETHODCALLTYPE Hook_GetBuffer(IAudioRenderClient* thiz,
                                         UINT32 num_frames,
                                         BYTE** pp_data) {
    FuncGetBuffer orig = nullptr;
    {
        auto* obj = reinterpret_cast<ComObj*>(thiz);
        std::lock_guard lock(g_vtbl_mu);
        if (obj && obj->vtbl) {
            auto it = g_orig_getbuffer.find(obj->vtbl);
            if (it != g_orig_getbuffer.end()) {
                orig = it->second;
            }
        }
    }
    if (!orig) {
        return E_POINTER;
    }
    auto hr = orig(thiz, num_frames, pp_data);
    g_gb_calls.fetch_add(1, std::memory_order_relaxed);
    if (SUCCEEDED(hr) && pp_data && *pp_data) {
        auto* p = reinterpret_cast<char*>(*pp_data);
        g_tls_render_buf = p;
        g_tls_render_client = thiz;
        // Prefer TLS for the hot path; map is a cross-thread fallback only.
        auto* api = HookCoreApi::Instance();
        if (api->state_mu_.try_lock()) {
            api->render_buffers_[thiz] = p;
            api->state_mu_.unlock();
        }
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE Hook_ReleaseBuffer(IAudioRenderClient* thiz,
                                             UINT32 num_frames,
                                             DWORD flags) {
    auto* api = HookCoreApi::Instance();
    g_rb_calls.fetch_add(1, std::memory_order_relaxed);

    char* buf = nullptr;
    HookCoreApi::StreamFormat sf{};
    {
        std::lock_guard lock(api->state_mu_);
        auto bit = api->render_buffers_.find(thiz);
        if (bit != api->render_buffers_.end()) {
            buf = bit->second;
            api->render_buffers_.erase(bit);
        }
        auto fit = api->render_formats_.find(thiz);
        if (fit != api->render_formats_.end()) {
            sf = fit->second;
        }
        // No per-client format: probe-vtable fallback below; if still invalid
        // the frame is dropped, never guessed.
    }
    if (!sf.valid) {
        // Client created before injection (never saw Hook_Initialize/GetService):
        // fall back to the probe mix format cached for this client's vtable.
        auto* obj = reinterpret_cast<ComObj*>(thiz);
        std::lock_guard lock(g_vtbl_mu);
        if (obj && obj->vtbl) {
            auto it = g_probe_fmt_by_vtbl.find(obj->vtbl);
            if (it != g_probe_fmt_by_vtbl.end() && it->second.valid) {
                sf = it->second;
                const auto n = g_rb_probe_fmt.fetch_add(1, std::memory_order_relaxed) + 1;
                if (n == 1 || (n % 500) == 0) {
                    LOGW("WASAPI ReleaseBuffer: probe fallback fmt for pre-injection client {} "
                         "n={} ({}Hz {}ch {}bit)",
                         static_cast<const void*>(thiz), n, sf.samples, sf.channels, sf.bits);
                }
            }
        }
    }
    if (!buf && g_tls_render_client == thiz) {
        buf = g_tls_render_buf;
    }
    g_tls_render_buf = nullptr;
    g_tls_render_client = nullptr;

    const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
    if (silent) {
        g_rb_silent.fetch_add(1, std::memory_order_relaxed);
    }
    if (!buf) {
        g_rb_null_buf.fetch_add(1, std::memory_order_relaxed);
    }

    float peak = 0.f;
    if (buf && num_frames > 0 && !silent && !sf.valid) {
        // Format unknown/unsupported — drop instead of reading with a guessed layout.
        const auto bad = g_rb_bad_fmt.fetch_add(1, std::memory_order_relaxed) + 1;
        if (bad == 1 || (bad % 500) == 0) {
            LOGW("WASAPI ReleaseBuffer: drop frame, unknown format n={}", bad);
        }
    }
    if (buf && num_frames > 0 && !silent && sf.valid && sf.block_align > 0) {
        const int bytes = static_cast<int>(num_frames) * sf.block_align;
        if (bytes > 0) {
            peak = BufferPeak(buf, (std::min)(bytes, 2048), sf.format);
            if (peak > 1.0e-5f) {
                NoteWasapiCapture();
                PushHookedPcm(buf, bytes, sf.format, sf.samples, sf.channels, "WASAPI");
                g_rb_posted.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    const auto n = g_rb_calls.load(std::memory_order_relaxed);
    if (n == 1 || (n % 500) == 0) {
        size_t clients = 0;
        {
            std::lock_guard lock(api->state_mu_);
            clients = api->render_formats_.size();
        }
        LOGI("WASAPI ReleaseBuffer: calls={} posted={} null_buf={} silent={} "
             "getbuffer={} probe_fmt={} peak={:.6f} clients={}",
             n, g_rb_posted.load(std::memory_order_relaxed),
             g_rb_null_buf.load(std::memory_order_relaxed),
             g_rb_silent.load(std::memory_order_relaxed),
             g_gb_calls.load(std::memory_order_relaxed),
             g_rb_probe_fmt.load(std::memory_order_relaxed), peak, clients);
    }

    FuncReleaseBuffer orig = nullptr;
    {
        auto* obj = reinterpret_cast<ComObj*>(thiz);
        std::lock_guard lock(g_vtbl_mu);
        if (obj && obj->vtbl) {
            auto it = g_orig_releasebuffer.find(obj->vtbl);
            if (it != g_orig_releasebuffer.end()) {
                orig = it->second;
            }
        }
    }
    if (!orig) {
        return E_POINTER;
    }
    return orig(thiz, num_frames, flags);
}

HRESULT STDMETHODCALLTYPE Hook_Initialize(IAudioClient* thiz,
                                          AUDCLNT_SHAREMODE share_mode,
                                          DWORD stream_flags,
                                          REFERENCE_TIME buffer_duration,
                                          REFERENCE_TIME periodicity,
                                          const WAVEFORMATEX* format,
                                          LPCGUID session) {
    FuncInitialize orig = nullptr;
    {
        auto* obj = reinterpret_cast<ComObj*>(thiz);
        std::lock_guard lock(g_vtbl_mu);
        if (obj && obj->vtbl) {
            auto it = g_orig_initialize.find(obj->vtbl);
            if (it != g_orig_initialize.end()) {
                orig = it->second;
            }
        }
    }
    if (!orig) {
        return E_POINTER;
    }
    auto hr = orig(thiz, share_mode, stream_flags, buffer_duration, periodicity, format, session);
    if (SUCCEEDED(hr) && format) {
        auto* api = HookCoreApi::Instance();
        auto sf = ParseFormat(format);
        {
            std::lock_guard lock(api->state_mu_);
            api->client_formats_[thiz] = sf;
        }
        const auto n = g_init_count.fetch_add(1) + 1;
        LOGI("WASAPI Initialize #{}: {}Hz {}ch {}bit share={} flags=0x{:x} client={}", n,
             sf.samples, sf.channels, sf.bits, static_cast<int>(share_mode), stream_flags,
             static_cast<const void*>(thiz));
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE Hook_GetService(IAudioClient* thiz, REFIID riid, void** ppv) {
    FuncGetService orig = nullptr;
    {
        auto* obj = reinterpret_cast<ComObj*>(thiz);
        std::lock_guard lock(g_vtbl_mu);
        if (obj && obj->vtbl) {
            auto it = g_orig_getservice.find(obj->vtbl);
            if (it != g_orig_getservice.end()) {
                orig = it->second;
            }
        }
    }
    if (!orig) {
        return E_POINTER;
    }
    auto hr = orig(thiz, riid, ppv);
    if (FAILED(hr) || !ppv || !*ppv) {
        return hr;
    }
    if (riid == __uuidof(IAudioRenderClient)) {
        auto* api = HookCoreApi::Instance();
        auto* rc = reinterpret_cast<IAudioRenderClient*>(*ppv);
        HookCoreApi::StreamFormat sf{};
        {
            std::lock_guard lock(api->state_mu_);
            auto it = api->client_formats_.find(thiz);
            if (it != api->client_formats_.end()) {
                sf = it->second;
            } else {
                // No Initialize seen for this client — keep sf invalid so
                // ReleaseBuffer drops frames instead of guessing 48k/f32.
                static std::atomic<uint64_t> s_no_fmt{0};
                const auto n = s_no_fmt.fetch_add(1, std::memory_order_relaxed) + 1;
                if (n == 1 || (n % 200) == 0) {
                    LOGW("WASAPI GetService: no client format for {} n={}",
                         static_cast<const void*>(thiz), n);
                }
            }
            api->render_formats_[rc] = sf;
        }
        api->PatchRenderClientVtable(rc);
        LOGI("WASAPI GetService(IAudioRenderClient) rc={} {}Hz {}ch ({} clients)",
             static_cast<const void*>(rc), sf.samples, sf.channels, api->render_formats_.size());
    }
    return hr;
}

bool IsAudioClientIid(REFIID iid) {
    return iid == __uuidof(IAudioClient) || iid == __uuidof(IAudioClient2) ||
           iid == __uuidof(IAudioClient3);
}

HRESULT STDMETHODCALLTYPE Hook_Activate(IMMDevice* thiz,
                                        REFIID iid,
                                        DWORD cls_ctx,
                                        PROPVARIANT* params,
                                        void** pp) {
    FuncActivate orig = nullptr;
    {
        auto* obj = reinterpret_cast<ComObj*>(thiz);
        std::lock_guard lock(g_vtbl_mu);
        if (obj && obj->vtbl) {
            auto it = g_orig_activate.find(obj->vtbl);
            if (it != g_orig_activate.end()) {
                orig = it->second;
            }
        }
    }
    if (!orig) {
        return E_POINTER;
    }
    auto hr = orig(thiz, iid, cls_ctx, params, pp);
    if (SUCCEEDED(hr) && pp && *pp && IsAudioClientIid(iid)) {
        const auto n = g_activate_count.fetch_add(1) + 1;
        LOGI("WASAPI Activate(IAudioClient*) #{} device={}", n, static_cast<const void*>(thiz));
        EnsureAudioClientHooked(reinterpret_cast<IAudioClient*>(*pp));
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE Hook_GetDefaultAudioEndpoint(IMMDeviceEnumerator* thiz,
                                                       EDataFlow flow,
                                                       ERole role,
                                                       IMMDevice** pp) {
    if (!origin_GetDefaultAudioEndpoint) {
        return E_POINTER;
    }
    auto hr = origin_GetDefaultAudioEndpoint(thiz, flow, role, pp);
    if (SUCCEEDED(hr) && pp && *pp) {
        EnsureDeviceActivateHooked(*pp);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE Hook_EnumAudioEndpoints(IMMDeviceEnumerator* thiz,
                                                  EDataFlow flow,
                                                  DWORD state,
                                                  IMMDeviceCollection** pp) {
    if (!origin_EnumAudioEndpoints) {
        return E_POINTER;
    }
    auto hr = origin_EnumAudioEndpoints(thiz, flow, state, pp);
    if (SUCCEEDED(hr) && pp && *pp && flow == eRender) {
        UINT count = 0;
        (*pp)->GetCount(&count);
        for (UINT i = 0; i < count; i++) {
            IMMDevice* dev = nullptr;
            if (SUCCEEDED((*pp)->Item(i, &dev)) && dev) {
                EnsureDeviceActivateHooked(dev);
                dev->Release();
            }
        }
        LOGI("WASAPI EnumAudioEndpoints: Activate hooked on {} render devices", count);
    }
    return hr;
}

void EnsureEnumeratorHooked(IMMDeviceEnumerator* enumerator) {
    if (!enumerator) {
        return;
    }
    auto* obj = reinterpret_cast<ComObj*>(enumerator);
    if (!obj || !obj->vtbl) {
        return;
    }
    std::lock_guard lock(g_vtbl_mu);
    if (g_hooked_enum_vtbls.count(obj->vtbl)) {
        return;
    }
    if (!origin_EnumAudioEndpoints) {
        origin_EnumAudioEndpoints = reinterpret_cast<FuncEnumEndpoints>(obj->vtbl->func[3]);
    }
    if (!origin_GetDefaultAudioEndpoint) {
        origin_GetDefaultAudioEndpoint = reinterpret_cast<FuncGetDefault>(obj->vtbl->func[4]);
    }
    PatchSlot(&obj->vtbl->func[3], reinterpret_cast<void*>(&Hook_EnumAudioEndpoints));
    PatchSlot(&obj->vtbl->func[4], reinterpret_cast<void*>(&Hook_GetDefaultAudioEndpoint));
    g_hooked_enum_vtbls.insert(obj->vtbl);
}

// Conservative probe: open a shared-mode stream just far enough to resolve
// the IAudioRenderClient vtable and patch it (the vtable is shared per COM
// class, so render clients the game created BEFORE injection get hooked too),
// then close everything. Deliberately minimal: shared mode, 0 flags — no
// EVENTCALLBACK, no LOOPBACK, no exclusive, and the stream is never Started.
// (An earlier probe silenced the game because it kept a running stream /
// used the wrong flags; Initialize+GetService+Release is what any normal app
// does when opening and immediately closing a shared stream.)
void ProbeRenderClientVtable(IAudioClient* ac) {
    if (!ac) {
        return;
    }
    auto* api = HookCoreApi::Instance();
    WAVEFORMATEX* mix = nullptr;
    HRESULT hr = ac->GetMixFormat(&mix);
    if (FAILED(hr) || !mix) {
        LOGE("AudioHook probe: GetMixFormat failed 0x{:08x}", static_cast<unsigned>(hr));
        return;
    }
    // Cache the mix format by render-client vtable below so pre-injection
    // clients (no per-client format) can fall back to it in ReleaseBuffer.
    const auto probe_sf = ParseFormat(mix);
    hr = ac->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 0, 0, mix, nullptr);
    CoTaskMemFree(mix);
    if (FAILED(hr)) {
        LOGE("AudioHook probe: Initialize failed 0x{:08x}", static_cast<unsigned>(hr));
        return;
    }
    IAudioRenderClient* rc = nullptr;
    hr = ac->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(&rc));
    if (SUCCEEDED(hr) && rc) {
        api->PatchRenderClientVtable(rc);
        if (probe_sf.valid) {
            auto* obj = reinterpret_cast<ComObj*>(rc);
            if (obj && obj->vtbl) {
                std::lock_guard lock(g_vtbl_mu);
                g_probe_fmt_by_vtbl[obj->vtbl] = probe_sf;
            }
        }
        rc->Release();
        LOGI("AudioHook probe: RenderClient vtable patched via probe rc={} fmt_valid={}",
             static_cast<const void*>(rc), probe_sf.valid);
    } else {
        LOGE("AudioHook probe: GetService(IAudioRenderClient) failed 0x{:08x}",
             static_cast<unsigned>(hr));
    }
    // Drop probe leftovers so dead pointers do not linger in the state maps
    // (the probe client/render client are released right after). The per-vtable
    // fallback cache (g_probe_fmt_by_vtbl) deliberately survives this cleanup.
    {
        std::lock_guard lock(api->state_mu_);
        if (rc) {
            api->render_formats_.erase(rc);
            api->render_buffers_.erase(rc);
        }
        api->client_formats_.erase(ac);
    }
}

}  // namespace

void HookCoreApi::PatchRenderClientVtable(IAudioRenderClient* rc) {
    if (!rc) {
        return;
    }
    auto* obj = reinterpret_cast<ComObj*>(rc);
    if (!obj || !obj->vtbl) {
        return;
    }
    std::lock_guard lock(g_vtbl_mu);
    if (g_orig_getbuffer.count(obj->vtbl)) {
        return;
    }
    auto* gb_slot = &obj->vtbl->func[3];
    auto* rb_slot = &obj->vtbl->func[4];
    if (!*gb_slot || !*rb_slot) {
        return;
    }
    if (*gb_slot == reinterpret_cast<void*>(&Hook_GetBuffer)) {
        return;
    }
    g_orig_getbuffer[obj->vtbl] = reinterpret_cast<FuncGetBuffer>(*gb_slot);
    g_orig_releasebuffer[obj->vtbl] = reinterpret_cast<FuncReleaseBuffer>(*rb_slot);
    PatchSlot(gb_slot, reinterpret_cast<void*>(&Hook_GetBuffer));
    PatchSlot(rb_slot, reinterpret_cast<void*>(&Hook_ReleaseBuffer));
    {
        std::lock_guard state_lock(state_mu_);
        patched_render_vtbls_.insert(obj->vtbl);
    }
    LOGI("WASAPI RenderClient vtable patched {}", static_cast<const void*>(obj->vtbl));
}

bool HookCoreApi::Init() {
    if (hooked_) {
        return true;
    }
    audio_share = std::make_shared<AudioShare>();
    audio_share->SetWriteWav(false);  // production: IPC only, no local WAV
    // Live path: dll -> host /ipc (CaptureAudioFrame).
    audio_share->SetIpcSender([](std::string&& msg) {
        auto* hm = HookManager::Instance();
        if (!hm) {
            static std::atomic<uint64_t> s_n{0};
            if (++s_n == 1 || (s_n.load() % 200) == 0) {
                LOGE("AudioHook IPC send: HookManager null n={}", s_n.load());
            }
            return;
        }
        if (!hm->ws_ipc_client_) {
            static std::atomic<uint64_t> s_n{0};
            if (++s_n == 1 || (s_n.load() % 200) == 0) {
                LOGE("AudioHook IPC send: ws_ipc_client_ null n={}", s_n.load());
            }
            return;
        }
        hm->Send(msg);
    });
    mixer = std::make_shared<AudioMixer>(audio_share);
    SetGlobalAudioMixer(mixer);
    LOGI("AudioHook: AudioShare+Mixer ready (wav=off)");

    const bool wasapi_ok = InitWasapiRenderHooks();
    HookXAudio2::Instance()->Start(mixer);
    HookDirectSound::Instance()->Start(mixer);
    HookWaveOut::Instance()->Start(mixer);

    if (!wasapi_ok) {
        LOGE("AudioHook: WASAPI init failed; other API hooks may still work");
    }
    hooked_ = true;
    LOGI("AudioHook: multi-API ready wasapi_ok={} (IPC audio enabled)", wasapi_ok);
    return true;
}

bool HookCoreApi::InitWasapiRenderHooks() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool need_uninit = SUCCEEDED(hr);
    if (FAILED(hr) && hr != S_FALSE && hr != RPC_E_CHANGED_MODE) {
        LOGE("AudioHook CoInitializeEx failed: 0x{:08x}", static_cast<unsigned>(hr));
        return false;
    }

    IMMDeviceEnumerator* enumerator = nullptr;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr) || !enumerator) {
        LOGE("AudioHook enumerator failed: 0x{:08x}", static_cast<unsigned>(hr));
        if (need_uninit) {
            CoUninitialize();
        }
        return false;
    }

    EnsureEnumeratorHooked(enumerator);

    // Hook Activate on every active render device, then run a conservative
    // probe (GetMixFormat + shared-mode Initialize + GetService + Release) so
    // the IAudioRenderClient class vtable is patched even when the game built
    // its audio graph BEFORE we were injected — the lazy Hook_GetService path
    // alone never fires in that case.
    IMMDeviceCollection* coll = nullptr;
    UINT count = 0;
    if (SUCCEEDED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &coll)) && coll) {
        coll->GetCount(&count);
        for (UINT i = 0; i < count; i++) {
            IMMDevice* dev = nullptr;
            if (SUCCEEDED(coll->Item(i, &dev)) && dev) {
                EnsureDeviceActivateHooked(dev);
                IAudioClient* ac = nullptr;
                if (SUCCEEDED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                            reinterpret_cast<void**>(&ac))) &&
                    ac) {
                    EnsureAudioClientHooked(ac);
                    ProbeRenderClientVtable(ac);
                    ac->Release();
                }
                dev->Release();
            }
        }
        coll->Release();
    } else {
        IMMDevice* device = nullptr;
        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)) && device) {
            EnsureDeviceActivateHooked(device);
            IAudioClient* ac = nullptr;
            if (SUCCEEDED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                           reinterpret_cast<void**>(&ac))) &&
                ac) {
                EnsureAudioClientHooked(ac);
                ProbeRenderClientVtable(ac);
                ac->Release();
            }
            device->Release();
            count = 1;
        }
    }

    bool ok = false;
    size_t activate_n = 0;
    size_t client_n = 0;
    size_t render_n = 0;
    {
        std::lock_guard lock(g_vtbl_mu);
        activate_n = g_orig_activate.size();
        client_n = g_orig_initialize.size();
        render_n = g_orig_getbuffer.size();
        // Activate+Initialize hooks are enough to start; RenderClient probe is
        // best-effort on top (games that open streams later still hit the lazy path).
        ok = activate_n > 0 && client_n > 0;
    }
    LOGI("WASAPI multi-device hooks ready ok={} endpoints={} activate_vtbls={} "
         "client_vtbls={} render_vtbls={} (probe)",
         ok, count, activate_n, client_n, render_n);

    enumerator->Release();
    if (need_uninit) {
        CoUninitialize();
    }
    return ok;
}

void HookCoreApi::Shutdown() {
    // Idempotent: after the first run every map below is empty, so repeat
    // calls are no-ops. Called from DllMain DETACH (FreeLibrary path only).
    HookWaveOut::Instance()->Stop();
    HookDirectSound::Instance()->Stop();
    HookXAudio2::Instance()->Stop();

    // Restore every patched vtable slot so no dangling pointer into this DLL
    // survives the unload.
    {
        std::lock_guard lock(g_vtbl_mu);
        for (auto& [vtbl, orig] : g_orig_activate) {
            RestoreSlot(&static_cast<ComVtable*>(vtbl)->func[3],
                        reinterpret_cast<void*>(&Hook_Activate), reinterpret_cast<void*>(orig));
        }
        for (auto& [vtbl, orig] : g_orig_initialize) {
            RestoreSlot(&static_cast<ComVtable*>(vtbl)->func[3],
                        reinterpret_cast<void*>(&Hook_Initialize), reinterpret_cast<void*>(orig));
        }
        for (auto& [vtbl, orig] : g_orig_getservice) {
            RestoreSlot(&static_cast<ComVtable*>(vtbl)->func[14],
                        reinterpret_cast<void*>(&Hook_GetService), reinterpret_cast<void*>(orig));
        }
        for (auto& [vtbl, orig] : g_orig_getbuffer) {
            RestoreSlot(&static_cast<ComVtable*>(vtbl)->func[3],
                        reinterpret_cast<void*>(&Hook_GetBuffer), reinterpret_cast<void*>(orig));
        }
        for (auto& [vtbl, orig] : g_orig_releasebuffer) {
            RestoreSlot(&static_cast<ComVtable*>(vtbl)->func[4],
                        reinterpret_cast<void*>(&Hook_ReleaseBuffer), reinterpret_cast<void*>(orig));
        }
        for (auto* vtbl : g_hooked_enum_vtbls) {
            RestoreSlot(&static_cast<ComVtable*>(vtbl)->func[3],
                        reinterpret_cast<void*>(&Hook_EnumAudioEndpoints),
                        reinterpret_cast<void*>(origin_EnumAudioEndpoints));
            RestoreSlot(&static_cast<ComVtable*>(vtbl)->func[4],
                        reinterpret_cast<void*>(&Hook_GetDefaultAudioEndpoint),
                        reinterpret_cast<void*>(origin_GetDefaultAudioEndpoint));
        }
        g_orig_activate.clear();
        g_orig_initialize.clear();
        g_orig_getservice.clear();
        g_orig_getbuffer.clear();
        g_orig_releasebuffer.clear();
        g_hooked_enum_vtbls.clear();
        g_probe_fmt_by_vtbl.clear();
        origin_EnumAudioEndpoints = nullptr;
        origin_GetDefaultAudioEndpoint = nullptr;
    }

    SetGlobalAudioMixer(nullptr);
    mixer.reset();
    audio_share.reset();
    {
        std::lock_guard lock(state_mu_);
        client_formats_.clear();
        render_formats_.clear();
        render_buffers_.clear();
        patched_render_vtbls_.clear();
    }
    hooked_ = false;
    LOGI("AudioHook: shutdown complete, all vtable patches restored");
}

}  // namespace tc
