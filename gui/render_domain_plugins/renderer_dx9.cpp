#include "render_domain/rd_backend_abi.h"

#ifdef _WIN32
#include <d3d9.h>
#include <windows.h>

#include <cstdint>
#include <iomanip>
#include <new>
#include <sstream>
#include <string>
#endif

namespace {

#ifdef _WIN32

struct Dx9BackendState {
    IDirect3D9* d3d9 = nullptr;
    IDirect3D9Ex* d3d9ex = nullptr;
    IDirect3DDevice9* device = nullptr;
    IDirect3DDevice9Ex* device_ex = nullptr;
    D3DPRESENT_PARAMETERS present_params{};
    HWND window = nullptr;
    rd_frame_stats_v1 frame_stats{};
};

template <typename T>
void release_com(T*& ptr) {
    if (ptr) {
        ptr->Release();
        ptr = nullptr;
    }
}

IDirect3DDevice9* base_device(Dx9BackendState* state) {
    if (!state) return nullptr;
    if (state->device_ex) return static_cast<IDirect3DDevice9*>(state->device_ex);
    return state->device;
}

void fill_present_parameters(D3DPRESENT_PARAMETERS* pp,
                             HWND window,
                             uint32_t width,
                             uint32_t height) {
    if (!pp) return;
    *pp = {};
    pp->Windowed = TRUE;
    pp->hDeviceWindow = window;
    pp->SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp->BackBufferFormat = D3DFMT_UNKNOWN;
    pp->BackBufferWidth = (width == 0u) ? 1u : width;
    pp->BackBufferHeight = (height == 0u) ? 1u : height;
    pp->BackBufferCount = 1;
    pp->EnableAutoDepthStencil = TRUE;
    pp->AutoDepthStencilFormat = D3DFMT_D24S8;
    pp->PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
}

HRESULT create_device_with_d3d9ex(Dx9BackendState* state) {
    if (!state) return E_POINTER;

    const auto create9ex = reinterpret_cast<HRESULT(WINAPI*)(UINT, IDirect3D9Ex**)>(
        GetProcAddress(GetModuleHandleA("d3d9.dll"), "Direct3DCreate9Ex"));
    if (!create9ex) return E_NOTIMPL;

    IDirect3D9Ex* d3d9ex = nullptr;
    HRESULT hr = create9ex(D3D_SDK_VERSION, &d3d9ex);
    if (FAILED(hr) || !d3d9ex) return FAILED(hr) ? hr : E_FAIL;

    state->d3d9ex = d3d9ex;

    DWORD behavior = D3DCREATE_FPU_PRESERVE | D3DCREATE_HARDWARE_VERTEXPROCESSING;
    IDirect3DDevice9Ex* device_ex = nullptr;
    hr = d3d9ex->CreateDeviceEx(D3DADAPTER_DEFAULT,
                                D3DDEVTYPE_HAL,
                                state->window,
                                behavior,
                                &state->present_params,
                                nullptr,
                                &device_ex);
    if (FAILED(hr)) {
        behavior = D3DCREATE_FPU_PRESERVE | D3DCREATE_SOFTWARE_VERTEXPROCESSING;
        hr = d3d9ex->CreateDeviceEx(D3DADAPTER_DEFAULT,
                                    D3DDEVTYPE_HAL,
                                    state->window,
                                    behavior,
                                    &state->present_params,
                                    nullptr,
                                    &device_ex);
    }

    if (FAILED(hr) || !device_ex) {
        release_com(state->d3d9ex);
        return FAILED(hr) ? hr : E_FAIL;
    }

    state->device_ex = device_ex;
    return S_OK;
}

HRESULT create_device_with_d3d9(Dx9BackendState* state) {
    if (!state) return E_POINTER;

    const auto create9 = reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(
        GetProcAddress(GetModuleHandleA("d3d9.dll"), "Direct3DCreate9"));
    if (!create9) return E_NOTIMPL;

    IDirect3D9* d3d9 = create9(D3D_SDK_VERSION);
    if (!d3d9) return E_FAIL;

    state->d3d9 = d3d9;

    DWORD behavior = D3DCREATE_FPU_PRESERVE | D3DCREATE_HARDWARE_VERTEXPROCESSING;
    IDirect3DDevice9* device = nullptr;
    HRESULT hr = d3d9->CreateDevice(D3DADAPTER_DEFAULT,
                                    D3DDEVTYPE_HAL,
                                    state->window,
                                    behavior,
                                    &state->present_params,
                                    &device);
    if (FAILED(hr)) {
        behavior = D3DCREATE_FPU_PRESERVE | D3DCREATE_SOFTWARE_VERTEXPROCESSING;
        hr = d3d9->CreateDevice(D3DADAPTER_DEFAULT,
                                D3DDEVTYPE_HAL,
                                state->window,
                                behavior,
                                &state->present_params,
                                &device);
    }

    if (FAILED(hr) || !device) {
        release_com(state->d3d9);
        return FAILED(hr) ? hr : E_FAIL;
    }

    state->device = device;
    return S_OK;
}

HRESULT reset_device(Dx9BackendState* state) {
    if (!state) return E_POINTER;
    if (state->device_ex) {
        return state->device_ex->ResetEx(&state->present_params, nullptr);
    }
    if (state->device) {
        return state->device->Reset(&state->present_params);
    }
    return E_POINTER;
}

void destroy_backend(void* userdata) {
    auto* state = static_cast<Dx9BackendState*>(userdata);
    if (!state) return;
    release_com(state->device_ex);
    release_com(state->device);
    release_com(state->d3d9ex);
    release_com(state->d3d9);
    delete state;
}

int resize_backend(void* userdata, uint32_t width, uint32_t height) {
    auto* state = static_cast<Dx9BackendState*>(userdata);
    if (!state) return RD_STATUS_INVALID_ARGUMENT;

    fill_present_parameters(&state->present_params, state->window, width, height);
    const HRESULT hr = reset_device(state);
    return SUCCEEDED(hr) ? RD_STATUS_OK : RD_STATUS_RUNTIME_ERROR;
}

int scene_create_or_update(void*, const rd_scene_blob_v1*) {
    // Pipeline upload is implemented in RD-12. RD-10 initializes and manages device lifecycle.
    return RD_STATUS_OK;
}

int render_frame(void* userdata, const rd_camera_blob_v1*) {
    auto* state = static_cast<Dx9BackendState*>(userdata);
    if (!state) return RD_STATUS_INVALID_ARGUMENT;

    IDirect3DDevice9* device = base_device(state);
    if (!device) return RD_STATUS_RUNTIME_ERROR;

    if (!state->device_ex) {
        const HRESULT cooperative = device->TestCooperativeLevel();
        if (cooperative == D3DERR_DEVICELOST) {
            return RD_STATUS_OK;
        }
        if (cooperative == D3DERR_DEVICENOTRESET) {
            if (FAILED(reset_device(state))) {
                return RD_STATUS_RUNTIME_ERROR;
            }
        } else if (FAILED(cooperative)) {
            return RD_STATUS_RUNTIME_ERROR;
        }
    }

    HRESULT hr = device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                               D3DCOLOR_XRGB(28, 28, 30), 1.0f, 0);
    if (FAILED(hr)) return RD_STATUS_RUNTIME_ERROR;

