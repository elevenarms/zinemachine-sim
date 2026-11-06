#!/usr/bin/env python3
"""ZineWrite: a pretend word processor that types Shakespeare by itself.

Runs in a 50x10 xterm (Terminus 32 px, the left 800 px of the bar). After every
keystroke it moves the X pointer onto the text cursor with xdotool, so the xeyes
beside it (run.sh) watch the words being typed. Occasional typos are noticed and
backspaced. Public-domain texts; loops. q quits.
"""
import math, os, random, select, shutil, subprocess, sys, termios, time, tty

COLS, ROWS = shutil.get_terminal_size((50, 10))
PAGE = ROWS - 3                     # title, ruler, ..., status
TEXTW = COLS - 3                    # 1 col margin each side
CW, CH = 16, 32                     # Terminus 32 px cell
EYES = (880, 62)                    # between the two eyes (run.sh: 150x116+805+4)

INV, DIM, RED, BOLD, RESET = "\033[7m", "\033[38;5;245m", "\033[38;5;124m", "\033[1m", "\033[0m"

PIECES = [
    ("sonnet18.txt", """Sonnet 18
Shall I compare thee to a summer's day?
Thou art more lovely and more temperate:
Rough winds do shake the darling buds of May,
And summer's lease hath all too short a date;
Sometime too hot the eye of heaven shines,
And often is his gold complexion dimm'd;
And every fair from fair sometime declines,
By chance or nature's changing course untrimm'd;
But thy eternal summer shall not fade,
Nor lose possession of that fair thou ow'st;
Nor shall Death brag thou wander'st in his shade,
When in eternal lines to time thou grow'st:
So long as men can breathe or eyes can see,
So long lives this, and this gives life to thee."""),
    ("hamlet.txt", """Hamlet, III.i
To be, or not to be, that is the question:
Whether 'tis nobler in the mind to suffer
The slings and arrows of outrageous fortune,
Or to take arms against a sea of troubles
And by opposing end them. To die: to sleep;
No more; and by a sleep to say we end
The heart-ache and the thousand natural shocks
That flesh is heir to."""),
    ("macbeth.txt", """Macbeth, V.v
Tomorrow, and tomorrow, and tomorrow,
Creeps in this petty pace from day to day,
To the last syllable of recorded time;
And all our yesterdays have lighted fools
The way to dusty death. Out, out, brief candle!
Life's but a walking shadow, a poor player,
That struts and frets his hour upon the stage,
And then is heard no more."""),
]

NEAR = {c: n for c, n in zip("qwertyuiopasdfghjklzxcvbnm", "wqrtyuiopoasdfghjkllzxcvbnm")}

doc = [""]
fname = ""
saved = False


def wrap_doc():
    """Hard-wrap long lines for display: (doc_line, start) per screen row."""
    rows = []
    for i, line in enumerate(doc):
        if not line:
            rows.append((i, 0)); continue
        for s in range(0, len(line), TEXTW):
            rows.append((i, s))
        if len(line) % TEXTW == 0:
            rows.append((i, len(line)))
    return rows


