#!/usr/bin/env python3
"""Build pico-mac for the browser (WebAssembly) into web/dist/.

    python web/build.py            # incremental (objects cached in build-web/)
    python web/build.py --clean    # from scratch

Needs the Emscripten SDK (EMSDK, else ~/emsdk). Compiles the same sources as the desktop build
(BOARD_DESKTOP), with web/src/hal_web.cpp (EMU_WEB) in place of src/hal/desktop (SDL), then copies
the page (web/shell/) and the files a new visitor starts with (web/dist/seed/) next to the .js/.wasm.
"""
import concurrent.futures as cf
import glob
import gzip
import os
import shutil
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKETCH = os.path.join(REPO, "picomac")
WEB = os.path.join(REPO, "web")
OBJ = os.path.join(REPO, "build-web")
DIST = os.path.join(WEB, "dist")
SD = os.path.join(REPO, "sdcard", "picomac")

EMSDK = os.environ.get("EMSDK") or os.path.join(os.path.expanduser("~"), "emsdk")
EXE = ".exe" if os.name == "nt" else ""
EMCC = os.path.join(EMSDK, "upstream", "emscripten", "emcc" + EXE)
EMXX = os.path.join(EMSDK, "upstream", "emscripten", "em++" + EXE)

# Seeded into a new visitor's /sdcard/picomac (only where missing). Empty: Apple's ROMs and System
# disks are not ours to hand out, so the visitor brings them (the Files screen). Big files would go
# gzipped (Cloudflare serves at most 25 MiB a file); the page inflates them with DecompressionStream.
SEED = []


def sources():
    src = glob.glob(os.path.join(SKETCH, "src", "**", "*.c"), recursive=True)
    src += glob.glob(os.path.join(SKETCH, "src", "**", "*.cpp"), recursive=True)
    skip = [os.path.join("src", "hal", d) + os.sep for d in ("picocalc", "picocalc_rp2350", "desktop")]
    src = [s for s in src if not any(k in s for k in skip)]
    src += glob.glob(os.path.join(WEB, "src", "*.cpp"))
    os.makedirs(OBJ, exist_ok=True)
    ino = os.path.join(OBJ, "picomac_ino.cpp")
    with open(os.path.join(SKETCH, "picomac.ino"), "rb") as f:
        data = f.read()
    if not os.path.exists(ino) or open(ino, "rb").read() != data:
        with open(ino, "wb") as f:
            f.write(data)
    src.append(ino)
    return src


COMMON = [
    "-O2", "-pthread", "-funsigned-char",
    "-DBOARD_DESKTOP", "-DEMU_WEB", '-DEMU_DEFAULT_SD_DIR="/sdcard"',
    "-I" + SKETCH,
]
CFLAGS = COMMON + ["-w"]
CXXFLAGS = COMMON + ["-std=c++17", "-Wno-unused-parameter", "-fexceptions"]

LDFLAGS = [
    "-O2", "-pthread", "-fexceptions",
    "-sPROXY_TO_PTHREAD=1",
    "-sPTHREAD_POOL_SIZE=6",           # main(), OPFS's backend thread and the page's file thread (hal_web.cpp)
    "-sDEFAULT_PTHREAD_STACK_SIZE=1048576",
    "-sSTACK_SIZE=4194304",           # main() runs on a pthread with this stack (the cores keep line buffers on it)
    "-sINITIAL_MEMORY=268435456",     # shared memory can't grow cheaply: the Mac II's 8 MB of RAM, its VRAM and the pictures fit
    "-sALLOW_MEMORY_GROWTH=0",
    "-sMODULARIZE=1", "-sEXPORT_NAME=createPicoMac",
    "-sENVIRONMENT=web,worker",
    "-sFORCE_FILESYSTEM=1", "-sWASMFS=1",   # the card in OPFS (hal_web.cpp mountCard)
    "-sEXIT_RUNTIME=0",
    "-sEXPORTED_RUNTIME_METHODS=FS,HEAPU8,HEAP32,UTF8ToString,stringToUTF8,addRunDependency,removeRunDependency",
    "-sEXPORTED_FUNCTIONS=_main,_malloc,_free",
]


