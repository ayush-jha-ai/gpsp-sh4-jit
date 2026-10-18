#include "fxcg100_gamefiles.h"

#include <string.h>

static const char storage_prefix[] = "\\\\fls0\\";

static char upper_alnum(char c)
{
  if (c >= 'a' && c <= 'z')
    return (char)(c - 'a' + 'A');
  if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
    return c;
  return 0;
}

/* Letters/digits of the label before its first '.', upper-cased. */
static unsigned label_prefix(char *out, unsigned max, const char *label)
{
  unsigned n = 0;

  for (; label && *label && *label != '.' && n < max; label++) {
    char c = upper_alnum(*label);

    if (c)
      out[n++] = c;
  }
  return n;
}

static size_t path_len(const uint16_t *path)
{
  size_t n = 0;

  while (path && path[n])
    n++;
  return n;
}

static size_t path_basename_start(const uint16_t *path, size_t len)
{
  size_t start = 0;

  for (size_t i = 0; i < len; i++)
    if (path[i] == '\\' || path[i] == '/')
      start = i + 1;
  return start;
}

static uint32_t fnv1a_step(uint32_t h, uint8_t byte)
{
  return (h ^ byte) * 16777619u;
}

/* Case-insensitive FNV-1a of the ROM file name (not the folder, so the
 * \\fls0\ prefix and its spelling never change the result). */
static uint32_t rom_name_hash(const char *label, const uint16_t *path)
{
  uint32_t h = 2166136261u;
  size_t len = path_len(path);

  if (len) {
    for (size_t i = path_basename_start(path, len); i < len; i++) {
      uint16_t c = path[i];

      if (c >= 'a' && c <= 'z')
        c = (uint16_t)(c - 'a' + 'A');
      h = fnv1a_step(h, (uint8_t)c);
      if (c >> 8)
        h = fnv1a_step(h, (uint8_t)(c >> 8));
    }
    return h;
  }

  for (; label && *label; label++) {
    char c = *label;

    if (c >= 'a' && c <= 'z')
      c = (char)(c - 'a' + 'A');
    h = fnv1a_step(h, (uint8_t)c);
  }
  return h;
}

void fxcg100_save_base(char out[FXCG100_SAVE_BASE_LEN + 1], const char *label,
                       const uint16_t *path)
{
  static const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  uint32_t h = rom_name_hash(label, path) % (36u * 36u * 36u);
  unsigned n = label_prefix(out, FXCG100_SAVE_BASE_PREFIX_LEN, label);

  while (n < FXCG100_SAVE_BASE_PREFIX_LEN)
    out[n++] = 'X';
  out[n + 2] = digits[h % 36u];
  h /= 36u;
  out[n + 1] = digits[h % 36u];
  h /= 36u;
  out[n] = digits[h % 36u];
  out[FXCG100_SAVE_BASE_LEN] = 0;
}

void fxcg100_save_base_legacy(char out[FXCG100_SAVE_BASE_LEGACY_LEN + 1],
                              const char *label, const char *fallback)
{
  unsigned n = label_prefix(out, FXCG100_SAVE_BASE_LEGACY_LEN, label);

  if (n == 0) {
    for (; fallback && fallback[n] && n < FXCG100_SAVE_BASE_LEGACY_LEN; n++)
      out[n] = fallback[n];
  }
  out[n] = 0;
}

int fxcg100_storage_path(uint16_t *dst, size_t max, const char *base,
                         const char *suffix)
{
  size_t n = 0;
  const char *parts[3] = { storage_prefix, base, suffix };

  for (unsigned p = 0; p < 3; p++) {
    for (const char *s = parts[p]; s && *s; s++) {
      if (n + 1 >= max)
        return 0;
      dst[n++] = (uint16_t)(uint8_t)*s;
    }
  }
  if (n >= max)
    return 0;
  dst[n] = 0;
  return 1;
}

/* Index where the extension's '.' starts, or len when there is none. */
static size_t path_extension_start(const uint16_t *path, size_t len)
{
  size_t start = path_basename_start(path, len);

  for (size_t i = len; i > start; i--)
    if (path[i - 1] == '.')
      return i - 1;
  return len;
}

