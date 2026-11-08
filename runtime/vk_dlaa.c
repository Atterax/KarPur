// NVIDIA DLAA for the Vulkan back end: DLSS at the scene's own size, fed the jittered picture, the scene depth and the
// camera's motion vectors.
//
// Adapted from ZA Recomp runtime/nvn/vulkan/dlaa.c @ dd77bf6cfc79ac77b48cf2a4ab746c4125a6f2f3 (MIT, Copyright (c) 2026
// Peiikai and ZA Recomp contributors), which follows TotK Recomp's runtime/vk_dlaa.c (MIT, Copyright (c) 2026 Valiantsin
// Dzerakh). KarPur changes: TOTK_DLAA instead of the game's menu, rt_log, the frame struct of vk_dlaa.h, one feature.
//
// No NVIDIA SDK code or header is compiled in. NGX is loaded at run time from the driver's NGX core (_nvngx.dll, found
// through HKLM\SOFTWARE\NVIDIA Corporation\Global\NGXCore); the DLSS model (nvngx_dlss.dll) is loaded by NGX from the
// folder of karpur.exe. The few NGX types used are declared here from the SDK's documented ABI.
//
// The core's parameter object is a C++ class whose virtual functions are called through the MSVC vtable: the Set
// overloads in slots 0-7 and the Get overloads in 8-15, each group in reverse declaration order. vtable_ok checks that
// layout with a set-then-get round trip before anything relies on it.
#include "vk_dlaa.h"

#include "cfg.h"
#include "host.h"
#include "vk.h"

#ifdef _WIN32
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef int NgxResult;  // NVSDK_NGX_Result: 1 success, 0xBAD00000 | n failures
#define NGX_OK(r) (((unsigned)(r) & 0xFFF00000u) != 0xBAD00000u)
#define NGX_VERSION_API 0x15
#define NGX_FEATURE_SUPERSAMPLING 1
#define NGX_PERFQ_MAXQUALITY 2
#define NGX_PERFQ_DLAA 5
// NVSDK_NGX_DLSS_Feature_Flags: motion vectors at the render size; reversed depth (near at 1) as vk_taa.c finds it from
// the projection; the picture is tone-mapped already (no IsHDR, no auto exposure).
#define NGX_FLAG_MV_LOWRES (1 << 1)
#define NGX_FLAG_DEPTH_INVERTED (1 << 3)

typedef struct NgxParam NgxParam;
typedef struct NgxHandle NgxHandle;
typedef struct { void *slot[17]; } NgxParamVtbl;
struct NgxParam { const NgxParamVtbl *vt; };
typedef struct {  // NVSDK_NGX_ImageViewInfo_VK
    VkImageView view;
    VkImage image;
    VkImageSubresourceRange range;
    VkFormat format;
    unsigned width, height;
} NgxImageView;
typedef struct {  // NVSDK_NGX_Resource_VK
    union { NgxImageView img; struct { VkBuffer buffer; unsigned bytes; } buf; } r;
    int type;  // 0 image view, 1 buffer
    _Bool rw;
} NgxResourceVK;

typedef NgxResult (*PFN_Init)(unsigned long long app, const wchar_t *path, VkInstance, VkPhysicalDevice, VkDevice,
                              PFN_vkGetInstanceProcAddr, PFN_vkGetDeviceProcAddr, int version, const void *info);
typedef NgxResult (*PFN_ReqExt)(unsigned *ni, const char ***ie, unsigned *nd, const char ***de);
typedef NgxResult (*PFN_Params)(NgxParam **);
typedef NgxResult (*PFN_DestroyParams)(NgxParam *);
typedef NgxResult (*PFN_Create)(VkCommandBuffer, int feature, NgxParam *, NgxHandle **);
typedef NgxResult (*PFN_Evaluate)(VkCommandBuffer, const NgxHandle *, const NgxParam *, void *callback);
typedef NgxResult (*PFN_Release)(NgxHandle *);
typedef NgxResult (*PFN_Shutdown)(VkDevice);

