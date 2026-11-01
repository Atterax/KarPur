// Transform feedback in the Vulkan replay. Written for KarPur (MIT).
//
// Violet draws its characters through transform feedback: a vertex shader skins the vertices into three buffers
// (observed: 12, 4 and 4 bytes a vertex), later draws read them as plain vertex buffers. The game never calls
// DrawTransformFeedback. Not recorded into Vulkan yet: the buffers stay unwritten, so skinned models are invisible or
// drawn from stale data.
//
// For now this file keeps the bound buffers and the active state and finds the transform feedback layout. The layout
// (which vertex outputs go to which buffer, the strides) is not an NVN call: the driver takes it from the program's
// 0x880-byte control section, at offsets nobody has documented. Every draw notes its vertex shader: drawn while
// transform feedback is active it is a transform feedback shader (up to TF_KEEP are copied), else its nonzero 32-bit
// words are ORed into one "plain" mask. When 1, 4 and 16 distinct transform feedback shaders were seen, the words set
// in all of them and in no plain shader are logged with their values in the first three (small numbers: strides,
// counts, output locations; never code). The first transform feedback draws are logged with their program and buffer
// sizes.
//
// Program binds have a stage mask (NVNshaderStageBits): the stages in it take the program's shaders, a stage in the
// mask the program lacks is unbound (the program may be NULL), stages outside the mask keep theirs. Violet unbinds the
// fragment stage this way for its transform feedback draws (rasterizer discard on), keeping the vertex shader of an
// earlier bind. vk_exec_draw.c still treats every bind as replacing all stages, so those draws have no program there
// and are skipped; this file tracks the vertex stage itself.
//
// Runs on the replay thread under vk_sync_submit's lock (vk_exec.h): no locking inside.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "host.h"
#include "nvn_api.h"
#include "vk_exec.h"

#define CTRL_SIZE 0x880
#define CTRL_WORDS (CTRL_SIZE / 4)
#define TF_KEEP 16
#define PLAIN_SLOTS 16384   // open addressing; the game has a few hundred vertex shaders

static uint8_t plain_nz[CTRL_WORDS];          // a plain vertex shader had this word nonzero
static const void *plain_seen[PLAIN_SLOTS];   // vertex controls already ORed into plain_nz
static int nplain;
static uint8_t tf_ctrl[TF_KEEP][CTRL_SIZE];   // copies of transform feedback vertex shader controls
static const void *tf_at[TF_KEEP];
static int ntf, tf_draws, tf_draws_logged, tf_no_vs;

// The vertex stage's control section of a program, as vk_exec_draw.c find_stage_scan finds it (stage id at +0x714).
static const uint8_t *vs_control(const prog_t *p) {
    if (!p) return NULL;
    for (uint32_t i = 0; i < p->nstage && i < 6; i++) {
        const uint8_t *c = p->sd[i].control;
        if (!c || !p->sd[i].data) continue;
        uint32_t st;
        memcpy(&st, c + 0x714, 4);
        if (st == 0) return c;   // 0: vertex (tools/shader_census.py STAGE_IDS)
    }
    return NULL;
}

static int is_tf(const void *ctl) {
    for (int i = 0; i < ntf; i++)
        if (tf_at[i] == ctl) return 1;
    return 0;
}

static void plain_add(const uint8_t *ctl) {
    if (is_tf(ctl)) return;   // also drawn with transform feedback: not plain
    uint64_t h = (uint64_t)(uintptr_t)ctl * 0x9E3779B97F4A7C15ull;
    for (uint32_t i = (uint32_t)(h >> 50), n = 0; n < PLAIN_SLOTS; i = (i + 1) % PLAIN_SLOTS, n++) {
        if (plain_seen[i] == ctl) return;
        if (!plain_seen[i]) {
            if (nplain >= PLAIN_SLOTS / 2) return;   // table full enough: the mask is settled by now
            plain_seen[i] = ctl, nplain++;
            for (int w = 0; w < CTRL_WORDS; w++) {
                uint32_t v;
                memcpy(&v, ctl + 4 * w, 4);
                if (v) plain_nz[w] = 1;
            }
            return;
        }
    }
}

static void tf_report(void) {
    int found = 0, some = 0;
    rt_log("tf: layout report over %d transform feedback vertex shaders and %d plain ones (words set in all of the "
           "former, in none of the latter; values in the first three):", ntf, nplain);
    for (int w = 0; w < CTRL_WORDS && found < 64; w++) {
        if (plain_nz[w]) continue;
        int all = 1;
        uint32_t v[3] = {0, 0, 0};
        for (int k = 0; k < ntf && all; k++) {
            uint32_t x;
            memcpy(&x, tf_ctrl[k] + 4 * w, 4);
            if (!x) all = 0;
            if (k < 3) v[k] = x;
        }
        if (!all) continue;
        rt_log("tf:   +%#05x: %#x %#x %#x", 4 * w, v[0], v[1], v[2]);
        found++;
    }
    // Zero in every plain shader, set in some (not all) transform feedback shaders: fields of buffers or outputs only
    // some programs use.
    for (int w = 0; w < CTRL_WORDS && some < 64; w++) {
        if (plain_nz[w]) continue;
        int cnt = 0;
        for (int k = 0; k < ntf; k++) {
            uint32_t x;
            memcpy(&x, tf_ctrl[k] + 4 * w, 4);
            cnt += x != 0;
        }
        if (cnt && cnt < ntf) {
            if (!some++) rt_log("tf: words set in some transform feedback shaders only (count of %d):", ntf);
            rt_log("tf:   +%#05x: in %d", 4 * w, cnt);
        }
    }
    rt_log("tf: %d words in all, %d in some", found, some);
}

