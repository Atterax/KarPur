// KarPur DLSS Frame Generation through NVIDIA Streamline: see vk_slfg.h.
//
// Streamline's "manual hooking" integration (third_party/streamline/ProgrammingGuideManualHooking.md upstream): KarPur
// keeps calling vulkan-1.dll and uses Streamline's proxies, from its interposer's vkGetInstanceProcAddr /
// vkGetDeviceProcAddr, only for the functions sl_hooks.h lists for Vulkan, plus vkCreateInstance and vkCreateDevice,
// whose proxies add the extensions, features and queues the loaded features ask for. The interposer is loaded at run
// time (no import library): the SL_API functions the headers declare are defined here as forwarders to it, so the
// headers' helpers (slDLSSGSetOptions ...) work as documented. Over-the-air updates of Streamline and NGX stay off.
#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#include "vk_slfg.h"

#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "third_party/streamline/sl.h"
#include "third_party/streamline/sl_dlss_g.h"
#include "third_party/streamline/sl_reflex.h"
#include "third_party/streamline/sl_helpers_vk.h"

extern "C" {
#include "cfg.h"
#include "host.h"
}

#undef vkDeviceWaitIdle   // (vk.h routes it here; this file calls Vulkan's own)

namespace {
struct {
    int tried, active, device_ok;
    int iat;   // the interposer mode: karpur.exe's vulkan-1.dll imports go to sl.interposer.dll
    HMODULE mod;
    PFN_vkGetInstanceProcAddr gipa;   // the interposer's
    PFN_vkGetDeviceProcAddr gdpa;
    PFun_slInit *init;
    PFun_slShutdown *shutdown;
    PFun_slIsFeatureSupported *supported;
    PFun_slGetFeatureRequirements *reqs;
    PFun_slGetFeatureFunction *feature_fn;
    PFun_slGetNewFrameToken *frame_token;
    PFun_slSetTagForFrame *tag;
    PFun_slSetConstants *constants;
    PFun_slEvaluateFeature *evaluate;
    PFun_slSetFeatureLoaded *set_loaded;
    PFun_slIsFeatureLoaded *is_loaded;
    PFun_slAllocateResources *alloc;
    PFun_slFreeResources *free_res;
    PFun_slGetFeatureVersion *version;
    PFN_vkCreateWin32SurfaceKHR create_surface;
    PFN_vkDestroySurfaceKHR destroy_surface;
    PFN_vkCreateSwapchainKHR create_swapchain;
    PFN_vkDestroySwapchainKHR destroy_swapchain;
    PFN_vkGetSwapchainImagesKHR swapchain_images;
    PFN_vkAcquireNextImageKHR acquire;
    PFN_vkQueuePresentKHR present;
    PFN_vkDeviceWaitIdle wait_idle;
} S;

void log_cb(sl::LogType type, const char *msg) {
    size_t n = strlen(msg);
    while (n && (msg[n - 1] == '\n' || msg[n - 1] == '\r')) n--;
    rt_log("framegen: streamline %s: %.*s", type == sl::LogType::eError ? "error" : type == sl::LogType::eWarn ? "warning" : "info",
           (int)n, msg);
}

const char *result_name(sl::Result r) {
    switch (r) {
    case sl::Result::eOk: return "ok";
    case sl::Result::eErrorAdapterNotSupported: return "adapter not supported";
    case sl::Result::eErrorOSOutOfDate: return "OS out of date";
    case sl::Result::eErrorDriverOutOfDate: return "driver out of date";
    case sl::Result::eErrorNoSupportedAdapterFound: return "no supported adapter";
    case sl::Result::eErrorFeatureMissing: return "feature missing (its DLLs?)";
    case sl::Result::eErrorFeatureNotSupported: return "feature not supported";
    case sl::Result::eErrorOSDisabledHWS: return "hardware-accelerated GPU scheduling off in Windows";
    default: return "error";
    }
}
}   // namespace

// ---- the SL_API functions, forwarded to the loaded interposer (the headers' inline helpers call these)
extern "C" {
sl::Result slGetFeatureFunction(sl::Feature feature, const char *name, void *&fn) {
    return S.feature_fn ? S.feature_fn(feature, name, fn) : sl::Result::eErrorNotInitialized;
}
}

extern "C" int slfg_active(void) { return S.active; }
extern "C" int slfg_manual(void) { return S.active && !S.iat; }

