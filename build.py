#!/usr/bin/env python3
"""Windows-friendly replacement for the Makefile (no make/pucrunch needed).

    python build.py [VARIANT] [--drive drv1541] [--input joydrv]

Builds build/<variant>/kernal_combined.prg, kernal_compressed.prg (exomizer)
and geos.d64 (c1541), using cc65/VICE/exomizer from the C64 tool folder.
"""
import argparse, os, re, shutil, subprocess, sys

TOOLS = os.environ.get("C64TOOLS", r"C:\Users\aegwh\OneDrive\dev\c64")
CA65 = os.path.join(TOOLS, "cc65", "bin", "ca65.exe")
LD65 = os.path.join(TOOLS, "cc65", "bin", "ld65.exe")
CL65 = os.path.join(TOOLS, "cc65", "bin", "cl65.exe")
C1541 = os.path.join(TOOLS, "vice", "bin", "c1541.exe")
EXOMIZER = os.path.join(TOOLS, "exomizer", "win32", "exomizer.exe")

ROOT = os.path.dirname(os.path.abspath(__file__))

DRIVES = ["drv1541", "drv1541parallel", "drv1571", "drv1571burst", "drv1581"]
INPUTS = ["joydrv", "amigamse", "lightpen", "mse1351", "koalapad", "pcanalog",
          "u64input"]


def makefile_list(text, name, cond=None):
    """Return the .s files of `NAME= \\` or `NAME += \\` blocks in the Makefile.
    cond selects blocks following a given ifeq/ifneq line."""
    out = []
    lines = text.splitlines()
    i = 0
    active = None
    while i < len(lines):
        l = lines[i]
        if l.startswith("ifeq") or l.startswith("ifneq"):
            active = l.strip()
        elif l.startswith("endif"):
            active = None
        m = re.match(r"\s*%s\s*(\+?=)" % name, l)
        if m and (active == cond):
            i += 1
            while i < len(lines) and lines[i].strip():
                f = lines[i].strip().rstrip("\\").strip()
                if f.endswith(".s"):
                    out.append(f)
                i += 1
            continue
        i += 1
    return out


def run(cmd):
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(" ".join(cmd) + "\n" + r.stdout + r.stderr)
        sys.exit(1)
    return r.stdout


# GEOS applications for the u64 variant: (directory, sources)
APPS = [
    ("nettest", ["nettestres.grc", "nettest.c", "../common/ultnet.s"]),
    ("u64setup", ["u64setupres.grc", "u64setup.c", "../common/settab.c",
                  "../common/rest.c", "../common/ucfg.c",
                  "../common/ultnet.s"]),
    ("network", ["networkres.grc", "network.c", "../common/settab.c",
                 "../common/rest.c", "../common/ucfg.c",
                  "../common/ultnet.s"]),
]


def make_icon(src, dst):
    """24x21 text art ('#' = set) -> 63 byte monochrome sprite"""
    rows = [l.rstrip() for l in open(src)][:21]
    out = bytearray()
    for r in rows:
        bits = int("".join("1" if c == "#" else "0" for c in r.ljust(24)), 2)
        out += bits.to_bytes(3, "big")
    open(dst, "wb").write(bytes(out))


# deskTop shows files in directory order, 8 per page: these come first
FIRST_PAGE = [b"GEOS", b"DESK TOP", b"Network", b"U64 Setup", b"nettest",
              b"CONFIGURE", b"PREFERENCE MGR", b"PAD COLOR MGR"]

# deskTop loads the first input driver file it finds and so replaces the
# turbo-safe 1351 driver built into the u64 kernal: keep none on the disk
INPUT_DRIVER_FILES = ["joystick", "comm 1351*"]


def d64_offset(track, sector):
    spt = [21] * 17 + [19] * 7 + [18] * 6 + [17] * 10
    return (sum(spt[:track - 1]) + sector) * 256


def reorder_directory(d64, first):
    """Move the directory entries named in `first` to the front of the
    directory. Only the 30-byte entries move; file data stays put."""
    img = bytearray(open(d64, "rb").read())
    slots = []
    t, s = 18, 1
    while t:
        off = d64_offset(t, s)
        for i in range(8):
            slots.append(off + i * 32 + 2)
        t, s = img[off], img[off + 1]
    entries = [bytes(img[o:o + 30]) for o in slots]
    used = [e for e in entries if e[0] & 0x07]
    name = lambda e: e[3:19].rstrip(b"\xa0").upper()
    want = [n.upper() for n in first]
    used.sort(key=lambda e: want.index(name(e)) if name(e) in want
              else len(want))
    entries = used + [bytes(30)] * (len(slots) - len(used))
    for o, e in zip(slots, entries):
        img[o:o + 30] = e
    open(d64, "wb").write(bytes(img))


