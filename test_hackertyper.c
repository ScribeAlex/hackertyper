/*
 * test_hackertyper.c -- tests for hackertyper's internals.
 *
 * Everything in hackertyper.c is static in one translation unit, so the test
 * includes the source directly and renames main out of the way. Nothing here
 * calls term_enter: the tests must never put the real console into raw mode.
 *
 * Build:  cc -std=c99 -O1 -Wall -Wextra -o test_hackertyper test_hackertyper.c
 *   MSVC: cl /nologo /W4 /D_CRT_SECURE_NO_WARNINGS test_hackertyper.c
 */

#define main hackertyper_main_under_test
#include "hackertyper.c"
#undef main

static int checks = 0;
static int failures = 0;

static void check(int ok, const char *what)
{
    checks++;
    if (!ok) {
        failures++;
        printf("  FAIL  %s\n", what);
    }
}

/* Keep /W4 quiet about the entry points the tests deliberately never call. */
static void reference_untested(void)
{
    void (*a)(void)          = term_enter;
    void (*b)(void)          = term_leave;
    void (*c)(int *, int *)  = term_size;
    int  (*d)(void)          = term_key;
    void (*e)(void)          = restore_terminal;
    void (*f)(int)           = on_signal;
    void (*g)(const char *)  = usage;
    int  (*h)(int, char **)  = hackertyper_main_under_test;
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; (void)h;
}

/* ------------------------------------------------------------------ *
 * Lexer
 * ------------------------------------------------------------------ */
static const char *g_src;
static unsigned char *g_tone;
static size_t g_len;

static void lex(const char *s)
{
    free(g_tone);
    g_src = s;
    g_len = strlen(s);
    g_tone = (unsigned char *)calloc(g_len ? g_len : 1, 1);
    paint(g_src, g_len, g_tone);
}

/* tone of the first byte of the first occurrence of `needle` */
static int tone_at(const char *needle)
{
    const char *p = strstr(g_src, needle);
    if (!p) return -1;
    return (int)g_tone[(size_t)(p - g_src)];
}

/* true when every byte of the first occurrence of `needle` carries `t` */
static int tone_all(const char *needle, Tone t)
{
    const char *p = strstr(g_src, needle);
    size_t i, n;
    if (!p) return 0;
    n = strlen(needle);
    for (i = 0; i < n; i++)
        if (g_tone[(size_t)(p - g_src) + i] != (unsigned char)t)
            return 0;
    return 1;
}

static void test_lexer(void)
{
    puts("lexer");

    lex("#include <stdio.h>\nint main(void) { return 0; }\n");
    check(tone_all("#include <stdio.h>", T_PREPROC),
          "preprocessor line is painted to end of line");
    check(tone_at("int") == T_TYPE, "int classifies as a type");
    check(tone_at("return") == T_KEYWORD, "return classifies as a keyword");
    check(tone_at("main") == T_PLAIN, "main classifies as plain");

    lex("  /* block comment */ x\n");
    check(tone_all("/* block comment */", T_COMMENT),
          "block comment covers its delimiters");
    check(tone_at("x") == T_PLAIN, "code after a block comment is not comment");

    lex("a // trailing\nb\n");
    check(tone_all("// trailing", T_COMMENT), "line comment runs to end of line");
    check(tone_at("b") == T_PLAIN, "line comment does not swallow the next line");

    /* word boundaries: a keyword must not match inside a longer identifier */
    lex("return_value intx voided\n");
    check(tone_at("return_value") == T_PLAIN, "return_value is not a keyword");
    check(tone_at("intx") == T_PLAIN, "intx is not a type");
    check(tone_at("voided") == T_PLAIN, "voided is not a type");

    lex("int n = 42; double d = 3.14; unsigned u = 0xFFu;\n");
    check(tone_at("42") == T_NUMBER, "integer literal is a number");
    check(tone_all("3.14", T_NUMBER), "float literal keeps its decimal point");
    check(tone_all("0xFFu", T_NUMBER), "hex literal with suffix is one number");

    /* a string holding an escaped quote must not end early */
    lex("char *s = \"a\\\"b\"; int after;\n");
    check(tone_all("\"a\\\"b\"", T_STRING), "escaped quote does not end the string");
    check(tone_at("after") == T_PLAIN, "code after a string is not string");

    lex("char c = 'z'; char q = '\\'';\n");
    check(tone_all("'z'", T_STRING), "character literal is painted");
    check(tone_all("'\\''", T_STRING), "escaped quote in a char literal holds");

    /* an unterminated string must stop at the newline, not run to EOF */
    lex("bad = \"oops\ngood;\n");
    check(tone_at("good") == T_PLAIN, "unterminated string stops at the newline");

    /* every byte must be classified; nothing left at a bogus value */
    lex("int x;\n");
    {
        size_t i;
        int in_range = 1;
        for (i = 0; i < g_len; i++)
            if (g_tone[i] >= T_COUNT) in_range = 0;
        check(in_range, "every tone byte is a valid Tone");
    }

    lex("");
    check(1, "empty input does not crash the lexer");
}

