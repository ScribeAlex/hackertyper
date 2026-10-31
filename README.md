# hackertyper

A terminal homage to hackertyper.net, in one C file with no dependencies.

Mash any key and plausible systems code streams out, syntax-highlighted, three
characters at a time. Nothing you type is ever interpreted — the keyboard is a
crank, the payload is the reel.

## Build

POSIX (Linux, macOS, BSD):

```bash
make
```

Windows with MSVC, from a Developer Command Prompt:

```bat
cl /nologo /W4 /O2 /D_CRT_SECURE_NO_WARNINGS hackertyper.c /Fe:hackertyper.exe
```

Windows with MinGW:

```bash
gcc -std=c99 -O2 -Wall -Wextra -o hackertyper.exe hackertyper.c
```

## Visual Studio

`hackertyper.sln` opens in Visual Studio 2019 or newer. Two projects:

- **hackertyper** — the program. It is listed first, so it is the startup
  project and `F5` builds and runs it.
- **tests** — the 63 checks. Right-click it, then Debug ▸ Start New Instance.

Both are x64, Debug and Release, compiled at `/W4` as C. The platform toolset is
`$(DefaultPlatformToolset)` rather than a pinned `v143` or `v180`, so the
solution opens on whichever Visual Studio is installed without the retarget
prompt. Leaving the property out entirely is not the same thing: it falls back
to `v100` and fails with `MSB8020`. Binaries land in `build\<Configuration>\`.

`tests.vcxproj` lists `hackertyper.c` as a `ClInclude`, not a `ClCompile`. The
test file `#include`s the source, so compiling it again as its own unit would
collide on every `static` at link time. MSBuild still reads the `#include`, so
editing `hackertyper.c` rebuilds the tests.

One caveat with nothing to do with the code: MSVC's build tracker writes `.tlog`
files under `build\obj\`, and those paths still hit the old 260-character limit.
Keep the project somewhere short — `C:\Users\you\hackertyper`, not ten folders
deep — or the build dies with `MSB6003 ... PathTooLongException`.

### VS Code

`.vscode/tasks.json` and `.vscode/launch.json` are set up for the Microsoft C/C++
extension. Start VS Code from a Developer Command Prompt so `cl.exe` is on PATH:

```bat
code .
```

Then `F5`. The launch configurations run the program in an **external** terminal,
and that is not cosmetic — the program puts the console into raw mode and draws
with ANSI escapes, neither of which VS Code's integrated debug console does. The
test configuration uses the integrated terminal, since it only prints.

## Run

```bash
./hackertyper
```

| Key | What happens |
| --- | --- |
| any printable key | reveals the next 3 characters of the payload |
| `TAB` three times | `ACCESS GRANTED` placard |
| `BACKSPACE` three times | `ACCESS DENIED` placard |
| any other key | dismisses the placard |
| `ESC` or `Ctrl-C` | restores the terminal and exits |

The real site uses triple `CAPS LOCK` and triple `ALT`. A terminal can't see bare
modifier keys, so `TAB` and `BACKSPACE` stand in for them.

## Options

```
-f, --file PATH    type out PATH instead of the built-in payload
-s, --speed N      characters revealed per keystroke (1-64, default 3)
-m, --mono         single-colour output, no syntax highlighting
-h, --help         usage
```

Type out this program's own source, four characters at a time:

```bash
./hackertyper --file hackertyper.c --speed 4
```

Each option reaches exactly one place in the program.

`--file` sends `slurp` to read PATH in place of `payload_builtin`. The lexer then
paints whatever came back the same way it paints the built-in reel, so a C file
arrives highlighted and a plain text file arrives green.

`--speed` sets the reveal count — the loop bound that decides how many
characters `screen_putc` receives per keystroke. It is clamped to 1-64, so a
fat-fingered `--speed 5000` degrades to a very fast crank rather than a buffer
overrun.

`--mono` sets one flag, which only `tone_ansi` reads. The lexer still runs and
the tone array is still built; the colours are computed and then thrown away.
That is why mono costs nothing to switch on, and why it collapses every colour
run in the frame into a single run.

`--help` prints the usage text and returns before `term_enter`, so it is the one
path through the program that never touches the terminal — no raw mode, no
hidden cursor, nothing to restore.

## Tests

```bash
make test
```

With MSVC, from a Developer Command Prompt:

```bat
cl /nologo /W4 /D_CRT_SECURE_NO_WARNINGS test_hackertyper.c /Fe:test_hackertyper.exe
```

Everything in `hackertyper.c` is `static` in one translation unit, so
`test_hackertyper.c` includes the source directly and renames `main` out of the
way with a `#define`. That keeps the shipped file untouched — there is no test
hook in it, and no symbol was made external just to be reachable. The suite
never calls `term_enter`, so running it does not put your console into raw mode.