def d64_alloc(img, prev=None):
    """Claim a free sector in the BAM, nearest to track 18, 8 sectors after
    the previous one (the 1541 interleave). Returns (track, sector)."""
    spt = [21] * 17 + [19] * 7 + [18] * 6 + [17] * 10
    bam = d64_offset(18, 0)
    tracks = [t for d in range(1, 18) for t in (18 - d, 18 + d)
              if 1 <= t <= 35]
    if prev:
        tracks.insert(0, prev[0])
    for t in tracks:
        if t == 18:
            continue
        n = spt[t - 1]
        start = (prev[1] + 8) % n if prev and t == prev[0] else 0
        for i in range(n):
            s = (start + i) % n
            e = bam + 4 * t
            if img[e + 1 + (s >> 3)] & (1 << (s & 7)):
                img[e + 1 + (s >> 3)] &= ~(1 << (s & 7)) & 0xff
                img[e] -= 1
                return t, s
    sys.exit("disk full")


def geoswrite(d64, cvt):
    """Put a SEQ CVT file on the disk. c1541 3.9's -geoswrite sometimes
    writes only one data block while the directory looks fine (the same
    call breaks one file and not the next), so do it here. CVT layout:
    254 bytes directory entry + signature, 254 bytes header block, data."""
    d = open(cvt, "rb").read()
    if d[21] != 0:              # VLIR: leave it to c1541, not checked below
        run([C1541, d64, "-geoswrite", cvt])
        return
    img = bytearray(open(d64, "rb").read())

    # free directory slot
    slot = None
    t, s = 18, 1
    while t and slot is None:
        off = d64_offset(t, s)
        for i in range(8):
            if not img[off + i * 32 + 2] & 0x07:
                slot = off + i * 32 + 2
                break
        t, s = img[off], img[off + 1]
    if slot is None:
        sys.exit(f"directory full on {d64}")

    # header block: link 00 FF, as on any GEOS disk
    ht, hs = d64_alloc(img)
    off = d64_offset(ht, hs)
    img[off:off + 256] = bytes([0, 0xff]) + d[254:508]

    # data chain, every block links to the next; the last one holds
    # 0 and the offset of its last used byte
    data = d[508:]
    blocks = [data[i:i + 254] for i in range(0, len(data), 254)]
    first = pos = d64_alloc(img, (ht, hs))
    for n, b in enumerate(blocks):
        off = d64_offset(*pos)
        if n + 1 < len(blocks):
            nxt = d64_alloc(img, pos)
            link = bytes(nxt)
        else:
            nxt, link = None, bytes([0, len(b) + 1])
        img[off:off + 256] = (link + b).ljust(256, bytes(1))
        pos = nxt

    entry = bytearray(d[:30])
    entry[1:3] = bytes(first)
    entry[19:21] = bytes((ht, hs))
    img[slot:slot + 30] = entry
    open(d64, "wb").write(bytes(img))


def check_geos_files(d64):
    """Every GEOS file's data chain must be as long as its directory
    entry says (SEQ: blocks - 1 for the header block)."""
    img = open(d64, "rb").read()
    t, s = 18, 1
    while t:
        off = d64_offset(t, s)
        for i in range(8):
            e = img[off + i * 32:off + i * 32 + 32]
            if not (e[2] & 7) or e[24] == 0 or e[23] != 0:
                continue        # empty, not GEOS, or VLIR
            n, tt, ss = 0, e[3], e[4]
            while tt and n < 700:
                b = d64_offset(tt, ss)
                tt, ss = img[b], img[b + 1]
                n += 1
            blocks = e[30] | e[31] << 8
            if n != blocks - 1:
                name = e[5:21].rstrip(bytes([0xa0])).decode("latin1")
                sys.exit(f"broken file on {d64}: {name}: chain {n} blocks, "
                         f"directory says {blocks - 1}")
        t, s = img[off], img[off + 1]


