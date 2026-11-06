#!/usr/bin/env python3
"""exetui: an exe.dev VM manager for the bar's 60x10 console.

Every action is one exe.dev HTTPS API call: POST https://exe.dev/exec with the
CLI command line as the body; JSON comes back (https://exe.dev/docs/https-api).

Backends
  SIM  (default)  an in-process emulator of the commands used here, with the
                  response shapes and errors of exe.dev's own API explorer
                  (https://exe.dev/api); fake latency, no account, no network.
  LIVE            the real API when EXE_TOKEN is set (a token from
                  `ssh exe.dev ssh-key generate-api-key`). Default token
                  permissions allow ls/new/pause/resume/whoami; rm, restart
                  and stat must be granted with --cmds.

Keys: up/down select, s stats, p pause, u resume, r restart, n new, d delete,
g refresh, q quit.  --demo drives it by itself (for the display simulator).
"""
import curses, json, math, os, random, sys, time, urllib.error, urllib.request

API = "https://exe.dev/exec"
REGIONS = {"lax": "Los Angeles, USA", "nyc": "New York, USA", "dal": "Dallas, USA",
           "fra": "Frankfurt, Germany", "tyo": "Tokyo, Japan", "syd": "Sydney, Australia",
           "sgp": "Singapore", "lon": "London, UK"}
BLOCKS = "▁▂▃▄▅▆▇█"


# --- backends: both take the command line and answer (http_status, json, ms) ---------

class Live:
    name = "LIVE"

    def __init__(self, token):
        self.token = token

    def exec(self, cmd):
        t0 = time.time()
        req = urllib.request.Request(API, data=cmd.encode(), method="POST",
                                     headers={"Authorization": f"Bearer {self.token}"})
        try:
            with urllib.request.urlopen(req, timeout=35) as r:
                status, body = r.status, r.read().decode()
        except urllib.error.HTTPError as e:
            status, body = e.code, e.read().decode(errors="replace")
        except OSError as e:
            return 0, {"error": str(e)}, int((time.time() - t0) * 1000)
        try:
            data = json.loads(body) if body.strip() else {}
        except ValueError:
            data = {"error": body.strip()} if status >= 400 else {"text": body}
        return status, data, int((time.time() - t0) * 1000)


