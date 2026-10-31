/*
 * hackertyper.c -- a terminal homage to hackertyper.net
 *
 * Mash any key and plausible systems code streams out of the terminal,
 * syntax-highlighted, three characters at a time.  Nothing you type is
 * ever interpreted: the keyboard is a crank, the payload is the reel.
 *
 * Build:  cc -std=c99 -O2 -Wall -Wextra -o hackertyper hackertyper.c
 * Keys:   any key reveals code · TAB x3 grants · BKSP x3 denies · ESC exits
 *
 * Public domain / MIT -- do as you like.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#ifdef _WIN32
#  include <windows.h>
#  include <conio.h>
#else
#  include <termios.h>
#  include <unistd.h>
#  include <sys/ioctl.h>
#endif

/* ------------------------------------------------------------------ *
 * The payload: fictional but syntactically honest C, defined at the
 * bottom of this file so the interesting machinery stays up here.
 * ------------------------------------------------------------------ */
static const char *payload_builtin(void);

/* ------------------------------------------------------------------ *
 * Tones -- one colour class per lexical category.
 * ------------------------------------------------------------------ */
typedef enum {
    T_PLAIN = 0,
    T_KEYWORD,
    T_TYPE,
    T_STRING,
    T_COMMENT,
    T_NUMBER,
    T_PREPROC,
    T_PUNCT,
    T_COUNT
} Tone;

static const char *const TONE_ANSI[T_COUNT] = {
    "\033[0;32m",   /* plain    - green        */
    "\033[1;32m",   /* keyword  - bright green */
    "\033[0;36m",   /* type     - cyan         */
    "\033[0;33m",   /* string   - amber        */
    "\033[0;90m",   /* comment  - grey         */
    "\033[0;35m",   /* number   - magenta      */
    "\033[1;36m",   /* preproc  - bright cyan  */
    "\033[2;32m"    /* punct    - dim green    */
};

static int g_mono = 0;

static const char *tone_ansi(Tone t)
{
    return g_mono ? "\033[0;32m" : TONE_ANSI[t];
}

/* ------------------------------------------------------------------ *
 * Lexer -- paints one tone byte per payload byte, once, at startup.
 * ------------------------------------------------------------------ */
static const char *const KEYWORDS[] = {
    "auto", "break", "case", "const", "continue", "default", "do", "else",
    "enum", "extern", "for", "goto", "if", "inline", "register", "restrict",
    "return", "sizeof", "static", "struct", "switch", "typedef", "union",
    "volatile", "while", NULL
};

static const char *const TYPES[] = {
    "bool", "char", "double", "float", "int", "long", "short", "signed",
    "unsigned", "void", "size_t", "ssize_t", "uint8_t", "uint16_t",
    "uint32_t", "uint64_t", "int8_t", "int16_t", "int32_t", "int64_t",
    "spinlock_t", "atomic32_t", NULL
};

static int is_word_byte(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9') || c == '_';
}

static int in_table(const char *const *table, const char *word, size_t len)
{
    size_t i;
    for (i = 0; table[i]; i++)
        if (strlen(table[i]) == len && memcmp(table[i], word, len) == 0)
            return 1;
    return 0;
}

static int at_line_start(const char *s, size_t i)
{
    while (i > 0) {
        char c = s[--i];
        if (c == '\n') return 1;
        if (c != ' ' && c != '\t') return 0;
    }
    return 1;
}

