#!/usr/bin/env python3
"""
Simple, robust 3GX builder that replaces the old Makefile.
- No hardcoded paths: reads everything from build_config.toml (or CLI/env).
- Incremental builds via a tiny JSON cache (mtime + flags signature).
- Cross-platform (Windows/MSYS2, WSL, macOS, Linux). Only requirement: devkitARM in PATH or DEVKITARM/DEVKITPRO set.
Usage:
  python build.py            # normal build
  python build.py clean      # delete /build and outputs
  python build.py -v         # verbose
  python build.py --config path/to/build_config.toml
Environment:
  DEVKITARM, DEVKITPRO used if set (recommended via devkitPro installer). Tools must be in PATH or resolved via DEVKITARM.
"""
import argparse, hashlib, json, os, shlex, subprocess, sys, time
from pathlib import Path

try:
    import tomllib  # 3.11+
except ModuleNotFoundError:
    # tiny fallback; try tomli if available
    try:
        import tomli as tomllib  # type: ignore
    except ModuleNotFoundError:
        print("ERROR: Python 3.11+ required or install 'tomli' for TOML parsing.", file=sys.stderr)
        sys.exit(2)

HERE = Path(__file__).resolve().parent
DEFAULT_CONFIG = HERE / "build_config.toml"
CACHE_FILE = HERE / ".buildcache.json"

def read_config(path: Path) -> dict:
    if not path.exists():
        print(f"ERROR: config not found: {path}", file=sys.stderr)
        sys.exit(2)
    with path.open("rb") as f:
        return tomllib.load(f)

def which(cand: str) -> str | None:
    # Respect absolute paths
    p = Path(cand)
    if p.exists():
        return str(p)
    # PATH lookup
    exts = [""] + (os.environ.get("PATHEXT","").split(os.pathsep) if os.name == "nt" else [])
    for folder in os.environ.get("PATH","").split(os.pathsep):
        f = Path(folder) / cand
        for e in exts:
            fe = f.with_suffix(e) if e and not f.suffix else f
            if fe.exists():
                return str(fe)
    return None

def resolve_toolchain(cfg: dict, verbose: bool) -> dict:
    # Prefer explicit tool paths in config; otherwise check PATH then DEVKITARM/bin
    tools = {}
    names = {
        "cxx": "arm-none-eabi-g++",
        "cc":  "arm-none-eabi-gcc",
        "as":  "arm-none-eabi-gcc",   # we drive assembler via gcc -x assembler-with-cpp
        "ar":  "arm-none-eabi-ar",
        "objcopy": "arm-none-eabi-objcopy",
    }
    # prepend DEVKITARM/bin to PATH if present
    dkarm = os.environ.get("DEVKITARM")
    if dkarm:
        binpath = str(Path(dkarm) / "bin")
        os.environ["PATH"] = binpath + os.pathsep + os.environ.get("PATH","")
        if verbose: print(f"[toolchain] DEVKITARM/bin prepended: {binpath}")
    for k, exe in names.items():
        override = cfg.get("tools", {}).get(k)
        cand = override or exe
        found = which(cand)
        if not found:
            print(f"ERROR: tool not found: {cand}. Ensure devkitARM is installed and in PATH.", file=sys.stderr)
            sys.exit(2)
        tools[k] = found
        if verbose: print(f"[toolchain] {k}: {found}")
    # 3gxtool (not part of devkitARM)
    gxt = cfg.get("tools", {}).get("gxtool", "3gxtool")
    gxt_res = which(gxt)
    tools["gxtool"] = gxt_res  # can be None until packaging step; we'll check later
    if verbose: print(f"[toolchain] 3gxtool: {gxt_res or '(not found yet)'}")
    return tools

def normpaths(lst: list[str]) -> list[str]:
    return [str(Path(p)) for p in lst]

def signature_blob(*parts: list[str]) -> str:
    h = hashlib.sha1()
    for part in parts:
        h.update(part.encode("utf-8"))
        h.update(b"\0")
    return h.hexdigest()

def load_cache() -> dict:
    if CACHE_FILE.exists():
        try:
            return json.loads(CACHE_FILE.read_text(encoding="utf-8"))
        except Exception:
            return {}
    return {}

def save_cache(cache: dict) -> None:
    CACHE_FILE.write_text(json.dumps(cache, indent=2), encoding="utf-8")

def glob_sources(globs: list[str]) -> list[Path]:
    files: list[Path] = []
    for pat in globs:
        files.extend(Path(".").glob(pat))
    # unique & sort
    files = sorted({p.resolve() for p in files if p.is_file()})
    return files

def run(cmd: list[str], verbose: bool) -> None:
    if verbose:
        print(" ".join(shlex.quote(c) for c in cmd))
    res = subprocess.run(cmd)
    if res.returncode != 0:
        sys.exit(res.returncode)

def compile_one(tools: dict, src: Path, obj: Path, includes: list[str], defines: list[str], cflags: list[str], is_cxx: bool, verbose: bool) -> None:
    obj.parent.mkdir(parents=True, exist_ok=True)
    base = [tools["cxx"] if is_cxx else tools["cc"]]
    # Dependency files are nice but optional; we do timestamp-based for simplicity
    cmd = base + ["-c", str(src), "-o", str(obj)] + sum([["-I", inc] for inc in includes], []) + sum([["-D", d] for d in defines], []) + cflags
    run(cmd, verbose)