/* ------------------------------------------------------------------ *
 * Screen grid
 * ------------------------------------------------------------------ */
static void test_screen(void)
{
    Screen s;
    puts("screen");

    /* wrap at the right edge */
    screen_alloc(&s, 10, 3);
    screen_print(&s, "0123456789A", T_PLAIN);
    check(s.cy == 1 && s.cx == 1, "cursor wraps to the next row at the edge");
    check(s.cell[1 * 10 + 0].ch == 'A', "the 11th char lands at row 1 col 0");
    screen_free(&s);

    /* tab expands to the next multiple of four */
    screen_alloc(&s, 20, 2);
    screen_putc(&s, '\t', T_PLAIN);
    check(s.cx == 4, "tab at column 0 advances to column 4");
    check(s.cell[0].ch == ' ', "tab is written as spaces, not a tab byte");
    screen_free(&s);

    screen_alloc(&s, 20, 2);
    screen_print(&s, "ab\t", T_PLAIN);
    check(s.cx == 4, "tab at column 2 advances to column 4, not 6");
    screen_free(&s);

    /* newline, and scroll when the cursor runs off the bottom */
    screen_alloc(&s, 4, 2);
    screen_print(&s, "a\nb\nc", T_PLAIN);
    check(s.cell[0 * 4 + 0].ch == 'b', "scroll moves row 1 up into row 0");
    check(s.cell[1 * 4 + 0].ch == 'c', "the newest line sits on the last row");
    check(s.cy == 1, "cursor stays on the last row after a scroll");
    screen_free(&s);

    /* carriage return is dropped, so CRLF payloads do not double-space */
    screen_alloc(&s, 10, 3);
    screen_print(&s, "a\r\nb", T_PLAIN);
    check(s.cy == 1 && s.cx == 1, "a CR before LF advances exactly one row");
    screen_free(&s);

    /* the scrolled-in row must be blank, not a copy of old cells */
    screen_alloc(&s, 4, 2);
    screen_print(&s, "xxxx\nyyyy\n", T_PLAIN);
    check(s.cell[1 * 4 + 0].ch == 0, "the row scrolled in is cleared");
    screen_free(&s);

    /* a 1x1 screen is degenerate but must not corrupt memory */
    screen_alloc(&s, 1, 1);
    screen_print(&s, "abcdef", T_PLAIN);
    check(s.cy == 0, "a one-row screen keeps the cursor on row 0");
    screen_free(&s);
}

/* ------------------------------------------------------------------ *
 * Reveal loop (the three lines main runs per keystroke)
 * ------------------------------------------------------------------ */
static void test_reveal_wraps(void)
{
    const char *src = payload_builtin();
    size_t total = strlen(src);
    unsigned char *tone = (unsigned char *)calloc(total, 1);
    Screen s;
    size_t pos;
    int k;

    puts("reveal loop");
    check(total > 0, "the built-in payload is not empty");

    paint(src, total, tone);
    screen_alloc(&s, 40, 5);

    /* start two bytes from the end and reveal five: it must wrap to the top */
    pos = total - 2;
    for (k = 0; k < 5; k++) {
        if (pos >= total) pos = 0;
        screen_putc(&s, src[pos], tone[pos]);
        pos++;
    }
    check(pos == 3, "the reel wraps to the start instead of reading past the end");

    screen_free(&s);
    free(tone);
}

/* ------------------------------------------------------------------ *
 * Frame composition
 * ------------------------------------------------------------------ */
