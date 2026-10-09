/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Guidjlk
 * Standalone, offline PS3 unlock manager. CPU-drawn, resolution-scaled UI. */
#include "environment.h"
#include "font.h"
#include "manager.h"
#include "navigation.h"
#include <cell/gcm.h>
#include <ctype.h>
#include <io/pad.h>
#include <malloc.h>
#include <ppu-types.h>
#include <rsx/rsx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/process.h>
#include <sys/systime.h>
#include <sysutil/sysutil.h>
#include <sysutil/video.h>
#include <unistd.h>

SYS_PROCESS_PARAM(1001, 0x100000);
#define HOST_SIZE (8 * 1024 * 1024)
#define GAME_DIR "/dev_hdd0/game/BCUS98120/USRDIR"
#define BACKUP_DIR "/dev_hdd0/R2TK_BACKUPS/BCUS98120_0160"
static volatile int running = 1;
static unsigned width, height;
static uint32_t *buffers[2], *pixels, *canvas;
static uint32_t buffer_offset[2], canvas_offset, depth_offset;
static CellGcmContextData *context;
static unsigned selected, present, original, managed;
static int ready, cursor, confirm = -1, credits, likely_rpcs3;
enum { ACTION_APPLY, ACTION_REVERT, ACTION_EXIT, ACTION_COUNT };
static char status[192] = "Reading installed game...";