static void paint(const char *s, size_t n, unsigned char *tone)
{
    size_t i = 0;

    while (i < n) {
        char c = s[i];

        /* preprocessor directive -- to end of line */
        if (c == '#' && at_line_start(s, i)) {
            while (i < n && s[i] != '\n') tone[i++] = T_PREPROC;
            continue;
        }

        /* block comment */
        if (c == '/' && i + 1 < n && s[i + 1] == '*') {
            tone[i++] = T_COMMENT;
            tone[i++] = T_COMMENT;
            while (i < n) {
                int closing = (s[i] == '*' && i + 1 < n && s[i + 1] == '/');
                tone[i++] = T_COMMENT;
                if (closing) { tone[i++] = T_COMMENT; break; }
            }
            continue;
        }

        /* line comment */
        if (c == '/' && i + 1 < n && s[i + 1] == '/') {
            while (i < n && s[i] != '\n') tone[i++] = T_COMMENT;
            continue;
        }

        /* string or character literal */
        if (c == '"' || c == '\'') {
            char quote = c;
            tone[i++] = T_STRING;
            while (i < n && s[i] != quote && s[i] != '\n') {
                if (s[i] == '\\' && i + 1 < n) tone[i++] = T_STRING;
                tone[i++] = T_STRING;
            }
            if (i < n && s[i] == quote) tone[i++] = T_STRING;
            continue;
        }

        /* number */
        if (c >= '0' && c <= '9') {
            while (i < n && (is_word_byte(s[i]) || s[i] == '.'))
                tone[i++] = T_NUMBER;
            continue;
        }

        /* identifier -- keyword, type, or plain */
        if (is_word_byte(c)) {
            size_t start = i;
            Tone t;
            while (i < n && is_word_byte(s[i])) i++;
            if (in_table(KEYWORDS, s + start, i - start))      t = T_KEYWORD;
            else if (in_table(TYPES, s + start, i - start))     t = T_TYPE;
            else                                               t = T_PLAIN;
            memset(tone + start, (int)t, i - start);
            continue;
        }

        /* punctuation and whitespace */
        tone[i] = (c == ' ' || c == '\t' || c == '\n') ? T_PLAIN : T_PUNCT;
        i++;
    }
}

/* ------------------------------------------------------------------ *
 * Growable output buffer -- one write() per frame, no flicker.
 * ------------------------------------------------------------------ */
typedef struct {
    char   *data;
    size_t  len;
    size_t  cap;
} Buf;

static void buf_need(Buf *b, size_t extra)
{
    if (b->len + extra + 1 <= b->cap) return;
    while (b->cap < b->len + extra + 1)
        b->cap = b->cap ? b->cap * 2 : 8192;
    b->data = (char *)realloc(b->data, b->cap);
    if (!b->data) { perror("realloc"); exit(EXIT_FAILURE); }
}

static void buf_put(Buf *b, const char *s, size_t n)
{
    buf_need(b, n);
    memcpy(b->data + b->len, s, n);
    b->len += n;
}

static void buf_puts(Buf *b, const char *s) { buf_put(b, s, strlen(s)); }
static void buf_putc(Buf *b, char c)        { buf_put(b, &c, 1); }

static void buf_printf(Buf *b, const char *fmt, ...)
{
    char tmp[512];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);

    if (n > 0) buf_put(b, tmp, (size_t)n < sizeof tmp ? (size_t)n : sizeof tmp - 1);
}

static void buf_reset(Buf *b) { b->len = 0; }

/* ------------------------------------------------------------------ *
 * Platform layer -- raw keyboard, terminal size, VT escapes.
 * ------------------------------------------------------------------ */
#define KEY_CTRL_C   3
#define KEY_CTRL_Q  17
#define KEY_TAB      9
#define KEY_ESC     27
#define KEY_DEL    127

/*
 * The platform contract. Exactly these four functions stand between the
 * program and the host terminal; everything above this line is portable.
 *
 * Declared once here and defined twice below -- once per backend -- so the
 * two implementations are checked against one signature instead of each
 * declaring its own. A backend that changes a parameter list, or drops a
 * function, now fails to build on its own platform: the compiler rejects the
 * conflicting definition, or reports a static function used but never defined
 * (MSVC C2129, GCC/Clang -Wundefined-internal). Without this block each
 * definition was self-declaring and nothing cross-checked them, so a drifting
 * backend stayed silent until somebody compiled on the other platform.
 */
static void term_enter(void);
static void term_leave(void);
static void term_size(int *w, int *h);
static int  term_key(void);

#ifdef _WIN32

static DWORD  g_saved_out_mode;
static UINT   g_saved_out_cp;

static void term_enter(void)
{
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;

    g_saved_out_cp = GetConsoleOutputCP();
    SetConsoleOutputCP(CP_UTF8);

    if (GetConsoleMode(out, &mode)) {
        g_saved_out_mode = mode;
        SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
}

static void term_leave(void)
{
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (g_saved_out_mode) SetConsoleMode(out, g_saved_out_mode);
    if (g_saved_out_cp)   SetConsoleOutputCP(g_saved_out_cp);
}

static void term_size(int *w, int *h)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        *w = csbi.srWindow.Right  - csbi.srWindow.Left + 1;
        *h = csbi.srWindow.Bottom - csbi.srWindow.Top  + 1;
    } else {
        *w = 80; *h = 25;
    }
}

