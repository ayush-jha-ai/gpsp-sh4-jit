#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../ports/fxcg100/fxcg100_gamefiles.h"

static void to_path(uint16_t *dst, size_t max, const char *s)
{
  size_t i;

  for (i = 0; s[i] && i + 1 < max; i++)
    dst[i] = (uint16_t)(uint8_t)s[i];
  dst[i] = 0;
}

static int path_equals(const uint16_t *path, const char *s)
{
  size_t i;

  for (i = 0; s[i]; i++)
    if (path[i] != (uint16_t)(uint8_t)s[i])
      return 0;
  return path[i] == 0;
}

static void base_for(char *out, const char *label)
{
  uint16_t path[128];
  char full[160];

  snprintf(full, sizeof full, "\\\\fls0\\%s", label);
  to_path(path, 128, full);
  fxcg100_save_base(out, label, path);
}

static void test_save_names(void)
{
  char a[FXCG100_SAVE_BASE_LEN + 1], b[FXCG100_SAVE_BASE_LEN + 1];
  char legacy[FXCG100_SAVE_BASE_LEGACY_LEN + 1];
  uint16_t path[128];

  /* The bug: both used to become POKEMO.SAV and overwrite each other. */
  base_for(a, "Pokemon FireRed.gba");
  base_for(b, "Pokemon Emerald.gba");
  assert(strlen(a) == FXCG100_SAVE_BASE_LEN);
  assert(strncmp(a, "POKE", 4) == 0 && strncmp(b, "POKE", 4) == 0);
  assert(strcmp(a, b) != 0);
  for (const char *c = a; *c; c++)
    assert((*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9'));

  /* Stable across calls, folder spelling and file-name case. */
  base_for(b, "Pokemon FireRed.gba");
  assert(strcmp(a, b) == 0);
  to_path(path, 128, "\\\\fls0\\POKEMON FIRERED.GBA");
  fxcg100_save_base(b, "POKEMON FIRERED.GBA", path);
  assert(strcmp(a, b) == 0);
  to_path(path, 128, "\\\\fls0\\games\\Pokemon FireRed.gba");
  fxcg100_save_base(b, "Pokemon FireRed.gba", path);
  assert(strcmp(a, b) == 0);

  /* Without a path the label is hashed instead. */
  fxcg100_save_base(b, "Pokemon FireRed.gba", NULL);
  assert(strlen(b) == FXCG100_SAVE_BASE_LEN && strncmp(b, "POKE", 4) == 0);

  /* Short and symbol-only names are padded to a full prefix. */
  base_for(a, "a.gba");
  assert(strncmp(a, "AXXX", 4) == 0 && strlen(a) == FXCG100_SAVE_BASE_LEN);
  base_for(a, "!!!.gba");
  assert(strncmp(a, "XXXX", 4) == 0);

  /* The 6-character names are reproduced exactly for migration. */
  fxcg100_save_base_legacy(legacy, "Pokemon FireRed.gba", "GAME");
  assert(strcmp(legacy, "POKEMO") == 0);
  fxcg100_save_base_legacy(legacy, "Zelda - Minish Cap.gba", "GAME");
  assert(strcmp(legacy, "ZELDAM") == 0);
  fxcg100_save_base_legacy(legacy, "!!!.gba", "GAME");
  assert(strcmp(legacy, "GAME") == 0);
  fxcg100_save_base_legacy(legacy, "!!!.gba", "");
  assert(strcmp(legacy, "") == 0);

  /* Full paths fit the runner's 24-unit buffers. */
  assert(fxcg100_storage_path(path, 24, "POKE7QX", "9.SVS"));
  assert(path_equals(path, "\\\\fls0\\POKE7QX9.SVS"));
  assert(fxcg100_storage_path(path, 24, "POKE7QX", ".SAV"));
  assert(path_equals(path, "\\\\fls0\\POKE7QX.SAV"));
  assert(!fxcg100_storage_path(path, 12, "POKE7QX", ".SAV"));
}

static void test_cheat_paths(void)
{
  uint16_t rom[128], cht[128];
  char label[64];

  to_path(rom, 128, "\\\\fls0\\Pokemon Emerald.gba");
  assert(fxcg100_cheat_path(cht, 128, rom));
  assert(path_equals(cht, "\\\\fls0\\Pokemon Emerald.cht"));

  to_path(rom, 128, "\\\\fls0\\v1.2\\GAME");          /* no extension */
  assert(fxcg100_cheat_path(cht, 128, rom));
  assert(path_equals(cht, "\\\\fls0\\v1.2\\GAME.cht"));

  to_path(rom, 128, "\\\\fls0\\Pokemon Emerald.gba");
  assert(!fxcg100_cheat_path(cht, 10, rom));            /* too long */
  rom[0] = 0;
  assert(!fxcg100_cheat_path(cht, 128, rom));

  fxcg100_cheat_file_label(label, sizeof label, "Pokemon Emerald.gba");
  assert(strcmp(label, "Pokemon Emerald.cht") == 0);
  fxcg100_cheat_file_label(label, sizeof label, "GAME");
  assert(strcmp(label, "GAME.cht") == 0);
  fxcg100_cheat_file_label(label, 8, "Pokemon Emerald.gba");
  assert(strlen(label) == 7);
}

typedef struct seen_cheat {
  char name[FXCG100_CHEAT_NAME_MAX];
  char code[FXCG100_CHEAT_CODE_MAX];
  int enabled;
} seen_cheat;

static seen_cheat seen[32];
static unsigned seen_count;

static void collect(void *ctx, const char *name, const char *code,
                    int enabled)
{
  (void)ctx;
  assert(seen_count < 32);
  snprintf(seen[seen_count].name, sizeof seen[0].name, "%s", name);
  snprintf(seen[seen_count].code, sizeof seen[0].code, "%s", code);
  seen[seen_count].enabled = enabled;
  seen_count++;
}

static unsigned parse(const char *text)
{
  seen_count = 0;
  memset(seen, 0, sizeof seen);
  return fxcg100_cheats_parse(text, strlen(text), collect, NULL);
}

static void test_plain_cheats(void)
{
  unsigned n = parse(
    "# Pokemon Emerald\r\n"
    "Infinite HP: 82025D98 03E7\r\n"
    "+Max money: 83005E3C 423F 83005E3E 000F\n"
    "\n"
    "Walk through walls\n"
    "82005074 0000\n"
    "82005076 0000\n"
    "\n"
    "// comment\n"
    "; another comment\n"
    "Rare candy:\n"
    "83005B90 0044\n"
    "Add 99 Bees\n"
    "82000000 1234\n");

  assert(n == 5 && seen_count == 5);
  assert(strcmp(seen[0].name, "Infinite HP") == 0);
  assert(strcmp(seen[0].code, "82025D98 03E7") == 0);
  assert(!seen[0].enabled);
  assert(strcmp(seen[1].name, "Max money") == 0 && seen[1].enabled);
  assert(strcmp(seen[2].name, "Walk through walls") == 0);
  assert(strcmp(seen[2].code, "82005074 0000 82005076 0000") == 0);
  assert(strcmp(seen[3].name, "Rare candy") == 0);
  assert(strcmp(seen[3].code, "83005B90 0044") == 0);
  /* "Add 99 Bees" is all hex letters/digits but has no 8-digit run, so it
   * is a name, not a code. */
  assert(strcmp(seen[4].name, "Add 99 Bees") == 0);

  /* Codes with no name get a numbered one; names with no codes are dropped. */
  n = parse("82025D98 03E7\n\nNo codes here\n\n83005B90 0044\n");
  assert(n == 2);
  assert(strcmp(seen[0].name, "CHEAT 0") == 0);
  assert(strcmp(seen[1].name, "CHEAT 1") == 0);

  /* Long names are cut to fit the menu, and odd bytes become '?'. */
  n = parse("A really very long cheat name that goes on: 82025D98 03E7\n"
            "Caf\xe9: 83005B90 0044\n");
  assert(n == 2);
  assert(strlen(seen[0].name) == FXCG100_CHEAT_NAME_MAX - 1);
  assert(strcmp(seen[1].name, "Caf?") == 0);

  assert(parse("") == 0);
  assert(parse("# only comments\n\n") == 0);
}

static void test_libretro_cheats(void)
{
  unsigned n = parse(
    "cheats = 3\n"
    "\n"
    "cheat0_desc = \"Infinite Health\"\n"
    "cheat0_code = \"82025D98+03E7\"\n"
    "cheat0_enable = false\n"
    "\n"
    "cheat1_desc = \"Master Code\"\n"
    "cheat1_code = \"0000BE99+000A+1003DB8C+0007\"\n"
    "cheat1_enable = true\n"
    "cheat2_desc = \"No code\"\n"
    "cheat2_enable = true\n");

  assert(n == 2);
  assert(strcmp(seen[0].name, "Infinite Health") == 0);
  assert(strcmp(seen[0].code, "82025D98+03E7") == 0);
  assert(!seen[0].enabled);
  assert(strcmp(seen[1].name, "Master Code") == 0);
  assert(strcmp(seen[1].code, "0000BE99+000A+1003DB8C+0007") == 0);
  assert(seen[1].enabled);

  /* A key without a description still produces a cheat. */
  n = parse("cheat0_code = \"82025D98 03E7\"\ncheat0_enable = true\n");
  assert(n == 1 && seen[0].enabled);
  assert(strcmp(seen[0].name, "CHEAT 0") == 0);

  /* Text after the parser stops (embedded NUL) is ignored. */
  {
    static const char text[] = "Hp: 82025D98 03E7\n\0Junk: 83005B90 0044\n";
    seen_count = 0;
    n = fxcg100_cheats_parse(text, sizeof(text) - 1, collect, NULL);
    assert(n == 1);
  }
}

static void test_codebreaker_shape(void)
{
  assert(fxcg100_cheat_code_is_codebreaker("82025D98 03E7"));
  assert(fxcg100_cheat_code_is_codebreaker("82025D98+03E7"));
  assert(fxcg100_cheat_code_is_codebreaker("0000BE99 000A 1003DB8C 0007"));
  assert(fxcg100_cheat_code_is_codebreaker("82025d98:03e7"));
  /* GameShark / Action Replay: 8+8 digits. */
  assert(!fxcg100_cheat_code_is_codebreaker("1234ABCD 5678EF01"));
  /* Odd number of tokens, wrong widths, nothing at all. */
  assert(!fxcg100_cheat_code_is_codebreaker("82025D98"));
  assert(!fxcg100_cheat_code_is_codebreaker("82025D98 03E"));
  assert(!fxcg100_cheat_code_is_codebreaker("2025D98 03E7"));
  assert(!fxcg100_cheat_code_is_codebreaker(""));
  assert(!fxcg100_cheat_code_is_codebreaker(NULL));
}

int main(void)
{
  test_save_names();
  test_cheat_paths();
  test_plain_cheats();
  test_libretro_cheats();
  test_codebreaker_shape();
  puts("fx-CG save names, cheat paths and cheat-file parser passed");
  return 0;
}
