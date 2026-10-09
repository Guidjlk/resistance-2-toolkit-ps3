/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Guidjlk */
#include "manager.h"
#include "sha256.h"
#include "unlock_data.h"
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
#define mkdir_one(p) _mkdir(p)
#define sync_fd(fd) _commit(fd)
#define close_fd(fd) _close(fd)
#else
#include <unistd.h>
#define mkdir_one(p) mkdir(p, 0755)
#define sync_fd(fd) fsync(fd)
#define close_fd(fd) close(fd)
#endif
#ifndef O_BINARY
#define O_BINARY 0
#endif
#define PATH_CAP 512
#define FILE_LIMIT (1024u * 1024u)
#define SNAP_SIZE (16 + 32 + UNLOCK_COUNT * 37 + 32)
static const unsigned char snap_magic[16] = "R2TK-BACKUP-V1";
static const unsigned char journal_magic[16] = "R2TK-PENDING-V1";
static const unsigned char original_eboot[32] = {
    0x4b, 0x52, 0x59, 0xdf, 0x00, 0xd6, 0x10, 0x3e, 0xb0, 0x03, 0x10,
    0xcd, 0x43, 0xc8, 0x0a, 0xfd, 0x39, 0x35, 0x3f, 0x91, 0x69, 0x63,
    0x45, 0x48, 0x8c, 0x93, 0x12, 0x49, 0x1e, 0xb7, 0x18, 0x96};
const Unlock unlocks[UNLOCK_COUNT] = {
    {"Collector Wraith", "wraith_unlock.dat", NULL, 0},
    {"Malikov", "malikov_unlock.dat", NULL, 0},
    {"Grim", "grim_unlock.dat", NULL, 0},
    {"Rachael Parker", "rachel_unlock.edat", edat_rachel, sizeof(edat_rachel)},
    {"Female Soldier", "fsoldier_unlock.edat", edat_fsoldier,
     sizeof(edat_fsoldier)},
    {"Ravager", "ravager_unlock.edat", edat_ravager, sizeof(edat_ravager)},
    {"Cloven", "cloven_unlock.edat", edat_cloven, sizeof(edat_cloven)},
    {"Ranger Variation", "ranger_unlock.edat", edat_ranger,
     sizeof(edat_ranger)},
    {"Black Ops Variation", "blackops_unlock.edat", edat_blackops,
     sizeof(edat_blackops)}};
typedef struct {
  unsigned present, size;
  unsigned char hash[32];
} Record;
static char game[PATH_CAP], backup[PATH_CAP], message[192];
static Record originals[UNLOCK_COUNT];
static unsigned char managed_hash[UNLOCK_COUNT][32];
static int supported, baseline, pending;
#ifdef R2TK_TESTING
static int interrupt_after = -1;
void manager_test_interrupt(int count) { interrupt_after = count; }
void manager_test_sha256(const void *data, unsigned size,
                         unsigned char out[32]) {
  sha256(data, size, out);
}
#endif
static int error(const char *fmt, ...) {
  va_list a;
  va_start(a, fmt);
  vsnprintf(message, sizeof(message), fmt, a);
  va_end(a);
  return -1;
}
const char *manager_message(void) { return message; }
int manager_has_backup(void) { return baseline; }
int manager_pending(void) { return pending; }
static int path(char *out, const char *root, const char *name) {
  int n = snprintf(out, PATH_CAP, "%s/%s", root, name);
  return n > 0 && n < PATH_CAP ? 0 : error("A filesystem path is too long.");
}
static int regular(const char *p, struct stat *st) {
#ifdef _WIN32
  DWORD a = GetFileAttributesA(p);
  if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT))
    return error("Refusing a filesystem link.");
  if (stat(p, st))
    return errno == ENOENT ? 0 : error("Cannot inspect a file.");
#elif defined(R2TK_PS3)
  /* The GameOS filesystem has no POSIX symbolic-link API. */
  if (stat(p, st))
    return errno == ENOENT ? 0 : error("Cannot inspect a file.");