class Sim:
    """The commands this TUI uses, as exe.dev's API explorer emulates them."""
    name = "SIM"

    def __init__(self):
        now = "2026-11-05T17:12:00Z"
        self.email, self.region, self.seq = "you@example.com", "lax", 1
        self.vms = {}
        for n, st, rg, tags, comment, img in [
            ("bloggy", "running", "lon", ["prod"], "my blog", "exeuntu"),
            ("scratch", "running", "lax", [], "", "exeuntu"),
            ("zine-site", "running", "lax", ["prod", "web"], "elevenarms.github.io mirror", "exeuntu"),
            ("kicad-ci", "paused", "fra", ["ci"], "DRC/ERC on push", "exeuntu"),
            ("fbdemo-build", "stopped", "lax", ["ci"], "arm64 cross builds", "exeuntu"),
            ("shelley", "running", "nyc", ["agent"], "coding agent", "exeuntu"),
            ("backups", "running", "syd", [], "restic target", "exeuntu"),
        ]:
            self.vms[n] = dict(name=n, status=st, region=rg, cpu=2, memoryGB=4, diskGB=20,
                               image=img, tags=tags, comment=comment, createdAt=now)

    def _ls(self, vm):
        n = vm["name"]
        return {"vm_name": n, "dns_name": f"{n}.exe.xyz", "status": vm["status"], "region": vm["region"],
                "region_display": REGIONS[vm["region"]], "image": vm["image"], "tags": vm["tags"],
                "comment": vm["comment"], "locked": False, "created_at": vm["createdAt"], "shelley_url": "",
                "https_url": f"https://{n}.exe.xyz", "ssh_dest": f"{n}.exe.xyz", "ssh_host": f"{n}.exe.xyz"}

    def exec(self, cmd):
        ms = random.randint(70, 260)
        time.sleep(ms / 1000)
        words = cmd.split()
        args = [w for w in words[1:] if not w.startswith("--")]
        flags = dict(w[2:].split("=", 1) for w in words[1:] if w.startswith("--") and "=" in w)
        err = lambda m: (422, {"error": m}, ms)
        c = words[0] if words else ""
        if c == "whoami":
            return 200, {"email": self.email, "region": self.region,
                         "region_display": REGIONS[self.region], "ssh_keys": []}, ms
        if c == "ls":
            return 200, {"vms": [self._ls(v) for _, v in sorted(self.vms.items())]}, ms
        vm = self.vms.get(args[0]) if args else None
        if c in ("rm", "restart", "pause", "resume", "stat") and not vm:
            return err(f'VM "{args[0]}" not found' if args else f"usage: {c} <vmname>")
        if c == "new":
            name = flags.get("name") or f"vm-{(self.seq * 2654435761) & 0xffffff:06x}"
            self.seq += 1
            if name in self.vms: return err(f'VM "{name}" already exists')
            self.vms[name] = dict(name=name, status="running", region=self.region, cpu=2, memoryGB=4, diskGB=20,
                                  image=flags.get("image", "exeuntu"), tags=[t for t in flags.get("tag", "").split(",") if t],
                                  comment=flags.get("comment", ""), createdAt=time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()))
            return 200, {"vm_name": name, "tags": self.vms[name]["tags"], "ssh_command": f"ssh {name}.exe.xyz",
                         "ssh_dest": f"{name}.exe.xyz", "ssh_host": f"{name}.exe.xyz", "ssh_port": 22,
                         "https_url": f"https://{name}.exe.xyz"}, ms
        if c == "rm":
            for a in args: self.vms.pop(a, None)
            return 200, {"deleted": args}, ms
        if c == "restart":
            vm["status"] = "running"
            return 200, {"vm_name": vm["name"], "restarted": True, "status": "running"}, ms
        if c == "pause":
            if vm["status"] == "stopped": return err(f'VM "{vm["name"]}" is stopped; only running VMs can be paused')
            vm["status"] = "paused"
            return 200, {"vm_name": vm["name"], "status": "paused"}, ms
        if c == "resume":
            vm["status"] = "running"
            return 200, {"vm_name": vm["name"], "status": "running"}, ms
        if c == "stat":
            self.seq += 1
            vals = lambda f, o: [round(o + abs(math.sin((i + self.seq) / f)) * f * 8, 2) for i in range(32)]
            return 200, {"vm_name": vm["name"], "range": flags.get("range", "24h"), "vcpu_pct": vals(3.7, 8),
                         "net_rx_kbps": vals(5.2, 12), "net_tx_kbps": vals(4.4, 5), "disk_iops": vals(2.3, 1)}, ms
        return 404, {"error": f"unknown command {c!r}"}, ms


# --- the TUI -------------------------------------------------------------------------

STATUS_COL = {"running": 2, "paused": 3, "stopped": 4}


