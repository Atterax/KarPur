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
    p.logLevel = sl::LogLevel::eDefault;
    p.pathsToPlugins = paths;
    p.numPathsToPlugins = 1;
    // NGX needs a writable data folder: without one its feature query failed with 0xbad00005 (invalid parameter) and
    // Streamline dropped DLSS-G at slInit (2026-11-08). The exe's folder, as vk_dlaa.c gives NGX.
    p.pathToLogsAndData = plugin_dir;
    p.logMessageCallback = log_cb;
    // manual hooking; frame-based tagging; no over-the-air downloads; no debug text
    p.flags = sl::PreferenceFlags::eDisableCLStateTracking | sl::PreferenceFlags::eUseManualHooking |
              sl::PreferenceFlags::eUseFrameBasedResourceTagging | sl::PreferenceFlags::eDisableDebugText;
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
    S.active = 1;
    rt_log("framegen: Streamline initialised (SDK %d.%d.%d headers); it creates the Vulkan instance and device",
           SL_VERSION_MAJOR, SL_VERSION_MINOR, SL_VERSION_PATCH);
}

// The requirements of the loaded features (DLSS-G, Reflex, PCL; a feature Streamline dropped answers with an error)
static sl::FeatureRequirements g_reqs[3];
static int g_reqs_ok[3], g_reqs_read;
static void read_reqs(void) {
    if (g_reqs_read || !S.active || !S.reqs) return;
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
    for (int i = 0; i < 3; i++)
        if (g_reqs_ok[i]) *graphics += g_reqs[i].vkNumGraphicsQueuesRequired, *compute += g_reqs[i].vkNumComputeQueuesRequired;
}

extern "C" void slfg_device_ready(VkInstance inst, VkPhysicalDevice phys, VkDevice dev, uint32_t gfx_family, uint32_t gfx_index,
                                  uint32_t compute_family, uint32_t compute_index) {
    if (!S.active) return;
    auto set_info = (PFun_slSetVulkanInfo *)(void *)GetProcAddress(S.mod, "slSetVulkanInfo");
    sl::VulkanInfo vi{};
    vi.device = dev, vi.instance = inst, vi.physicalDevice = phys;
    vi.graphicsQueueFamily = gfx_family, vi.graphicsQueueIndex = gfx_index;
    vi.computeQueueFamily = compute_family, vi.computeQueueIndex = compute_index;
    vi.useNativeOpticalFlowMode = false;   // (optical flow in interop mode: no queue of its own yet)
    sl::Result sr = set_info ? set_info(vi) : sl::Result::eErrorMissingInputParameter;
    if (sr != sl::Result::eOk) {
        rt_log("framegen: slSetVulkanInfo failed (%d, %s): off", (int)sr, result_name(sr));
        S.active = 0;
        return;
    }
    rt_log("framegen: Streamline has the device; its queues: graphics family %u from %u, compute family %u from %u",
           gfx_family, gfx_index, compute_family, compute_index);
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
    if (!S.active) return;
    S.shutdown();
    S.active = 0;
    rt_log("framegen: Streamline shut down");
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
extern "C" void slfg_start(void) {}
extern "C" void slfg_instance_exts(const char **ext, uint32_t *n, uint32_t max) { (void)ext, (void)n, (void)max; }
extern "C" void slfg_device_exts(const VkExtensionProperties *have, uint32_t nhave, const char **ext, uint32_t *n, uint32_t max) {
    (void)have, (void)nhave, (void)ext, (void)n, (void)max;
}
extern "C" void slfg_extra_queues(uint32_t *g, uint32_t *c) { *g = *c = 0; }
extern "C" void slfg_device_ready(VkInstance inst, VkPhysicalDevice phys, VkDevice dev, uint32_t gf, uint32_t gi, uint32_t cf, uint32_t ci) {
    (void)inst, (void)phys, (void)dev, (void)gf, (void)gi, (void)cf, (void)ci;
}
extern "C" void slfg_shutdown(void) {}
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