def obj_for(src):
    rel = os.path.relpath(src, REPO).replace(os.sep, "_").replace(":", "")
    return os.path.join(OBJ, rel + ".o")


def up_to_date(obj, src):
    """The object is newer than its source and every file it included (its .d file)."""
    dep = obj + ".d"
    if not os.path.exists(obj) or not os.path.exists(dep):
        return False
    t = os.path.getmtime(obj)
    with open(dep, encoding="utf-8", errors="replace") as f:
        text = f.read().replace("\\\n", " ")
    files = text.split(": ", 1)[1].split() if ": " in text else []
    for d in [src] + files:
        if not os.path.exists(d) or os.path.getmtime(d) > t:
            return False
    return True


def compile_one(src):
    obj = obj_for(src)
    if not os.environ.get("FORCE") and up_to_date(obj, src):
        return obj, None
    dep = ["-MMD", "-MF", obj + ".d"]   # the headers and included sources (macquadra_core.cpp includes macii_core.cpp)
    if src.endswith(".c"):
        cmd = [EMCC, "-c", src, "-o", obj] + CFLAGS + dep
    else:
        cmd = [EMXX, "-c", src, "-o", obj] + CXXFLAGS + dep
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        return obj, f"--- {src}\n{r.stdout}{r.stderr}"
    return obj, None


def seed():
    out = os.path.join(DIST, "seed")
    os.makedirs(out, exist_ok=True)
    listed = []
    for rel in SEED:
        src = os.path.join(SD, rel.replace("/", os.sep))
        if not os.path.exists(src):
            print("seed: missing", src)
            continue
        size = os.path.getsize(src)
        gz = size > 1024 * 1024
        name = rel.replace("/", "_") + (".gz" if gz else "")
        dst = os.path.join(out, name)
        if not os.path.exists(dst) or os.path.getmtime(dst) < os.path.getmtime(src):
            if gz:
                with open(src, "rb") as fi, gzip.open(dst, "wb", 9) as fo:
                    shutil.copyfileobj(fi, fo)
            else:
                shutil.copy2(src, dst)
        listed.append('{"path":"picomac/%s","url":"seed/%s","gz":%s,"size":%d}' % (rel, name, "true" if gz else "false", size))
    with open(os.path.join(out, "index.json"), "w") as f:
        f.write("[" + ",".join(listed) + "]")


def main():
    if "--clean" in sys.argv and os.path.isdir(OBJ):
        shutil.rmtree(OBJ)
    if not os.path.exists(EMXX):
        sys.exit(f"em++ not found at {EMXX} (set EMSDK)")
    srcs = sources()
    # a header changed: rebuild everything older than it (cheap and safe)
    heads = glob.glob(os.path.join(SKETCH, "**", "*.h"), recursive=True)
    newest_h = max(os.path.getmtime(h) for h in heads)
    for s in srcs:
        o = obj_for(s)
        if os.path.exists(o) and os.path.getmtime(o) < newest_h:
            os.remove(o)
    errors, objs = [], []
    with cf.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as ex:
        for obj, err in ex.map(compile_one, srcs):
            objs.append(obj)
            if err:
                errors.append(err)
    if errors:
        print("\n".join(errors[:8]))
        sys.exit(f"{len(errors)} file(s) failed to compile")
    os.makedirs(DIST, exist_ok=True)
    rsp = os.path.join(OBJ, "objs.rsp")              # Windows' command line is too short for every object
    with open(rsp, "w") as f:
        f.write("\n".join('"%s"' % o.replace("\\", "/") for o in objs))
    out = os.path.join(DIST, "picomac.js")
    r = subprocess.run([EMXX, "@" + rsp, "-o", out] + LDFLAGS, capture_output=True, text=True)
    sys.stdout.write(r.stdout + r.stderr)
    if r.returncode != 0:
        sys.exit("link failed")
    for f in glob.glob(os.path.join(WEB, "shell", "*")):
        if os.path.isfile(f):
            shutil.copy2(f, DIST)
    seed()
    print("OK ->", DIST)


if __name__ == "__main__":
    main()