// karpur.exe's imports from vulkan-1.dll pointed at sl.interposer.dll's functions of the same name (it exports the
// whole API; what it does not intercept it forwards to vulkan-1.dll). This is Streamline's standard integration, an
// application linking sl.interposer instead of vulkan-1, without relinking: Streamline then sees vkCreateInstance,
// vkCreateDevice, the command buffers and the swapchain itself. Returns the number of imports redirected.
static int patch_imports(HMODULE interposer) {
    uint8_t *base = (uint8_t *)GetModuleHandleW(NULL);
    auto dos = (IMAGE_DOS_HEADER *)base;
    auto nt = (IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return 0;
    int n = 0, missing = 0;
    for (auto d = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir.VirtualAddress); d->Name; d++) {
        if (_stricmp((const char *)(base + d->Name), "vulkan-1.dll")) continue;
        auto names = (IMAGE_THUNK_DATA64 *)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        auto iat = (IMAGE_THUNK_DATA64 *)(base + d->FirstThunk);
        for (; names->u1.AddressOfData; names++, iat++) {
            if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) continue;
            const char *fn = (const char *)((IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData))->Name;
            FARPROC p = GetProcAddress(interposer, fn);
            if (!p) { missing++; continue; }
            DWORD old;
            if (!VirtualProtect(&iat->u1.Function, sizeof iat->u1.Function, PAGE_READWRITE, &old)) continue;
            iat->u1.Function = (ULONGLONG)(uintptr_t)p;
            VirtualProtect(&iat->u1.Function, sizeof iat->u1.Function, old, &old);
            n++;
        }
    }
    rt_log("framegen: %d vulkan-1.dll imports now go through sl.interposer.dll (%d it does not export stay)", n, missing);
    return n;
}

extern "C" void slfg_start(void) {
    if (S.tried) return;
    S.tried = 1;
    if (!rt_knob("FRAMEGEN", "0")) return;
    wchar_t dir[MAX_PATH], path[MAX_PATH + 32];
    DWORD len = GetModuleFileNameW(NULL, dir, MAX_PATH);
    while (len && dir[len - 1] != L'\\') len--;
    dir[len] = 0;
    static const wchar_t *const need[] = {L"sl.interposer.dll", L"sl.common.dll", L"sl.dlss_g.dll", L"sl.reflex.dll",
                                          L"sl.pcl.dll", L"nvngx_dlssg.dll"};
    for (const wchar_t *n : need) {
        swprintf(path, MAX_PATH + 32, L"%ls%ls", dir, n);
        if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
            rt_log("framegen: %ls is not next to karpur.exe: frame generation off", n);
            return;
        }
    }
    // OBS Studio's game capture layers (VK_LAYER_OBS_HOOK, VK_LAYER_WEOBS_HOOK, implicit: in every Vulkan process where
    // OBS is installed) faulted in the driver when DLSS-G cloned the swapchain's images (2026-11-08: the fault's stack
    // ran through graphics-hook64.dll and we-graphics-hook64.dll). Their own switch turns them off for this process only;
    // OBS's window or display capture still works. TOTK_FRAMEGEN_OBS=1 keeps them.
    if (!rt_knob("FRAMEGEN_OBS", "0")) {
        SetEnvironmentVariableA("DISABLE_VULKAN_OBS_CAPTURE", "1");
        _putenv("DISABLE_VULKAN_OBS_CAPTURE=1");
        rt_log("framegen: OBS's Vulkan capture layers off for this process (they break DLSS-G's swapchain)");
    }
    swprintf(path, MAX_PATH + 32, L"%lssl.interposer.dll", dir);
    S.mod = LoadLibraryW(path);
    if (!S.mod) { rt_log("framegen: cannot load sl.interposer.dll (error %lu): off", GetLastError()); return; }
#define FN(f, name) S.f = (decltype(S.f))(void *)GetProcAddress(S.mod, name)
    FN(gipa, "vkGetInstanceProcAddr");
    FN(gdpa, "vkGetDeviceProcAddr");
    FN(init, "slInit");
    FN(shutdown, "slShutdown");
    FN(supported, "slIsFeatureSupported");
    FN(reqs, "slGetFeatureRequirements");
    FN(feature_fn, "slGetFeatureFunction");
    FN(frame_token, "slGetNewFrameToken");
    FN(tag, "slSetTagForFrame");
    FN(constants, "slSetConstants");
    FN(evaluate, "slEvaluateFeature");
    FN(set_loaded, "slSetFeatureLoaded");
    FN(is_loaded, "slIsFeatureLoaded");
    FN(alloc, "slAllocateResources");
    FN(free_res, "slFreeResources");
    FN(version, "slGetFeatureVersion");
