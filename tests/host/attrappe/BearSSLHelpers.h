// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Ersatz fuer BearSSLs SHA-256. Er ist hier KEIN Kryptografie-Ersatz und wird auch
// nicht als solcher geprueft -- die Tests interessieren sich ausschliesslich fuer die
// ENTSCHEIDUNGEN von SecureStorage (ueberschreiben oder nicht, schreiben oder nicht).
// Verlangt wird nur, dass die Ableitung deterministisch ist, damit Schreiben und
// spaeteres Lesen zusammenpassen.
#include <cstddef>
#include <cstdint>

struct br_sha256_context {
    uint32_t h;
};

inline void br_sha256_init(br_sha256_context* ctx) { ctx->h = 0x811C9DC5U; }

inline void br_sha256_update(br_sha256_context* ctx, const unsigned char* p, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        ctx->h = (ctx->h ^ p[i]) * 0x01000193U;
    }
}

inline void br_sha256_out(const br_sha256_context* ctx, uint8_t* out32) {
    uint32_t h = ctx->h;
    for (int i = 0; i < 32; ++i) {
        h = (h * 1103515245U) + 12345U;
        out32[i] = static_cast<uint8_t>(h >> 16);
    }
}
