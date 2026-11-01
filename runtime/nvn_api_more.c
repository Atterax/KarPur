// NVN entry points Violet resolves beyond the ones TotK's NVN front end implements: getters that report stored object
// state, debugging and checkpoint switches that only matter to NVN tools, and queue memory counters. None of them
// changes rendering results. Written for KarPur (MIT); the object layouts are those of nvn_obj.h.
//
// Command-buffer entry points that would record GPU work (transform feedback, tessellation levels, indirect counts,
// integer clears, downsampling, fence syncs inside command buffers) are not here: they stay weak stand-ins that stop
// with the proc's name, so a call shows up instead of silently drawing something wrong.
#include <stdint.h>
#include <string.h>

#include "gfx.h"
#include "host.h"
#include "nvn_api.h"
#include "platform/platform.h"

// ---------- Texture and sampler pools
// const NVNmemoryPool *nvnTexturePoolGetMemoryPool(const NVNtexturePool *tp)
NVN_API(nvnTexturePoolGetMemoryPool) { ret_x(c, (uint64_t)(uintptr_t)((const dpool_t *)arg_p(c, 0))->pool); }
// ptrdiff_t nvnTexturePoolGetMemoryOffset(const NVNtexturePool *tp)
NVN_API(nvnTexturePoolGetMemoryOffset) { ret_x(c, (uint64_t)((const dpool_t *)arg_p(c, 0))->off); }
// int nvnTexturePoolGetSize(const NVNtexturePool *tp): the descriptor count given to nvnTexturePoolInitialize
NVN_API(nvnTexturePoolGetSize) { ret_x(c, (uint64_t)(int64_t)((const dpool_t *)arg_p(c, 0))->n); }
// void nvnTexturePoolRegisterImage(const NVNtexturePool *tp, int id, const NVNtexture *t, const NVNtextureView *v):
// image descriptors share the texture pool's id space, and nvnDeviceGetImageHandle returns the id itself, so the
// back end's texture table serves image binds as well
NVN_API(nvnTexturePoolRegisterImage) {
    gfx_backend()->texture_register((int)arg_x(c, 1), arg_p(c, 2), arg_p(c, 3));
}
NVN_API(nvnSamplerPoolGetMemoryPool) { ret_x(c, (uint64_t)(uintptr_t)((const dpool_t *)arg_p(c, 0))->pool); }
NVN_API(nvnSamplerPoolGetMemoryOffset) { ret_x(c, (uint64_t)((const dpool_t *)arg_p(c, 0))->off); }
NVN_API(nvnSamplerPoolGetSize) { ret_x(c, (uint64_t)(int64_t)((const dpool_t *)arg_p(c, 0))->n); }
// void nvnSamplerPoolRegisterSamplerBuilder(const NVNsamplerPool *sp, int id, const NVNsamplerBuilder *b): as
// RegisterSampler, with the parameters taken from the builder
NVN_API(nvnSamplerPoolRegisterSamplerBuilder) {
    gfx_backend()->sampler_register((int)arg_x(c, 1), &((const samp_builder_t *)arg_p(c, 2))->p);
}

