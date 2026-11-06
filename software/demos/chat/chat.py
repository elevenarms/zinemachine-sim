#!/usr/bin/env python3
"""chat: a scripted, Claude Code-style conversation for the bar's 60x10 console.

Purely a canned animation (no model, no network) to see how an agent CLI reads on
the 960x320 panel at Terminus 32 px: a prompt box that gets typed into, a spinner,
tool calls with results, replies streamed word by word, transcript scrolling above.
Glyphs are limited to what Terminus has (no U+23FA/U+23BF), so ● and └ stand in.
Loops forever; q or Esc quits.
"""
import os, random, re, select, shutil, sys, termios, time, tty

COLS, ROWS = shutil.get_terminal_size((60, 10))
TRANSCRIPT_ROWS = ROWS - 4          # box (3) + hint line (1)
W = COLS - 2

ORANGE, DIM, TEXT, USER, GREEN, BOLD, RESET = (
    "\033[38;5;173m", "\033[38;5;244m", "\033[38;5;252m", "\033[38;5;250m",
    "\033[38;5;114m", "\033[1m", "\033[0m")
SPIN = ["·", "•", "●", "•"]
VERBS = ["Thinking", "Pondering", "Reticulating", "Soldering", "Routing"]

SCRIPT = [
    ("user", "what's on the bar right now?"),
    ("think", 1.6),
    ("tool", "Bash", "tools/sim/sim status", "running · term · 32px · 60×10"),
    ("say", "This conversation. The bar is 960×320 RGB565; at Terminus "
            "32 px that's 60×10 characters, about 3.7 mm tall."),
    ("user", "will htop's process list fit?"),
    ("think", 2.0),
    ("tool", "Read", "software/demos/eyechart/eyechart.c", "Read the size table"),
    ("say", "Not at 32 px: 10 rows leave htop no room for processes. "
            "Use -s 24 (80×13) for lists, or design for 60×10."),
    ("user", "make the clock purple"),
    ("think", 1.4),
    ("tool", "Update", "software/demos/clock/clock.c", "1 addition, 1 removal (simulated)"),
    ("say", "Done. Rebuild and run: sim sdl clock/bin/clock"),
    ("pause", 4.0),
]

lines = []          # transcript, already wrapped: (text, colour)
box = ""            # what is typed in the prompt box
status = None       # spinner text while working


ANSI = re.compile(r"\033\[[0-9;]*m")


def fit(s, n):
    """Cut s to n visible characters, keeping (not counting) colour codes."""
    out, seen, i = [], 0, 0
    while i < len(s) and seen < n:
        m = ANSI.match(s, i)
        if m:
            out.append(m.group()); i = m.end(); continue
        out.append(s[i]); seen += 1; i += 1
    return "".join(out) + RESET


def wrap(text, first, rest):
    out, cur, prefix = [], "", first
    for word in text.split():
        if len(prefix) + len(cur) + len(word) + (1 if cur else 0) > W:
            out.append(prefix + cur); cur, prefix = word, rest
        else:
            cur = f"{cur} {word}" if cur else word
    out.append(prefix + cur)
    return out


def draw():
    out = ["\033[H"]
    view = lines[-TRANSCRIPT_ROWS:] if status is None else lines[-(TRANSCRIPT_ROWS - 1):]
    for text, col in view:
        out.append(f" {col}{fit(text, W)}\033[K\r\n")
    if status is not None:
        out.append(f" {ORANGE}{status}{RESET}\033[K\r\n")
    shown = len(view) + (status is not None)
    out.append("\033[K\r\n" * (TRANSCRIPT_ROWS - shown))
    inner = COLS - 2
    typed = ("> " + box)[: inner - 2]
    out.append(f"{DIM}╭{'─' * inner}╮{RESET}\r\n")
    cursor = "█" if int(time.time() * 2) % 2 == 0 else " "
    out.append(f"{DIM}│{RESET} {TEXT}{typed}{RESET}{cursor}{' ' * (inner - 2 - len(typed))}{DIM}│{RESET}\r\n")
    out.append(f"{DIM}╰{'─' * inner}╯{RESET}\r\n")
    hint_l, hint_r = "  ? for shortcuts", "simulated · zinemachine0 "
    out.append(f"{DIM}{hint_l}{' ' * (COLS - len(hint_l) - len(hint_r))}{hint_r}{RESET}")
    sys.stdout.write("".join(out)); sys.stdout.flush()


def quit_requested():
    while select.select([sys.stdin], [], [], 0)[0]:
        if sys.stdin.read(1) in ("q", "\x1b"):
            return True
    return False


def sleep(t):
    end = time.time() + t
    while time.time() < end:
        if quit_requested():
            raise KeyboardInterrupt
        draw()
        time.sleep(min(0.05, max(0, end - time.time())))


def run_once():
    global box, status
    for step in SCRIPT:
        kind = step[0]
        if kind == "user":
            sleep(0.8)
            for ch in step[1]:
                box += ch; sleep(random.uniform(0.04, 0.11))
            sleep(0.4)
            lines.append(("", TEXT))
            lines.extend((l, USER) for l in wrap(step[1], "> ", "  "))
            box = ""; draw()
        elif kind == "think":
            verb, t0 = random.choice(VERBS), time.time()
            while time.time() - t0 < step[1]:
                i = int((time.time() - t0) * 8) % len(SPIN)
                secs = int(time.time() - t0)
                status = f"{SPIN[i]} {verb}… ({secs}s · esc to interrupt)"
                sleep(0.12)
            status = None
        elif kind == "tool":
            _, name, arg, result = step
            room = W - len(name) - 4
            if len(arg) > room: arg = arg[: room - 1] + "…"
            lines.append((f"{GREEN}●{RESET} {BOLD}{TEXT}{name}{RESET}{TEXT}({arg})", TEXT))
            sleep(0.7)
            lines.append((f"  └ {result}", DIM))
            sleep(0.5)
        elif kind == "say":
            words = step[1].split(); shown = []
            start = len(lines)
            for wd in words:
                shown.append(wd)
                del lines[start:]
                lines.extend((l, TEXT) for l in wrap(" ".join(shown), "● ", "  "))
                sleep(random.uniform(0.03, 0.09))
        elif kind == "pause":
            sleep(step[1])


def main():
    fd = sys.stdin.fileno()
    old = termios.tcgetattr(fd) if os.isatty(fd) else None
    if old: tty.setcbreak(fd)
    sys.stdout.write("\033[?25l\033[2J")
    try:
        while True:
            lines.clear()
            lines.append((f"{ORANGE}*{RESET} {BOLD}Claude Code{RESET}{DIM} (simulated){RESET}", TEXT))
            lines.append((f"  cwd: /work · {COLS}×{ROWS} · Terminus 32px", DIM))
            run_once()
    except KeyboardInterrupt:
        pass
    finally:
        if old: termios.tcsetattr(fd, termios.TCSADRAIN, old)
        sys.stdout.write(f"{RESET}\033[?25h\033[2J\033[H"); sys.stdout.flush()


if __name__ == "__main__":
    main()