// The nonzero bytes of a transform feedback shader's layout regions (found by the first report: per-buffer words at
// 0x0dc + 0x80 i, 0x4dc + 0x80 i, strides as u16[4] at 0x6dc), as offset=value; '*' marks a word that is nonzero in
// some plain shader too. Small numbers (output locations, counts, strides), at most 160 bytes a shader.
static void tf_dump(int k, const uint8_t *ctl) {
    static const int lo[2] = {0x0c0, 0x4c0}, hi[2] = {0x2e0, 0x700};
    char line[200];
    int o = 0, n = 0, total = 0;
    for (int r = 0; r < 2; r++)
        for (int off = lo[r]; off < hi[r] && total < 160; off++) {
            if (!ctl[off]) continue;
            o += snprintf(line + o, sizeof line - o, " %03x=%02x%s", off, ctl[off], plain_nz[off / 4] ? "*" : "");
            n++, total++;
            if (n == 16) rt_log("tf: shader %d layout bytes:%s", k, line), o = n = 0;
        }
    if (n) rt_log("tf: shader %d layout bytes:%s", k, line);
    rt_log("tf: shader %d: %d nonzero layout bytes", k, total);
}

static void tf_add(const uint8_t *ctl) {
    if (ntf >= TF_KEEP || is_tf(ctl)) return;
    memcpy(tf_ctrl[ntf], ctl, CTRL_SIZE);
    tf_at[ntf++] = ctl;
    if (ntf <= 4) tf_dump(ntf, ctl);
    if (ntf == 1 || ntf == 4 || ntf == TF_KEEP) tf_report();
}

static int draw_count(uint32_t op, const void *a) {
    if (op == OP_DRAW) return ((const p_draw *)a)->count;
    if (op == OP_DRAW_IDX) return ((const p_draw_idx *)a)->count;
    return -1;   // indirect
}

static const prog_t *vs_prog;   // program whose vertex shader is bound (per-stage tracking, see above)

void vk_xfb_note_bind(const p_bind_program *b) {
    static struct { int null_prog; int32_t stages; } seen[12];
    static int nseen;
    int null_prog = b->prog == NULL, fresh = 1;
    for (int i = 0; i < nseen && fresh; i++)
        if (seen[i].null_prog == null_prog && seen[i].stages == b->stages) fresh = 0;
    if (fresh && nseen < 12) {
        seen[nseen].null_prog = null_prog, seen[nseen++].stages = b->stages;
        rt_log("tf: program bind seen: %s program, stage mask %#x", null_prog ? "no" : "a", b->stages);
    }
    if (b->stages & NVN_SHADER_STAGE_VERTEX_BIT) vs_prog = vs_control(b->prog) ? b->prog : NULL;
}

void vk_xfb_note_draw(VkExec *ex, uint32_t op, const void *payload) {
    static const prog_t *last;
    static uint64_t last_serial;
    const prog_t *p = vs_prog;
    if (!ex->xfb_active) {
        if (p && (p != last || p->serial != last_serial)) {   // one note per program bind, not per draw
            last = p, last_serial = p->serial;
            const uint8_t *vs = vs_control(p);
            if (vs) plain_add(vs);
        }
        return;
    }
    last = NULL;
    tf_draws++;
    const uint8_t *vs = vs_control(p);
    if (tf_draws_logged < 6) {
        tf_draws_logged++;
        char st[64] = "";
        for (uint32_t i = 0, o = 0; p && i < p->nstage && i < 6 && o < sizeof st - 12; i++) {
            uint32_t id = 0xff;
            if (p->sd[i].control) memcpy(&id, (const uint8_t *)p->sd[i].control + 0x714, 4);
            o += (uint32_t)snprintf(st + o, sizeof st - o, " %u", id);
        }
        rt_log("tf: draw %d with transform feedback: vertex program %p (%u stages, ids%s), vertex shader %s, bound "
               "program %s, raster discard %d, %d vertices, buffers %#llx %#llx %#llx %#llx bytes",
               tf_draws, (const void *)p, p ? p->nstage : 0, st, vs ? "found" : "missing", ex->prog ? "set" : "none", ex->pipe.raster_discard,
               draw_count(op, payload), (unsigned long long)ex->xfb_buf[0].size,
               (unsigned long long)ex->xfb_buf[1].size, (unsigned long long)ex->xfb_buf[2].size,
               (unsigned long long)ex->xfb_buf[3].size);
    }
    if (vs) tf_add(vs);
    else if (++tf_no_vs <= 3) rt_log("tf: draw with transform feedback without a vertex shader (%d so far)", tf_no_vs);
}

void vk_exec_op_xfb(VkExec *ex, uint32_t op, const void *payload) {
    switch (op) {
    case OP_BIND_XFB: {
        const p_bind_xfb *b = payload;
        if (b->index >= 0 && b->index < 4) ex->xfb_buf[b->index] = *b;
        break;
    }
    case OP_XFB: {
        int kind = ((const p_xfb *)payload)->kind;
        ex->xfb_active = kind == XFB_BEGIN || kind == XFB_RESUME;
        break;
    }
    case OP_DRAW_XFB: {
        static int once;
        if (!once++) rt_log("tf: DrawTransformFeedback is not replayed yet: nothing drawn (logged once)");
        break;
    }
    }
}
