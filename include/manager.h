/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Guidjlk */
#ifndef R2TK_MANAGER_H
#define R2TK_MANAGER_H
#include <stdint.h>
#ifdef _WIN32
#define API __declspec(dllexport)
#else
#define API
#endif
#define UNLOCK_COUNT 9
#define ALL_UNLOCKS ((1u << UNLOCK_COUNT) - 1u)
typedef struct {
  const char *name, *file;
  const unsigned char *data;
  unsigned size;
} Unlock;
extern const Unlock unlocks[UNLOCK_COUNT];
/* This core only touches the nine allowlisted unlock filenames, its own
 * backup records, and matching temporary files. EBOOT and saves are read-only.
 */
API int manager_init(const char *game_directory, const char *backup_directory);
API int manager_backup(void);
API int manager_apply(unsigned selected);
API int manager_restore(void);
API int manager_scan(unsigned *present, unsigned *original, unsigned *managed);
API int manager_has_backup(void);
API int manager_pending(void);
API const char *manager_message(void);
API int manager_export_backup(const char *usb_root);
#ifdef R2TK_TESTING
API void manager_test_interrupt(int after_files);
API void manager_test_sha256(const void *data, unsigned size,
                             unsigned char out[32]);
#endif
#endif