class App:
    def __init__(self, scr, api, demo):
        self.scr, self.api, self.demo = scr, api, demo
        self.vms, self.sel, self.top, self.who = [], 0, 0, {}
        self.last = ("", 0, 0)            # cmd, http status, ms
        self.msg, self.stats = "", None
        curses.curs_set(0)
        curses.use_default_colors()
        for i, c in enumerate([curses.COLOR_CYAN, curses.COLOR_GREEN, curses.COLOR_YELLOW,
                               curses.COLOR_RED, curses.COLOR_MAGENTA, curses.COLOR_WHITE], 1):
            curses.init_pair(i, c, -1)
        curses.init_pair(7, curses.COLOR_BLACK, curses.COLOR_CYAN)
        scr.timeout(100)

    def call(self, cmd):
        self.last = (cmd, 0, 0); self.msg = "…"; self.draw()
        status, data, ms = self.api.exec(cmd)
        self.last = (cmd, status, ms)
        self.msg = data.get("error", "") if isinstance(data, dict) else ""
        return status, data

    def refresh(self):
        st, data = self.call("ls --json")
        if st == 200:
            self.vms = data.get("vms", [])
            self.sel = min(self.sel, max(0, len(self.vms) - 1))

    def cur(self):
        return self.vms[self.sel] if self.vms else None

    # drawing ------------------------------------------------------------------
    def put(self, y, x, s, attr=0):
        h, w = self.scr.getmaxyx()
        if 0 <= y < h and x < w:
            try: self.scr.addnstr(y, x, s, w - x - (1 if y == h - 1 else 0), attr)
            except curses.error: pass

    def draw(self):
        s = self.scr; s.erase(); h, w = s.getmaxyx()
        run = sum(v["status"] == "running" for v in self.vms)
        mode = f" {self.api.name} "
        head = f" exe.dev · {self.who.get('email', '…')} · {run}/{len(self.vms)} up"
        self.put(0, 0, head.ljust(w), curses.color_pair(7))
        self.put(0, w - len(mode) - 1, mode, curses.color_pair(7) | curses.A_BOLD)
        body = h - 4
        if self.stats:
            self.draw_stats(body)
        else:
            if self.sel < self.top: self.top = self.sel
            if self.sel >= self.top + body: self.top = self.sel - body + 1
            for i in range(body):
                k = self.top + i
                if k >= len(self.vms): break
                v = self.vms[k]
                mark = "›" if k == self.sel else " "
                attr = curses.A_REVERSE if k == self.sel else 0
                name = v["vm_name"][:16].ljust(16)
                tags = ",".join(v.get("tags") or [])[:w - 36]
                self.put(1 + i, 0, f"{mark}{name} ", attr | curses.A_BOLD)
                self.put(1 + i, 18, f"{v['status']:<8}", attr | curses.color_pair(STATUS_COL.get(v["status"], 6)))
                self.put(1 + i, 27, f" {v['region']} ", attr | curses.color_pair(1))
                self.put(1 + i, 32, f" {tags}".ljust(w - 32), attr | curses.color_pair(5))
        v = self.cur()
        if v and not self.stats:
            self.put(h - 3, 1, f"{v['https_url']}  {v.get('comment', '')}", curses.color_pair(1))
        cmd, st, ms = self.last
        col = 2 if st == 200 else (3 if st == 0 else 4)
        line = f'POST /exec "{cmd}"'
        tail = f" {st} · {ms}ms" if st else " …"
        self.put(h - 2, 1, line[: w - len(tail) - 2], curses.color_pair(6) | curses.A_DIM)
        self.put(h - 2, w - len(tail) - 1, tail, curses.color_pair(col) | curses.A_BOLD)
        if self.msg and self.msg != "…":
            self.put(h - 3, 1, f"! {self.msg}".ljust(w - 2), curses.color_pair(4) | curses.A_BOLD)
        keys = "s stats p pause u resume r restart n new d rm q" if not self.stats else "any key: back"
        self.put(h - 1, 0, keys.center(w - 1), curses.A_DIM)
        s.refresh()

    def draw_stats(self, body):
        d = self.stats
        def spark(xs):
            m = max(xs + [1])
            return "".join(BLOCKS[min(7, int(x / m * 7))] for x in xs)
        avg = lambda xs: sum(xs) / len(xs)
        self.put(1, 1, f"{d['vm_name']} — last {d['range']}", curses.A_BOLD)
        rows = [("vcpu", d["vcpu_pct"], "%"), ("rx", d["net_rx_kbps"], "KB/s"),
                ("tx", d["net_tx_kbps"], "KB/s"), ("io", d["disk_iops"], "op/s")]
        for i, (lab, xs, unit) in enumerate(rows[: body - 1]):
            self.put(2 + i, 1, f"{lab:<4}", curses.color_pair(1))
            self.put(2 + i, 6, spark(xs), curses.color_pair(2 + i % 3))
            self.put(2 + i, 39, f"avg {avg(xs):5.1f} {unit}")

    # actions ------------------------------------------------------------------
    def action(self, key):
        v = self.cur()
        if self.stats:
            self.stats = None; return
        if key in (curses.KEY_DOWN, ord("j")): self.sel = min(self.sel + 1, len(self.vms) - 1)
        elif key in (curses.KEY_UP, ord("k")): self.sel = max(self.sel - 1, 0)
        elif key == ord("g"): self.refresh()
        elif key == ord("s") and v:
            st, d = self.call(f"stat {v['vm_name']} --range=24h --json")
            if st == 200: self.stats = d
        elif key in (ord("p"), ord("u"), ord("r")) and v:
            verb = {ord("p"): "pause", ord("u"): "resume", ord("r"): "restart"}[key]
            st, _ = self.call(f"{verb} {v['vm_name']} --json")
            if st == 200: self.refresh_keep()
        elif key == ord("n"):
            name = self.prompt("new VM name: ")
            if name is not None:
                st, d = self.call(f"new --name={name} --json" if name else "new --json")
                if st == 200: self.refresh_keep(d.get("vm_name"))
        elif key == ord("d") and v:
            if self.confirm(f"delete {v['vm_name']}? y/N "):
                st, _ = self.call(f"rm {v['vm_name']} --json")
                if st == 200: self.refresh_keep()

    def refresh_keep(self, name=None):
        name = name or (self.cur() or {}).get("vm_name")
        cmd, st, ms = self.last
        self.refresh()
        self.last = (cmd, st, ms)        # keep showing the action's call, not the ls
        for i, v in enumerate(self.vms):
            if v["vm_name"] == name: self.sel = i

    def prompt(self, q, typed=None):
        h, w = self.scr.getmaxyx(); buf = ""
        while True:
            self.put(h - 3, 1, (q + buf + "█").ljust(w - 2), curses.color_pair(3) | curses.A_BOLD); self.scr.refresh()
            if typed is not None:                     # demo: type it out
                if not typed: return buf
                time.sleep(0.12); buf += typed[0]; typed = typed[1:]; continue
            k = self.scr.getch()
            if k in (10, 13): return buf
            if k == 27: return None
            if k in (curses.KEY_BACKSPACE, 127, 8): buf = buf[:-1]
            elif 32 <= k < 127 and len(buf) < 30: buf += chr(k).lower()

    def confirm(self, q, answer=None):
        h, w = self.scr.getmaxyx()
        self.put(h - 3, 1, q.ljust(w - 2), curses.color_pair(4) | curses.A_BOLD); self.scr.refresh()
        if answer is not None:
            time.sleep(0.8); return answer
        return self.scr.getch() in (ord("y"), ord("Y"))

    # main loops ---------------------------------------------------------------
    def run(self):
        st, self.who = self.call("whoami --json")
        self.refresh()
        if self.demo:
            return self.run_demo()
        while True:
            self.draw()
            k = self.scr.getch()
            if k in (ord("q"), 27): return
            if k != -1: self.action(k)

    def pause(self, t):
        end = time.time() + t
        while time.time() < end:
            self.draw()
            if self.scr.getch() in (ord("q"), 27): raise SystemExit

    def goto(self, name):
        for _ in range(len(self.vms)):
            v = self.cur()
            if v and v["vm_name"] == name: return
            want = [x["vm_name"] for x in self.vms].index(name) if name in [x["vm_name"] for x in self.vms] else 0
            self.action(curses.KEY_DOWN if want > self.sel else curses.KEY_UP); self.pause(0.35)

    def run_demo(self):
        n = 0
        while True:
            n += 1
            self.pause(1.5)
            self.goto("zine-site"); self.pause(0.6)
            self.action(ord("s")); self.pause(3.5); self.action(-1); self.pause(0.8)
            self.goto("scratch"); self.pause(0.6)
            self.action(ord("p")); self.pause(1.6)
            self.action(ord("u")); self.pause(1.6)
            # a name that exists: the API answers 422 {"error": ...}, shown in red
            st, d = self.call(f"new --name={self.prompt('new VM name: ', 'bloggy') or 'bloggy'} --json")
            self.pause(2.5)
            name = f"bar-demo-{n}"
            st, d = self.call(f"new --name={self.prompt('new VM name: ', name) or name} --tag=demo --json")
            if st == 200: self.refresh_keep(d.get("vm_name"))
            self.pause(2.0)
            if self.confirm(f"delete {name}? y/N ", True):
                st, _ = self.call(f"rm {name} --json")
                if st == 200: self.refresh_keep()
            self.pause(1.5)
            self.goto("kicad-ci"); self.pause(0.5)
            self.action(ord("u")); self.pause(1.2); self.action(ord("r")); self.pause(1.5)


def main(scr):
    token = os.environ.get("EXE_TOKEN", "").strip()
    api = Live(token) if token and "--sim" not in sys.argv else Sim()
    try:
        App(scr, api, "--demo" in sys.argv).run()
    except SystemExit:
        pass


if __name__ == "__main__":
    curses.wrapper(main)