int fxcg100_cheat_path(uint16_t *dst, size_t max, const uint16_t *rom_path)
{
  static const char ext[] = ".cht";
  size_t len = path_len(rom_path);
  size_t stem;

  if (len == 0)
    return 0;
  stem = path_extension_start(rom_path, len);
  if (stem + sizeof(ext) > max)
    return 0;
  memmove(dst, rom_path, stem * sizeof(*dst));
  for (size_t i = 0; i < sizeof(ext); i++)
    dst[stem + i] = (uint16_t)ext[i];
  return 1;
}

void fxcg100_cheat_file_label(char *dst, size_t max, const char *label)
{
  size_t len = label ? strlen(label) : 0;
  size_t stem = len;
  size_t n;

  if (max == 0)
    return;
  while (stem > 0 && label[stem - 1] != '.')
    stem--;
  stem = stem ? stem - 1 : len;
  n = stem < max - 1 ? stem : max - 1;
  memcpy(dst, label, n);
  dst[n] = 0;
  strncat(dst, ".cht", max - 1 - n);
}

/* ---- cheat files --------------------------------------------------------- */

typedef struct cheat_pending {
  char name[FXCG100_CHEAT_NAME_MAX];
  char code[FXCG100_CHEAT_CODE_MAX];
  size_t code_len;
  int enabled;
  int open;
  long index;           /* libretro cheatN index, -1 for plain-text cheats */
} cheat_pending;

typedef struct cheat_parser {
  cheat_pending cur;
  fxcg100_cheat_sink sink;
  void *ctx;
  unsigned count;
} cheat_parser;

static int is_blank(char c)
{
  return c == ' ' || c == '\t' || c == '\r';
}

static int is_hex(char c)
{
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
    (c >= 'A' && c <= 'F');
}

static int ascii_lower(int c)
{
  return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
}

static int starts_with_nocase(const char *s, size_t len, const char *prefix)
{
  size_t i;

  for (i = 0; prefix[i]; i++)
    if (i >= len || ascii_lower((unsigned char)s[i]) != prefix[i])
      return 0;
  return 1;
}

static void trim(const char **s, size_t *len)
{
  while (*len && is_blank(**s)) {
    (*s)++;
    (*len)--;
  }
  while (*len && is_blank((*s)[*len - 1]))
    (*len)--;
}

/* A run of codes: hex digits and separators, with at least one 8-digit hex
 * number, so a short word like "Add" or "Bee" is never taken for a code. */
static int looks_like_codes(const char *s, size_t len)
{
  size_t run = 0;
  int long_run = 0;

  for (size_t i = 0; i < len; i++) {
    char c = s[i];

    if (is_hex(c)) {
      if (++run >= 8)
        long_run = 1;
    } else if (is_blank(c) || c == '+' || c == ',') {
      run = 0;
    } else {
      return 0;
    }
  }
  return long_run;
}

static void set_name(cheat_pending *p, const char *s, size_t len)
{
  size_t n = 0;

  trim(&s, &len);
  if (len >= 2 && s[0] == '"' && s[len - 1] == '"') {
    s++;
    len -= 2;
  }
  for (size_t i = 0; i < len && n + 1 < sizeof(p->name); i++) {
    unsigned char c = (unsigned char)s[i];

    p->name[n++] = (c >= 32 && c < 127) ? (char)c : '?';
  }
  p->name[n] = 0;
  p->open = 1;
}

static void append_code(cheat_pending *p, const char *s, size_t len)
{
  trim(&s, &len);
  if (len >= 2 && s[0] == '"' && s[len - 1] == '"') {
    s++;
    len -= 2;
  }
  if (len == 0)
    return;
  if (p->code_len && p->code_len + 1 < sizeof(p->code))
    p->code[p->code_len++] = ' ';
  for (size_t i = 0; i < len && p->code_len + 1 < sizeof(p->code); i++)
    p->code[p->code_len++] = s[i];
  p->code[p->code_len] = 0;
  p->open = 1;
}

static void emit(cheat_parser *ps)
{
  cheat_pending *p = &ps->cur;

  if (p->open && p->code_len) {
    if (!p->name[0]) {
      unsigned n = ps->count;
      char *d = p->name;

      memcpy(d, "CHEAT ", 6);
      d += 6;
      if (n >= 10)
        *d++ = (char)('0' + (n / 10) % 10);
      *d++ = (char)('0' + n % 10);
      *d = 0;
    }
    ps->sink(ps->ctx, p->name, p->code, p->enabled);
    ps->count++;
  }
  p->name[0] = 0;
  p->code[0] = 0;
  p->code_len = 0;
  p->enabled = 0;
  p->open = 0;
  p->index = -1;
}