// ---------- Builders: report what the setters stored
// int nvnDeviceBuilderGetFlags(const NVNdeviceBuilder *b)
NVN_API(nvnDeviceBuilderGetFlags) { ret_x(c, ((const dev_builder_t *)arg_p(c, 0))->flags); }
// NVNdevice *nvnMemoryPoolBuilderGetDevice / void *GetMemory / size_t GetSize / int GetFlags(const NVNmemoryPoolBuilder *b)
NVN_API(nvnMemoryPoolBuilderGetDevice) { ret_x(c, (uint64_t)(uintptr_t)((const pool_builder_t *)arg_p(c, 0))->dev); }
NVN_API(nvnMemoryPoolBuilderGetMemory) { ret_x(c, (uint64_t)(uintptr_t)((const pool_builder_t *)arg_p(c, 0))->mem); }
NVN_API(nvnMemoryPoolBuilderGetSize) { ret_x(c, ((const pool_builder_t *)arg_p(c, 0))->size); }
NVN_API(nvnMemoryPoolBuilderGetFlags) { ret_x(c, ((const pool_builder_t *)arg_p(c, 0))->flags); }
// NVNdevice *nvnBufferBuilderGetDevice / NVNmemoryPool *GetMemoryPool / ptrdiff_t GetMemoryOffset / size_t GetSize
NVN_API(nvnBufferBuilderGetDevice) { ret_x(c, (uint64_t)(uintptr_t)((const buf_builder_t *)arg_p(c, 0))->dev); }
NVN_API(nvnBufferBuilderGetMemoryPool) { ret_x(c, (uint64_t)(uintptr_t)((const buf_builder_t *)arg_p(c, 0))->pool); }
NVN_API(nvnBufferBuilderGetMemoryOffset) { ret_x(c, (uint64_t)((const buf_builder_t *)arg_p(c, 0))->off); }
NVN_API(nvnBufferBuilderGetSize) { ret_x(c, ((const buf_builder_t *)arg_p(c, 0))->size); }
// const NVNmemoryPool *nvnEventBuilderGetMemoryPool / ptrdiff_t GetMemoryOffset(const NVNeventBuilder *b)
NVN_API(nvnEventBuilderGetMemoryPool) { ret_x(c, (uint64_t)(uintptr_t)((const ev_builder_t *)arg_p(c, 0))->pool); }
NVN_API(nvnEventBuilderGetMemoryOffset) { ret_x(c, (uint64_t)((const ev_builder_t *)arg_p(c, 0))->off); }
// const NVNmemoryPool *nvnEventBuilderGetStorage(const NVNeventBuilder *b, int64_t *offset)
NVN_API(nvnEventBuilderGetStorage) {
    const ev_builder_t *b = arg_p(c, 0);
    int64_t *off = arg_p(c, 1);
    if (off) *off = b->off;
    ret_x(c, (uint64_t)(uintptr_t)b->pool);
}
// const NVNmemoryPool *nvnEventGetMemoryPool / ptrdiff_t nvnEventGetMemoryOffset(const NVNevent *e)
NVN_API(nvnEventGetMemoryPool) { ret_x(c, (uint64_t)(uintptr_t)((const ev_t *)arg_p(c, 0))->pool); }
NVN_API(nvnEventGetMemoryOffset) { ret_x(c, (uint64_t)((const ev_t *)arg_p(c, 0))->off); }
// Queue builder getters: NVNdevice *GetDevice, int GetFlags, size_t Get{Command,Compute,Control}MemorySize,
// size_t GetCommandFlushThreshold, void *GetMemory, size_t GetMemorySize, NVNqueuePriority GetQueuePriority
#define QB(c) ((queue_builder_t *)arg_p(c, 0))
NVN_API(nvnQueueBuilderGetDevice) { ret_x(c, (uint64_t)(uintptr_t)QB(c)->dev); }
NVN_API(nvnQueueBuilderGetFlags) { ret_x(c, QB(c)->flags); }
NVN_API(nvnQueueBuilderGetCommandMemorySize) { ret_x(c, QB(c)->cmd_sz); }
NVN_API(nvnQueueBuilderGetComputeMemorySize) { ret_x(c, QB(c)->comp_sz); }
NVN_API(nvnQueueBuilderGetControlMemorySize) { ret_x(c, QB(c)->ctl_sz); }
NVN_API(nvnQueueBuilderGetCommandFlushThreshold) { ret_x(c, QB(c)->flush); }
NVN_API(nvnQueueBuilderGetMemory) { ret_x(c, (uint64_t)(uintptr_t)QB(c)->mem); }
NVN_API(nvnQueueBuilderGetMemorySize) { ret_x(c, QB(c)->mem_sz); }
NVN_API(nvnQueueBuilderGetQueuePriority) { ret_x(c, (uint64_t)(int64_t)QB(c)->priority); }
// void nvnQueueBuilderSetQueuePriority(NVNqueueBuilder *b, NVNqueuePriority priority): stored only; the host has one
// queue and the Vulkan back end submits in the order the game does
NVN_API(nvnQueueBuilderSetQueuePriority) { QB(c)->priority = (int32_t)arg_x(c, 1); }
#undef QB
// uint64_t nvnBufferGetDebugID(const NVNbuffer *b): unique while the buffer lives, as nvnSamplerGetDebugID
NVN_API(nvnBufferGetDebugID) { ret_x(c, arg_x(c, 0)); }