#else
  if (lstat(p, st))
    return errno == ENOENT ? 0 : error("Cannot inspect a file.");
#endif
  if (!S_ISREG(st->st_mode))
    return error("An expected file is not a regular file.");
  return 1;
}
static int record_file(const char *p, Record *r, unsigned limit) {
  struct stat st;
  int exists = regular(p, &st);
  unsigned char bytes[8192];
  Sha256 h;
  FILE *f;
  size_t n, total = 0;
  memset(r, 0, sizeof(*r));
  if (exists <= 0)
    return exists < 0 ? -1 : 0;
  if (st.st_size < 0 || (uint64_t)st.st_size > limit)
    return error("File size exceeds the supported limit.");
  f = fopen(p, "rb");
  if (!f)
    return error("Cannot read a file.");
  sha256_init(&h);
  while ((n = fread(bytes, 1, sizeof(bytes), f))) {
    total += n;
    if (total > limit) {
      fclose(f);
      return error("File changed while reading.");
    }
    sha256_update(&h, bytes, n);
  }
  if (ferror(f) || total != (size_t)st.st_size) {
    fclose(f);
    return error("File could not be read completely.");
  }
  if (fclose(f))
    return error("Cannot close a file.");
  sha256_final(&h, r->hash);
  r->size = (unsigned)total;
  r->present = 1;
  return 0;
}
static int same(const Record *a, const Record *b) {
  return a->present == b->present &&
         (!a->present || (a->size == b->size && !memcmp(a->hash, b->hash, 32)));
}
static int replace_path(const char *from, const char *to) {
#ifdef _WIN32
  return MoveFileExA(from, to,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
             ? 0
             : error("Cannot finish replacing a file.");
#else
  return rename(from, to) ? error("Cannot finish replacing a file.") : 0;
#endif
}
static int write_file(const char *p, const void *data, unsigned size) {
  int fd = open(p, O_WRONLY | O_CREAT | O_EXCL | O_BINARY, 0644);
  const unsigned char *b = data;
  unsigned written = 0;
  if (fd < 0)
    return error("Cannot create a new file.");
  while (written < size) {
    int n = (int)write(fd, b + written, size - written);
    if (n <= 0) {
      close_fd(fd);
      return error("Cannot complete a file write.");
    }
    written += (unsigned)n;
  }
  if (sync_fd(fd)) {
    close_fd(fd);
    return error("Cannot flush a file to disk.");
  }
  return close_fd(fd) ? error("Cannot close a written file.") : 0;
}
static int read_small(const char *p, unsigned char **out, unsigned *size,
                      unsigned limit) {
  Record r;
  FILE *f;
  unsigned char *data;
  if (record_file(p, &r, limit) || !r.present)
    return error("Required file is missing or unreadable.");
  data = malloc(r.size ? r.size : 1);
  if (!data)
    return error("Not enough memory.");
  f = fopen(p, "rb");
  if (!f) {
    free(data);
    return error("Cannot open a required file.");
  }
  size_t got = fread(data, 1, r.size, f);
  int failed = ferror(f);
  int closed = fclose(f);
  if (got != r.size || failed || closed) {
    free(data);
    return error("Cannot read a required file completely.");
  }
  unsigned char hash[32];
  sha256(data, r.size, hash);
  if (memcmp(hash, r.hash, 32)) {
    free(data);
    return error("File changed while reading.");
  }
  *out = data;
  *size = r.size;
  return 0;
}
static unsigned le32(const unsigned char *p) {
  return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 |
         (unsigned)p[3] << 24;
}
static unsigned be32(const unsigned char *p) {
  return (unsigned)p[3] | (unsigned)p[2] << 8 | (unsigned)p[1] << 16 |
         (unsigned)p[0] << 24;
}
static void put32(unsigned char *p, unsigned v) {
  p[0] = v >> 24;
  p[1] = v >> 16;
  p[2] = v >> 8;
  p[3] = v;
}
static int game_valid(void) {
  char pth[PATH_CAP];
  Record r;
  unsigned char *sfo;
  unsigned size;
  unsigned i, count, keys, values;
  int title = 0, version = 0;
  if (path(pth, game, "EBOOT.BIN") || record_file(pth, &r, 64u * 1024u * 1024u))
    return -1;
  if (!r.present || memcmp(r.hash, original_eboot, 32))
    return error("Original BCUS98120 v1.60 EBOOT required.");
  if (path(pth, game, "../PARAM.SFO") || read_small(pth, &sfo, &size, 65536))
    return -1;
  if (size < 20 || memcmp(sfo, "\0PSF", 4) || le32(sfo + 4) != 0x101) {
    free(sfo);
    return error("Invalid game PARAM.SFO.");
  }
  keys = le32(sfo + 8);
  values = le32(sfo + 12);
  count = le32(sfo + 16);
  if (count > 100 || 20 + count * 16 > keys || keys > values || values > size) {
    free(sfo);
    return error("Invalid SFO table.");
  }
  for (i = 0; i < count; i++) {
    const unsigned char *row = sfo + 20 + i * 16;
    unsigned key = row[0] | (unsigned)row[1] << 8, len = le32(row + 4),
             max = le32(row + 8), off = le32(row + 12);
    if (key >= values - keys ||
        !memchr(sfo + keys + key, 0, values - keys - key) || len > max ||
        off > size - values || max > size - values - off) {
      free(sfo);
      return error("Invalid SFO entry.");
    }
    const char *name = (const char *)sfo + keys + key;
    if (!strcmp(name, "TITLE_ID"))
      title = len == 10 && !memcmp(sfo + values + off, "BCUS98120\0", 10);
    if (!strcmp(name, "APP_VER"))
      version = len == 6 && !memcmp(sfo + values + off, "01.60\0", 6);
  }
  free(sfo);
  return title && version ? 0 : error("BCUS98120 update 1.60 is required.");
}
static int ensure_dir(const char *p) {
  struct stat st;
  if (!stat(p, &st))
    return S_ISDIR(st.st_mode) ? 0 : error("Backup path is not a directory.");
  if (errno != ENOENT || mkdir_one(p))
    return error("Cannot create backup directory.");
  return 0;
}
static int ensure_backup_dirs(void) {
  char parent[PATH_CAP];
  char *slash;
  snprintf(parent, sizeof(parent), "%s", backup);
  slash = strrchr(parent, '/');
  if (!slash)
    return error("Invalid backup directory.");
  *slash = 0;
  return ensure_dir(parent) || ensure_dir(backup) ? -1 : 0;
}
static int original_path(char *out, unsigned i) {
  char name[80];
  snprintf(name, sizeof(name), "%s.original", unlocks[i].file);
  return path(out, backup, name);
}
static int load_baseline(void) {
  char p[PATH_CAP];
  Record file;
  unsigned char *data, hash[32];
  unsigned size, i;
  baseline = 0;
  if (path(p, backup, "snapshot.bin") || record_file(p, &file, SNAP_SIZE))
    return -1;
  if (!file.present)
    return 0;
  if (read_small(p, &data, &size, SNAP_SIZE))
    return -1;
  sha256(data, size >= 32 ? size - 32 : 0, hash);
  if (size != SNAP_SIZE || memcmp(data, snap_magic, 16) ||
      memcmp(data + 16, original_eboot, 32) ||
      memcmp(hash, data + size - 32, 32)) {
    free(data);
    return error("Backup manifest is damaged or incompatible.");
  }
  for (i = 0; i < UNLOCK_COUNT; i++) {
    unsigned char *e = data + 48 + i * 37;
    originals[i].present = e[0];
    originals[i].size = be32(e + 1);
    memcpy(originals[i].hash, e + 5, 32);
    if (e[0] > 1 || originals[i].size > FILE_LIMIT ||
        (!e[0] && (originals[i].size ||
                   memcmp(originals[i].hash, (unsigned char[32]){0}, 32)))) {
      free(data);
      return error("Invalid backup record.");
    }
    if (e[0]) {
      Record stored;
      if (original_path(p, i) || record_file(p, &stored, FILE_LIMIT) ||
          !same(&stored, &originals[i])) {
        free(data);
        return error("Original backup file is missing or damaged.");
      }
    }
  }
  free(data);
  baseline = 1;
  return 0;
}
static int load_journal(void) {
  char p[PATH_CAP];
  Record r;
  unsigned char *data, hash[32];
  unsigned size;
  pending = 0;
  if (path(p, backup, "pending.bin") || record_file(p, &r, 64))
    return -1;
  if (!r.present)
    return 0;
  if (read_small(p, &data, &size, 64))
    return -1;
  sha256(data, size >= 32 ? size - 32 : 0, hash);
  if (size != 64 || memcmp(data, journal_magic, 16) ||
      memcmp(data + 32, hash, 32) || !baseline) {
    free(data);
    return error("Recovery record is damaged; originals kept.");
  }
  free(data);
  pending = 1;
  return 0;
}
int manager_init(const char *game_directory, const char *backup_directory) {
  unsigned i;
  supported = baseline = pending = 0;
  message[0] = 0;
  memset(originals, 0, sizeof(originals));
  if (strlen(game_directory) >= PATH_CAP - 80 ||
      strlen(backup_directory) >= PATH_CAP - 80)
    return error("Path too long.");
  snprintf(game, sizeof(game), "%s", game_directory);
  snprintf(backup, sizeof(backup), "%s", backup_directory);
  for (i = 0; i < UNLOCK_COUNT; i++)
    sha256(unlocks[i].data, unlocks[i].size, managed_hash[i]);
  if (game_valid())
    return -1;
  supported = 1;
  if (load_baseline() || load_journal())
    return -1;
  snprintf(message, sizeof(message),
           pending ? "Interrupted change: restore originals first."
                   : "Ready. Choose unlocks; originals will be backed up.");
  return 0;
}
int manager_scan(unsigned *present, unsigned *original, unsigned *managed) {
  unsigned i;
  char p[PATH_CAP];
  Record r;
  *present = *original = *managed = 0;
  for (i = 0; i < UNLOCK_COUNT; i++) {
    if (path(p, game, unlocks[i].file) || record_file(p, &r, FILE_LIMIT))
      return -1;
    if (r.present)
      *present |= 1u << i;
    if (baseline && originals[i].present && same(&r, &originals[i]))
      *original |= 1u << i;
    else if (baseline && !originals[i].present && r.present &&
             r.size == unlocks[i].size && !memcmp(r.hash, managed_hash[i], 32))
      *managed |= 1u << i;
  }
  return 0;
}
int manager_backup(void) {
  unsigned i;
  char p[PATH_CAP], dest[PATH_CAP];
  unsigned char manifest[SNAP_SIZE] = {0};
  if (!supported || game_valid())
    return -1;
  if (pending)
    return error("Restore interrupted changes first.");
  if (load_baseline())
    return -1;
  if (baseline) {
    snprintf(message, sizeof(message),
             "Your game's first unlock backup already exists; kept unchanged.");
    return 0;
  }
  if (ensure_backup_dirs())
    return -1;
  memcpy(manifest, snap_magic, 16);
  memcpy(manifest + 16, original_eboot, 32);
  for (i = 0; i < UNLOCK_COUNT; i++) {
    Record r;
    unsigned char *data;
    unsigned size;
    if (path(p, game, unlocks[i].file) || record_file(p, &r, FILE_LIMIT))
      return -1;
    unsigned char *e = manifest + 48 + i * 37;
    e[0] = r.present;
    put32(e + 1, r.size);
    memcpy(e + 5, r.hash, 32);
    if (original_path(dest, i))
      return -1;
    /* A partial first snapshot never mutates the game. Recreate its known
     * backup files until snapshot.bin has been durably committed. */
    if (remove(dest) && errno != ENOENT)
      return error("Cannot clean incomplete backup.");
    if (r.present) {
      if (read_small(p, &data, &size, FILE_LIMIT))
        return -1;
      unsigned char hash[32];
      sha256(data, size, hash);
      if (size != r.size || memcmp(hash, r.hash, 32)) {
        free(data);
        return error("Unlock file changed during backup.");
      }
      int rc = write_file(dest, data, size);
      free(data);
      if (rc)
        return -1;
    }
  }
  sha256(manifest, SNAP_SIZE - 32, manifest + SNAP_SIZE - 32);
  if (path(dest, backup, "snapshot.new") || path(p, backup, "snapshot.bin"))
    return -1;
  if (remove(dest) && errno != ENOENT)
    return error("Cannot clean incomplete manifest.");
  if (write_file(dest, manifest, sizeof(manifest)) || replace_path(dest, p) ||
      load_baseline())
    return -1;
  snprintf(message, sizeof(message),
           "Your game's unlock state was backed up successfully.");
  return 0;
}
static int preflight(void) {
  unsigned i;
  char p[PATH_CAP];
  Record r;
  if (!supported || game_valid() || load_baseline() || !baseline)
    return error("A valid original backup is required.");
  for (i = 0; i < UNLOCK_COUNT; i++) {
    if (path(p, game, unlocks[i].file) || record_file(p, &r, FILE_LIMIT))
      return -1;
    if (r.present && !same(&r, &originals[i]) &&
        !(r.size == unlocks[i].size && !memcmp(r.hash, managed_hash[i], 32)))
      return error("Conflict: %s changed outside R2TK.", unlocks[i].name);
    char stage[PATH_CAP], name[96];
    snprintf(name, sizeof(name), "%s.r2tk-stage", unlocks[i].file);
    if (path(stage, game, name) || record_file(stage, &r, FILE_LIMIT))
      return -1;
    /* A durable pending record owns this exact staging filename. An interrupted
     * write can leave a partial file, which recovery may safely discard. */
    if (r.present && !pending && !same(&r, &originals[i]) &&
        !(r.size == unlocks[i].size && !memcmp(r.hash, managed_hash[i], 32)))
      return error("Unknown temporary file; originals kept.");
  }
  return 0;
}
static int journal_begin(unsigned mask) {
  char tmp[PATH_CAP], dst[PATH_CAP];
  unsigned char data[64] = {0};
  if (pending)
    return 0;
  memcpy(data, journal_magic, 16);
  put32(data + 16, mask);
  sha256(data, 32, data + 32);
  if (path(tmp, backup, "pending.new") || path(dst, backup, "pending.bin"))
    return -1;
  if (remove(tmp) && errno != ENOENT)
    return error("Cannot clean recovery staging file.");
  if (write_file(tmp, data, sizeof(data)) || replace_path(tmp, dst))
    return -1;
  pending = 1;
  return 0;
}
static int install_record(unsigned i, int selected) {
  char dest[PATH_CAP], stage[PATH_CAP], name[96];
  Record r, want;
  unsigned char *allocated = NULL;
  const unsigned char *data = NULL;
  unsigned size = 0;
  if (path(dest, game, unlocks[i].file))
    return -1;
  if (originals[i].present) {
    char p[PATH_CAP];
    if (original_path(p, i) || read_small(p, &allocated, &size, FILE_LIMIT))
      return -1;
    data = allocated;
    want = originals[i];
  } else {
    memset(&want, 0, sizeof(want));
    if (selected) {
      want.present = 1;
      want.size = unlocks[i].size;
      memcpy(want.hash, managed_hash[i], 32);
      data = unlocks[i].data;
      size = unlocks[i].size;
    }
  }
  if (record_file(dest, &r, FILE_LIMIT)) {
    free(allocated);
    return -1;
  }
  if (r.present && !same(&r, &originals[i]) &&
      !(r.size == unlocks[i].size && !memcmp(r.hash, managed_hash[i], 32))) {
    free(allocated);
    return error("Unlock file changed during the operation.");
  }
  snprintf(name, sizeof(name), "%s.r2tk-stage", unlocks[i].file);
  if (path(stage, game, name)) {
    free(allocated);
    return -1;
  }
  if (remove(stage) && errno != ENOENT) {
    free(allocated);
    return error("Cannot remove known staging file.");
  }
  if (same(&r, &want)) {
    free(allocated);
    return 0;
  }
  if (!want.present) {
    free(allocated);
    if (remove(dest) && errno != ENOENT)
      return error("Cannot remove managed marker.");
  } else {
    if (write_file(stage, data, size)) {
      free(allocated);
      return -1;
    }
    free(allocated);
    if (record_file(stage, &r, FILE_LIMIT) || !same(&r, &want))
      return error("Staged marker verification failed.");
    if (replace_path(stage, dest))
      return -1;
  }
  if (record_file(dest, &r, FILE_LIMIT) || !same(&r, &want))
    return error("Installed marker verification failed.");
  return 0;
}
static int change(unsigned mask, int restore) {
  unsigned i;
  char p[PATH_CAP];
  if (mask & ~ALL_UNLOCKS)
    return error("Invalid unlock selection.");
  if (!restore && pending)
    return error("Restore interrupted changes first.");
  if (!baseline && manager_backup())
    return -1;
  if (preflight() || journal_begin(mask))
    return -1;
  for (i = 0; i < UNLOCK_COUNT; i++) {
    if (install_record(i, (mask >> i) & 1u))
      return -1;
#ifdef R2TK_TESTING
    if (interrupt_after == (int)i + 1) {
      interrupt_after = -1;
      return error("Simulated interrupted operation.");
    }
#endif
  }
  if (path(p, backup, "pending.bin") || remove(p))
    return error("Files verified; cannot clear recovery record.");
  pending = 0;
  snprintf(message, sizeof(message),
           restore
               ? "Your game's previous unlock state was restored successfully."
               : "Selection applied. Close R2TK, then launch the game.");
  return 0;
}
int manager_apply(unsigned selected) { return change(selected, 0); }
int manager_restore(void) {
  if (!baseline)
    return error("No original backup to restore.");
  return change(0, 1);
}
int manager_export_backup(const char *usb_root) {
  char root[PATH_CAP], dest[PATH_CAP], src[PATH_CAP];
  unsigned i;
  if (!baseline || load_baseline())
    return error("A valid backup is required.");
  struct stat st;
  if (stat(usb_root, &st) || !S_ISDIR(st.st_mode))
    return error("USB drive is not available.");
  if (path(root, usb_root, "R2TK-BCUS98120-0160") || ensure_dir(root))
    return -1;
  for (i = 0; i <= UNLOCK_COUNT; i++) {
    if (i < UNLOCK_COUNT && !originals[i].present)
      continue;
    char name[96];
    if (i == UNLOCK_COUNT)
      snprintf(name, sizeof(name), "snapshot.bin");
    else
      snprintf(name, sizeof(name), "%s.original", unlocks[i].file);
    if (path(src, backup, name) || path(dest, root, name))
      return -1;
    Record exists, r;
    if (record_file(dest, &exists, FILE_LIMIT) ||
        record_file(src, &r, FILE_LIMIT))
      return -1;
    if (exists.present) {
      if (!same(&r, &exists))
        return error("USB backup differs; use another drive/folder.");
      continue;
    }
    unsigned char *data;
    unsigned size;
    if (read_small(src, &data, &size, FILE_LIMIT))
      return -1;
    int rc = write_file(dest, data, size);
    free(data);
    if (rc)
      return -1;
    if (record_file(dest, &exists, FILE_LIMIT) || !same(&r, &exists))
      return error("USB backup verification failed.");
  }
  snprintf(message, sizeof(message),
           "Your game's unlock backup was copied to USB successfully.");
  return 0;
}