/* "cheat12_code = ..." -> index 12, key "code", value "...". */
static int parse_retro_key(const char *s, size_t len, long *index,
                           const char **key, size_t *key_len,
                           const char **value, size_t *value_len)
{
  size_t i = 5;
  long n = 0;

  if (!starts_with_nocase(s, len, "cheat") || i >= len ||
      s[i] < '0' || s[i] > '9')
    return 0;
  while (i < len && s[i] >= '0' && s[i] <= '9')
    n = n * 10 + (s[i++] - '0');
  if (i >= len || s[i] != '_')
    return 0;
  *key = s + ++i;
  while (i < len && s[i] != '=' && !is_blank(s[i]))
    i++;
  *key_len = (size_t)(s + i - *key);
  while (i < len && is_blank(s[i]))
    i++;
  if (i >= len || s[i] != '=')
    return 0;
  *value = s + i + 1;
  *value_len = len - i - 1;
  *index = n;
  return 1;
}

static void parse_line(cheat_parser *ps, const char *s, size_t len)
{
  cheat_pending *p = &ps->cur;
  const char *colon;
  long index;
  const char *key, *value;
  size_t key_len, value_len;
  int enabled = 0;

  trim(&s, &len);
  if (len == 0) {
    emit(ps);
    return;
  }
  if (s[0] == '#' || s[0] == ';' || (len >= 2 && s[0] == '/' && s[1] == '/'))
    return;

  if (parse_retro_key(s, len, &index, &key, &key_len, &value, &value_len)) {
    if (index != p->index || (p->open && p->index < 0))
      emit(ps);
    p->index = index;
    if (key_len == 4 && starts_with_nocase(key, key_len, "desc"))
      set_name(p, value, value_len);
    else if (key_len == 4 && starts_with_nocase(key, key_len, "code"))
      append_code(p, value, value_len);
    else if (key_len == 6 && starts_with_nocase(key, key_len, "enable")) {
      trim(&value, &value_len);
      p->enabled = value_len && (value[0] == 't' || value[0] == 'T' ||
                                 value[0] == '1');
    }
    return;
  }
  if (starts_with_nocase(s, len, "cheats") && memchr(s, '=', len))
    return;   /* libretro "cheats = N" header */

  if (looks_like_codes(s, len)) {
    if (p->index >= 0)
      emit(ps);
    append_code(p, s, len);
    return;
  }

  if (s[0] == '+') {
    enabled = 1;
    s++;
    len--;
  }
  emit(ps);
  colon = memchr(s, ':', len);
  if (colon && looks_like_codes(colon + 1, len - (size_t)(colon + 1 - s))) {
    set_name(p, s, (size_t)(colon - s));
    append_code(p, colon + 1, len - (size_t)(colon + 1 - s));
  } else {
    if (colon && colon == s + len - 1)
      len--;           /* "Infinite HP:" with the codes on the next lines */
    set_name(p, s, len);
  }
  p->enabled = enabled;
}

unsigned fxcg100_cheats_parse(const char *text, size_t len,
                              fxcg100_cheat_sink sink, void *ctx)
{
  cheat_parser ps;
  size_t start = 0;

  if (!text || !sink)
    return 0;
  memset(&ps, 0, sizeof(ps));
  ps.cur.index = -1;
  ps.sink = sink;
  ps.ctx = ctx;

  for (size_t i = 0; i <= len; i++) {
    if (i == len || text[i] == '\n' || text[i] == '\0') {
      parse_line(&ps, text + start, i - start);
      start = i + 1;
      if (i < len && text[i] == '\0')
        break;
    }
  }
  emit(&ps);
  return ps.count;
}

int fxcg100_cheat_code_is_codebreaker(const char *code)
{
  unsigned tokens = 0;

  if (!code)
    return 0;
  while (*code) {
    unsigned run = 0;

    while (*code && !is_hex(*code))
      code++;
    while (is_hex(*code)) {
      run++;
      code++;
    }
    if (run == 0)
      break;
    if (run != ((tokens & 1) ? 4u : 8u))
      return 0;
    tokens++;
  }
  return tokens > 0 && (tokens & 1) == 0;
}
