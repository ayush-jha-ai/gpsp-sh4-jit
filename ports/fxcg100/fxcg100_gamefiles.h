#ifndef CGBA_FXCG100_GAMEFILES_H
#define CGBA_FXCG100_GAMEFILES_H

#include <stddef.h>
#include <stdint.h>

/* Per-game file naming and cheat-file parsing. These helpers have no OS or
 * gpSP dependencies so they are unit-tested on the host (tests/). */

/* Save base names are 7 characters, e.g. "POKE7QX" gives POKE7QX.SAV and
 * POKE7QX0.SVS .. POKE7QX9.SVS, which keeps every name 8.3-safe. The first 4
 * characters are letters/digits of the ROM label; the last 3 are a base-36
 * hash of the ROM file name, so ROMs that share a prefix ("Pokemon FireRed",
 * "Pokemon Emerald") no longer share save files. */
#define FXCG100_SAVE_BASE_LEN 7
#define FXCG100_SAVE_BASE_PREFIX_LEN 4
#define FXCG100_SAVE_BASE_LEGACY_LEN 6

/* label: the ROM label shown in the picker (ASCII file name). path: the ROM's
 * storage path, hashed from its base name; NULL or empty hashes the label. */
void fxcg100_save_base(char out[FXCG100_SAVE_BASE_LEN + 1], const char *label,
                       const uint16_t *path);

/* The naming used before hashed names: the first 6 letters/digits of the
 * label, or `fallback` when it has none. Kept so older saves still load. */
void fxcg100_save_base_legacy(char out[FXCG100_SAVE_BASE_LEGACY_LEN + 1],
                              const char *label, const char *fallback);

/* Store `base` + `suffix` (e.g. "0.SVS") under \\fls0\ as a UTF-16 path.
 * Returns 0 if it does not fit in max code units including the terminator. */
int fxcg100_storage_path(uint16_t *dst, size_t max, const char *base,
                         const char *suffix);

/* The cheat file sits next to its ROM with a .cht extension:
 * \\fls0\Pokemon Emerald.gba -> \\fls0\Pokemon Emerald.cht. Returns 0 if
 * the result does not fit in max code units including the terminator. */
int fxcg100_cheat_path(uint16_t *dst, size_t max, const uint16_t *rom_path);

/* Same rename for an ASCII label, for showing the expected file name. */
void fxcg100_cheat_file_label(char *dst, size_t max, const char *label);

#define FXCG100_CHEAT_SLOTS 10
#define FXCG100_CHEAT_NAME_MAX 32
#define FXCG100_CHEAT_CODE_MAX 1024

/* Called once per cheat found in the file, in file order. */
typedef void (*fxcg100_cheat_sink)(void *ctx, const char *name,
                                   const char *code, int enabled);

/* Parse a cheat file. Two formats are accepted, and may be mixed:
 *
 *   Plain text, one cheat per "Name: codes" line, or a name line followed
 *   by lines of codes. A leading '+' turns the cheat on at load. Lines
 *   starting with '#', ';' or "//" are comments; a blank line ends a cheat.
 *
 *   libretro .cht files: cheatN_desc, cheatN_code and cheatN_enable keys.
 *
 * Returns the number of cheats passed to sink. */
unsigned fxcg100_cheats_parse(const char *text, size_t len,
                              fxcg100_cheat_sink sink, void *ctx);

/* gpSP only understands unencrypted CodeBreaker codes: pairs of an 8-digit
 * and a 4-digit hex number ("82025D98 03E7"). Returns 1 if every token in
 * code fits that shape, so GameShark/Action Replay codes (8+8 digits) are
 * rejected instead of being misread as garbage memory writes. */
int fxcg100_cheat_code_is_codebreaker(const char *code);

#endif
