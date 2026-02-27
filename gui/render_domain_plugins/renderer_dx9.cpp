#include "render_domain/rd_backend_abi.h"

#ifdef _WIN32
#include <d3d9.h>
#include <windows.h>

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#endif

namespace {

int create_backend(const rd_backend_create_desc_v1*,
                   rd_backend_instance_v1*) {
    return RD_STATUS_NOT_IMPLEMENTED;
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