#undef FN
    if (!S.gipa || !S.gdpa || !S.init || !S.shutdown || !S.supported || !S.feature_fn || !S.frame_token || !S.tag ||
        !S.constants) {
        rt_log("framegen: sl.interposer.dll lacks entry points: off");
        return;
    }
    static const sl::Feature features[] = {sl::kFeatureDLSS_G, sl::kFeatureReflex, sl::kFeaturePCL};
    static const wchar_t *paths[1];
    static wchar_t plugin_dir[MAX_PATH];
    wcscpy(plugin_dir, dir);
    paths[0] = plugin_dir;
    sl::Preferences p{};
    p.showConsole = false;
    p.logLevel = rt_knob("FRAMEGEN_VERBOSE", "0") ? sl::LogLevel::eVerbose : sl::LogLevel::eDefault;   // (diagnostic)
    p.pathsToPlugins = paths;
    p.numPathsToPlugins = 1;
    // NGX needs a writable data folder: without one its feature query failed with 0xbad00005 (invalid parameter) and
    // Streamline dropped DLSS-G at slInit (2026-11-08). The exe's folder, as vk_dlaa.c gives NGX.
    p.pathToLogsAndData = plugin_dir;
    p.logMessageCallback = log_cb;
    // manual hooking; frame-based tagging; no over-the-air downloads; no debug text
    // TOTK_FRAMEGEN_MODE: interposer (default; Streamline's standard integration) or manual (its manual hooking: in
    // that mode sl.common reported its command buffer hooks unsupported and the driver faulted while DLSS-G cloned the
    // swapchain's images, 2026-11-08)
    const char *mode = cfg_get("TOTK_FRAMEGEN_MODE");
    S.iat = !(mode && !strcmp(mode, "manual"));
    p.flags = sl::PreferenceFlags::eDisableCLStateTracking | sl::PreferenceFlags::eUseFrameBasedResourceTagging |
              sl::PreferenceFlags::eDisableDebugText;
    if (!S.iat) p.flags |= sl::PreferenceFlags::eUseManualHooking;
    // TOTK_FRAMEGEN_FEATURES=<n>: load only the first n of DLSS-G, Reflex, PCL (diagnostic; default all 3)
    const char *nf = cfg_get("TOTK_FRAMEGEN_FEATURES");
    p.featuresToLoad = features;
    p.numFeaturesToLoad = nf && *nf ? (uint32_t)atoi(nf) : 3;
    p.applicationId = 231313132;   // the id NVIDIA's samples use for builds without an NVIDIA-issued one (as vk_dlaa.c)
    p.engine = sl::EngineType::eCustom;
    p.engineVersion = "KarPur";
    p.renderAPI = sl::RenderAPI::eVulkan;
    sl::Result r = S.init(p, sl::kSDKVersion);
    if (r != sl::Result::eOk) {
        rt_log("framegen: slInit failed (%d, %s): off", (int)r, result_name(r));
        return;
    }
    if (S.iat && !patch_imports(S.mod)) {
        rt_log("framegen: no vulkan-1.dll imports to redirect: off");
        S.shutdown();
        return;
    }
    S.active = 1;
    rt_log("framegen: Streamline initialised (SDK %d.%d.%d headers), %s", SL_VERSION_MAJOR, SL_VERSION_MINOR, SL_VERSION_PATCH,
           S.iat ? "as the Vulkan loader (interposer mode)" : "manual hooking");
}

// The requirements of the loaded features (DLSS-G, Reflex, PCL; a feature Streamline dropped answers with an error)
static sl::FeatureRequirements g_reqs[3];
static int g_reqs_ok[3], g_reqs_read;
static void read_reqs(void) {
    if (g_reqs_read || !S.active || !S.reqs || S.iat) return;
    g_reqs_read = 1;
    static const sl::Feature f[3] = {sl::kFeatureDLSS_G, sl::kFeatureReflex, sl::kFeaturePCL};
    for (int i = 0; i < 3; i++) g_reqs_ok[i] = S.reqs(f[i], g_reqs[i]) == sl::Result::eOk;
}

static int listed(const char *const *list, uint32_t n, const char *x) {
    for (uint32_t i = 0; i < n; i++)
        if (!strcmp(list[i], x)) return 1;
    return 0;
}

extern "C" void slfg_instance_exts(const char **ext, uint32_t *n, uint32_t max) {
    read_reqs();
    for (int i = 0; i < 3; i++)
        for (uint32_t k = 0; g_reqs_ok[i] && k < g_reqs[i].vkNumInstanceExtensions; k++)
            if (!listed(ext, *n, g_reqs[i].vkInstanceExtensions[k])) {
                if (*n < max) ext[(*n)++] = g_reqs[i].vkInstanceExtensions[k];
                else rt_log("framegen: no room for instance extension %s", g_reqs[i].vkInstanceExtensions[k]);
            }
}

extern "C" void slfg_device_exts(const VkExtensionProperties *have, uint32_t nhave, const char **ext, uint32_t *n, uint32_t max) {
    read_reqs();
    for (int i = 0; i < 3; i++)
        for (uint32_t k = 0; g_reqs_ok[i] && k < g_reqs[i].vkNumDeviceExtensions; k++) {
            const char *x = g_reqs[i].vkDeviceExtensions[k];
            int ok = 0;
            for (uint32_t j = 0; j < nhave && !ok; j++) ok = !strcmp(have[j].extensionName, x);
            if (!ok) rt_log("framegen: the device lacks %s, which Streamline asks for", x);
            else if (!listed(ext, *n, x)) {
                if (*n < max) ext[(*n)++] = x;
                else rt_log("framegen: no room for device extension %s", x);
            }
        }
}