static struct {
    HMODULE core;
    PFN_Init init;
    PFN_ReqExt req_ext;
    PFN_Params caps, alloc_params;
    PFN_DestroyParams destroy_params;
    PFN_Create create;
    PFN_Evaluate evaluate;
    PFN_Release release;
    PFN_Shutdown shutdown;
    int tried, up, loaded, available, failed;
    NgxParam *params;
    NgxHandle *feat;
    uint32_t fw, fh;
} N;

typedef void (*SetI)(NgxParam *, const char *, int);
typedef void (*SetU)(NgxParam *, const char *, unsigned);
typedef void (*SetF)(NgxParam *, const char *, float);
typedef void (*SetP)(NgxParam *, const char *, void *);
typedef NgxResult (*GetI)(const NgxParam *, const char *, int *);
typedef NgxResult (*GetU)(const NgxParam *, const char *, unsigned *);
typedef NgxResult (*GetF)(const NgxParam *, const char *, float *);
typedef NgxResult (*GetP)(const NgxParam *, const char *, void **);
enum { S_P = 0, S_D12, S_D11, S_I, S_U32, S_D, S_F, S_U64, G_P, G_D12, G_D11, G_I, G_U32, G_D, G_F, G_U64 };
static void p_seti(NgxParam *p, const char *n, int v) { ((SetI)p->vt->slot[S_I])(p, n, v); }
static void p_setu(NgxParam *p, const char *n, unsigned v) { ((SetU)p->vt->slot[S_U32])(p, n, v); }
static void p_setf(NgxParam *p, const char *n, float v) { ((SetF)p->vt->slot[S_F])(p, n, v); }
static void p_setp(NgxParam *p, const char *n, void *v) { ((SetP)p->vt->slot[S_P])(p, n, v); }
static int p_geti(const NgxParam *p, const char *n, int *v) { return NGX_OK(((GetI)p->vt->slot[G_I])(p, n, v)); }
static int p_getu(const NgxParam *p, const char *n, unsigned *v) { return NGX_OK(((GetU)p->vt->slot[G_U32])(p, n, v)); }
static int p_getf(const NgxParam *p, const char *n, float *v) { return NGX_OK(((GetF)p->vt->slot[G_F])(p, n, v)); }
static int p_getp(const NgxParam *p, const char *n, void **v) { return NGX_OK(((GetP)p->vt->slot[G_P])(p, n, v)); }

int vk_dlaa_wanted(void) {
    static int wanted = -1;
    if (wanted < 0) wanted = rt_knob("DLAA", "0");
    return wanted;
}

static int core_load(void) {
    if (N.tried) return N.loaded;
    N.tried = 1;
    char dir[MAX_PATH] = {0}, path[MAX_PATH + 32];
    DWORD n = sizeof dir;
    if (RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\NVIDIA Corporation\\Global\\NGXCore", "FullPath", RRF_RT_REG_SZ, NULL,
                     dir, &n) != ERROR_SUCCESS) {
        rt_log("dlaa: the driver has no NGX core (no NVIDIA RTX driver): DLAA off");
        return 0;
    }
    snprintf(path, sizeof path, "%s\\_nvngx.dll", dir);
    N.core = LoadLibraryA(path);
    if (!N.core) {
        rt_log("dlaa: cannot load the driver's NGX core (error %lu): DLAA off", GetLastError());
        return 0;
    }
#define FN(f, name) N.f = (void *)GetProcAddress(N.core, name)
    FN(init, "NVSDK_NGX_VULKAN_Init_Ext2");
    FN(req_ext, "NVSDK_NGX_VULKAN_RequiredExtensions");
    FN(caps, "NVSDK_NGX_VULKAN_GetCapabilityParameters");
    FN(alloc_params, "NVSDK_NGX_VULKAN_AllocateParameters");
    FN(destroy_params, "NVSDK_NGX_VULKAN_DestroyParameters");
    FN(create, "NVSDK_NGX_VULKAN_CreateFeature");
    FN(evaluate, "NVSDK_NGX_VULKAN_EvaluateFeature");
    FN(release, "NVSDK_NGX_VULKAN_ReleaseFeature");
    FN(shutdown, "NVSDK_NGX_VULKAN_Shutdown1");