static int term_key(void)
{
    int c = _getch();
    /* function and arrow keys arrive as a two-byte prefixed sequence */
    if (c == 0 || c == 0xE0) { _getch(); return 0x100; }
    return c;
}

#else /* POSIX */

static struct termios g_saved_tios;
static int            g_tios_valid = 0;

static void term_enter(void)
{
    struct termios raw;

    if (tcgetattr(STDIN_FILENO, &g_saved_tios) != 0) return;
    g_tios_valid = 1;

    raw = g_saved_tios;
    raw.c_lflag &= (tcflag_t)~(ECHO | ICANON | ISIG);
    raw.c_iflag &= (tcflag_t)~(IXON | ICRNL);
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

static void term_leave(void)
{
    if (g_tios_valid) tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_saved_tios);
}

static void term_size(int *w, int *h)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        *w = ws.ws_col;
        *h = ws.ws_row;
    } else {
        *w = 80; *h = 25;
    }
}

static int term_key(void)
{
    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) != 1) return KEY_CTRL_C;
    if (c == KEY_ESC) {
        /* drain a possible escape sequence so arrows do not type three chars */
        struct termios t, saved;
        unsigned char junk;
        if (tcgetattr(STDIN_FILENO, &saved) == 0) {
            t = saved;
            t.c_cc[VMIN]  = 0;
            t.c_cc[VTIME] = 1;      /* 100 ms */
            tcsetattr(STDIN_FILENO, TCSANOW, &t);
            if (read(STDIN_FILENO, &junk, 1) == 1) {
                while (read(STDIN_FILENO, &junk, 1) == 1) { /* discard */ }
                tcsetattr(STDIN_FILENO, TCSANOW, &saved);
                return 0x100;       /* it was an arrow or function key */
            }
            tcsetattr(STDIN_FILENO, TCSANOW, &saved);
        }
    }
    return (int)c;
}

#endif

/* ------------------------------------------------------------------ *
 * Screen -- a cell grid the payload is typed into, with scrolling.
 * ------------------------------------------------------------------ */
typedef struct {
    char          ch;
    unsigned char tone;
} Cell;

typedef struct {
    int   w, h;
    int   cx, cy;
    Cell *cell;
} Screen;

static void screen_alloc(Screen *s, int w, int h)
{
    s->w = w > 1 ? w : 1;
    s->h = h > 1 ? h : 1;
    s->cx = s->cy = 0;
    s->cell = (Cell *)calloc((size_t)s->w * (size_t)s->h, sizeof *s->cell);
    if (!s->cell) { perror("calloc"); exit(EXIT_FAILURE); }
}

static void screen_free(Screen *s) { free(s->cell); s->cell = NULL; }

static void screen_scroll(Screen *s)
{
    size_t row = (size_t)s->w * sizeof *s->cell;
    memmove(s->cell, s->cell + s->w, row * (size_t)(s->h - 1));
    memset(s->cell + (size_t)s->w * (size_t)(s->h - 1), 0, row);
}

static void screen_newline(Screen *s)
{
    s->cx = 0;
    if (++s->cy >= s->h) { screen_scroll(s); s->cy = s->h - 1; }
}

static void screen_putc(Screen *s, char ch, unsigned char tone)
{
    Cell *c;

    if (ch == '\r') return;
    if (ch == '\n') { screen_newline(s); return; }
    if (ch == '\t') {
        int pad = 4 - (s->cx % 4);
        while (pad--) screen_putc(s, ' ', T_PLAIN);
        return;
    }

    if (s->cx >= s->w) screen_newline(s);

    c = &s->cell[(size_t)s->cy * (size_t)s->w + (size_t)s->cx];
    c->ch   = ch;
    c->tone = tone;
    s->cx++;
}

static void screen_print(Screen *s, const char *text, unsigned char tone)
{
    while (*text) screen_putc(s, *text++, tone);
}

/* ------------------------------------------------------------------ *
 * Overlay -- the ACCESS GRANTED / ACCESS DENIED placard.
 * ------------------------------------------------------------------ */
typedef enum { OV_NONE = 0, OV_GRANTED, OV_DENIED } Overlay;