extern "C" void slfg_extra_queues(uint32_t *graphics, uint32_t *compute) {
    *graphics = *compute = 0;
    read_reqs();
    static const char *const name[3] = {"DLSS-G", "Reflex", "PCL"};
    for (int i = 0; i < 3; i++)
        if (g_reqs_ok[i]) {
            *graphics += g_reqs[i].vkNumGraphicsQueuesRequired, *compute += g_reqs[i].vkNumComputeQueuesRequired;
            rt_log("framegen: %s asks for %u graphics, %u compute and %u optical flow queues, %u device and %u instance extensions",
                   name[i], g_reqs[i].vkNumGraphicsQueuesRequired, g_reqs[i].vkNumComputeQueuesRequired,
                   g_reqs[i].vkNumOpticalFlowQueuesRequired, g_reqs[i].vkNumDeviceExtensions, g_reqs[i].vkNumInstanceExtensions);
            char line[1024];
            size_t o = 0;
            line[0] = 0;
            for (uint32_t k = 0; k < g_reqs[i].vkNumDeviceExtensions && o < sizeof line - 80; k++)
                o += snprintf(line + o, sizeof line - o, " %s", g_reqs[i].vkDeviceExtensions[k]);
            for (uint32_t k = 0; k < g_reqs[i].vkNumInstanceExtensions && o < sizeof line - 80; k++)
                o += snprintf(line + o, sizeof line - o, " (instance) %s", g_reqs[i].vkInstanceExtensions[k]);
            for (uint32_t k = 0; k < g_reqs[i].vkNumFeatures12 && o < sizeof line - 80; k++)
                o += snprintf(line + o, sizeof line - o, " (1.2) %s", g_reqs[i].vkFeatures12[k]);
            for (uint32_t k = 0; k < g_reqs[i].vkNumFeatures13 && o < sizeof line - 80; k++)
                o += snprintf(line + o, sizeof line - o, " (1.3) %s", g_reqs[i].vkFeatures13[k]);
            if (o) rt_log("framegen:   %s:%s", name[i], line);
        }
}

extern "C" void slfg_device_ready(VkInstance inst, VkPhysicalDevice phys, VkDevice dev, uint32_t gfx_family, uint32_t gfx_index,
                                  uint32_t compute_family, uint32_t compute_index, uint32_t of_family) {
    if (!S.active) return;
    if (S.iat) {   // Streamline saw the instance and the device being made: only the support check below
        sl::AdapterInfo ai{};
        ai.vkPhysicalDevice = phys;
        sl::Result r = S.supported(sl::kFeatureDLSS_G, ai), rr = S.supported(sl::kFeatureReflex, ai);
        rt_log("framegen: DLSS-G %s (%s), Reflex %s", r == sl::Result::eOk ? "supported" : "NOT supported", result_name(r),
               rr == sl::Result::eOk ? "supported" : "not supported");
        (void)inst, (void)dev, (void)gfx_family, (void)gfx_index, (void)compute_family, (void)compute_index, (void)of_family;
        return;
    }
    auto set_info = (PFun_slSetVulkanInfo *)(void *)GetProcAddress(S.mod, "slSetVulkanInfo");
    sl::VulkanInfo vi{};
    vi.device = dev, vi.instance = inst, vi.physicalDevice = phys;
    vi.graphicsQueueFamily = gfx_family, vi.graphicsQueueIndex = gfx_index;
    vi.computeQueueFamily = compute_family, vi.computeQueueIndex = compute_index;
    vi.useNativeOpticalFlowMode = of_family != UINT32_MAX;   // its own queue, index 0 of the first such family
    vi.opticalFlowQueueFamily = of_family != UINT32_MAX ? of_family : 0, vi.opticalFlowQueueIndex = 0;
    sl::Result sr = set_info ? set_info(vi) : sl::Result::eErrorMissingInputParameter;
    if (sr != sl::Result::eOk) {
        rt_log("framegen: slSetVulkanInfo failed (%d, %s): off", (int)sr, result_name(sr));
        S.active = 0;
        return;
    }
    rt_log("framegen: Streamline has the device; its queues: graphics family %u from %u, compute family %u from %u, "
           "optical flow %s", gfx_family, gfx_index, compute_family, compute_index,
           of_family != UINT32_MAX ? "native" : "interop (no optical flow family)");
    S.create_surface = (PFN_vkCreateWin32SurfaceKHR)S.gipa(inst, "vkCreateWin32SurfaceKHR");
    S.destroy_surface = (PFN_vkDestroySurfaceKHR)S.gipa(inst, "vkDestroySurfaceKHR");
    S.create_swapchain = (PFN_vkCreateSwapchainKHR)S.gdpa(dev, "vkCreateSwapchainKHR");
    S.destroy_swapchain = (PFN_vkDestroySwapchainKHR)S.gdpa(dev, "vkDestroySwapchainKHR");
    S.swapchain_images = (PFN_vkGetSwapchainImagesKHR)S.gdpa(dev, "vkGetSwapchainImagesKHR");
    S.acquire = (PFN_vkAcquireNextImageKHR)S.gdpa(dev, "vkAcquireNextImageKHR");
    S.present = (PFN_vkQueuePresentKHR)S.gdpa(dev, "vkQueuePresentKHR");
    S.wait_idle = (PFN_vkDeviceWaitIdle)S.gdpa(dev, "vkDeviceWaitIdle");
    S.device_ok = S.create_swapchain && S.destroy_swapchain && S.swapchain_images && S.acquire && S.present && S.wait_idle;
    sl::AdapterInfo ai{};
    ai.vkPhysicalDevice = phys;
    sl::Result r = S.supported(sl::kFeatureDLSS_G, ai);
    sl::Result rr = S.supported(sl::kFeatureReflex, ai);
    sl::FeatureRequirements req{};
    if (S.reqs) S.reqs(sl::kFeatureDLSS_G, req);
    sl::FeatureVersion ver{};
    if (S.version) S.version(sl::kFeatureDLSS_G, ver);
    rt_log("framegen: DLSS-G %s (%s), Reflex %s; DLSS-G %u.%u.%u, NGX %u.%u.%u; Vulkan %s, needs V-Sync off %s, hardware "
           "scheduling %s; driver %u.%u needed, %u.%u here; swapchain proxies %s",
           r == sl::Result::eOk ? "supported" : "NOT supported", result_name(r), rr == sl::Result::eOk ? "supported" : "not supported",
           ver.versionSL.major, ver.versionSL.minor, ver.versionSL.build, ver.versionNGX.major, ver.versionNGX.minor, ver.versionNGX.build,
           (req.flags & sl::FeatureRequirementFlags::eVulkanSupported) ? "yes" : "no",
           (req.flags & sl::FeatureRequirementFlags::eVSyncOffRequired) ? "yes" : "no",
           (req.flags & sl::FeatureRequirementFlags::eHardwareSchedulingRequired) ? "required" : "not required",
           req.driverVersionRequired.major, req.driverVersionRequired.minor, req.driverVersionDetected.major,
           req.driverVersionDetected.minor, S.device_ok ? "ok" : "MISSING");
}