// ---------- Device state queries
// NVNwindowOriginMode nvnDeviceGetWindowOriginMode / NVNdepthMode nvnDeviceGetDepthMode(const NVNdevice *d)
NVN_API(nvnDeviceGetWindowOriginMode) { ret_x(c, ((const dev_t_ *)arg_p(c, 0))->origin); }
NVN_API(nvnDeviceGetDepthMode) { ret_x(c, ((const dev_t_ *)arg_p(c, 0))->depth_mode); }

// ---------- State objects
#define ST(T) ((T *)arg_p(c, 0))
// Only blend advanced mode "none" exists here (nvnBlendStateSetAdvancedMode stops on any other), and its three
// modifiers are not stored, so the getters report the NVN defaults.
NVN_API(nvnBlendStateGetAdvancedMode) { ret_x(c, 0); }   // NVN_BLEND_ADVANCED_MODE_BLEND_NONE
NVN_API(nvnBlendStateGetAdvancedOverlap) { ret_x(c, NVN_BLEND_ADVANCED_OVERLAP_UNCORRELATED); }
NVN_API(nvnBlendStateGetAdvancedPremultipliedSrc) { ret_x(c, 1); }
NVN_API(nvnBlendStateGetAdvancedNormalizedDst) { ret_x(c, 1); }
// Multisample state: coverage modulation, coverage-to-colour and the sample-location grid are stored and reported; the
// Vulkan back end does not apply them (Violet's use of them is not known yet).
NVN_API(nvnMultisampleStateGetAlphaToCoverageDither) { ret_x(c, ST(ms_st)->a2c_dither); }
NVN_API(nvnMultisampleStateGetCoverageModulationMode) { ret_x(c, ST(ms_st)->cov_mod); }
NVN_API(nvnMultisampleStateGetCoverageToColorEnable) { ret_x(c, ST(ms_st)->cov2color_en); }
NVN_API(nvnMultisampleStateGetCoverageToColorOutput) { ret_x(c, ST(ms_st)->cov2color_out); }
NVN_API(nvnMultisampleStateGetSampleLocationsEnable) { ret_x(c, ST(ms_st)->sl_en); }
NVN_API(nvnMultisampleStateGetSampleLocationsGridEnable) { ret_x(c, ST(ms_st)->sl_grid); }
NVN_API(nvnMultisampleStateSetCoverageModulationMode) { ST(ms_st)->cov_mod = (uint8_t)arg_x(c, 1); }
NVN_API(nvnMultisampleStateSetCoverageToColorEnable) { ST(ms_st)->cov2color_en = nvn_arg_bool(c, 1) != 0; }
NVN_API(nvnMultisampleStateSetCoverageToColorOutput) { ST(ms_st)->cov2color_out = (uint8_t)arg_x(c, 1); }
NVN_API(nvnMultisampleStateSetSampleLocationsGridEnable) { ST(ms_st)->sl_grid = nvn_arg_bool(c, 1) != 0; }
// void nvnMultisampleStateGetSampleLocationsGrid(NVNmultisampleState *s, int *width, int *height): the grid NVN uses
// when none was set, one sample location per pixel
NVN_API(nvnMultisampleStateGetSampleLocationsGrid) {
    int32_t *w = arg_p(c, 1), *h = arg_p(c, 2);
    if (w) *w = 1;
    if (h) *h = 1;
}
#undef ST