    hr = device->BeginScene();
    if (SUCCEEDED(hr)) {
        device->EndScene();
    } else if (hr != D3DERR_INVALIDCALL) {
        return RD_STATUS_RUNTIME_ERROR;
    }

    hr = device->Present(nullptr, nullptr, nullptr, nullptr);
    if (hr == D3DERR_DEVICELOST || hr == D3DERR_DEVICENOTRESET) {
        return RD_STATUS_OK;
    }
    if (FAILED(hr)) return RD_STATUS_RUNTIME_ERROR;

    state->frame_stats.draw_calls = 0;
    state->frame_stats.triangles = 0;
    state->frame_stats.cpu_frame_ms = 0.0f;
    state->frame_stats.gpu_frame_ms = -1.0f;
    return RD_STATUS_OK;
}

int get_frame_stats(void* userdata, rd_frame_stats_v1* stats) {
    auto* state = static_cast<Dx9BackendState*>(userdata);
    if (!state || !stats) return RD_STATUS_INVALID_ARGUMENT;
    *stats = state->frame_stats;
    return RD_STATUS_OK;
}

#endif  // _WIN32

int create_backend(const rd_backend_create_desc_v1* desc,
                   rd_backend_instance_v1* out_instance) {
    if (!desc || !out_instance) return RD_STATUS_INVALID_ARGUMENT;
    if (desc->struct_size < sizeof(rd_backend_create_desc_v1)) return RD_STATUS_INVALID_ARGUMENT;
#ifdef _WIN32
    HWND window = reinterpret_cast<HWND>(desc->native_window);
    if (!window || !IsWindow(window)) {
        return RD_STATUS_INVALID_ARGUMENT;
    }

    auto* state = new (std::nothrow) Dx9BackendState();
    if (!state) return RD_STATUS_RUNTIME_ERROR;
    state->window = window;
    fill_present_parameters(&state->present_params, window, desc->width, desc->height);

    HMODULE d3d9_module = LoadLibraryA("d3d9.dll");
    if (!d3d9_module) {
        delete state;
        return RD_STATUS_RUNTIME_ERROR;
    }

    HRESULT hr = create_device_with_d3d9ex(state);
    if (FAILED(hr)) {
        hr = create_device_with_d3d9(state);
    }

    FreeLibrary(d3d9_module);

    if (FAILED(hr) || !base_device(state)) {
        destroy_backend(state);
        return RD_STATUS_RUNTIME_ERROR;
    }

    out_instance->userdata = state;
    out_instance->destroy = destroy_backend;
    out_instance->resize = resize_backend;
    out_instance->scene_create_or_update = scene_create_or_update;
    out_instance->render_frame = render_frame;
    out_instance->get_frame_stats = get_frame_stats;
    return RD_STATUS_OK;
#else
    (void)desc;
    (void)out_instance;
    return RD_STATUS_NOT_IMPLEMENTED;
#endif
}