extern "C" void slfg_shutdown(void) {
    if (!S.active) return;   // (interposer mode: the imports stay redirected; the process is ending)
    S.shutdown();
    S.active = 0;
    rt_log("framegen: Streamline shut down");
}

// ---- stage 2: frame generation
static struct {
    int mult;                            // TOTK_FRAMEGEN: presented frames per rendered one (2..4), 1: Streamline only
    int options_set, logged, errors;
    int on;                              // DLSS-G's mode is on (off while frames come without a scene)
    uint64_t paused;                     // times it was paused for frames without a scene
    sl::FrameToken *token;               // the frame being presented (slfg_frame -> slfg_present_mark)
    uint32_t index;
    uint32_t ow, oh;
} F;

static void copy_rows(sl::float4x4 &d, const float *m) {   // Violet's column array read as rows: the transpose
    for (int r = 0; r < 4; r++) d.row[r] = sl::float4(m[4 * r], m[4 * r + 1], m[4 * r + 2], m[4 * r + 3]);
}

static void mul4c(const float *a, const float *b, float *o) {   // column-major o = a x b
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            o[c * 4 + r] = a[r] * b[c * 4] + a[4 + r] * b[c * 4 + 1] + a[8 + r] * b[c * 4 + 2] + a[12 + r] * b[c * 4 + 3];
}