#undef FN
    if (!N.init || !N.req_ext || !N.caps || !N.alloc_params || !N.create || !N.evaluate || !N.release) {
        rt_log("dlaa: the driver's NGX core lacks Vulkan entry points: DLAA off");
        return 0;
    }
    N.loaded = 1;
    rt_log("dlaa: NGX core loaded from the driver");
    return 1;
}

static int listed(const char *const *list, uint32_t n, const char *s) {
    for (uint32_t i = 0; i < n; i++)
        if (!strcmp(list[i], s)) return 1;
    return 0;
}

void vk_dlaa_instance_exts(const char **ext, uint32_t *n, uint32_t max) {
    unsigned ni = 0, nd = 0;
    const char **ie = NULL, **de = NULL;
    if (!vk_dlaa_wanted() || !core_load() || !NGX_OK(N.req_ext(&ni, &ie, &nd, &de))) return;
    for (unsigned i = 0; i < ni; i++)
        if (!listed(ext, *n, ie[i])) {
            if (*n < max) ext[(*n)++] = ie[i];
            else rt_log("dlaa: no room for instance extension %s", ie[i]);
        }
}

void vk_dlaa_device_exts(const VkExtensionProperties *have, uint32_t nhave, const char **ext, uint32_t *n, uint32_t max) {
    unsigned ni = 0, nd = 0;
    const char **ie = NULL, **de = NULL;
    if (!vk_dlaa_wanted() || !core_load() || !NGX_OK(N.req_ext(&ni, &ie, &nd, &de))) return;
    for (unsigned i = 0; i < nd; i++) {
        int ok = 0;
        for (uint32_t k = 0; k < nhave && !ok; k++) ok = !strcmp(have[k].extensionName, de[i]);
        if (!ok) rt_log("dlaa: the device lacks %s, which NGX asks for", de[i]);
        else if (!listed(ext, *n, de[i])) {
            if (*n < max) ext[(*n)++] = de[i];
            else rt_log("dlaa: no room for device extension %s", de[i]);
        }
    }
}

static int vtable_ok(void) {
    NgxParam *p = NULL;
    if (!NGX_OK(N.alloc_params(&p)) || !p) return 0;
    int i = 0;
    unsigned u = 0;
    float f = 0;
    void *v = NULL;
    p_seti(p, "KarPur.Test.I", -12345);
    p_setu(p, "KarPur.Test.U", 4000000000u);
    p_setf(p, "KarPur.Test.F", 1.25f);
    p_setp(p, "KarPur.Test.P", (void *)p);
    int ok = p_geti(p, "KarPur.Test.I", &i) && i == -12345 && p_getu(p, "KarPur.Test.U", &u) && u == 4000000000u &&
             p_getf(p, "KarPur.Test.F", &f) && f == 1.25f && p_getp(p, "KarPur.Test.P", &v) && v == (void *)p;
    if (N.destroy_params) N.destroy_params(p);
    if (!ok) rt_log("dlaa: NGX parameter layout check failed (i=%d u=%u f=%g): DLAA off", i, u, f);
    return ok;
}

