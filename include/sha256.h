/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Guidjlk */
#ifndef R2TK_SHA256_H
#define R2TK_SHA256_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
  uint32_t h[8];
  uint64_t bytes;
  unsigned used;
  unsigned char block[64];
} Sha256;
void sha256_init(Sha256 *s);
void sha256_update(Sha256 *s, const void *data, size_t size);
void sha256_final(Sha256 *s, unsigned char out[32]);
void sha256(const void *data, size_t size, unsigned char out[32]);
#endif
