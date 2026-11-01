// The "csrng" service (spl's ICsrng): cryptographically random bytes. Written for KarPur (MIT); the command layout
// follows the public switchbrew description. nn::crypto::GenerateCryptographicallyRandomBytes asks it through IPC
// (Violet does at start, from main+0x20f9ba0); the bytes come from the host OS (plat_random).
#include <stdint.h>

#include "host.h"
#include "ipc.h"
#include "platform/platform.h"

void csrng_handler(IpcSession *s, const IpcRequest *rq, IpcResponse *rs) {
    switch (rq->cmd) {
    case 0: {   // GenerateRandomBytes() -> B buffer (type 0x6) filled with random bytes
        const IpcBuffer *b = rq->nb ? &rq->b[0] : rq->nc ? &rq->c[0] : NULL;
        if (!b) {
            ipc_backtrace();
            rt_fatal("csrng: GenerateRandomBytes without an output buffer");
        }
        if (b->size && !plat_random(ipc_guest(b->addr, b->size), (size_t)b->size))
            rt_fatal("csrng: the host OS gave no random bytes");
        if (!rq->nb) rs->xout[rs->nxout++] = (IpcBuffer){b->addr, b->size, 0};
        return;
    }
    default: ipc_unimplemented(s, rq);
    }
}