#define BOX_W 46
#define BOX_H  7

static void draw_overlay(Buf *b, Overlay ov, int termw, int termh)
{
    const char *title = (ov == OV_GRANTED) ? "ACCESS GRANTED" : "ACCESS DENIED";
    const char *note  = (ov == OV_GRANTED) ? "root shell attached -- lane 0x00"
                                           : "biometric mismatch -- lockout 30s";
    const char *frame = (ov == OV_GRANTED) ? "\033[1;32m" : "\033[1;31m";
    const char *fill  = (ov == OV_GRANTED) ? "\033[1;30;42m" : "\033[1;37;41m";
    int x = (termw - BOX_W) / 2 + 1;
    int y = (termh - BOX_H) / 2 + 1;
    int inner = BOX_W - 2;
    int row, pad;

    if (x < 1 || y < 1 || termw < BOX_W + 2 || termh < BOX_H + 2) return;

    for (row = 0; row < BOX_H; row++) {
        buf_printf(b, "\033[%d;%dH%s", y + row, x, frame);

        if (row == 0 || row == BOX_H - 1) {
            int i;
            buf_putc(b, '+');
            for (i = 0; i < inner; i++) buf_putc(b, '=');
            buf_putc(b, '+');
        } else if (row == 3) {
            pad = (inner - (int)strlen(title)) / 2;
            buf_printf(b, "|%s%*s%s%*s\033[0m%s|", fill, pad, "", title,
                       inner - pad - (int)strlen(title), "", frame);
        } else if (row == 5) {
            pad = (inner - (int)strlen(note)) / 2;
            buf_printf(b, "|%*s%s%*s|", pad, "", note,
                       inner - pad - (int)strlen(note), "");
        } else {
            buf_printf(b, "|%*s|", inner, "");
        }
        buf_puts(b, "\033[0m");
    }
}

/* ------------------------------------------------------------------ *
 * Frame composition.
 * ------------------------------------------------------------------ */
static void render(Buf *b, const Screen *s, Overlay ov,
                   long keys, size_t pos, size_t total, int termw, int termh)
{
    char status[512];
    int pct = total ? (int)((pos * 100) / total) : 0;
    int x, y, n;

    buf_reset(b);
    buf_puts(b, "\033[H");

    for (y = 0; y < s->h; y++) {
        int cur = -1;
        for (x = 0; x < s->w; x++) {
            const Cell *c = &s->cell[(size_t)y * (size_t)s->w + (size_t)x];
            char ch = c->ch ? c->ch : ' ';

            if (x == s->cx && y == s->cy) {       /* block cursor */
                buf_puts(b, "\033[7;32m \033[0m");
                cur = -1;
                continue;
            }
            if (c->tone != (unsigned char)cur) {
                buf_puts(b, tone_ansi((Tone)c->tone));
                cur = c->tone;
            }
            buf_putc(b, ch);
        }
        buf_puts(b, "\033[0m\033[K");
        if (y < s->h - 1) buf_puts(b, "\r\n");
    }

    n = snprintf(status, sizeof status,
                 " HACKERTYPE  keys %ld  payload %lu/%lu (%d%%)  "
                 "TAB x3 grant . BKSP x3 deny . ESC exit",
                 keys, (unsigned long)pos, (unsigned long)total, pct);
    if (n < 0) n = 0;
    if (n > termw) { status[termw] = '\0'; n = termw; }

    buf_printf(b, "\033[%d;1H\033[7;32m%s%*s\033[0m", termh, status,
               termw - n, "");

    if (ov != OV_NONE) draw_overlay(b, ov, termw, termh);
}

/* ------------------------------------------------------------------ *
 * Teardown that survives signals and exit().
 * ------------------------------------------------------------------ */
static volatile sig_atomic_t g_interrupted = 0;

/*
 * Registered with atexit() *and* called on the normal path, so it has to be
 * idempotent -- otherwise the atexit run would clear the farewell line that
 * main() prints after it.
 */
static void restore_terminal(void)
{
    static int done = 0;

    if (done) return;
    done = 1;

    fputs("\033[0m\033[?25h\033[2J\033[H", stdout);
    fflush(stdout);
    term_leave();
}

static void on_signal(int sig)
{
    (void)sig;
    g_interrupted = 1;
}