// ---------- Debugging, checkpoints and the shader cache: meaningful only to NVN tools
NVN_API(nvnDeviceSetAutomaticCheckpointsByDebugGroup) { (void)c; }
NVN_API(nvnDeviceSetAutomaticCheckpointsEnable) { (void)c; }
NVN_API(nvnDeviceSetAutomaticCheckpointsSamplingInterval) { (void)c; }
NVN_API(nvnDeviceSetCheckpointCallStacksEnable) { (void)c; }
NVN_API(nvnDeviceSetCheckpointMaxCallStacksDepth) { (void)c; }
// void nvnDeviceSetIntermediateShaderCache(NVNdevice *d, int entries): the driver's GLSLC cache; shaders arrive
// compiled, and the host translates them with its own cache (gfx_shader.c)
NVN_API(nvnDeviceSetIntermediateShaderCache) { (void)c; }
// void nvnDeviceWalkDebugDatabase(const NVNdevice *d, NVNdebugObjectType type, callback, void *data): the host keeps no
// debug database, so the callback is never called
NVN_API(nvnDeviceWalkDebugDatabase) { (void)c; }
// NVNdebugDomainId nvnDeviceGenerateDebugDomainId(const NVNdevice *d, const char *name): a fresh id per call
NVN_API(nvnDeviceGenerateDebugDomainId) {
    static int32_t next;
    ret_x(c, (uint64_t)(uint32_t)__atomic_add_fetch(&next, 1, __ATOMIC_RELAXED));
}

// ---------- Queue memory counters: the host records commands into its own memory, so the driver's usage is 0
NVN_API(nvnQueueGetTotalCommandMemoryUsed) { ret_x(c, 0); }
NVN_API(nvnQueueGetTotalComputeMemoryUsed) { ret_x(c, 0); }
NVN_API(nvnQueueGetTotalControlMemoryUsed) { ret_x(c, 0); }
NVN_API(nvnQueueResetMemoryUsageCounts) { (void)c; }

// ---------- Hints and waits that the host's single in-order queue already satisfies
// void nvnQueueWaitSync(NVNqueue *q, const NVNsync *s): later submissions wait on the GPU for the sync. The host has
// one queue and the back end runs submissions in order, so everything fenced before is already ahead of them.
NVN_API(nvnQueueWaitSync) { (void)c; }
// void nvnCommandBufferSetStencilCullCriteria(NVNcommandBuffer *cb, NVNstencilFunc func, int ref, int mask): a hint
// for Maxwell's stencil culling hardware; stencil results do not depend on it
NVN_API(nvnCommandBufferSetStencilCullCriteria) { (void)c; }
// void nvnCommandBufferBindCoverageModulationTable(NVNcommandBuffer *cb, const float *entries): used only with a
// coverage modulation mode, which the Vulkan back end does not apply (see the multisample state above)
NVN_API(nvnCommandBufferBindCoverageModulationTable) { (void)c; }

// ---------- Transform feedback: not implemented yet, ignored so the game goes on; with a layout report
// Violet draws its characters through transform feedback (vertex shader output written back to buffers and drawn
// again; GPU skinning, most likely). Until the Vulkan back end records it (VK_EXT_transform_feedback), binding,
// begin/end/pause/resume do nothing and DrawTransformFeedback draws nothing.
//
// The layout (which vertex outputs go to which buffer, the strides) is not an NVN call: the driver takes it from the
// program's 0x880-byte control section, at offsets nobody has documented. To find them this file compares control
// sections: every vertex shader bound without a BeginTransformFeedback before the next program bind counts as
// "plain" and ORs the nonzero 32-bit words of its control into one mask; the vertex shaders bound when a transform
// feedback begins are kept (up to TF_KEEP). When 1, 4 and 16 distinct ones were seen (so a game with few of them still
// gets a report; later reports are sharper), the words set in every one of them and in no plain shader are logged with their values in the first three (small numbers: strides, counts,
// output locations; no code). The entry points also log their first calls' arguments.
#define CTRL_SIZE 0x880
#define CTRL_WORDS (0x700 / 4)   // below the c1/length words at 0x700
#define TF_KEEP 16
#define SEEN_CAP 4096

