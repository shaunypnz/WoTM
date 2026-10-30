#!/usr/bin/env python3
# ABOUTME: Runs bot-only scenarios against a Linux build, each in its own bounded window on a private display, and judges them: crash,
# ABOUTME: early exit, blank or frozen picture, slow game pace. Writes report.json and contact sheets. --spectate shows bots on your screen.
"""Usage:
  scenarios.py --game <folder with wotm> [--only a,b] [--seconds 90] [--out DIR] [--xvfb "Xvfb"] [--lock FILE]   run and judge
  scenarios.py --game <folder> --spectate [--seconds 120]                 watch bot matches in a window, one after another; Ctrl+Q stops
  scenarios.py --list
The disc comes from PS2X_CD_IMAGE or the game folder's play.sh. Exit status is 0 only if every scenario passed. A pass on the private
display uses software graphics: it says the game logic and drawing hold up, not how fast a real GPU is."""
import argparse, fcntl, json, os, re, shutil, signal, subprocess, sys, time

# id: (what it covers, environment). Match: monster,bots...,stage (docs/guides/NATIVE.md, WOTM_MATCH)
FIGHT = {"WOTM_AIVSAI": "1", "WOTM_CHEATS": "inv"}
SCENARIOS = {
    "menu": ("boot, movies, main menu", {}),
    "duel": ("1 v 1 in Midtown Park", {**FIGHT, "WOTM_MATCH": "3,1,1"}),
    "wins": ("rounds end and the next one loads", {"WOTM_AIVSAI": "1", "WOTM_MATCH": "3,1,1", "WOTM_CHEATS": "inv,esp,vencer=25"}),
    "stage2": ("another stage and roster", {**FIGHT, "WOTM_MATCH": "1,2,2"}),
    "stage3": ("another stage and roster", {**FIGHT, "WOTM_MATCH": "8,9,3"}),
    "stage5": ("another stage and roster", {**FIGHT, "WOTM_MATCH": "10,4,5"}),
    "split2": ("2-player split screen", {**FIGHT, "WOTM_PLAYERS": "2", "WOTM_MATCH": "3,1,1"}),
    "split4": ("4-player split screen", {**FIGHT, "WOTM_PLAYERS": "4", "WOTM_MATCH": "3,1,2,4,1"}),
    "eight": ("8 monsters on one stage", {**FIGHT, "WOTM_MATCH": "3,1,2,4,5,7,8,9,1"}),
    "filters": ("FXAA and sharpening", {**FIGHT, "WOTM_MATCH": "3,1,1", "WOTM_FXAA": "1", "WOTM_SHARPEN": "0.5"}),
    "hires": ("1080 lines, MSAA 4x", {**FIGHT, "WOTM_MATCH": "3,1,1", "WOTM_RES": "1080", "WOTM_MSAA": "4"}),
}
SPECTATE = ["duel", "stage2", "stage3", "stage5", "split2", "split4", "eight"]
BASE = {"WOTM_SETTINGS": "0", "WOTM_FULLSCREEN": "0", "WOTM_RES": "720", "PS2X_STATS": "1"}
HOST = {"PS2X_NO_VU1": "1", "PS2X_NO_VIF1": "1", "PS2X_NO_GS": "1", "PS2X_SCENE": "1", "PS2X_FMV": "1"}


def disc(game):
    if os.environ.get("PS2X_CD_IMAGE"):
        return os.environ["PS2X_CD_IMAGE"]
    try:
        m = re.search(r'PS2X_CD_IMAGE:-([^}]+)\}', open(os.path.join(game, "play.sh")).read())
        return m.group(1) if m else None
    except OSError:
        return None


def game_env(game, iso, env):
    e = {**os.environ, **HOST, "PS2X_CD_IMAGE": iso, **env}
    lib = os.path.join(game, "lib")
    if os.path.isdir(lib):
        e["LD_LIBRARY_PATH"] = lib + ":" + e.get("LD_LIBRARY_PATH", "")
    return e


def free_display():
    for n in range(90, 110):
        if not os.path.exists(f"/tmp/.X11-unix/X{n}"):
            return n
    sys.exit("no free X display number")


def grab(display, path):
    from PIL import ImageGrab
    ImageGrab.grab(xdisplay=f":{display}").save(path)


def picture_checks(shots):
    """the last shots: not blank, and still moving"""
    from PIL import Image, ImageChops, ImageStat
    if len(shots) < 3:
        return ["too few screen grabs"]
    bad = []
    imgs = [Image.open(s).convert("L").resize((480, 270)) for s in shots[-4:]]
    means = [ImageStat.Stat(i).mean[0] for i in imgs]
    if min(means) < 3:
        bad.append(f"blank picture (brightness {[round(m, 1) for m in means]})")
    moved = [ImageStat.Stat(ImageChops.difference(a, b)).mean[0] for a, b in zip(imgs, imgs[1:])]
    if max(moved) < 0.2:
        bad.append("frozen picture")
    return bad


def contact_sheet(shots, path):
    from PIL import Image
    picks = shots[-4:]
    if not picks:
        return
    sheet = Image.new("RGB", (480 * len(picks), 270))
    for i, s in enumerate(picks):
        sheet.paste(Image.open(s).convert("RGB").resize((480, 270)), (480 * i, 0))
    sheet.save(path)