/* ------------------------------------------------------------------ *
 * Payload loading.
 * ------------------------------------------------------------------ */
static char *slurp(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    char *data;
    long size;

    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    size = ftell(f);
    if (size < 0) { fclose(f); return NULL; }
    rewind(f);

    data = (char *)malloc((size_t)size + 1);
    if (!data) { fclose(f); return NULL; }

    *out_len = fread(data, 1, (size_t)size, f);
    data[*out_len] = '\0';
    fclose(f);
    return data;
}

static void usage(const char *argv0)
{
    printf("usage: %s [options]\n\n", argv0);
    puts("  -f, --file PATH    type out PATH instead of the built-in payload");
    puts("  -s, --speed N      characters revealed per keystroke (default 3)");
    puts("  -m, --mono         single-colour output, no syntax highlighting");
    puts("  -h, --help         this text");
    puts("");
    puts("Any key types. TAB three times grants access, BACKSPACE three times");
    puts("denies it, ESC or Ctrl-C leaves. Nothing you type is executed.");
}

/* ------------------------------------------------------------------ *
 * main
 * ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
    const char *path = NULL;
    char *loaded = NULL;
    const char *source;
    size_t total, pos = 0;
    unsigned char *tone;
    Screen scr;
    Buf out = {0};
    Overlay ov = OV_NONE;
    int step = 3, termw, termh, i;
    int tab_run = 0, del_run = 0;
    long keys = 0;
    int running = 1;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if ((!strcmp(a, "-f") || !strcmp(a, "--file")) && i + 1 < argc) {
            path = argv[++i];
        } else if ((!strcmp(a, "-s") || !strcmp(a, "--speed")) && i + 1 < argc) {
            step = atoi(argv[++i]);
            if (step < 1)  step = 1;
            if (step > 64) step = 64;
        } else if (!strcmp(a, "-m") || !strcmp(a, "--mono")) {
            g_mono = 1;
        } else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "%s: unknown option '%s' (try --help)\n", argv[0], a);
            return 2;
        }
    }

    if (path) {
        loaded = slurp(path, &total);
        if (!loaded) {
            fprintf(stderr, "%s: cannot read '%s'\n", argv[0], path);
            return 1;
        }
        source = loaded;
    } else {
        source = payload_builtin();
        total = strlen(source);
    }

    if (total == 0) {
        fprintf(stderr, "%s: payload is empty\n", argv[0]);
        free(loaded);
        return 1;
    }

    tone = (unsigned char *)calloc(total, 1);
    if (!tone) { perror("calloc"); free(loaded); return 1; }
    paint(source, total, tone);

    signal(SIGINT, on_signal);
#ifdef SIGTERM
    signal(SIGTERM, on_signal);
#endif

    term_enter();
    atexit(restore_terminal);
    fputs("\033[2J\033[?25l", stdout);

    term_size(&termw, &termh);
    screen_alloc(&scr, termw, termh - 1);

    screen_print(&scr, "mesh-fabric console 4.2.1  (tty0, 10GbE, lane 0x00)\n",
                 T_COMMENT);
    screen_print(&scr, "link up. operator not authenticated.\n", T_COMMENT);
    screen_print(&scr, "start typing.\n\n", T_KEYWORD);

    while (running && !g_interrupted) {
        int w, h, key;

        term_size(&w, &h);
        if (w != termw || h != termh) {
            termw = w; termh = h;
            screen_free(&scr);
            screen_alloc(&scr, termw, termh - 1);
            fputs("\033[2J", stdout);
        }

        render(&out, &scr, ov, keys, pos, total, termw, termh);
        fwrite(out.data, 1, out.len, stdout);
        fflush(stdout);

        key = term_key();
        if (g_interrupted) break;

        switch (key) {
        case KEY_ESC:
        case KEY_CTRL_C:
        case KEY_CTRL_Q:
            running = 0;
            continue;

        case KEY_TAB:
            del_run = 0;
            if (++tab_run >= 3) { ov = OV_GRANTED; tab_run = 0; }
            else                { ov = OV_NONE; }
            keys++;
            continue;

        case '\b':
        case KEY_DEL:
            tab_run = 0;
            if (++del_run >= 3) { ov = OV_DENIED; del_run = 0; }
            else                { ov = OV_NONE; }
            keys++;
            continue;

        case 0x100:             /* arrow or function key -- ignore */
            continue;

        default:
            tab_run = del_run = 0;
            ov = OV_NONE;
            break;
        }

        keys++;
        for (i = 0; i < step; i++) {
            if (pos >= total) pos = 0;           /* loop the reel */
            screen_putc(&scr, source[pos], tone[pos]);
            pos++;
        }
    }

    screen_free(&scr);
    free(out.data);
    free(tone);
    free(loaded);

    restore_terminal();
    printf("session closed. %ld keystrokes, %lu bytes revealed.\n",
           keys, (unsigned long)pos);
    return 0;
}