def post_u64(bd, d64):
    """Build the GEOS applications (cc65 GEOS target), add them to the disk."""
    for f in INPUT_DRIVER_FILES:
        subprocess.run([C1541, d64, "-delete", f], capture_output=True)
    os.makedirs(os.path.join(ROOT, "build", "icons"), exist_ok=True)
    env = dict(os.environ, CC65_HOME=os.path.join(TOOLS, "cc65"))
    for name, srcs in APPS:
        app = os.path.join(ROOT, "apps", name)
        icon = os.path.join(app, name + ".icon")
        if os.path.exists(icon):
            make_icon(icon, os.path.join(ROOT, "build", "icons", name + ".raw"))
        cvt = os.path.join(bd, name + ".cvt")
        objdir = os.path.join(bd, "apps", name)
        os.makedirs(objdir, exist_ok=True)
        r = subprocess.run([CL65, "-t", "geos-cbm", "-O", "--obj-path", objdir,
                            "-o", cvt] + [os.path.join(app, s) for s in srcs],
                           cwd=ROOT, capture_output=True, text=True, env=env)
        if r.returncode:
            sys.exit(r.stdout + r.stderr)
        geoswrite(d64, cvt)
    reorder_directory(d64, FIRST_PAGE)
    check_geos_files(d64)


def build_w128(a):
    """Native C128 kernal: same steps and memory layout as the bsw128 part
    of the Makefile, assembled with bsw128 and w128 defined."""
    bd = os.path.join("build", "w128")
    defs = ["-D", "bsw128=1", "-D", "w128=1", "-D", f"{a.drive}=1",
            "-D", f"{a.input}=1"]
    for d in a.defines:
        defs += ["-D", d]
    asflags = ["-I", "inc", "-I", ".", "--cpu", "6502X"]
    mk = open(os.path.join(ROOT, "Makefile")).read()

    def assemble(src):
        obj = os.path.join(bd, src[:-2] + ".o")
        os.makedirs(os.path.dirname(obj), exist_ok=True)
        run([CA65] + defs + asflags + [src, "-o", obj])
        return obj

    def link(name, cfg, srcs):
        objs = [assemble(s) for s in srcs]
        run([LD65, "-C", cfg] + objs + ["-o", f"{bd}/kernal/{name}.bin",
             "-m", f"{bd}/kernal/{name}.map", "-Ln", f"{bd}/kernal/{name}.lab"])
        return open(f"{bd}/kernal/{name}.bin", "rb").read()

    cond = "ifeq ($(VARIANT), bsw128)"
    os.makedirs(os.path.join(bd, "kernal"), exist_ok=True)
    k = link("kernal", "kernal/kernal_bsw128.cfg",
             makefile_list(mk, "KERNAL_SOURCES") +
             makefile_list(mk, "KERNAL_SOURCES", cond))
    k2 = link("kernal2", "kernal/kernal2_bsw128.cfg",
              makefile_list(mk, "KERNAL2_SOURCES"))
    rel = link("relocator", "kernal/relocator_bsw128.cfg",
               makefile_list(mk, "RELOCATOR_SOURCES", cond))
    o = assemble(f"drv/{a.drive}.s")
    os.makedirs(f"{bd}/drv", exist_ok=True)
    run([LD65, "-C", f"drv/{a.drive}.cfg", o, "-o", f"{bd}/drv/{a.drive}.bin"])
    drv = open(f"{bd}/drv/{a.drive}.bin", "rb").read()
    o = assemble(f"input/{a.input}.s")
    run([LD65, "-C", "input/inputdrv_bsw128.cfg", o, "-o",
         f"{bd}/input/{a.input}.bin"])
    inp = open(f"{bd}/input/{a.input}.bin", "rb").read()

    pad = lambda b, n: (b + bytes(n))[:n]
    img = b"\x00\x48" + pad(rel, 1024)          # relocator    -> $4800
    img += pad(k, 1024)                         # $5000-$53ff  -> $4c00
    img += pad(k[0xc000 - 0x5000:], 0x3d00)     # $c000-$fcff  -> $5000
    img += pad(inp, 384)                        # $fd00-$fe7f  -> $8d00
    img += pad(k[0xfe80 - 0x5000:], 384)        # $fe80-$ffff  -> $8e80
    img += pad(drv, 3456)                       # $9000-$9d7f
    img += pad(k[0x9d80 - 0x5000:], 640)        # $9d80-$9fff
    img += pad(k2[0x2000:], 0x2000)             # back $e000-  -> $a000
    img += pad(k2, 0x2000)                      # back $c000-  -> $c000
    comb = f"{bd}/kernal_combined.prg"
    open(comb, "wb").write(img)
    comp = f"{bd}/kernal_compressed.prg"
    run([EXOMIZER, "sfx", "0x4800", "-t128", "-Di_ram_exit=$3e", "-q",
         "-o", comp, comb])
    return bd, comb, comp