void vk_dlaa_init(VkInstance inst, VkPhysicalDevice phys, VkDevice dev) {
    if (!vk_dlaa_wanted()) return;
    if (!core_load()) return;
    wchar_t wdir[MAX_PATH];
    DWORD len = GetModuleFileNameW(NULL, wdir, MAX_PATH);
    while (len && wdir[len - 1] != L'\\') len--;
    wdir[len] = 0;
    // 231313132: the application id NVIDIA's DLSS samples use for builds without an NVIDIA-issued id
    NgxResult r = N.init(231313132ull, wdir, inst, phys, dev, vkGetInstanceProcAddr, vkGetDeviceProcAddr, NGX_VERSION_API, NULL);
    if (!NGX_OK(r)) {
        rt_log("dlaa: NGX init failed (%08x): DLAA off", (unsigned)r);
        return;
    }
    N.up = 1;
    if (!vtable_ok()) return;
    NgxParam *caps = NULL;
    int avail = 0, driver = 0;
    if (NGX_OK(N.caps(&caps)) && caps) {
        p_geti(caps, "SuperSampling.Available", &avail);
        p_geti(caps, "SuperSampling.NeedsUpdatedDriver", &driver);
    }
    N.available = avail != 0;
    rt_log("dlaa: NGX up; DLSS %s%s", avail ? "available" : "NOT available (is nvngx_dlss.dll next to karpur.exe?)",
           driver ? "; the driver needs an update" : "");
}

int vk_dlaa_available(void) { return vk_dlaa_wanted() && N.available && !N.failed; }

static NgxResourceVK res_image(VkImageView v, VkImage img, VkFormat f, uint32_t w, uint32_t h, VkImageAspectFlags a, int rw) {
    NgxResourceVK r;
    memset(&r, 0, sizeof r);
    r.r.img = (NgxImageView){v, img, {a, 0, 1, 0, 1}, f, w, h};
    r.type = 0;
    r.rw = rw != 0;
    return r;
}

static int fail(const char *what, NgxResult r) {
    rt_log("dlaa: %s failed (%08x): DLAA off for this run", what, (unsigned)r);
    N.failed = 1;
    return 0;
}