def log_checks(log, seconds):
    bad, info = [], {}
    if re.search(r"\[CRASH\]", log):
        bad.append("crash: " + re.search(r"\[CRASH\].*", log).group(0)[:120])
    ticks = [int(t) for t in re.findall(r"vsyncTick=(\d+)", log)]
    if len(ticks) >= 2:
        info["game_ticks_per_s"] = round(ticks[-1] / max(seconds, 1), 1)
    slow = re.findall(r"\[vigia\] LENTO", log)
    info["slow_warnings"] = len(slow)
    info["log_lines"] = log.count("\n")
    return bad, info


def run_one(name, game, iso, seconds, out, xvfb):
    what, env = SCENARIOS[name]
    d = os.path.join(out, name)
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    n = free_display()
    x = subprocess.Popen(xvfb.split() + [f":{n}", "-screen", "0", "1920x1080x24", "-nolisten", "tcp", "-ac"],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, close_fds=True)
    time.sleep(1)
    e = game_env(game, iso, {**BASE, "DISPLAY": f":{n}", "LIBGL_ALWAYS_SOFTWARE": "1", **env})
    log = os.path.join(d, "log.txt")
    t0 = time.time()
    with open(log, "wb") as f:
        p = subprocess.Popen([os.path.join(game, "wotm")], cwd=game, env=e, stdout=f, stderr=subprocess.STDOUT)
    shots, early = [], None
    while time.time() - t0 < seconds:
        time.sleep(5)
        if p.poll() is not None:
            early = p.returncode
            break
        s = os.path.join(d, f"t{int(time.time() - t0):04d}.png")
        try:
            grab(n, s)
            shots.append(s)
        except Exception as err:
            print(f"  grab failed: {err}")
    elapsed = time.time() - t0
    if p.poll() is None:
        p.terminate()
        try:
            p.wait(5)
        except subprocess.TimeoutExpired:
            p.kill()
    x.terminate()
    text = open(log, errors="replace").read()
    bad, info = log_checks(text, elapsed)
    if early is not None:
        bad.append(f"exited early with status {early} after {int(elapsed)} s")
    bad += picture_checks(shots)
    contact_sheet(shots, os.path.join(d, "sheet.png"))
    for s in shots[:-4]:
        os.remove(s)
    return {"scenario": name, "covers": what, "pass": not bad, "problems": bad, "seconds": int(elapsed), **info}


def spectate(game, iso, seconds):
    print("Bot matches in a window. Ctrl+Q in the game stops; F3 shows the frame rate.")
    i = 0
    while True:
        name = SPECTATE[i % len(SPECTATE)]
        i += 1
        print(f"{name}: {SCENARIOS[name][0]}")
        env = {k: v for k, v in SCENARIOS[name][1].items()}
        p = subprocess.Popen([os.path.join(game, "wotm")], cwd=game, env=game_env(game, iso, {**BASE, **env}))
        try:
            code = p.wait(seconds)
        except subprocess.TimeoutExpired:
            p.terminate()
            p.wait()
            continue
        if code != 0:
            print(f"the game exited with status {code}: stopping")
        return code


def main():
    a = argparse.ArgumentParser()
    a.add_argument("--game"); a.add_argument("--only"); a.add_argument("--seconds", type=int)
    a.add_argument("--out", default="scenario-runs"); a.add_argument("--xvfb", default="Xvfb"); a.add_argument("--lock")
    a.add_argument("--list", action="store_true"); a.add_argument("--spectate", action="store_true")
    o = a.parse_args()
    if o.list:
        for k, v in SCENARIOS.items():
            print(f"{k:10} {v[0]}")
        return 0
    if not o.game:
        a.error("--game is required")
    game = os.path.realpath(o.game)
    iso = disc(game)
    if not iso or not os.path.exists(iso):
        sys.exit("disc image not found: set PS2X_CD_IMAGE")
    if o.spectate:
        return spectate(game, iso, o.seconds or 120)
    names = o.only.split(",") if o.only else list(SCENARIOS)
    unknown = [n for n in names if n not in SCENARIOS]
    if unknown:
        sys.exit(f"unknown scenario: {unknown}")
    lock = None
    if o.lock:
        lock = open(o.lock, "w")
        fcntl.flock(lock, fcntl.LOCK_EX)
    os.makedirs(o.out, exist_ok=True)
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(143))
    results = []
    for n in names:
        print(f"== {n}: {SCENARIOS[n][0]}", flush=True)
        r = run_one(n, game, iso, o.seconds or 90, o.out, o.xvfb)
        results.append(r)
        print(f"   {'PASS' if r['pass'] else 'FAIL'} ticks/s={r.get('game_ticks_per_s')} slow={r['slow_warnings']} {'; '.join(r['problems'])}", flush=True)
        json.dump(results, open(os.path.join(o.out, "report.json"), "w"), indent=1)
    failed = [r["scenario"] for r in results if not r["pass"]]
    print(f"{len(results) - len(failed)}/{len(results)} passed" + (f"; failed: {', '.join(failed)}" if failed else ""))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