static PlatMutex tf_lock = PLAT_MUTEX_INIT;
static uint8_t plain_nz[CTRL_WORDS];            // a plain vertex shader had this word nonzero
static uint8_t tf_ctrl[TF_KEEP][CTRL_SIZE];     // copies of transform feedback vertex shader controls
static const void *tf_ctrl_at[TF_KEEP];
static int tf_count;
static const void *seen_plain[SEEN_CAP];        // vertex shader controls already ORed into plain_nz
static int nseen_plain;
static _Thread_local const void *last_vs;       // vertex control of the program bound last on this thread
static _Thread_local int last_vs_tf;            // a transform feedback began since that bind

static const uint8_t *vs_control(const prog_t *p) {
    if (!p || p->magic != NVN_PROG_MAGIC) return NULL;
    for (uint32_t i = 0; i < p->nstage && i < 6; i++) {
        const uint8_t *ctl = p->sd[i].control;
        uint32_t stage;
        if (!ctl || !plat_mem_readable((uintptr_t)ctl, CTRL_SIZE)) continue;
        memcpy(&stage, ctl + 0x714, 4);
        if (stage == 0) return ctl;   // 0: vertex (tools/shader_census.py STAGE_IDS)
    }
    return NULL;
}

static void plain_add(const uint8_t *ctl) {   // under tf_lock
    for (int i = 0; i < nseen_plain; i++)
        if (seen_plain[i] == ctl) return;
    for (int i = 0; i < tf_count && i < TF_KEEP; i++)
        if (tf_ctrl_at[i] == ctl) return;    // also used with transform feedback elsewhere: not plain
    if (nseen_plain < SEEN_CAP) seen_plain[nseen_plain++] = ctl;
    for (int w = 0; w < CTRL_WORDS; w++) {
        uint32_t v;
        memcpy(&v, ctl + 4 * w, 4);
        if (v) plain_nz[w] = 1;
    }
}

static void tf_report(void) {   // under tf_lock
    int n = tf_count < TF_KEEP ? tf_count : TF_KEEP, found = 0;
    rt_log("tf: layout report over %d transform feedback vertex shaders and %d plain ones (words set in all of the "
           "former, in none of the latter; values in the first three):", n, nseen_plain);
    for (int w = 0; w < CTRL_WORDS && found < 96; w++) {
        if (plain_nz[w]) continue;
        int all = 1;
        uint32_t v[3] = {0, 0, 0};
        for (int k = 0; k < n && all; k++) {
            uint32_t x;
            memcpy(&x, tf_ctrl[k] + 4 * w, 4);
            if (!x) all = 0;
            if (k < 3) v[k] = x;
        }
        if (!all) continue;
        rt_log("tf:   +%#05x: %#x %#x %#x", 4 * w, v[0], v[1], v[2]);
        found++;
    }
    // Words that are zero in every plain shader and set in some (not all) transform feedback shaders: per-buffer
    // fields of buffers only some programs use.
    int some = 0;
    for (int w = 0; w < CTRL_WORDS && some < 96; w++) {
        if (plain_nz[w]) continue;
        int cnt = 0;
        for (int k = 0; k < n; k++) {
            uint32_t x;
            memcpy(&x, tf_ctrl[k] + 4 * w, 4);
            cnt += x != 0;
        }
        if (cnt && cnt < n) {
            if (!some++) rt_log("tf: words set in some transform feedback shaders only (count of %d):", n);
            rt_log("tf:   +%#05x: in %d", 4 * w, cnt);
        }
    }
    rt_log("tf: %d words in all, %d in some", found, some);
}

// Called by nvnCommandBufferBindProgram (nvn_api_cmd.c).
void nvn_tf_note_program(const void *prog) {
    const uint8_t *vs = vs_control(prog);
    if (last_vs && !last_vs_tf && last_vs != vs) {
        plat_mutex_lock(&tf_lock);
        plain_add(last_vs);
        plat_mutex_unlock(&tf_lock);
    }
    last_vs = vs;
    last_vs_tf = 0;
}