def assemble_one(tools: dict, src: Path, obj: Path, includes: list[str], defines: list[str], asflags: list[str], verbose: bool) -> None:
    obj.parent.mkdir(parents=True, exist_ok=True)
    cmd = [tools["as"], "-x", "assembler-with-cpp", "-c", str(src), "-o", str(obj)] + sum([["-I", inc] for inc in includes], []) + sum([["-D", d] for d in defines], []) + asflags
    run(cmd, verbose)

def link_elf(tools: dict, objects: list[Path], out_elf: Path, libdirs: list[str], libs: list[str], ldflags: list[str], verbose: bool) -> None:
    out_elf.parent.mkdir(parents=True, exist_ok=True)
    cxx = tools["cxx"]
    cmd = [cxx] + ldflags + [str(p) for p in objects] + sum([["-L", d] for d in libdirs], []) + libs + ["-o", str(out_elf)]
    run(cmd, verbose)

def make_3gx(tools: dict, elf: Path, plginfo: Path, out_3gx: Path, verbose: bool) -> None:
    if tools.get("gxtool") is None:
        print("WARNING: 3gxtool not found in PATH. Skipping .3gx packaging.", file=sys.stderr)
        return
    out_3gx.parent.mkdir(parents=True, exist_ok=True)
    cmd = [tools["gxtool"], str(elf), str(plginfo), str(out_3gx)]
    run(cmd, verbose)

def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("action", nargs="?", choices=["build","clean"], default="build")
    ap.add_argument("-v","--verbose", action="store_true")
    ap.add_argument("--config", default=str(DEFAULT_CONFIG))
    args = ap.parse_args(argv)

    cfg = read_config(Path(args.config))

    # Basic paths & names
    out_name = cfg.get("output_name", "plugin")
    out_dir  = Path(cfg.get("build_dir", "build")).resolve()
    out_elf  = out_dir / f"{out_name}.elf"
    out_3gx  = out_dir / f"{out_name}.3gx"
    plginfo  = Path(cfg.get("plginfo", "plugin/plginfo.bin")).resolve()

    if args.action == "clean":
        if out_dir.exists():
            print(f"Removing {out_dir}")
            import shutil
            shutil.rmtree(out_dir)
        if CACHE_FILE.exists():
            CACHE_FILE.unlink()
        return 0

    tools = resolve_toolchain(cfg, args.verbose)

    src_globs = cfg.get("sources_glob", ["Sources/**/*.cpp", "Sources/**/*.c", "Sources/**/*.s"])
    srcs = glob_sources(src_globs)
    if not srcs:
        print("ERROR: No sources matched. Check 'sources_glob' in build_config.toml", file=sys.stderr)
        return 2

    includes = normpaths(cfg.get("include_dirs", ["include"]))
    libdirs  = normpaths(cfg.get("lib_dirs", []))
    libs     = cfg.get("libs", [])
    defines  = cfg.get("defines", [])
    cxxflags = cfg.get("cxxflags", [])
    cflags   = cfg.get("cflags", [])
    asflags  = cfg.get("asflags", [])
    ldflags  = cfg.get("ldflags", [])

    # Reasonable defaults for 3DS ARM11
    default_cpu = ["-march=armv6k", "-mtune=mpcore", "-mfloat-abi=hard", "-mtp=soft"]
    default_common = ["-ffunction-sections","-fdata-sections","-O2","-g0"]
    default_cxx = ["-fno-exceptions","-fno-rtti"]
    default_ld  = ["-Wl,--gc-sections"]
    # Only extend if not already specified
    if not any(flag.startswith("-march=") for flag in (cxxflags+cflags+asflags)):
        cxxflags = default_cpu + default_common + default_cxx + cxxflags
        cflags   = default_cpu + default_common + cflags
        asflags  = default_cpu + asflags
    if not any(f.startswith("-Wl,") for f in ldflags):
        ldflags = default_cpu + default_ld + ldflags

    obj_dir = out_dir / "obj"

    cache = load_cache()
    newcache = {}

    objects: list[Path] = []
    for src in srcs:
        rel = src.relative_to(Path(".").resolve())
        obj = obj_dir / rel.with_suffix(".o")
        # Decide compile or assemble
        ext = src.suffix.lower()
        is_cxx = ext in [".cpp", ".cxx", ".cc"]
        is_c   = ext == ".c"
        is_s   = ext in [".s", ".asm"]

        # build signature: tool + flags + includes + defines + file mtime
        sig = signature_blob(
            tools["cxx" if is_cxx else "cc"],
            " ".join(cxxflags if is_cxx else cflags),
            " ".join(asflags),
            " ".join(includes),
            " ".join(defines),
            str(src.stat().st_mtime_ns),
        )
        prev = cache.get(str(src))
        need = (prev != sig) or (not obj.exists()) or (obj.stat().st_mtime_ns < src.stat().st_mtime_ns)
        if need:
            print(f"Compiling {rel}")
            if is_s:
                assemble_one(tools, src, obj, includes, defines, asflags, args.verbose)
            else:
                compile_one(tools, src, obj, includes, defines, cxxflags if is_cxx else cflags, is_cxx, args.verbose)
        objects.append(obj)
        newcache[str(src)] = sig

    # Link
    print(f"Linking {out_elf.name}")
    link_elf(tools, objects, out_elf, libdirs, libs, ldflags, args.verbose)

    # Package .3gx (optional if PLGINFO missing)
    if plginfo.exists():
        out_3gx = out_3gx  # keep same
        print(f"Packaging {out_3gx.name}")
        make_3gx(tools, out_elf, plginfo, out_3gx, args.verbose)
    else:
        print(f"NOTE: plginfo not found at '{plginfo}'. Skipping .3gx packaging.")

    save_cache(newcache)
    print("Build: OK")
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
