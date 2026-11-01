// The "set" service (nn::settings::ISettingsServer): language and region queries. Written for KarPur (MIT); command
// numbers and argument layouts follow the public switchbrew description of the interface.
//
// Violet calls nn::settings::operator==(LanguageCode, Language), whose SDK body asks this service for
// MakeLanguageCode instead of using a table. The answers follow the host's language choice (ov_language, which
// nn::oe::GetDesiredLanguage also reports), so the game sees one consistent language.
#include <stdint.h>
#include <string.h>

#include "host.h"
#include "ipc.h"
#include "ov.h"

// nn::settings::Language in order: the index is the enum value, the string its LanguageCode (char[8], zero padded).
static const char *const codes[] = {
    "ja", "en-US", "fr", "de", "it", "es", "zh-CN", "ko", "nl", "pt", "ru", "zh-TW",
    "en-GB", "fr-CA", "es-419", "zh-Hans", "zh-Hant", "pt-BR",
};
#define NCODES (sizeof codes / sizeof *codes)
// The older commands (1, 3) list only the first 15 languages; the "2" variants list all of them.
#define NCODES_OLD 15

static uint64_t code_of(const char *s) {
    uint64_t v = 0;
    memcpy(&v, s, strlen(s) < 8 ? strlen(s) : 8);
    return v;
}

// Writes up to n language codes into a client buffer of size bytes; returns how many were written.
static uint32_t list_codes(uint64_t addr, uint64_t size, uint32_t n) {
    uint32_t fit = (uint32_t)(size / 8);
    if (fit < n) n = fit;
    uint64_t *out = ipc_guest(addr, (uint64_t)n * 8);
    for (uint32_t i = 0; i < n; i++) out[i] = code_of(codes[i]);
    return n;
}

void settings_handler(IpcSession *s, const IpcRequest *rq, IpcResponse *rs) {
    switch (rq->cmd) {
    case 0: {   // GetLanguageCode() -> LanguageCode: the system language
        uint64_t v = code_of(ov_language());
        IPC_OUT(rs, v);
        return;
    }
    case 1: {   // GetAvailableLanguageCodes() -> u32 count, C buffer of LanguageCode
        uint32_t n = 0;
        if (rq->nc) {
            n = list_codes(rq->c[0].addr, rq->c[0].size, NCODES_OLD);
            rs->xout[rs->nxout++] = (IpcBuffer){rq->c[0].addr, (uint64_t)n * 8, 0};
        }
        IPC_OUT(rs, n);
        return;
    }
    case 2: {   // MakeLanguageCode(u32 Language) -> LanguageCode
        uint32_t lang = IPC_ARG(rq, uint32_t, 0);
        if (lang >= NCODES) {
            ipc_backtrace();
            rt_fatal("set: MakeLanguageCode(%u): no such language", lang);
        }
        uint64_t v = code_of(codes[lang]);
        IPC_OUT(rs, v);
        return;
    }
    case 3: {   // GetAvailableLanguageCodeCount() -> u32
        uint32_t n = NCODES_OLD;
        IPC_OUT(rs, n);
        return;
    }
    case 4: {   // GetRegionCode() -> u32: 2 = Europe, the region of a German console
        uint32_t region = 2;
        IPC_OUT(rs, region);
        return;
    }
    case 5: {   // GetAvailableLanguageCodes2() -> u32 count, B buffer of LanguageCode
        uint32_t n = rq->nb ? list_codes(rq->b[0].addr, rq->b[0].size, NCODES) : 0;
        IPC_OUT(rs, n);
        return;
    }
    case 6: {   // GetAvailableLanguageCodeCount2() -> u32
        uint32_t n = NCODES;
        IPC_OUT(rs, n);
        return;
    }
    case 8: {   // GetQuestFlag() -> bool: not a store demo unit
        uint8_t quest = 0;
        IPC_OUT(rs, quest);
        return;
    }
    default: ipc_unimplemented(s, rq);
    }
}