static void tf_begin_note(void) {
    last_vs_tf = 1;
    if (!last_vs) return;
    plat_mutex_lock(&tf_lock);
    int fresh = 1;
    for (int i = 0; i < tf_count && i < TF_KEEP && fresh; i++)
        if (tf_ctrl_at[i] == last_vs) fresh = 0;
    if (fresh && tf_count < TF_KEEP) {
        memcpy(tf_ctrl[tf_count], last_vs, CTRL_SIZE);
        tf_ctrl_at[tf_count] = last_vs;
        tf_count++;
        if (tf_count == 1 || tf_count == 4 || tf_count == TF_KEEP) tf_report();
    }
    plat_mutex_unlock(&tf_lock);
}

static int tf_note(int32_t *n, int max) { return __atomic_add_fetch(n, 1, __ATOMIC_RELAXED) <= max; }

// void nvnCommandBufferBindTransformFeedbackBuffer(NVNcommandBuffer *cb, int index, NVNbufferAddress buffer,
// size_t size)
NVN_API(nvnCommandBufferBindTransformFeedbackBuffer) {
    static int32_t n;
    if (tf_note(&n, 8))
        rt_log("tf: BindTransformFeedbackBuffer(index %d, %#llx, size %#llx)", (int)arg_x(c, 1),
               (unsigned long long)arg_x(c, 2), (unsigned long long)arg_x(c, 3));
}
// void nvnCommandBufferBindTransformFeedbackBuffers(NVNcommandBuffer *cb, int first, int count,
// const NVNbufferRange *buffers): NVNbufferRange = {NVNbufferAddress address; uint64_t size}
NVN_API(nvnCommandBufferBindTransformFeedbackBuffers) {
    static int32_t n;
    if (!tf_note(&n, 8)) return;
    int first = (int)arg_x(c, 1), count = (int)arg_x(c, 2);
    const uint64_t *r = arg_p(c, 3);
    rt_log("tf: BindTransformFeedbackBuffers(first %d, count %d)", first, count);
    for (int i = 0; r && i < count && i < 4; i++)
        rt_log("tf:   buffer %d: %#llx size %#llx", first + i, (unsigned long long)r[2 * i], (unsigned long long)r[2 * i + 1]);
}
// void nvnCommandBuffer{Begin,End,Pause,Resume}TransformFeedback(NVNcommandBuffer *cb, NVNbufferAddress control)
NVN_API(nvnCommandBufferBeginTransformFeedback) {
    static int32_t n;
    if (tf_note(&n, 4)) rt_log("tf: BeginTransformFeedback(control %#llx)", (unsigned long long)arg_x(c, 1));
    tf_begin_note();
}
NVN_API(nvnCommandBufferEndTransformFeedback) {
    static int32_t n;
    if (tf_note(&n, 4)) rt_log("tf: EndTransformFeedback(control %#llx)", (unsigned long long)arg_x(c, 1));
}
NVN_API(nvnCommandBufferPauseTransformFeedback) {
    static int32_t n;
    if (tf_note(&n, 4)) rt_log("tf: PauseTransformFeedback(control %#llx)", (unsigned long long)arg_x(c, 1));
}
NVN_API(nvnCommandBufferResumeTransformFeedback) {
    static int32_t n;
    if (tf_note(&n, 4)) rt_log("tf: ResumeTransformFeedback(control %#llx)", (unsigned long long)arg_x(c, 1));
}
// void nvnCommandBufferDrawTransformFeedback(NVNcommandBuffer *cb, NVNdrawPrimitive mode, NVNbufferAddress control)
NVN_API(nvnCommandBufferDrawTransformFeedback) {
    static int32_t n;
    if (tf_note(&n, 8))
        rt_log("tf: DrawTransformFeedback(mode %d, control %#llx)", (int)arg_x(c, 1), (unsigned long long)arg_x(c, 2));
}