int vk_dlaa_record(VkDevice dev, VkCommandBuffer cmd, const VkTaaFrame *f) {
    if (!vk_dlaa_available()) return 0;
    if (!N.params && !NGX_OK(N.alloc_params(&N.params))) return fail("parameter allocation", 0);
    NgxParam *p = N.params;
    int reset = f->reset;
    if (N.feat && (N.fw != f->w || N.fh != f->h)) {   // another size: a new feature
        vk_queue_lock();   // vkDeviceWaitIdle needs the queue externally synchronized
        vkDeviceWaitIdle(dev);
        vk_queue_unlock();
        N.release(N.feat);
        N.feat = NULL;
    }
    if (!N.feat) {
        p_setu(p, "CreationNodeMask", 1);
        p_setu(p, "VisibilityNodeMask", 1);
        p_setu(p, "Width", f->w);
        p_setu(p, "Height", f->h);
        p_setu(p, "OutWidth", f->ow);
        p_setu(p, "OutHeight", f->oh);
        p_seti(p, "DLSS.Feature.Create.Flags", NGX_FLAG_MV_LOWRES | (f->depth_inv ? NGX_FLAG_DEPTH_INVERTED : 0));
        p_seti(p, "DLSS.Enable.Output.Subrects", 0);
        // the model: TOTK_DLAA_PRESET=<n> (NVSDK_NGX_DLSS_Hint_Render_Preset: 0 the DLL's default, 6 F, 10 J, 11 K
        // transformer), as a hint for every quality value; default K (an older DLSS ignores what it does not know)
        {
            const char *e = cfg_get("TOTK_DLAA_PRESET");
            unsigned pr = e && *e ? (unsigned)atoi(e) : 11u;
            static const char *const q[] = {"DLAA", "Quality", "Balanced", "Performance", "UltraPerformance", "UltraQuality"};
            char k[64];
            for (int i = 0; i < 6; i++) snprintf(k, sizeof k, "DLSS.Hint.Render.Preset.%s", q[i]), p_setu(p, k, pr);
        }
        // DLAA's quality value; an older DLSS (before it had one) gets Max Quality at the same size
        p_seti(p, "PerfQualityValue", NGX_PERFQ_DLAA);
        NgxResult r = N.create(cmd, NGX_FEATURE_SUPERSAMPLING, p, &N.feat);
        if (!NGX_OK(r) || !N.feat) {
            N.feat = NULL;
            p_seti(p, "PerfQualityValue", NGX_PERFQ_MAXQUALITY);
            r = N.create(cmd, NGX_FEATURE_SUPERSAMPLING, p, &N.feat);
            if (!NGX_OK(r) || !N.feat) { N.feat = NULL; return fail("DLSS feature creation", r); }
            rt_log("dlaa: this DLSS has no DLAA quality value: Max Quality at the scene's size");
        }
        N.fw = f->w, N.fh = f->h;
        reset = 1;
        rt_log("dlaa: DLSS feature created at %ux%u -> %ux%u", f->w, f->h, f->ow, f->oh);
    }
    NgxResourceVK rc = res_image(f->color_view, f->color, f->color_format, f->w, f->h, VK_IMAGE_ASPECT_COLOR_BIT, 0);
    NgxResourceVK ro = res_image(f->out_view, f->out, f->out_format, f->ow, f->oh, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    NgxResourceVK rz = res_image(f->depth_view, f->depth, f->depth_format, f->w, f->h, VK_IMAGE_ASPECT_DEPTH_BIT, 0);
    NgxResourceVK rm = res_image(f->mv_view, f->mv, VK_FORMAT_R16G16_SFLOAT, f->w, f->h, VK_IMAGE_ASPECT_COLOR_BIT, 0);
    p_setp(p, "Color", &rc);
    p_setp(p, "Output", &ro);
    p_setp(p, "Depth", &rz);
    p_setp(p, "MotionVectors", &rm);
    p_setf(p, "Jitter.Offset.X", f->jx);
    p_setf(p, "Jitter.Offset.Y", f->jy);
    p_setf(p, "Sharpness", 0.0f);
    p_seti(p, "Reset", reset);
    p_setf(p, "MV.Scale.X", 1.0f);   // pixels, current to previous
    p_setf(p, "MV.Scale.Y", 1.0f);
    p_setu(p, "DLSS.Render.Subrect.Dimensions.Width", f->w);
    p_setu(p, "DLSS.Render.Subrect.Dimensions.Height", f->h);
    p_setf(p, "DLSS.Pre.Exposure", 1.0f);
    p_setf(p, "DLSS.Exposure.Scale", 1.0f);
    p_setf(p, "FrameTimeDeltaInMsec", f->dt_ms);
    NgxResult r = N.evaluate(cmd, N.feat, p, NULL);
    if (!NGX_OK(r)) return fail("DLSS evaluation", r);
    return 1;
}

void vk_dlaa_shutdown(VkDevice dev) {
    if (!N.up) return;
    if (N.feat) N.release(N.feat), N.feat = NULL;
    if (N.params && N.destroy_params) N.destroy_params(N.params), N.params = NULL;
    if (N.shutdown) N.shutdown(dev);
    N.up = 0, N.available = 0;
}

#else   // NGX is Windows-only
int vk_dlaa_wanted(void) { return 0; }
void vk_dlaa_instance_exts(const char **ext, uint32_t *n, uint32_t max) { (void)ext, (void)n, (void)max; }
void vk_dlaa_device_exts(const VkExtensionProperties *have, uint32_t nhave, const char **ext, uint32_t *n, uint32_t max) {
    (void)have, (void)nhave, (void)ext, (void)n, (void)max;
}
void vk_dlaa_init(VkInstance inst, VkPhysicalDevice phys, VkDevice dev) { (void)inst, (void)phys, (void)dev; }
int vk_dlaa_available(void) { return 0; }
int vk_dlaa_record(VkDevice dev, VkCommandBuffer cmd, const VkTaaFrame *f) { (void)dev, (void)cmd, (void)f; return 0; }
void vk_dlaa_shutdown(VkDevice dev) { (void)dev; }
#endif
