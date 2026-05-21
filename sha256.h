/*
 * sha256.h — Implementação autocontida de SHA-256
 * Baseada na especificação FIPS PUB 180-4 (domínio público)
 *
 * Uso:
 *   SHA256_CTX_P ctx;
 *   sha256_init(&ctx);
 *   sha256_update(&ctx, dados, tamanho);
 *   uint8_t digest[SHA256_DIGEST_LEN];
 *   sha256_final(&ctx, digest);
 */
#ifndef SHA256_H
#define SHA256_H

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define SHA256_DIGEST_LEN 32   /* 256 bits = 32 bytes */

/* Contexto interno do SHA-256 */
typedef struct {
    uint32_t h[8];       /* valores de hash parciais */
    uint8_t  buf[64];    /* buffer para bloco de 512 bits */
    uint32_t buf_len;    /* bytes no buffer */
    uint64_t total_len;  /* total de bytes processados */
} SHA256_CTX_P;

/* Constantes K: primeiros 32 bits das raízes cúbicas dos 64 primeiros primos */
static const uint32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

/* Macros de rotação e funções SHA */
#define ROTR32(x, n)   (((x) >> (n)) | ((x) << (32 - (n))))
#define SHA_S0(x)      (ROTR32(x, 2)  ^ ROTR32(x, 13) ^ ROTR32(x, 22))
#define SHA_S1(x)      (ROTR32(x, 6)  ^ ROTR32(x, 11) ^ ROTR32(x, 25))
#define SHA_R0(x)      (ROTR32(x, 7)  ^ ROTR32(x, 18) ^ ((x) >> 3))
#define SHA_R1(x)      (ROTR32(x, 17) ^ ROTR32(x, 19) ^ ((x) >> 10))
#define SHA_CH(x,y,z)  (((x) & (y)) ^ (~(x) & (z)))
#define SHA_MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))

/* Processa um bloco de 512 bits (64 bytes) */
static void sha256_processar_bloco(SHA256_CTX_P *ctx) {
    uint32_t w[64], a, b, c, d, e, f, g, h, t1, t2;
    int i;

    /* Expansão da mensagem */
    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)ctx->buf[i*4  ] << 24) |
               ((uint32_t)ctx->buf[i*4+1] << 16) |
               ((uint32_t)ctx->buf[i*4+2] <<  8) |
               ((uint32_t)ctx->buf[i*4+3]);
    }
    for (i = 16; i < 64; i++) {
        w[i] = SHA_R1(w[i-2]) + w[i-7] + SHA_R0(w[i-15]) + w[i-16];
    }

    /* Inicializar variáveis de trabalho */
    a = ctx->h[0]; b = ctx->h[1]; c = ctx->h[2]; d = ctx->h[3];
    e = ctx->h[4]; f = ctx->h[5]; g = ctx->h[6]; h = ctx->h[7];

    /* 64 rodadas de compressão */
    for (i = 0; i < 64; i++) {
        t1 = h + SHA_S1(e) + SHA_CH(e,f,g) + sha256_k[i] + w[i];
        t2 = SHA_S0(a) + SHA_MAJ(a,b,c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    /* Atualizar hash parcial */
    ctx->h[0] += a; ctx->h[1] += b; ctx->h[2] += c; ctx->h[3] += d;
    ctx->h[4] += e; ctx->h[5] += f; ctx->h[6] += g; ctx->h[7] += h;
}

/* Inicializa contexto SHA-256 com valores de hash iniciais */
/* (primeiros 32 bits das raízes quadradas dos 8 primeiros primos) */
static void sha256_init(SHA256_CTX_P *ctx) {
    ctx->h[0] = 0x6a09e667; ctx->h[1] = 0xbb67ae85;
    ctx->h[2] = 0x3c6ef372; ctx->h[3] = 0xa54ff53a;
    ctx->h[4] = 0x510e527f; ctx->h[5] = 0x9b05688c;
    ctx->h[6] = 0x1f83d9ab; ctx->h[7] = 0x5be0cd19;
    ctx->buf_len  = 0;
    ctx->total_len = 0;
}

/* Alimenta dados ao contexto SHA-256 */
static void sha256_update(SHA256_CTX_P *ctx, const uint8_t *dados, size_t len) {
    ctx->total_len += len;
    for (size_t i = 0; i < len; i++) {
        ctx->buf[ctx->buf_len++] = dados[i];
        if (ctx->buf_len == 64) {
            sha256_processar_bloco(ctx);
            ctx->buf_len = 0;
        }
    }
}

/* Finaliza e retorna os 32 bytes do digest */
static void sha256_final(SHA256_CTX_P *ctx, uint8_t digest[SHA256_DIGEST_LEN]) {
    uint64_t bits_total = ctx->total_len * 8;
    int i;

    /* Padding: bit '1' seguido de zeros até sobrar 8 bytes para o comprimento */
    ctx->buf[ctx->buf_len++] = 0x80;
    if (ctx->buf_len > 56) {
        /* Não cabe neste bloco; preencher e processar */
        while (ctx->buf_len < 64) ctx->buf[ctx->buf_len++] = 0x00;
        sha256_processar_bloco(ctx);
        ctx->buf_len = 0;
    }
    while (ctx->buf_len < 56) ctx->buf[ctx->buf_len++] = 0x00;

    /* Comprimento em bits (big-endian, 8 bytes) */
    for (i = 7; i >= 0; i--) {
        ctx->buf[ctx->buf_len++] = (uint8_t)(bits_total >> (i * 8));
    }
    sha256_processar_bloco(ctx);

    /* Serializar hash em bytes (big-endian) */
    for (i = 0; i < 8; i++) {
        digest[i*4  ] = (ctx->h[i] >> 24) & 0xff;
        digest[i*4+1] = (ctx->h[i] >> 16) & 0xff;
        digest[i*4+2] = (ctx->h[i] >>  8) & 0xff;
        digest[i*4+3] =  ctx->h[i]        & 0xff;
    }
}

/*
 * sha256_arquivo — calcula SHA-256 de um arquivo e grava string hex em saida_hex
 * saida_hex deve ter ao menos 65 bytes (64 hex + '\0')
 */
static void sha256_arquivo(const char *caminho, char *saida_hex) {
    FILE *fp = fopen(caminho, "rb");
    if (!fp) {
        strcpy(saida_hex, "ERRO_ABRINDO_ARQUIVO");
        return;
    }

    SHA256_CTX_P ctx;
    sha256_init(&ctx);

    uint8_t buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        sha256_update(&ctx, buf, n);
    }
    fclose(fp);

    uint8_t digest[SHA256_DIGEST_LEN];
    sha256_final(&ctx, digest);

    for (int i = 0; i < SHA256_DIGEST_LEN; i++) {
        sprintf(saida_hex + i * 2, "%02x", digest[i]);
    }
    saida_hex[64] = '\0';
}

#endif /* SHA256_H */