rd_backend_probe_result_v1 probe_backend() {
    rd_backend_probe_result_v1 result{};
    result.struct_size = sizeof(rd_backend_probe_result_v1);
#ifdef _WIN32
    struct ProbeStrings {
        std::string device_name;
        std::string driver_info;
        std::string reason;
    } strings;

    auto to_hex_u32 = [](uint32_t value) -> std::string {
        std::ostringstream out;
        out << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << value;
        return out.str();
    };

    auto hresult_string = [&](HRESULT hr) -> std::string {
        return to_hex_u32(static_cast<uint32_t>(hr));
    };

    auto set_unavailable = [&](const std::string& reason) {
        result.available = 0;
        result.score = 0;
        strings.device_name = "n/a";
        strings.driver_info = "n/a";
        strings.reason = reason;
    };

    auto set_available = [&](const D3DADAPTER_IDENTIFIER9& id, const char* api_tag) {
        const uint32_t hi = static_cast<uint32_t>(id.DriverVersion.HighPart);
        const uint32_t lo = static_cast<uint32_t>(id.DriverVersion.LowPart);
        const uint32_t major = HIWORD(hi);
        const uint32_t minor = LOWORD(hi);
        const uint32_t build = HIWORD(lo);
        const uint32_t revision = LOWORD(lo);

        result.available = 1;
        result.score = 80;

        strings.device_name = id.Description[0] ? id.Description : "Direct3D 9 Adapter";

        std::ostringstream info;
        info << (id.Driver[0] ? id.Driver : "driver")
             << " v" << major << "." << minor << "." << build << "." << revision
             << " vendor=" << to_hex_u32(id.VendorId)
             << " device=" << to_hex_u32(id.DeviceId)
             << " (" << api_tag << ")";
        strings.driver_info = info.str();
        strings.reason = std::string("Direct3D 9 probe succeeded via ") + api_tag;
    };

    HMODULE d3d9 = LoadLibraryA("d3d9.dll");
    if (!d3d9) {
        set_unavailable("d3d9.dll is not available");
    } else {
        using Direct3DCreate9ExFn = HRESULT(WINAPI*)(UINT, IDirect3D9Ex**);
        using Direct3DCreate9Fn = IDirect3D9*(WINAPI*)(UINT);

        const auto create9ex = reinterpret_cast<Direct3DCreate9ExFn>(
            GetProcAddress(d3d9, "Direct3DCreate9Ex"));
        const auto create9 = reinterpret_cast<Direct3DCreate9Fn>(
            GetProcAddress(d3d9, "Direct3DCreate9"));

        bool probed = false;

        if (create9ex) {
            IDirect3D9Ex* d3d9ex = nullptr;
            const HRESULT hr = create9ex(D3D_SDK_VERSION, &d3d9ex);
            if (SUCCEEDED(hr) && d3d9ex) {
                D3DADAPTER_IDENTIFIER9 id{};
                const HRESULT id_hr =
                    d3d9ex->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &id);
                if (SUCCEEDED(id_hr)) {
                    set_available(id, "d3d9ex");
                } else {
                    result.available = 1;
                    result.score = 80;
                    strings.device_name = "Direct3D 9Ex";
                    strings.driver_info = "adapter id unavailable";
                    strings.reason = "Direct3D 9Ex created, adapter query failed: " +
                        hresult_string(id_hr);
                }
                d3d9ex->Release();
                probed = true;
            } else {
                strings.reason = "Direct3DCreate9Ex failed: " + hresult_string(hr);
            }
        }

        if (!probed && create9) {
            IDirect3D9* d3d9obj = create9(D3D_SDK_VERSION);
            if (d3d9obj) {
                D3DADAPTER_IDENTIFIER9 id{};
                const HRESULT id_hr =
                    d3d9obj->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &id);
                if (SUCCEEDED(id_hr)) {
                    set_available(id, "d3d9");
                } else {
                    result.available = 1;
                    result.score = 80;
                    strings.device_name = "Direct3D 9";
                    strings.driver_info = "adapter id unavailable";
                    strings.reason = "Direct3D 9 created, adapter query failed: " +
                        hresult_string(id_hr);
                }
                d3d9obj->Release();
                probed = true;
            } else if (strings.reason.empty()) {
                strings.reason = "Direct3DCreate9 returned null";
            }
        }

        if (!probed) {
            if (strings.reason.empty()) {
                if (!create9ex && !create9) {
                    strings.reason = "d3d9.dll missing Direct3DCreate9/Direct3DCreate9Ex";
                } else {
                    strings.reason = "Direct3D 9 probe failed";
                }
            }
            set_unavailable(strings.reason);
        }

        FreeLibrary(d3d9);
    }

    // rd_backend_probe_result_v1 stores const char*; keep storage stable per thread.
    static thread_local std::string device_name_storage;
    static thread_local std::string driver_info_storage;
    static thread_local std::string reason_storage;
    device_name_storage = strings.device_name;
    driver_info_storage = strings.driver_info;
    reason_storage = strings.reason;
    result.device_name = device_name_storage.c_str();
    result.driver_info = driver_info_storage.c_str();
    result.reason = reason_storage.c_str();
#else
    result.available = 0;
    result.score = 0;
    result.device_name = "n/a";
    result.driver_info = "n/a";
    result.reason = "Direct3D 9 is only available on Windows";
#endif
    return result;
}

const rd_backend_factory_v1 k_factory = {
    RD_ABI_VERSION,
    "dx9",
    "Direct3D 9",
    probe_backend,
    create_backend,
};

}  // namespace

extern "C" RD_PLUGIN_EXPORT const rd_backend_factory_v1* rdGetBackendFactory() {
    return &k_factory;
}