63 checks over the parts that are worth pinning down:

- **The lexer**, category by category: preprocessor lines, block and line
  comments, string and character literals with escaped quotes, numbers with
  decimal points and hex suffixes. Plus the two cases that are easy to get
  wrong — a keyword must not match inside a longer identifier (`return_value`
  is not `return`), and an unterminated string must stop at the newline rather
  than painting the rest of the file.
- **The screen grid**: wrapping at the right edge, tab expansion to the next
  multiple of four from any column, newline handling, `memmove` scroll-up with
  the vacated row actually cleared, `\r` dropped so a CRLF payload does not
  double-space, and a degenerate 1x1 screen that must not corrupt memory.
- **The reveal loop**: starting two bytes from the end of the payload and
  revealing five must wrap to the top, not read past the end.
- **Frame composition**: one frame holds no NUL bytes and nothing but printable
  ASCII or ESC, homes the cursor first, and reports the counters it was handed.
  Placards draw only when active. On a terminal too small for the box the
  placard is skipped rather than scribbled outside it, and the status line is
  cut at the terminal width rather than padded by a negative count.
- **The buffer**: append order, `buf_reset` emptying without freeing, growth
  across 5000 appends without dropping a byte, and the documented truncation of
  an over-long `buf_printf` at 511 bytes.
- **`--mono`**, which must collapse every tone onto the plain colour and restore
  the palette when switched off.
- **`slurp`**: byte count, contents, NUL termination, and `NULL` for a file that
  is not there.

What the suite does not cover: the keyboard itself. `term_key` reads the console
directly, so the key-to-reveal path, the TAB and BACKSPACE streak counters, and
the resize branch are only exercised by running the program and typing.

## How it works

Four pieces, in the order they appear in the file:

**The lexer** (`paint`) walks the payload once at startup and writes one tone
byte per source byte into a parallel array — keyword, type, string, comment,
number, preprocessor, punctuation. Highlighting is therefore free at render
time: a byte already knows its own colour. The classifier handles block and
line comments, string and character literals with escapes, numbers, and two
keyword tables.

**The screen** is a `Cell` grid (`char` + tone) with a cursor and a scroll.
`screen_putc` is the only way text enters it, and it owns wrapping, tab
expansion, newlines, and `memmove`-based scroll-up. The grid is one row shorter
than the terminal; the last row belongs to the status bar.

**The frame** is composed into a single growable `Buf` and written with one
`fwrite` per keystroke — cursor home, then every row with colour changes
coalesced into runs, then the status bar, then the placard if one is up.
One write means no flicker and no partial frames.

**The platform layer** is two implementations of the same four functions:
`term_enter`, `term_leave`, `term_size`, `term_key`. On Windows that's
`ENABLE_VIRTUAL_TERMINAL_PROCESSING`, UTF-8 output, and `_getch`. On POSIX it's
`termios` with `ECHO`/`ICANON`/`ISIG` cleared, `TIOCGWINSZ`, and a timed read
that distinguishes a bare `ESC` from the start of an arrow-key sequence.

Those four signatures are declared once, in a contract block above the
`#ifdef`, and defined twice below it. That single declaration is what keeps the
two backends honest: each is checked against one signature instead of declaring
its own, so a backend that drops a function or changes a parameter list fails to
build on its own platform rather than staying silent until somebody compiles on
the other one. Verified both ways on MSVC — a dropped function gives
`error C2129: static function declared but not defined` pointing at the contract
block, and a changed parameter list gives `warning C4029` at the definition plus
`error C2198` at every call site. GCC and Clang report the same two cases as
`-Wundefined-internal` and a conflicting-types error.

Terminal state is restored through an idempotent `restore_terminal` registered
with `atexit`, so a signal, an `exit()`, or the normal path all leave the
terminal usable — and the farewell line survives the second call.

Resizing is handled at the top of the loop: if the terminal changed size the
grid is reallocated and cleared, and typing continues from the same payload
offset.

## The payload

The built-in reel is a fictional `fabric/quantum_lane.c` — a credit-based packet
lane scheduler with sibling hand-off. It is syntactically honest C and
semantically invented: no such fabric exists, no such API, nothing to look up.
Swap it for anything you like with `--file`.

## Licence

MIT.