static void test_render(void)
{
    Screen s;
    Buf b;
    size_t i;
    int clean = 1;

    puts("render");

    /* wide enough that the whole status line fits without truncation */
    memset(&b, 0, sizeof b);
    screen_alloc(&s, 80, 3);
    screen_print(&s, "int x;\n", T_PLAIN);
    render(&b, &s, OV_NONE, 7, 10, 100, 80, 4);

    check(b.len > 0, "render produces output");
    check(memchr(b.data, '\0', b.len) == NULL, "the frame holds no NUL bytes");
    for (i = 0; i < b.len; i++) {
        unsigned char c = (unsigned char)b.data[i];
        if (c != 0x1b && c != '\r' && c != '\n' && (c < 0x20 || c > 0x7e)) clean = 0;
    }
    check(clean, "every frame byte is printable ASCII or ESC");

    /* the status bar reports the counters it was handed */
    b.data[b.len] = '\0';
    check(strstr(b.data, "HACKERTYPE") != NULL, "the status bar is drawn");
    check(strstr(b.data, "keys 7") != NULL, "the status bar shows the keystroke count");
    check(strstr(b.data, "10/100") != NULL, "the status bar shows payload progress");
    check(strstr(b.data, "\033[H") != NULL, "the frame homes the cursor first");

    /* placards draw only when asked for */
    buf_reset(&b);
    render(&b, &s, OV_GRANTED, 0, 0, 1, 60, 20);
    b.data[b.len] = '\0';
    check(strstr(b.data, "ACCESS GRANTED") != NULL, "the granted placard draws");

    buf_reset(&b);
    render(&b, &s, OV_DENIED, 0, 0, 1, 60, 20);
    b.data[b.len] = '\0';
    check(strstr(b.data, "ACCESS DENIED") != NULL, "the denied placard draws");

    buf_reset(&b);
    render(&b, &s, OV_NONE, 0, 0, 1, 60, 20);
    b.data[b.len] = '\0';
    check(strstr(b.data, "ACCESS") == NULL, "no placard draws when none is active");

    screen_free(&s);

    /* On a narrow terminal the placard must be skipped rather than scribbled
     * outside the box, and the status line truncated rather than padded by a
     * negative width. 20 columns cuts the line before the payload counter. */
    screen_alloc(&s, 20, 3);
    screen_print(&s, "int x;\n", T_PLAIN);

    buf_reset(&b);
    render(&b, &s, OV_GRANTED, 0, 0, 1, 20, 4);
    b.data[b.len] = '\0';
    check(strstr(b.data, "ACCESS GRANTED") == NULL,
          "the placard is skipped when the terminal is too small");
    check(strstr(b.data, "HACKERTYPE") != NULL,
          "a narrow terminal still draws a truncated status bar");
    check(strstr(b.data, "10/100") == NULL,
          "the status line is cut at the terminal width");

    buf_reset(&b);
    render(&b, &s, OV_NONE, 999999, 123456, 999999, 12, 4);
    check(b.len > 0, "a 12-column terminal still renders a frame");

    screen_free(&s);
    free(b.data);
}

/* ------------------------------------------------------------------ *
 * Buffer and colour helpers
 * ------------------------------------------------------------------ */
static void test_buf_and_tone(void)
{
    Buf b;
    char big[700];
    const char *plain;

    puts("buffer and tones");

    memset(&b, 0, sizeof b);
    buf_puts(&b, "abc");
    buf_putc(&b, 'd');
    buf_put(&b, "ef", 2);
    b.data[b.len] = '\0';
    check(strcmp(b.data, "abcdef") == 0, "buf_puts/putc/put append in order");

    buf_reset(&b);
    check(b.len == 0, "buf_reset empties without freeing");

    /* buf_printf formats through a 512-byte stack buffer and truncates there */
    memset(big, 'x', sizeof big - 1);
    big[sizeof big - 1] = '\0';
    buf_reset(&b);
    buf_printf(&b, "%s", big);
    check(b.len == 511, "buf_printf truncates an over-long format at 511 bytes");

    /* growth across many appends must not lose anything */
    buf_reset(&b);
    {
        int i;
        for (i = 0; i < 5000; i++) buf_putc(&b, 'z');
        check(b.len == 5000, "the buffer grows without dropping bytes");
        check(b.data[4999] == 'z', "the last appended byte survived the growth");
    }
    free(b.data);

    /* mono collapses every tone onto one colour */
    plain = tone_ansi(T_PLAIN);
    check(strcmp(tone_ansi(T_KEYWORD), plain) != 0, "colours differ by default");
    g_mono = 1;
    check(strcmp(tone_ansi(T_KEYWORD), plain) == 0, "mono maps keyword to plain");
    check(strcmp(tone_ansi(T_COMMENT), plain) == 0, "mono maps comment to plain");
    g_mono = 0;
    check(strcmp(tone_ansi(T_KEYWORD), plain) != 0, "colours return when mono is off");
}

/* ------------------------------------------------------------------ *
 * Payload loading
 * ------------------------------------------------------------------ */
static void test_slurp(void)
{
    const char *path = "_test_slurp_tmp.txt";
    const char *body = "hello\nworld\n";
    size_t len = 0;
    char *got;
    FILE *f;

    puts("slurp");

    f = fopen(path, "wb");
    check(f != NULL, "the test fixture file can be created");
    if (f) {
        fwrite(body, 1, strlen(body), f);
        fclose(f);
    }

    got = slurp(path, &len);
    check(got != NULL, "slurp reads an existing file");
    if (got) {
        check(len == strlen(body), "slurp reports the byte count it read");
        check(strcmp(got, body) == 0, "slurp returns the file's bytes");
        check(got[len] == '\0', "slurp NUL-terminates what it returns");
        free(got);
    }
    remove(path);

    len = 12345;
    got = slurp("_no_such_file_here_.txt", &len);
    check(got == NULL, "slurp returns NULL for a missing file");
    free(got);
}

int main(void)
{
    reference_untested();

    test_lexer();
    test_screen();
    test_reveal_wraps();
    test_render();
    test_buf_and_tone();
    test_slurp();

    free(g_tone);

    printf("\n%d checks, %d failure%s\n",
           checks, failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
