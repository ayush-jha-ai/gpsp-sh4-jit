/* Drives the real fxcg100_menu.c with scripted keys: the CHEATS page lists
 * and toggles the runner's cheats, and SAVE STATE AND EXIT returns its menu
 * result. Strong definitions here replace the menu's weak cheat hooks. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../ports/fxcg100/fxcg100_platform.h"

/* ---- scripted keys ------------------------------------------------------- */
static const uint32_t *script;
static unsigned script_pos;
static int release_next;

uint32_t fxcg100_poll_app_keys(void)
{
  uint32_t key;

  /* Every press is followed by a release so the menu sees a new edge. */
  if (release_next) {
    release_next = 0;
    return 0;
  }
  key = script[script_pos];
  assert(key != 0 && "the menu wanted more keys than the script has");
  script_pos++;
  release_next = 1;
  return key;
}

uint16_t fxcg100_poll_physical_key(void) { return 0; }
int fxcg100_key_bindable(uint16_t key) { (void)key; return 1; }
const char *fxcg100_key_label(uint16_t key) { (void)key; return "KEY"; }
void fxcg100_keymap_defaults(uint16_t *keymap)
{
  memset(keymap, 0, FXCG100_GBA_KEY_COUNT * sizeof(*keymap));
}
void fxcg100_hotkey_defaults(uint16_t *hotkey_map)
{
  memset(hotkey_map, 0, FXCG100_HOTKEY_COUNT * sizeof(*hotkey_map));
}
int fxcg100_config_load(fxcg100_menu_state *state) { (void)state; return 0; }
int fxcg100_config_save(const fxcg100_menu_state *state)
{
  (void)state;
  return 1;
}

/* ---- every string the menu draws, in order ------------------------------- */
static char drawn[1 << 16];

void fxcg100_lcd_clear(uint16_t color) { (void)color; }
void fxcg100_lcd_fill_rect(unsigned x, unsigned y, unsigned w, unsigned h,
                           uint16_t color)
{
  (void)x; (void)y; (void)w; (void)h; (void)color;
}
void fxcg100_lcd_draw_text(unsigned x, unsigned y, const char *text,
                           uint16_t fg, uint16_t bg)
{
  size_t used = strlen(drawn);

  (void)x; (void)y; (void)fg; (void)bg;
  assert(used + strlen(text) + 2 < sizeof drawn);
  strcat(drawn, text);
  strcat(drawn, "\n");
}
void fxcg100_lcd_update(void) {}

static int drew(const char *text)
{
  return strstr(drawn, text) != NULL;
}

/* ---- a fake runner with three cheats ------------------------------------- */
static int cheat_on;

uint32_t fxcg100_cheat_count(void) { return 3; }
const char *fxcg100_cheat_name(uint32_t slot)
{
  static const char *names[] = { "Infinite HP", "GS code", "Encrypted" };
  return slot < 3 ? names[slot] : NULL;
}
int fxcg100_cheat_state(uint32_t slot)
{
  if (slot == 0)
    return cheat_on ? FXCG100_CHEAT_ON : FXCG100_CHEAT_OFF;
  if (slot == 1)
    return FXCG100_CHEAT_UNSUPPORTED;
  if (slot == 2)
    return FXCG100_CHEAT_ENCRYPTED;
  return FXCG100_CHEAT_EMPTY;
}
int fxcg100_cheat_toggle(uint32_t slot)
{
  if (slot == 0)
    cheat_on = !cheat_on;
  return fxcg100_cheat_state(slot);
}
const char *fxcg100_cheat_status(void) { return "3 CHEATS FROM Game.cht"; }

#define UP    FXCG100_APPKEY_UP
#define DOWN  FXCG100_APPKEY_DOWN
#define LEFT  FXCG100_APPKEY_LEFT
#define RIGHT FXCG100_APPKEY_RIGHT
#define EXE   FXCG100_APPKEY_EXE
#define BACK  FXCG100_APPKEY_BACK

static fxcg100_menu_result run(fxcg100_menu_state *state, const uint32_t *keys)
{
  script = keys;
  script_pos = 0;
  release_next = 1;   /* the menu's first poll snapshots keys already held */
  drawn[0] = 0;
  return fxcg100_menu_run(state, 1, 0, NULL);
}

int main(void)
{
  fxcg100_menu_state state;

  fxcg100_menu_init(&state);

  /* Open the cheats page (main row 7 in release builds): it lists every slot
   * and shows where the cheats came from. */
  {
    static const uint32_t keys[] = {
      DOWN, DOWN, DOWN, DOWN, DOWN, DOWN, DOWN, EXE, BACK, BACK, 0
    };
    assert(run(&state, keys) == FXCG100_MENU_RETURN);
    assert(drew("CHEATS AND MISC OPTIONS"));
    assert(drew("3 CHEATS FROM Game.cht"));
    assert(drew("Infinite HP: OFF"));
    assert(drew("GS code: UNSUPPORTED"));
    assert(drew("Encrypted: ENCRYPTED"));
    assert(drew("CHEAT 3: EMPTY"));
    assert(drew("CHEAT 9: EMPTY"));
    assert(!cheat_on);
  }

  /* EXE, RIGHT and LEFT all toggle; unusable slots explain why. The main
   * page remembers its selection, so EXE reopens the cheats page. */
  {
    static const uint32_t keys[] = {
      EXE,              /* cheats page */
      EXE,              /* slot 0 on */
      RIGHT,            /* off */
      LEFT,             /* on */
      DOWN, EXE,        /* slot 1 */
      DOWN, EXE,        /* slot 2 */
      DOWN, EXE,        /* slot 3, empty */
      BACK, BACK, 0
    };
    assert(run(&state, keys) == FXCG100_MENU_RETURN);
    assert(cheat_on);
    assert(drew("CHEAT ON"));
    assert(drew("CHEAT OFF"));
    assert(drew("Infinite HP: ON"));
    assert(drew("ONLY CODEBREAKER CODES ARE SUPPORTED"));
    assert(drew("ENCRYPTED CODES ARE NOT SUPPORTED"));
  }

  /* The page remembers slot 3; UP x3 reaches slot 0 and one more wraps to
   * BACK, whose EXE returns to the main page (the final BACK then leaves). */
  {
    static const uint32_t keys[] = { EXE, UP, UP, UP, UP, EXE, BACK, 0 };
    assert(run(&state, keys) == FXCG100_MENU_RETURN);
    assert(cheat_on);
  }

  /* SAVE STATE AND EXIT is row 11, four below the cheats entry. */
  {
    static const uint32_t keys[] = { DOWN, DOWN, DOWN, DOWN, EXE, 0 };
    assert(run(&state, keys) == FXCG100_MENU_SAVE_EXIT);
    assert(drew("SAVE STATE AND EXIT"));
  }

  puts("fx-CG menu cheats page and SAVE STATE AND EXIT passed");
  return 0;
}