/* ------------------------------------------------------------------ *
 * The reel. Entirely fictional: no such fabric, no such API.
 * ------------------------------------------------------------------ */
static const char *payload_builtin(void)
{
    static const char text[] =
"/*\n"
" *  fabric/quantum_lane.c - deterministic lane scheduler for the mesh fabric\n"
" *\n"
" *  Credit-based admission with sibling hand-off. A lane that runs below\n"
" *  the credit floor lends its burst to its pair rather than stalling the\n"
" *  whole fabric for an epoch.\n"
" */\n"
"\n"
"#include <fabric/lane.h>\n"
"#include <fabric/qpacket.h>\n"
"#include <fabric/atomic.h>\n"
"#include <fabric/trace.h>\n"
"\n"
"#define LANE_RING_ORDER     12\n"
"#define LANE_RING_SIZE      (1u << LANE_RING_ORDER)\n"
"#define LANE_RING_MASK      (LANE_RING_SIZE - 1u)\n"
"#define LANE_CREDIT_FLOOR   48\n"
"#define LANE_CREDIT_CEILING 65536\n"
"#define LANE_MAX_BURST      7\n"
"\n"
"enum lane_state {\n"
"        LANE_IDLE = 0,\n"
"        LANE_PRIMED,\n"
"        LANE_DRAINING,\n"
"        LANE_QUIESCED,\n"
"};\n"
"\n"
"struct lane_ring {\n"
"        struct qpacket  *slot[LANE_RING_SIZE];\n"
"        uint32_t         head;\n"
"        uint32_t         tail;\n"
"        atomic32_t       inflight;\n"
"};\n"
"\n"
"struct fabric_lane {\n"
"        struct lane_ring         ring;\n"
"        enum lane_state          state;\n"
"        uint64_t                 epoch;\n"
"        int32_t                  credit;\n"
"        uint16_t                 lane_id;\n"
"        spinlock_t               guard;\n"
"        struct lane_stats        stats;\n"
"        struct fabric_lane      *sibling;\n"
"};\n"
"\n"
"static inline uint32_t ring_used(const struct lane_ring *r)\n"
"{\n"
"        return (r->head - r->tail) & LANE_RING_MASK;\n"
"}\n"
"\n"
"static inline int ring_full(const struct lane_ring *r)\n"
"{\n"
"        return ring_used(r) == LANE_RING_MASK;\n"
"}\n"
"\n"
"static void lane_account(struct fabric_lane *lane, const struct qpacket *pkt)\n"
"{\n"
"        lane->stats.bytes += pkt->len;\n"
"        lane->stats.packets++;\n"
"\n"
"        if (pkt->flags & QPKT_URGENT)\n"
"                lane->stats.urgent++;\n"
"}\n"
"\n"
"static int lane_push(struct fabric_lane *lane, struct qpacket *pkt)\n"
"{\n"
"        struct lane_ring *r = &lane->ring;\n"
"\n"
"        if (ring_full(r)) {\n"
"                trace_lane_drop(lane->lane_id, pkt->seq, \"ring full\");\n"
"                return -ENOBUFS;\n"
"        }\n"
"\n"
"        r->slot[r->head & LANE_RING_MASK] = pkt;\n"
"        barrier_store_release();\n"
"        r->head++;\n"
"\n"
"        atomic32_inc(&r->inflight);\n"
"        lane_account(lane, pkt);\n"
"\n"
"        return 0;\n"
"}\n"
"\n"
"static struct qpacket *lane_pop(struct fabric_lane *lane)\n"
"{\n"
"        struct lane_ring *r = &lane->ring;\n"
"        struct qpacket *pkt;\n"
"\n"
"        if (r->tail == r->head)\n"
"                return NULL;\n"
"\n"
"        pkt = r->slot[r->tail & LANE_RING_MASK];\n"
"        r->slot[r->tail & LANE_RING_MASK] = NULL;\n"
"        barrier_load_acquire();\n"
"        r->tail++;\n"
"\n"
"        atomic32_dec(&r->inflight);\n"
"        return pkt;\n"
"}\n"
"\n"
"/*\n"
" * fabric_admit() - decide whether a packet may enter the lane.\n"
" *\n"
" * Admission is credit based: every epoch refills the lane with the weight\n"
" * it negotiated at bring-up. Returns 0, or a negative errno.\n"
" */\n"
"int fabric_admit(struct fabric_lane *lane, struct qpacket *pkt)\n"
"{\n"
"        unsigned long flags;\n"
"        int err;\n"
"\n"
"        if (!lane || !pkt)\n"
"                return -EINVAL;\n"
"\n"
"        spin_lock_irqsave(&lane->guard, flags);\n"
"\n"
"        if (lane->state == LANE_QUIESCED) {\n"
"                err = -ESHUTDOWN;\n"
"                goto out;\n"
"        }\n"
"\n"
"        if (lane->credit < LANE_CREDIT_FLOOR && lane->sibling) {\n"
"                trace_lane_handoff(lane->lane_id, lane->sibling->lane_id);\n"
"                err = lane_push(lane->sibling, pkt);\n"
"                goto out;\n"
"        }\n"
"\n"
"        err = lane_push(lane, pkt);\n"
"        if (!err) {\n"
"                lane->credit -= (int32_t)pkt->len;\n"
"                lane->state = LANE_PRIMED;\n"
"        }\n"
"\n"
"out:\n"
"        spin_unlock_irqrestore(&lane->guard, flags);\n"
"        return err;\n"
"}\n"
"\n"
"/*\n"
" * fabric_drain() - move up to LANE_MAX_BURST packets onto the wire.\n"
" */\n"
"int fabric_drain(struct fabric_lane *lane, struct wire_queue *wire)\n"
"{\n"
"        struct qpacket *pkt;\n"
"        int sent = 0;\n"
"\n"
"        while (sent < LANE_MAX_BURST) {\n"
"                pkt = lane_pop(lane);\n"
"                if (!pkt)\n"
"                        break;\n"
"\n"
"                if (wire_submit(wire, pkt) < 0) {\n"
"                        qpacket_release(pkt);\n"
"                        return -EIO;\n"
"                }\n"
"\n"
"                sent++;\n"
"        }\n"
"\n"
"        if (!ring_used(&lane->ring))\n"
"                lane->state = LANE_IDLE;\n"
"\n"
"        trace_lane_drain(lane->lane_id, sent, lane->credit);\n"
"        return sent;\n"
"}\n"
"\n"
"void fabric_epoch_tick(struct fabric_lane *lanes, size_t count, uint64_t epoch)\n"
"{\n"
"        size_t i;\n"
"\n"
"        for (i = 0; i < count; i++) {\n"
"                struct fabric_lane *lane = &lanes[i];\n"
"\n"
"                lane->epoch = epoch;\n"
"                lane->credit = clamp_i32(lane->credit + lane->stats.weight,\n"
"                                         0, LANE_CREDIT_CEILING);\n"
"\n"
"                if (lane->state == LANE_DRAINING && !ring_used(&lane->ring))\n"
"                        lane->state = LANE_IDLE;\n"
"        }\n"
"}\n"
"\n"
"/*\n"
" * fabric_lane_quiesce() - stop admitting, let the ring run dry, park.\n"
" */\n"
"int fabric_lane_quiesce(struct fabric_lane *lane, unsigned int timeout_ms)\n"
"{\n"
"        unsigned int waited = 0;\n"
"\n"
"        lane->state = LANE_DRAINING;\n"
"\n"
"        while (ring_used(&lane->ring) && waited < timeout_ms) {\n"
"                fabric_yield(1);\n"
"                waited++;\n"
"        }\n"
"\n"
"        if (ring_used(&lane->ring)) {\n"
"                trace_lane_timeout(lane->lane_id, waited);\n"
"                return -ETIMEDOUT;\n"
"        }\n"
"\n"
"        lane->state = LANE_QUIESCED;\n"
"        return 0;\n"
"}\n";

    return text;
}