# GEOS 128 system disks in disks/ (not in this repository), most important
# first; what does not fit on the chosen format stays off
W128_SOURCES = ["1a_system", "2b_apps", "3a_write_utils",
                "3b_geospell"]
W128_DRIVERS = {"d64": "drv1541", "d71": "drv1571", "d81": "drv1581"}
# the boot files are replaced by our kernal; deskTop would load an input
# driver file over the built-in mouse + joystick driver
W128_SKIP = [b"GEOS", b"GEOS BOOT", b"GEOS KERNAL"]
W128_SKIP_TYPES = (10, 15)      # INPUT_DEVICE, INPUT_128
W128_RESERVE = 40               # blocks kept free for the user
# your own C128 programs in disks/extra/, right after our application so
# that they are on deskTop's first page: (file, names on a D64 or None for a
# CVT file, versions on the system disks that they replace)
W128_EXTRA = [
    ("geowrite128-4.cvt", None, [b"GEOWRITE 128"]),
    ("GEOS - Geopaint128 (19xx)(-).d64", [b"GeoPaint 128"], [b"GEOPAINT"]),
    ("GeoCalc 128.d64", [b"GEOCALC 128", b"New Car"], []),
]


def post_w128(bd, comp, fmt, base_only=False):
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import geosdisk as D
    path = f"{bd}/geos-w128{'-base' if base_only else ''}.{fmt}"
    if os.path.exists(path):
        os.remove(path)
    run([C1541, "-format", "geos-w128,w1", fmt, path, "-write", comp,
         "geos128"])
    disk = D.FORMATS[fmt](path)
    disk.make_geos_disk()
    # our own application first: it lands on deskTop's first page
    os.makedirs(os.path.join(ROOT, "build", "icons"), exist_ok=True)
    env = dict(os.environ, CC65_HOME=os.path.join(TOOLS, "cc65"))
    app = os.path.join(ROOT, "apps", "wic64")
    make_icon(os.path.join(app, "wic64.icon"),
              os.path.join(ROOT, "build", "icons", "wic64.raw"))

    bbs = os.path.join(ROOT, "apps", "bbs")
    make_icon(os.path.join(bbs, "bbs.icon"),
              os.path.join(ROOT, "build", "icons", "bbs.raw"))

    def own_app(name, res, *srcs, where=app):
        objdir = os.path.join(bd, "apps", name)
        os.makedirs(objdir, exist_ok=True)
        cvt = os.path.join(bd, name + ".cvt")
        r = subprocess.run([CL65, "-t", "geos-cbm", "-O", "--obj-path", objdir,
                            "-o", cvt, os.path.join(where, res)]
                           + [os.path.join(where, s) for s in srcs]
                           + [os.path.join(ROOT, "apps", "common", "wic64.s")],
                           cwd=ROOT, capture_output=True, text=True, env=env)
        if r.returncode:
            sys.exit(r.stdout + r.stderr)
        disk.add_cvt(cvt)

    own_app("wic64", "wic64appres.grc", "wic64app.c")
    own_app("bbs", "bbsres.grc", "bbs.c", "term.c", "xmodem.c", "vdc.s", where=bbs)
    seen = set()
    left_out = 0
    for name, files, replaces in [] if base_only else W128_EXTRA:
        src = os.path.join(ROOT, "disks", "extra", name)
        if not os.path.exists(src):
            continue
        if files is None:
            disk.add_cvt(src)
        else:
            D.copy_named(disk, src, files)
        seen.update(n.upper() for n in replaces)
    own_app("wictest", "wictestres.grc", "wictest.c")
    for s in [] if base_only else W128_SOURCES:
        src = os.path.join(ROOT, "disks", f"geos_128_2_0_{s}.d64")
        for name, e, h, body in D.read_d64_files(src):
            key = name.upper()
            if key in W128_SKIP or e[22] in W128_SKIP_TYPES or key in seen:
                continue
            seen.add(key)
            if D.blocks_of(e) > disk.free_blocks() - W128_RESERVE:
                left_out += 1
                continue
            if isinstance(body, list):
                disk.add_vlir(e, h, body)
            else:
                disk.add_seq(e, h, body)
    disk.check()
    disk.save()
    if left_out:
        print(f"{left_out} files did not fit on the {fmt}")
    return path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("variant", nargs="?", default="bsw")
    ap.add_argument("--drive", default="drv1541")
    ap.add_argument("--input", default=None,
                    help="input driver (default: u64input for u64, else joydrv)")
    ap.add_argument("--format", default="d71", choices=["d64", "d71", "d81"],
                    help="w128: disk image format, picks the disk driver")
    ap.add_argument("--base-only", action="store_true",
                    help="w128: only our kernal and application, no files "
                         "from the GEOS disks in disks/")
    ap.add_argument("-D", dest="defines", action="append", default=[],
                    help="extra ca65 define, e.g. -D testAutostart=1")
    a = ap.parse_args()
    v = a.variant
    if a.input is None:
        a.input = "u64input" if v == "u64" else "joydrv"
    if v == "bsw128":
        sys.exit("bsw128 is not supported by build.py; use the Makefile")
    if v == "w128":
        a.drive = W128_DRIVERS[a.format]
        if a.input == "joydrv":
            a.input = "u64input"
        bd, comb, comp = build_w128(a)
        disk = post_w128(bd, comp, a.format, a.base_only)
        print(f"*** w128: {comb}, {comp}, {disk}")
        return

    mk = open(os.path.join(ROOT, "Makefile")).read()
    srcs = makefile_list(mk, "KERNAL_SOURCES")
    srcs += makefile_list(mk, "KERNAL_SOURCES", "ifneq ($(VARIANT), bsw128)")
    if v == "wheels":
        srcs += makefile_list(mk, "KERNAL_SOURCES", "ifeq ($(VARIANT), wheels)")
    if v == "u64":
        srcs += makefile_list(mk, "KERNAL_SOURCES", "ifeq ($(VARIANT), u64)")

    bd = os.path.join("build", v)
    defs = ["-D", f"{v}=1", "-D", f"{a.drive}=1", "-D", f"{a.input}=1"]
    for d in a.defines:
        defs += ["-D", d]
    asflags = ["-I", "inc", "-I", ".", "--cpu", "6502X"]

    def assemble(src):
        obj = os.path.join(bd, src[:-2] + ".o")
        os.makedirs(os.path.dirname(obj), exist_ok=True)
        run([CA65] + defs + asflags + [src, "-o", obj])
        return obj

    objs = [assemble(s) for s in srcs]
    os.makedirs(os.path.join(bd, "kernal"), exist_ok=True)
    run([LD65, "-C", f"kernal/kernal_{v}.cfg"] + objs +
        ["-o", f"{bd}/kernal/kernal.bin", "-m", f"{bd}/kernal/kernal.map",
         "-Ln", f"{bd}/kernal/kernal.lab"])

    for d in DRIVES:
        o = assemble(f"drv/{d}.s")
        run([LD65, "-C", f"drv/{d}.cfg", o, "-o", f"{bd}/drv/{d}.bin"])
    for d in INPUTS:
        o = assemble(f"input/{d}.s")
        run([LD65, "-C", "input/inputdrv.cfg", o, "-o", f"{bd}/input/{d}.bin"])

    # same layout as the Makefile: kernal $5000-$8fff, drive $9000-$9d7f,
    # kernal $9d80-$fe7f, input driver $fe80-
    k = open(f"{bd}/kernal/kernal.bin", "rb").read()
    drv = open(f"{bd}/drv/{a.drive}.bin", "rb").read()
    inp = open(f"{bd}/input/{a.input}.bin", "rb").read()
    pad = lambda b, n: (b + bytes(n))[:n]
    img = b"\x00\x50" + pad(k[:16384], 16384) + pad(drv, 3456) + \
        pad(k[19840:], 24832) + inp
    comb = f"{bd}/kernal_combined.prg"
    open(comb, "wb").write(img)

    comp = f"{bd}/kernal_compressed.prg"
    run([EXOMIZER, "sfx", "0x5000", "-q", "-o", comp, comb])

    d64 = f"{bd}/geos.d64"
    tmpl = os.path.join(ROOT, "GEOS64.D64")
    if os.path.exists(tmpl):
        shutil.copy(tmpl, d64)
        run([C1541, d64, "-delete", "geos", "geoboot", "-write", comp, "geos"])
    else:
        if os.path.exists(d64):
            os.remove(d64)
        run([C1541, "-format", "geos,00", "d64", d64, "-write", comp, "geos"])
        if os.path.exists(os.path.join(ROOT, "desktop.cvt")):
            geoswrite(d64, os.path.join(ROOT, "desktop.cvt"))
    extra = getattr(sys.modules[__name__], "post_" + v, None)
    if extra:
        extra(bd, d64)
    print(f"*** {v}: {comb} ({len(img)} bytes), {comp}, {d64}")


if __name__ == "__main__":
    main()