static void exit_callback(u64 state, u64 param, void *data) {
  (void)param;
  (void)data;
  if (state == SYSUTIL_EXIT_GAME)
    running = 0;
}
static void rectangle(int x, int y, int w, int h, uint32_t color) {
  unsigned sx = (unsigned)x * width / 1280, sy = (unsigned)y * height / 720;
  unsigned ex = (unsigned)(x + w) * width / 1280,
           ey = (unsigned)(y + h) * height / 720;
  if (ex > width)
    ex = width;
  if (ey > height)
    ey = height;
  for (unsigned row = sy; row < ey; row++)
    for (unsigned col = sx; col < ex; col++)
      pixels[row * width + col] = color;
}
static void text(int x, int y, int scale, uint32_t color, const char *string) {
  for (const unsigned char *p = (const unsigned char *)string; *p;
       p++, x += 6 * scale) {
    unsigned c = (unsigned)toupper(*p);
    if (c < 32 || c > 126)
      c = '?';
    for (unsigned row = 0; row < 7; row++)
      for (unsigned col = 0; col < 5; col++)
        if (font[c - 32][row] & (1u << (4 - col)))
          rectangle(x + (int)col * scale, y + (int)row * scale, scale, scale,
                    color);
  }
}
static void paragraph(int x, int y, const char *string, unsigned chars) {
  char line[100];
  unsigned n = 0;
  const char *p = string;
  while (*p && y < 640) {
    n = 0;
    while (p[n] && p[n] != '\n' && n < chars)
      n++;
    if (p[n] && p[n] != '\n') {
      unsigned split = n;
      while (split && p[split] != ' ')
        split--;
      if (split)
        n = split;
    }
    memcpy(line, p, n);
    line[n] = 0;
    text(x, y, 2, 0xffc4d1da, line);
    y += 23;
    p += n;
    while (*p == ' ' || *p == '\n')
      p++;
  }
}
enum ButtonSymbol {
  SYMBOL_DPAD,
  SYMBOL_CROSS,
  SYMBOL_SQUARE,
  SYMBOL_TRIANGLE,
  SYMBOL_CIRCLE
};
/* Draw controller symbols directly; the compact text font is ASCII-only. */
static void symbol_line(int x, int y, int ex, int ey, uint32_t color) {
  int dx = abs(ex - x), sx = x < ex ? 1 : -1;
  int dy = -abs(ey - y), sy = y < ey ? 1 : -1, error = dx + dy;
  for (;;) {
    rectangle(x, y, 2, 2, color);
    if (x == ex && y == ey)
      break;
    int twice = 2 * error;
    if (twice >= dy) {
      error += dy;
      x += sx;
    }
    if (twice <= dx) {
      error += dx;
      y += sy;
    }
  }
}
static void button_symbol(int x, int y, enum ButtonSymbol symbol) {
  static const uint32_t colors[] = {0xff9daebd, 0xff80b9ff, 0xffee94bd,
                                    0xff75ddaa, 0xffff9a8e};
  uint32_t color = colors[symbol];
  switch (symbol) {
  case SYMBOL_CROSS:
    symbol_line(x + 3, y + 3, x + 19, y + 19, color);
    symbol_line(x + 19, y + 3, x + 3, y + 19, color);
    break;
  case SYMBOL_SQUARE:
    rectangle(x + 2, y + 2, 20, 2, color);
    rectangle(x + 2, y + 20, 20, 2, color);
    rectangle(x + 2, y + 2, 2, 20, color);
    rectangle(x + 20, y + 2, 2, 20, color);
    break;
  case SYMBOL_TRIANGLE:
    symbol_line(x + 11, y + 1, x + 1, y + 21, color);
    symbol_line(x + 1, y + 21, x + 21, y + 21, color);
    symbol_line(x + 21, y + 21, x + 11, y + 1, color);
    break;
  case SYMBOL_CIRCLE:
    for (int row = 0; row < 23; row++)
      for (int col = 0; col < 23; col++) {
        int dx = col - 11, dy = row - 11, radius = dx * dx + dy * dy;
        if (radius >= 81 && radius <= 121)
          rectangle(x + col, y + row, 1, 1, color);
      }
    break;
  case SYMBOL_DPAD:
    symbol_line(x + 11, y + 1, x + 11, y + 9, color);
    symbol_line(x + 5, y + 7, x + 11, y + 1, color);
    symbol_line(x + 11, y + 1, x + 17, y + 7, color);
    symbol_line(x + 11, y + 14, x + 11, y + 22, color);
    symbol_line(x + 5, y + 16, x + 11, y + 22, color);
    symbol_line(x + 11, y + 22, x + 17, y + 16, color);
    break;
  }
}
static int button_hint(int x, int y, enum ButtonSymbol symbol,
                       const char *label) {
  button_symbol(x, y, symbol);
  text(x + 34, y + 5, 2, 0xff9daebd, label);
  return x + 34 + (int)strlen(label) * 12 + 30;
}
/* Use the same availability rules for rendering and controller activation. */
static int action_enabled(int action) {
  if (action == ACTION_EXIT)
    return 1;
  if (!ready)
    return 0;
  if (action == ACTION_REVERT)
    return manager_has_backup();
  return action == ACTION_APPLY && !manager_pending();
}
static void refresh(void) {
  if (manager_scan(&present, &original, &managed))
    snprintf(status, sizeof(status), "%s", manager_message());
}
static void operation(int action) {
  int rc = -1;
  snprintf(status, sizeof(status),
           "Working... please keep the console powered on.");
  if (action == ACTION_APPLY)
    rc = manager_apply(selected);
  if (action == ACTION_REVERT)
    rc = manager_restore();
  snprintf(status, sizeof(status), "%s", manager_message());
  refresh();
  if (!rc && action == ACTION_REVERT)
    selected = 0;
}
static void draw(void) {
  rectangle(0, 0, 1280, 720, 0xff0c1420);
  rectangle(36, 36, 1208, 100, 0xff17273a);
  text(60, 58, 4, 0xffe8eff4, "RESISTANCE 2 TOOL KIT");
  text(62, 104, 2, 0xff9daebd,
        likely_rpcs3 ? "RPCS3  /  BCUS98120  /  UPDATE 1.60  /  0.1.10"
                    : "PS3 CFW  /  BCUS98120  /  UPDATE 1.60  /  0.1.10");
  rectangle(36, 150, 540, 498, 0xff132031);
  rectangle(594, 150, 650, 498, 0xff132031);
  if (credits) {
    text(64, 182, 3, 0xff56cbb7, "BY GUIDJLK");
    paragraph(64, 230,
              "Original application: GPL-2.0-only.\nPS3DEV / PSL1GHT and "
              "FirebirdTA01 PS3DK: SDK, runtime, package tools and graphics "
              "examples.\nRPCS3 contributors / Hykem: EDAT format "
              "references.\nLinblow: DLC unlock patch reference.\nInsomniac "
              "Games / Sony: Resistance 2.",
              39);
    paragraph(620, 182,
              "This application is offline. It edits only nine unlock marker "
              "files and its own backup records.\nEBOOT and save files are "
              "never modified.",
              47);
  } else {
    for (unsigned i = 0; i < UNLOCK_COUNT; i++) {
      char label[64];
      int y = 170 + (int)i * 31;
      if (cursor == (int)i && confirm < 0)
        rectangle(48, y - 7, 516, 28, 0xff284655);
      snprintf(label, sizeof(label), "[%c] %s",
               selected & (1u << i) ? 'X' : ' ', unlocks[i].name);
      text(62, y, 2, 0xffe7eef4, label);
      const char *state = (original & (1u << i))  ? "ORIGINAL RETAINED"
                          : (managed & (1u << i)) ? "INSTALLED"
                          : (present & (1u << i)) ? "ALREADY PRESENT"
                                                  : "NOT INSTALLED";
      /* Keep every state in a right-aligned column inside its row highlight. */
      text(550 - (int)strlen(state) * 6, y + 4, 1, 0xff92aaa9, state);
    }
    const char *actions[ACTION_COUNT] = {"APPLY SELECTION",
                                         "REVERT TO ORIGINAL", "EXIT"};
    for (int i = 0; i < ACTION_COUNT; i++) {
      int y = 468 + i * 42;
      int enabled = action_enabled(i);
      if (cursor == UNLOCK_COUNT + i && confirm < 0)
        rectangle(48, y - 7, 516, 30, enabled ? 0xff284655 : 0xff1c2d3c);
      text(62, y, 2, enabled ? 0xff56cbb7 : 0xff657584, actions[i]);
    }
    text(620, 176, 3, 0xff56cbb7, ready ? "UNLOCK FILES" : "GAME CHECK");
    if (confirm >= 0) {
      const char *prompts[ACTION_COUNT] = {
          "Apply the selected unlocks? Your game's previous unlock state is "
          "backed up before the first change. "
          "Existing unlock files are retained.",
          "Restore the unlock state saved before your first Apply Selection? "
          "Original means your starting state, including existing DLC. "
          "Toolkit-added unlocks will be removed.",
          "Exit Resistance 2 Tool Kit and return to the XMB? "
          "Selections that have not been applied will be discarded."};
      paragraph(620, 222, prompts[confirm], 47);
      int hint_x = button_hint(620, 343, SYMBOL_CROSS,
                               confirm == ACTION_EXIT ? "EXIT" : "CONFIRM");
      button_hint(hint_x, 343, SYMBOL_CIRCLE,
                  confirm == ACTION_EXIT ? "STAY" : "CANCEL");
    } else if (!ready)
      paragraph(620, 222, status, 47);
    else if (manager_pending())
      paragraph(620, 222,
                "An earlier operation was interrupted. Choose Revert to "
                "Original to "
                "recover. Apply is blocked until recovery completes.",
                47);
    else if (cursor == UNLOCK_COUNT + ACTION_REVERT && !manager_has_backup())
      paragraph(620, 222,
                "No revert point is available yet. Apply Selection saves your "
                "starting unlock state before the first change, then Revert "
                "to Original becomes available.",
                47);
    else if (cursor < UNLOCK_COUNT) {
      text(620, 222, 2, 0xffe7eef4, unlocks[cursor].name);
      paragraph(
          620, 266,
          cursor < 3
              ? "Uses an empty DAT marker. The game checks whether "
                "this filename exists.\nUnchecking removes only changes added "
                "by R2TK; original unlock files are preserved."
              : "Uses an EDAT unlock file.\nUnchecking removes only changes "
                "added by R2TK; original unlock files are preserved.",
          47);
    } else
      paragraph(620, 222,
                "Choose individual unlocks or select all using the controls "
                "below.\nRevert to Original restores your unlock state from "
                "before the first Apply Selection, including existing "
                "DLC.\nThis automatic backup covers nine unlock files, "
                "not the full game or saves.",
                47);
    if (ready && likely_rpcs3 && confirm < 0 && !manager_pending())
      paragraph(620, 389,
                "RPCS3: Disable Resistance 2 unlock patches\n"
                "when checking the effects of these files.",
                47);
    if (ready) {
      text(620, 442, 2, 0xff9daebd,
           manager_has_backup() ? "REVERT POINT: SAVED"
                                : "REVERT POINT: SAVES ON FIRST APPLY");
      paragraph(620, 479, status, 47);
    }
  }
  int hint_x = 48;
  if (credits)
    button_hint(hint_x, 662, SYMBOL_CIRCLE, "BACK");
  else if (confirm >= 0) {
    hint_x = button_hint(hint_x, 662, SYMBOL_CROSS,
                         confirm == ACTION_EXIT ? "EXIT" : "CONFIRM");
    button_hint(hint_x, 662, SYMBOL_CIRCLE,
                confirm == ACTION_EXIT ? "STAY" : "CANCEL");
  } else {
    hint_x = button_hint(hint_x, 662, SYMBOL_DPAD, "MOVE");
    hint_x = button_hint(hint_x, 662, SYMBOL_CROSS, "SELECT");
    hint_x = button_hint(hint_x, 662, SYMBOL_SQUARE, "ALL");
    hint_x = button_hint(hint_x, 662, SYMBOL_TRIANGLE, "CREDITS");
    button_hint(hint_x, 662, SYMBOL_CIRCLE, "EXIT");
  }
  text(48, 696, 2, 0xff56cbb7, "BY GUIDJLK");
  text(908, 696, 1, 0xff9daebd, "REVERT TO UNDO TOOLKIT UNLOCKS");
}
static int screen_init(void *host) {
  videoState state;
  videoResolution resolution;
  videoConfiguration config = {0};
  if (cellGcmInit(1024 * 1024, HOST_SIZE, host))
    return -1;
  context = CELL_GCM_CURRENT;
  if (videoGetState(0, 0, &state) || state.state ||
      videoGetResolution(state.displayMode.resolution, &resolution))
    return -1;
  width = resolution.width;
  height = resolution.height;
  config.resolution = state.displayMode.resolution;
  config.format = VIDEO_BUFFER_FORMAT_XRGB;
  config.pitch = width * 4;
  config.aspect = state.displayMode.aspect;
  if (videoConfigure(0, &config, NULL, 0))
    return -1;
  cellGcmSetFlipMode(GCM_FLIP_VSYNC);
  for (unsigned i = 0; i < 2; i++) {
    buffers[i] = rsxMemalign(64, width * height * 4);
    if (!buffers[i] || cellGcmAddressToOffset(buffers[i], &buffer_offset[i]) ||
        cellGcmSetDisplayBuffer(i, buffer_offset[i], width * 4, width, height))
      return -1;
  }
  canvas = rsxMemalign(64, width * height * 4);
  void *depth = rsxMemalign(64, width * height * 2);
  if (!canvas || !depth || cellGcmAddressToOffset(canvas, &canvas_offset) ||
      cellGcmAddressToOffset(depth, &depth_offset))
    return -1;
  pixels = memalign(128, width * height * 4);
  if (!pixels)
    return -1;
  cellGcmResetFlipStatus();
  return 0;
}
static void present_buffer(unsigned buffer) {
  /* The explicit surface and RSX blit keep framebuffer ownership coherent on
   * both hardware and emulators. The cached CPU canvas changes only on input.
   */
  CellGcmSurface surface = {0};
  surface.type = GCM_SURFACE_TYPE_LINEAR;
  surface.antiAlias = GCM_SURFACE_CENTER_1;
  surface.colorFormat = GCM_SURFACE_X8R8G8B8;
  surface.colorTarget = GCM_SURFACE_TARGET_0;
  surface.colorOffset[0] = buffer_offset[buffer];
  surface.colorPitch[0] = width * 4;
  for (unsigned i = 1; i < 4; i++)
    surface.colorPitch[i] = 64;
  surface.depthFormat = GCM_SURFACE_ZETA_Z16;
  surface.depthOffset = depth_offset;
  surface.depthPitch = width * 2;
  surface.width = width;
  surface.height = height;
  cellGcmSetSurface(context, &surface);
  cellGcmSetTransferData(context, GCM_TRANSFER_LOCAL_TO_LOCAL,
                         buffer_offset[buffer], width * 4, canvas_offset,
                         width * 4, width * 4, height);
  cellGcmResetFlipStatus();
  cellGcmSetFlip(context, buffer);
  cellGcmFlush(context);
  cellGcmSetWaitFlip(context);
}
static void render(unsigned buffer) {
  draw();
  memcpy(canvas, pixels, width * height * 4);
  __sync_synchronize();
  present_buffer(buffer);
}
static int wait_flip(void) {
  unsigned n = 0;
  while (cellGcmGetFlipStatus() != 0 && running) {
    cellSysutilCheckCallback();
    usleep(1000);
    if (++n > 10000)
      return -1;
  }
  return 0;
}
int main(int argc, const char **argv) {
  (void)argc;
  (void)argv;
  void *host = memalign(1024 * 1024, HOST_SIZE);
  unsigned buffer = 0;
  unsigned previous = 0;
  NavigationRepeat navigation = {0};
  int redraw = 1;
  if (!host || screen_init(host)) {
    free(host);
    return 1;
  }
  ioPadInit(7);
  cellSysutilRegisterCallback(0, exit_callback, NULL);
  render(buffer);
  buffer ^= 1;
  if (wait_flip()) {
    running = 0;
  }
  ready = manager_init(GAME_DIR, BACKUP_DIR) == 0;
  likely_rpcs3 =
      environment_likely_rpcs3(sys_process_getpid(), sys_process_getppid());
  snprintf(status, sizeof(status), "%s", manager_message());
  if (ready) {
    refresh();
    selected = managed;
  }
  while (running) {
    cellSysutilCheckCallback();
    padInfo info;
    padData pad;
    unsigned buttons = previous;
    int connected = 0;
    memset(&info, 0, sizeof(info));
    ioPadGetInfo(&info);
    for (unsigned port = 0; port < 7; port++)
      if (info.status[port] & 1u) {
        connected = 1;
        memset(&pad, 0, sizeof(pad));
        if (!ioPadGetData(port, &pad) && pad.len) {
          buttons = pad.button[2] | (unsigned)pad.button[3] << 8;
        }
        break;
      }
    /* len == 0 means unchanged, not released. Keep the previous state to
     * avoid generating repeated press edges while a button remains held. */
    if (!connected)
      buttons = 0;
    unsigned down = buttons & ~previous;
    previous = buttons;
    int direction = 0;
    if (!credits && confirm < 0) {
      direction = !!(buttons & PAD_CTRL_DOWN) - !!(buttons & PAD_CTRL_UP);
    }
    int move =
        navigation_move(&navigation, direction, (uint64_t)sysGetSystemTime());
    if (credits) {
      if (down & (PAD_CTRL_CIRCLE << 8))
        credits = 0;
    } else if (confirm >= 0) {
      if (down & (PAD_CTRL_CIRCLE << 8))
        confirm = -1;
      else if (down & (PAD_CTRL_CROSS << 8)) {
        int action = confirm;
        confirm = -1;
        if (action == ACTION_EXIT)
          running = 0;
        else if (action_enabled(action)) {
          snprintf(status, sizeof(status),
                   "Working... please keep the console powered on.");
          render(buffer);
          buffer ^= 1;
          if (!wait_flip())
            operation(action);
        }
      }
    } else {
      if (move < 0)
        cursor = (cursor + UNLOCK_COUNT + ACTION_COUNT - 1) %
                 (UNLOCK_COUNT + ACTION_COUNT);
      if (move > 0)
        cursor = (cursor + 1) % (UNLOCK_COUNT + ACTION_COUNT);
      if (down & (PAD_CTRL_SQUARE << 8))
        selected = selected == ALL_UNLOCKS ? 0 : ALL_UNLOCKS;
      if (down & (PAD_CTRL_TRIANGLE << 8))
        credits = 1;
      else if (down & (PAD_CTRL_CIRCLE << 8))
        confirm = ACTION_EXIT;
      else if (down & (PAD_CTRL_CROSS << 8)) {
        if (cursor < UNLOCK_COUNT)
          selected ^= 1u << cursor;
        else if (cursor == UNLOCK_COUNT + ACTION_EXIT)
          confirm = ACTION_EXIT;
        else {
          int action = cursor - UNLOCK_COUNT;
          if (action_enabled(action))
            confirm = action;
        }
      }
    }
    /* Poll the pad independently of the relatively expensive framebuffer
     * redraw. A static menu must not make short button presses disappear. */
    if (wait_flip())
      break;
    if (down || move || redraw) {
      render(buffer);
      buffer ^= 1;
      redraw = 0;
    } else {
      present_buffer(buffer);
      buffer ^= 1;
    }
    usleep(10000);
  }
  cellGcmSetWaitFlip(context);
  cellGcmFinish(context, 1);
  cellSysutilUnregisterCallback(0);
  ioPadEnd();
  free(pixels);
  free(host);
  return 0;
}
