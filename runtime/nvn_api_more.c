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

// ---------- Transform feedback: not implemented yet, ignored so the game goes on
// Violet's intro uses transform feedback (vertex shader output written back to buffers and drawn again, typically
// for particles). Until the Vulkan back end records it (VK_EXT_transform_feedback), binding, begin/end/pause/resume
// do nothing and DrawTransformFeedback draws nothing: what the game draws from captured vertices is missing on
// screen, everything else renders. Each entry point logs its first call.
static void tf_ignored(uint8_t *once, const char *name) {
    if (!__atomic_exchange_n(once, 1, __ATOMIC_RELAXED))
        rt_log("nvn: %s: transform feedback is not implemented, ignored (logged once)", name);
}
#define TF_IGNORED(n) NVN_API(n) { (void)c; static uint8_t once; tf_ignored(&once, #n); }
// void nvnCommandBufferBindTransformFeedbackBuffer(NVNcommandBuffer *cb, int index, NVNbufferAddress buffer,
// size_t size)
TF_IGNORED(nvnCommandBufferBindTransformFeedbackBuffer)
// void nvnCommandBufferBindTransformFeedbackBuffers(NVNcommandBuffer *cb, int first, int count,
// const NVNbufferRange *buffers)
TF_IGNORED(nvnCommandBufferBindTransformFeedbackBuffers)
// void nvnCommandBuffer{Begin,End,Pause,Resume}TransformFeedback(NVNcommandBuffer *cb, NVNbufferAddress control)
TF_IGNORED(nvnCommandBufferBeginTransformFeedback)
TF_IGNORED(nvnCommandBufferEndTransformFeedback)
TF_IGNORED(nvnCommandBufferPauseTransformFeedback)
TF_IGNORED(nvnCommandBufferResumeTransformFeedback)
// void nvnCommandBufferDrawTransformFeedback(NVNcommandBuffer *cb, NVNdrawPrimitive mode, NVNbufferAddress control)
TF_IGNORED(nvnCommandBufferDrawTransformFeedback)
#undef TF_IGNORED