static int inv4c(const float *m, float *o) {   // the 4x4 inverse by cofactors (the same layout in and out)
    float a[16];
    a[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    a[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    a[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    a[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    a[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    a[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    a[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    a[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    a[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    a[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    a[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    a[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    a[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    a[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    a[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    a[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    float det = m[0] * a[0] + m[1] * a[4] + m[2] * a[8] + m[3] * a[12];
    if (fabsf(det) < 1e-30f) return 0;
    for (int i = 0; i < 16; i++) o[i] = a[i] / det;
    return 1;
}

static sl::Resource image_res(VkImage img, VkImageView view, VkFormat fmt, uint32_t usage, uint32_t w, uint32_t h) {
    sl::Resource r(sl::ResourceType::eTex2d, (void *)img, nullptr, (void *)view, (uint32_t)VK_IMAGE_LAYOUT_GENERAL);
    r.width = w, r.height = h, r.nativeFormat = (uint32_t)fmt, r.mipLevels = 1, r.arrayLayers = 1, r.usage = usage;
    r.flags = 0;
    return r;
}

static void fg_fail(const char *what, sl::Result r) {
    if (F.errors++ < 8) rt_log("framegen: %s failed (%d, %s)", what, (int)r, result_name(r));
}

extern "C" void slfg_frame(VkCommandBuffer cb, const SlfgFrame *f) {
    if (!S.active) return;
    if (!F.mult) {
        const char *e = cfg_get("TOTK_FRAMEGEN");
        F.mult = e && *e ? atoi(e) : 1;
        if (F.mult < 1) F.mult = 1;
        if (F.mult > 4) F.mult = 4;
    }
    if (F.mult < 2) return;   // Streamline only (stage 1)
    sl::ViewportHandle vp(0);
    // Every present needs its frame's Reflex markers, or DLSS-G stops (test CE: "eDLSSGStatusFailReflexNotDetected-
    // AtRuntime" whenever frames came without a scene - loading screens, menus - and so without this call). Such a frame
    // (f NULL) gets its token and markers, and DLSS-G pauses until the scene is back: it has no inputs for it.
    if (!f) {
        uint32_t idx = ++F.index;
        sl::Result r = S.frame_token(F.token, &idx);
        if (r != sl::Result::eOk || !F.token) { fg_fail("slGetNewFrameToken", r); F.token = nullptr; return; }
        slPCLSetMarker(sl::PCLMarker::eSimulationStart, *F.token);
        slPCLSetMarker(sl::PCLMarker::eSimulationEnd, *F.token);
        slPCLSetMarker(sl::PCLMarker::eRenderSubmitStart, *F.token);
        slPCLSetMarker(sl::PCLMarker::eRenderSubmitEnd, *F.token);
        if (F.on) {
            sl::DLSSGOptions o{};
            o.mode = sl::DLSSGMode::eOff;
            r = slDLSSGSetOptions(vp, o);
            if (r != sl::Result::eOk) fg_fail("slDLSSGSetOptions (off)", r);
            F.on = 0, F.options_set = 0;
            if (F.paused++ < 4) rt_log("framegen: paused for frames without a scene");
        }
        return;
    }
    if (!F.options_set || F.ow != f->ow || F.oh != f->oh) {
        sl::ReflexOptions ro{};
        ro.mode = sl::ReflexMode::eLowLatency;
        sl::Result r = slReflexSetOptions(ro);
        if (r != sl::Result::eOk) fg_fail("slReflexSetOptions", r);
        sl::DLSSGOptions o{};
        o.mode = sl::DLSSGMode::eOn;
        o.numFramesToGenerate = (uint32_t)(F.mult - 1);
        o.mvecDepthWidth = f->w, o.mvecDepthHeight = f->h, o.colorWidth = f->ow, o.colorHeight = f->oh;
        r = slDLSSGSetOptions(vp, o);
        if (r != sl::Result::eOk) fg_fail("slDLSSGSetOptions", r);
        else rt_log("framegen: DLSS-G on, %d presented frames per rendered one; inputs %ux%u, picture %ux%u", F.mult,
                    f->w, f->h, f->ow, f->oh);
        F.options_set = 1, F.on = 1, F.ow = f->ow, F.oh = f->oh;
    }
    uint32_t idx = ++F.index;
    sl::Result r = S.frame_token(F.token, &idx);
    if (r != sl::Result::eOk || !F.token) { fg_fail("slGetNewFrameToken", r); F.token = nullptr; return; }
    // Reflex markers: the game's simulation and render submission of this frame have happened by now (the present
    // thread sees the frame only when it is done); they are marked here, in order, so PCL and DLSS-G see a whole frame
    slPCLSetMarker(sl::PCLMarker::eSimulationStart, *F.token);
    slPCLSetMarker(sl::PCLMarker::eSimulationEnd, *F.token);
    slPCLSetMarker(sl::PCLMarker::eRenderSubmitStart, *F.token);
    // the tags: copied now, in this command buffer (the game's next frame overwrites the depth)
    sl::Resource depth = image_res(f->depth, f->depth_view, f->depth_format, f->depth_usage, f->w, f->h);
    sl::Resource mv = image_res(f->mv, f->mv_view, VK_FORMAT_R16G16_SFLOAT,
                                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                    VK_IMAGE_USAGE_TRANSFER_DST_BIT, f->w, f->h);
    sl::Resource hl = image_res(f->hudless, f->hudless_view, VK_FORMAT_R8G8B8A8_UNORM,
                                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                    VK_IMAGE_USAGE_TRANSFER_DST_BIT, f->ow, f->oh);
    sl::Extent er{0, 0, f->w, f->h}, eo{0, 0, f->ow, f->oh};
    sl::ResourceTag tags[3] = {sl::ResourceTag(&depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eOnlyValidNow, &er),
                               sl::ResourceTag(&mv, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eOnlyValidNow, &er),
                               sl::ResourceTag(&hl, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eOnlyValidNow, &eo)};
    r = S.tag(*F.token, vp, tags, 3, (sl::CommandBuffer *)cb);
    if (r != sl::Result::eOk) fg_fail("slSetTagForFrame", r);
    // the camera (row-major, no jitter: Violet's column arrays read as rows are those matrices)
    sl::Constants k{};
    float inv_proj[16], inv_cur[16], c2p[16], p2c[16], inv_prev[16], view[16];
    copy_rows(k.cameraViewToClip, f->proj);
    if (inv4c(f->proj, inv_proj)) copy_rows(k.clipToCameraView, inv_proj);
    static const float ident[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    copy_rows(k.clipToLensClip, ident);
    if (inv4c(f->cur, inv_cur) && inv4c(f->prev, inv_prev)) {
        mul4c(f->prev, inv_cur, c2p), mul4c(f->cur, inv_prev, p2c);
        copy_rows(k.clipToPrevClip, c2p), copy_rows(k.prevClipToClip, p2c);
    }
    k.jitterOffset = sl::float2(f->jx, f->jy);
    k.mvecScale = sl::float2(1.0f / (float)f->w, 1.0f / (float)f->h);
    k.cameraPinholeOffset = sl::float2(0, 0);
    mul4c(inv_proj, f->cur, view);   // the view matrix: rows 0-2 are the camera's right, up and back axes in the world
    float px = -(view[0] * view[12] + view[1] * view[13] + view[2] * view[14]);
    float py = -(view[4] * view[12] + view[5] * view[13] + view[6] * view[14]);
    float pz = -(view[8] * view[12] + view[9] * view[13] + view[10] * view[14]);
    k.cameraPos = sl::float3(px, py, pz);
    k.cameraRight = sl::float3(view[0], view[4], view[8]);
    k.cameraUp = sl::float3(view[1], view[5], view[9]);
    k.cameraFwd = sl::float3(-view[2], -view[6], -view[10]);
    // near and far from the projection: clip depth = p22 / p32 + p23 / w (Violet: reversed, near at 1)
    float p22 = f->proj[10], p23 = f->proj[14], p32 = f->proj[11];
    float a = fabsf(p32) > 1e-6f ? p22 / p32 : 0;
    k.cameraNear = fabsf(1 - a) > 1e-6f ? fabsf(p23 / (1 - a)) : 0.1f;
    k.cameraFar = fabsf(a) > 1e-9f ? fabsf(p23 / a) : 100000.0f;
    k.cameraFOV = 2.0f * atanf(1.0f / f->proj[5]);
    k.cameraAspectRatio = f->proj[5] / f->proj[0];
    k.depthInverted = f->depth_inv ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    k.cameraMotionIncluded = sl::Boolean::eTrue;
    k.motionVectors3D = sl::Boolean::eFalse;
    k.reset = f->reset ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    k.orthographicProjection = sl::Boolean::eFalse;
    k.motionVectorsDilated = sl::Boolean::eFalse;
    k.motionVectorsJittered = sl::Boolean::eFalse;
    r = S.constants(k, *F.token, vp);
    if (r != sl::Result::eOk) fg_fail("slSetConstants", r);
    slPCLSetMarker(sl::PCLMarker::eRenderSubmitEnd, *F.token);
    if (!F.logged++)
        rt_log("framegen: first frame tagged: depth and motion vectors %ux%u, HUD-less picture %ux%u; near %.3f far %.1f, fov %.1f deg",
               f->w, f->h, f->ow, f->oh, k.cameraNear, k.cameraFar, k.cameraFOV * 57.29578f);
}

extern "C" void slfg_present_mark(int end) {
    if (!S.active || F.mult < 2 || !F.token) return;
    slPCLSetMarker(end ? sl::PCLMarker::ePresentEnd : sl::PCLMarker::ePresentStart, *F.token);
    if (end) F.token = nullptr;
}

// ---- the intercepted functions
extern "C" VkResult slfg_vkCreateWin32SurfaceKHR(VkInstance inst, const void *ci, const VkAllocationCallbacks *a, VkSurfaceKHR *s) {
    auto f = S.active && S.create_surface ? S.create_surface
                                          : (PFN_vkCreateWin32SurfaceKHR)vkGetInstanceProcAddr(inst, "vkCreateWin32SurfaceKHR");
    return f ? f(inst, (const VkWin32SurfaceCreateInfoKHR *)ci, a, s) : VK_ERROR_EXTENSION_NOT_PRESENT;
}
extern "C" void slfg_vkDestroySurfaceKHR(VkInstance inst, VkSurfaceKHR s, const VkAllocationCallbacks *a) {
    if (S.active && S.destroy_surface) S.destroy_surface(inst, s, a);
    else vkDestroySurfaceKHR(inst, s, a);
}
extern "C" VkResult slfg_vkCreateSwapchainKHR(VkDevice d, const VkSwapchainCreateInfoKHR *ci, const VkAllocationCallbacks *a, VkSwapchainKHR *sc) {
    return S.device_ok ? S.create_swapchain(d, ci, a, sc) : vkCreateSwapchainKHR(d, ci, a, sc);
}
extern "C" void slfg_vkDestroySwapchainKHR(VkDevice d, VkSwapchainKHR sc, const VkAllocationCallbacks *a) {
    if (S.device_ok) S.destroy_swapchain(d, sc, a);
    else vkDestroySwapchainKHR(d, sc, a);
}
extern "C" VkResult slfg_vkGetSwapchainImagesKHR(VkDevice d, VkSwapchainKHR sc, uint32_t *n, VkImage *img) {
    return S.device_ok ? S.swapchain_images(d, sc, n, img) : vkGetSwapchainImagesKHR(d, sc, n, img);
}
extern "C" VkResult slfg_vkAcquireNextImageKHR(VkDevice d, VkSwapchainKHR sc, uint64_t timeout, VkSemaphore s, VkFence f, uint32_t *idx) {
    return S.device_ok ? S.acquire(d, sc, timeout, s, f, idx) : vkAcquireNextImageKHR(d, sc, timeout, s, f, idx);
}
extern "C" VkResult slfg_vkQueuePresentKHR(VkQueue q, const VkPresentInfoKHR *pi) {
    return S.device_ok ? S.present(q, pi) : vkQueuePresentKHR(q, pi);
}
extern "C" VkResult slfg_vkDeviceWaitIdle(VkDevice d) {
    return S.device_ok ? S.wait_idle(d) : vkDeviceWaitIdle(d);
}

#else   // Streamline is Windows-only: Vulkan's own functions
#include "vk_slfg.h"
#undef vkDeviceWaitIdle
extern "C" int slfg_active(void) { return 0; }
extern "C" int slfg_manual(void) { return 0; }
extern "C" void slfg_start(void) {}
extern "C" void slfg_instance_exts(const char **ext, uint32_t *n, uint32_t max) { (void)ext, (void)n, (void)max; }
extern "C" void slfg_device_exts(const VkExtensionProperties *have, uint32_t nhave, const char **ext, uint32_t *n, uint32_t max) {
    (void)have, (void)nhave, (void)ext, (void)n, (void)max;
}
extern "C" void slfg_extra_queues(uint32_t *g, uint32_t *c) { *g = *c = 0; }
extern "C" void slfg_device_ready(VkInstance inst, VkPhysicalDevice phys, VkDevice dev, uint32_t gf, uint32_t gi, uint32_t cf, uint32_t ci,
                                  uint32_t of) {
    (void)inst, (void)phys, (void)dev, (void)gf, (void)gi, (void)cf, (void)ci, (void)of;
}
extern "C" void slfg_shutdown(void) {}
extern "C" void slfg_frame(VkCommandBuffer cb, const SlfgFrame *f) { (void)cb, (void)f; }
extern "C" void slfg_present_mark(int end) { (void)end; }
extern "C" VkResult slfg_vkCreateWin32SurfaceKHR(VkInstance inst, const void *ci, const VkAllocationCallbacks *a, VkSurfaceKHR *s) {
    (void)inst, (void)ci, (void)a, (void)s;
    return VK_ERROR_EXTENSION_NOT_PRESENT;
}
extern "C" void slfg_vkDestroySurfaceKHR(VkInstance inst, VkSurfaceKHR s, const VkAllocationCallbacks *a) { vkDestroySurfaceKHR(inst, s, a); }
extern "C" VkResult slfg_vkCreateSwapchainKHR(VkDevice d, const VkSwapchainCreateInfoKHR *ci, const VkAllocationCallbacks *a, VkSwapchainKHR *sc) {
    return vkCreateSwapchainKHR(d, ci, a, sc);
}
extern "C" void slfg_vkDestroySwapchainKHR(VkDevice d, VkSwapchainKHR sc, const VkAllocationCallbacks *a) { vkDestroySwapchainKHR(d, sc, a); }
extern "C" VkResult slfg_vkGetSwapchainImagesKHR(VkDevice d, VkSwapchainKHR sc, uint32_t *n, VkImage *img) {
    return vkGetSwapchainImagesKHR(d, sc, n, img);
}
extern "C" VkResult slfg_vkAcquireNextImageKHR(VkDevice d, VkSwapchainKHR sc, uint64_t t, VkSemaphore s, VkFence f, uint32_t *idx) {
    return vkAcquireNextImageKHR(d, sc, t, s, f, idx);
}
extern "C" VkResult slfg_vkQueuePresentKHR(VkQueue q, const VkPresentInfoKHR *pi) { return vkQueuePresentKHR(q, pi); }
extern "C" VkResult slfg_vkDeviceWaitIdle(VkDevice d) { return vkDeviceWaitIdle(d); }
#endif