def draw():
    rows = wrap_doc()
    cur_line = len(doc) - 1
    cur_row = len(rows) - 1
    top = max(0, cur_row - PAGE + 1)
    words = sum(len(l.split()) for l in doc)
    col = len(doc[-1]) + 1
    out = ["\033[H"]
    title = f" ZineWrite · {fname}{' (saved)' if saved else ' *'}"
    pos = f"Ln {cur_line + 1} Col {col} "
    out.append(INV + title + " " * (COLS - len(title) - len(pos)) + pos + RESET + "\r\n")
    ruler = "".join("|" if i % 10 == 0 else ("." if i % 5 else ":") for i in range(1, COLS - 1))
    out.append(DIM + " " + ruler + RESET + "\033[K\r\n")
    for r in range(PAGE):
        k = top + r
        if k < len(rows):
            i, s = rows[k]
            text = doc[i][s:s + TEXTW]
            style = BOLD if i == 0 else ""
            out.append(f" {style}{text}{RESET}\033[K\r\n")
        else:
            out.append("\033[K\r\n")
    status = f" Words {words}   INS   ^S Save   ^Q Quit"
    out.append(DIM + INV + status + " " * (COLS - len(status)) + RESET)
    # the real terminal cursor at the insertion point
    i, s = rows[cur_row]
    crow = 2 + (cur_row - top)
    ccol = 1 + (len(doc[-1]) - s)
    out.append(f"\033[{crow + 1};{ccol + 1}H")
    sys.stdout.write("".join(out)); sys.stdout.flush()
    look(ccol * CW + CW // 2, crow * CH + CH // 2)


def look(x, y):
    """Point xeyes at screen pixel (x, y). xeyes pins its pupils to the rim for anything
    outside the eyes, so put the pointer at a stand-in point in the same direction but
    close to the eyes (nearer for near targets): the pupils then sweep through their
    whole range instead of sitting on the left edge."""
    dx, dy = x - EYES[0], y - EYES[1]
    d = math.hypot(dx, dy) or 1
    r = 14 + 46 * min(1.0, d / 700)          # 14..60 px from the eyes' centre
    px, py = EYES[0] + dx / d * r, EYES[1] + dy / d * r * 1.25
    subprocess.run(["xdotool", "mousemove", str(int(px)), str(int(py))],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def glance(x, y, t):
    """Look somewhere else for a moment, then back at the cursor."""
    look(x, y); wait(t); draw()


def check_quit():
    while select.select([sys.stdin], [], [], 0)[0]:
        if sys.stdin.read(1) in ("q", "\x11", "\x1b"):
            raise KeyboardInterrupt


def wait(t):
    end = time.time() + t
    while time.time() < end:
        check_quit()
        time.sleep(min(0.05, max(0, end - time.time())))


def key(ch):
    global saved
    saved = False
    if ch == "\n": doc.append("")
    elif ch == "\b": doc[-1] = doc[-1][:-1]
    else: doc[-1] += ch
    draw()


def type_text(text):
    for ch in text:
        low = ch.lower()
        if low in NEAR and random.random() < 0.035:      # a typo, noticed, fixed
            wrong = NEAR[low]
            key(wrong.upper() if ch.isupper() else wrong)
            wait(random.uniform(0.25, 0.6))
            key("\b"); wait(random.uniform(0.1, 0.2))
        key(ch)
        if ch == "\n":
            # re-read the line just typed: eyes run back along it
            n = len(doc[-2]) if len(doc) > 1 else 0
            rows = min(PAGE, len(wrap_doc())) + 1
            for c in (n, n * 2 // 3, n // 3, 0):
                look((1 + c) * CW, rows * CH - CH // 2); wait(0.12)
            draw(); wait(random.uniform(0.3, 0.7))
        elif ch in ",;:": wait(random.uniform(0.2, 0.45))
        elif ch in ".?!": wait(random.uniform(0.35, 0.7))
        elif ch == " ": wait(random.uniform(0.06, 0.16))
        else: wait(random.uniform(0.04, 0.12))


def main():
    global doc, fname, saved
    fd = sys.stdin.fileno()
    old = termios.tcgetattr(fd) if os.isatty(fd) else None
    if old: tty.setcbreak(fd)
    sys.stdout.write("\033[2J\033[?25h")
    # keyboard focus on this xterm for good: with the default focus-follows-pointer it would
    # go to xeyes as soon as the pointer moves there, and the cursor would turn hollow
    wid = os.environ.get("WINDOWID")
    if wid:
        subprocess.run(["xdotool", "windowfocus", wid], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        while True:
            for name, text in PIECES:
                doc, fname, saved = [""], name, False
                draw(); wait(1.0)
                type_text(text)
                wait(1.2)
                saved = True; draw()            # ^S
                glance(COLS * CW // 3, CH // 2, 0.8)          # admire the title bar
                glance(400, 400, 0.9)                         # look at you
                glance(20, 6 * CH, 0.6)                       # back at the page
                wait(1.5)
    except KeyboardInterrupt:
        pass
    finally:
        if old: termios.tcsetattr(fd, termios.TCSADRAIN, old)


if __name__ == "__main__":
    main()
