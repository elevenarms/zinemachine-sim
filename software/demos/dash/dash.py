#!/usr/bin/env python3
"""dash: a text-mode status dashboard for the bar's 120x20 console (curses).

Clock, uptime/load/memory/disk with bar gauges, a CPU history sparkline and a
news-ticker crawl along the bottom. Pure stdlib, reads /proc, so it runs the same
on the PocketBeagle. Keys: q quits.
"""
import curses, os, shutil, time

TICKER = ("ZINE MACHINE V0  ·  POCKETBEAGLE 2 + 4.58\" 960x320 BAR  ·  40 CHOC KEYS  ·  "
          "TEXT · FRAMEBUFFER · SDL2  ·  ")
SPARK = " ▁▂▃▄▅▆▇█"

def cpu_times():
    with open("/proc/stat") as f:
        v = list(map(int, f.readline().split()[1:]))
    idle = v[3] + v[4]
    return sum(v), idle

def meminfo():
    m = {}
    with open("/proc/meminfo") as f:
        for line in f:
            k, v = line.split(":")
            m[k] = int(v.split()[0])
    return m["MemTotal"], m["MemTotal"] - m.get("MemAvailable", m["MemFree"])

def gauge(frac, width):
    n = int(round(frac * width))
    return "█" * n + "░" * (width - n)

def main(scr):
    curses.curs_set(0)
    curses.use_default_colors()
    for i, c in enumerate([curses.COLOR_GREEN, curses.COLOR_CYAN, curses.COLOR_YELLOW, curses.COLOR_MAGENTA, curses.COLOR_RED], 1):
        curses.init_pair(i, c, -1)
    scr.nodelay(True)
    hist = [0.0] * 60
    t0, i0 = cpu_times()
    tick = 0
    while True:
        ch = scr.getch()
        if ch in (ord("q"), 27):
            return
        h, w = scr.getmaxyx()
        scr.erase()
        t1, i1 = cpu_times()
        cpu = 1 - (i1 - i0) / max(1, t1 - t0)
        t0, i0 = t1, i1
        hist = hist[1:] + [cpu]
        total, used = meminfo()
        du = shutil.disk_usage("/")
        load = os.getloadavg()
        with open("/proc/uptime") as f:
            up = int(float(f.read().split()[0]))

        # header
        title = " ZINE MACHINE "
        scr.addstr(0, 0, "═" * (w - 1), curses.color_pair(2))
        scr.addstr(0, 2, title, curses.color_pair(2) | curses.A_BOLD)
        clock = time.strftime(" %H:%M:%S  %a %d %b ")
        scr.addstr(0, w - len(clock) - 3, clock, curses.color_pair(3) | curses.A_BOLD)

        # gauges
        gw = 40
        rows = [
            ("CPU ", cpu, f"{cpu*100:5.1f}%", 1),
            ("MEM ", used / total, f"{used/1024:6.0f}/{total/1024:.0f} MiB", 2),
            ("DISK", du.used / du.total, f"{du.used/2**30:5.1f}/{du.total/2**30:.0f} GiB", 3),
        ]
        for k, (name, frac, txt, col) in enumerate(rows):
            y = 2 + k * 2
            scr.addstr(y, 2, name, curses.A_BOLD)
            scr.addstr(y, 8, gauge(frac, gw), curses.color_pair(5 if frac > 0.85 else col))
            scr.addstr(y, 9 + gw, txt)

        # right column: system facts
        x = 70
        facts = [
            ("host", os.uname().nodename),
            ("kernel", os.uname().release),
            ("arch", os.uname().machine),
            ("uptime", f"{up//86400}d {up%86400//3600:02d}:{up%3600//60:02d}"),
            ("load", " ".join(f"{l:.2f}" for l in load)),
            ("procs", str(len([p for p in os.listdir('/proc') if p.isdigit()]))),
        ]
        for k, (a, b) in enumerate(facts):
            scr.addstr(2 + k, x, f"{a:>7} ", curses.color_pair(2))
            scr.addstr(2 + k, x + 8, b[: w - x - 10])

        # cpu history sparkline, 60 s
        scr.addstr(9, 2, "CPU 60s", curses.A_BOLD)
        spark = "".join(SPARK[min(8, int(v * 8.999))] for v in hist)
        scr.addstr(9, 10, spark, curses.color_pair(1))
        scr.addstr(10, 10, "└" + "─" * 58 + "┘ now", curses.color_pair(2))

        # a little "bar display" self-portrait
        scr.addstr(12, 2, "display", curses.A_BOLD)
        scr.addstr(12, 10, "960 × 320 · RGB565 · 4.58\" · 120×20 text", curses.color_pair(3))

        # ticker
        crawl = (TICKER * 4)[tick % len(TICKER):][: w - 1]
        scr.addstr(h - 2, 0, "─" * (w - 1), curses.color_pair(2))
        scr.addstr(h - 1, 0, crawl, curses.color_pair(4) | curses.A_BOLD)
        scr.refresh()
        tick += 1
        time.sleep(0.15)

if __name__ == "__main__":
    curses.wrapper(main)
