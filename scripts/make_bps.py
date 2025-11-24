#!/usr/bin/env python3
# Used to convert raw code.bin edits to BPS for use in luma
import argparse, hashlib, os, re, shutil, struct, subprocess, sys
from pathlib import Path
import yaml

# --- Config constants
REPO = Path(__file__).resolve().parents[1]
BASES = REPO / "patch" / "bases"
ADDR_DIR = REPO / "addresses"
BUILD = REPO / "build"
PATCH_DIR = REPO / "patch"
DIST = REPO / "dist"
TITLEID = "<TitleID>"  # fill later
EXEFS_DIST = DIST / TITLEID / "exefs"

# 10-byte inline detour: movw r12, imm16; movt r12, imm16; bx r12
ASM_DETOUR_TEMPLATE = """
    .thumb
    .syntax unified
    movw r12, #{lo}
    movt r12, #{hi}
    bx   r12
"""

NOP16 = b"\x00\xBF"  # 16-bit Thumb NOP
NOP32 = b"\xC0\x46\xC0\x46"  # two 16-bit NOPs (keeps alignment)

def sha1(path: Path) -> str:
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1<<20), b""):
            h.update(chunk)
    return h.hexdigest()

def read_expected_sha1(region: str) -> str:
    p = BASES / f"{region}.sha1"
    s = p.read_text().strip()
    # pick first non-comment line
    for line in s.splitlines():
        line=line.strip()
        if not line or line.startswith("#"): continue
        return line.lower()
    raise SystemExit(f"[x] No SHA1 found in {p}")

def load_map(region: str) -> dict:
    p = ADDR_DIR / f"{region}.yml"
    return yaml.safe_load(p.read_text())

def parse_hex(s: str) -> bytes:
    s = s.replace(" ", "")
    return bytes.fromhex(s)

def aob_to_bytes_mask(aob: str):
    # "12 34 ?? 9A" -> pattern=b"\x12\x34\x00\x9A", mask=b"\xFF\xFF\x00\xFF"
    toks = aob.strip().split()
    pat = bytearray()
    msk = bytearray()
    for t in toks:
        if t == "??" or re.fullmatch(r"\?\?", t):
            pat.append(0x00); msk.append(0x00)
        else:
            pat.append(int(t, 16)); msk.append(0xFF)
    return bytes(pat), bytes(msk)

def scan_aob(buf: bytes, pat: bytes, msk: bytes) -> int:
    # returns offset or -1
    n = len(pat)
    for i in range(0, len(buf)-n+1):
        window = buf[i:i+n]
        ok = True
        for a,b,m in zip(window, pat, msk):
            if m and a != b:
                ok = False; break
        if ok: return i
    return -1

def find_code_cave(buf: bytearray, need: int, prefer_from: int = None) -> int:
    # find a run of 0x00 or 0xFF of at least 'need' bytes
    # naive linear scan; prefer searching from the end
    start = len(buf)-need if prefer_from is None else max(0, min(len(buf)-need, prefer_from))
    for i in range(start, -1, -1):
        chunk = buf[i:i+need]
        if all(x in (0x00, 0xFF) for x in chunk):
            return i
    return -1

def assemble_detour(lo16: int, hi16: int) -> bytes:
    try:
        from keystone import Ks, KS_ARCH_ARM, KS_MODE_THUMB
    except Exception as e:
        raise SystemExit(
            "[x] keystone-engine not available. Install with: pip install keystone-engine\n"
            f"    Import error: {e}"
        )
    ks = Ks(KS_ARCH_ARM, KS_MODE_THUMB)
    asm = ASM_DETOUR_TEMPLATE.format(lo=lo16, hi=hi16)
    encoding, _ = ks.asm(asm)
    b = bytes(bytearray(encoding))
    if len(b) != 10:
        raise SystemExit(f"[x] Detour assembled to {len(b)} bytes, expected 10.")
    return b

def bl_trampoline(from_off: int, to_off: int) -> bytes:
    # Assemble a 32-bit Thumb BL from 'from_off' to 'to_off' (file offsets).
    # Just use Keystone here for correctness.
    try:
        from keystone import Ks, KS_ARCH_ARM, KS_MODE_THUMB
    except Exception as e:
        raise SystemExit(
            "[x] keystone-engine not available. Install with: pip install keystone-engine\n"
            f"    Import error: {e}"
        )
    ks = Ks(KS_ARCH_ARM, KS_MODE_THUMB)
    # PC at time of BL is (addr_of_bl + 4) in Thumb
    asm = f".thumb\nbl #{to_off - (from_off + 4)}"
    encoding, _ = ks.asm(asm, from_off | 1)  # set T-bit for keystone origin
    b = bytes(bytearray(encoding))
    if len(b) != 4:
        raise SystemExit(f"[x] BL assembled to {len(b)} bytes, expected 4.")
    return b

def flips_create_bps(base: Path, mod: Path, outbps: Path):
    exe = shutil.which("flips") or shutil.which("flips.exe")
    if not exe:
        raise SystemExit("[x] FLIPS not found in PATH. Install Floating IPS and add it to PATH.")
    # flips --create --bps base target patch
    cmd = [exe, "--create", "--bps", str(base), str(mod), str(outbps)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout); print(r.stderr, file=sys.stderr)
        raise SystemExit("[x] FLIPS failed to create BPS.")

def flips_apply_bps(base: Path, bps: Path, out: Path):
    exe = shutil.which("flips") or shutil.which("flips.exe")
    cmd = [exe, "--apply", str(bps), str(base), str(out)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout); print(r.stderr, file=sys.stderr)
        raise SystemExit("[x] FLIPS failed to apply BPS.")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--region", default="na_v11")
    ap.add_argument("--base", default=str(REPO / "code.bin"))
    args = ap.parse_args()

    base = Path(args.base)
    if not base.exists():
        raise SystemExit(f"[x] Missing base code.bin at {base}")

    expected = read_expected_sha1(args.region)
    actual = sha1(base)
    if actual.lower() != expected.lower():
        raise SystemExit(f"[x] code.bin SHA1 mismatch.\n  expected {expected}\n  actual   {actual}\n")

    m = load_map(args.region)
    hooks = m.get("hooks", {})

    buf = bytearray(base.read_bytes())
    mod = bytearray(buf)  # start with base

    arena_need_each = 0x40  # bytes per trampoline (roomy)
    arena_total = arena_need_each * max(1, len(hooks))
    arena_off = find_code_cave(mod, arena_total, prefer_from=None)
    if arena_off < 0:
        raise SystemExit("[x] No suitable code cave found for trampolines.")
    arena_cur = arena_off

    # record-keeping for plugin (optional)
    hook_records = []

    for name, spec in hooks.items():
        addr = spec.get("addr", None)
        aob = spec.get("aob", None)
        guard = spec.get("guard", None)

        site_off = None
        if addr is not None:
            site_off = int(str(addr), 0)  # allow 0x...
        elif aob:
            pat, msk = aob_to_bytes_mask(aob)
            site_off = scan_aob(buf, pat, msk)
            if site_off < 0:
                print(f"[!] AOB failed for {name}, skipping…")
                continue
        else:
            print(f"[!] No addr/aob for {name}, skipping…")
            continue

        # Guard check on the base
        if guard:
            gpat, gmsk = aob_to_bytes_mask(guard)
            head = buf[site_off:site_off+len(gpat)]
            ok = True
            for a,b,m in zip(head, gpat, gmsk):
                if m and a != b: ok=False; break
            if not ok:
                print(f"[!] Guard mismatch at {name} (offset 0x{site_off:X}), skipping…")
                continue

        # Stolen bytes (10) — always take 10 for consistency
        stolen = bytes(mod[site_off:site_off+10])

        # Make trampoline:
        tramp_off = arena_cur
        arena_cur += arena_need_each

        # Trampoline does: [stolen bytes] ; branch back to resume
        resume = site_off + 10
        tramp = bytearray()
        tramp += stolen
        # pad to 4-align, then emit a wide branch back
        if len(tramp) & 1: tramp += NOP16
        if (len(tramp) & 3): tramp += NOP16
        # Use BL to get back? Always want an unconditional B.W
        try:
            from keystone import Ks, KS_ARCH_ARM, KS_MODE_THUMB
            ks = Ks(KS_ARCH_ARM, KS_MODE_THUMB)
            # pc for branch origin is tramp_off | 1
            delta = resume - (tramp_off + len(tramp) + 4)  # PC=origin+4
            asm = f".thumb\nb.w #{delta}"
            enc, _ = ks.asm(asm, tramp_off | 1)
            tramp += bytes(bytearray(enc))
        except Exception as e:
            raise SystemExit(f"[x] Keystone needed for trampoline branch: {e}")

        # Write trampoline to arena
        mod[tramp_off:tramp_off+len(tramp)] = tramp

        # Now write 10-byte inline detour at site with target=0 placeholder.
        detour = assemble_detour(0, 0)  # plugin will retarget imm later
        mod[site_off:site_off+10] = detour

        # If ever overwrote more than 10 bytes originally (shouldn't), nop the remainder
        # If less (never), already handled.
        hook_records.append({
            "name": name,
            "site_off": site_off,
            "tramp_off": tramp_off,
            "resume_off": resume,
            "stolen_len": 10,
        })

        print(f"[ok] Hook {name}: site 0x{site_off:X} -> tramp 0x{tramp_off:X}")

    BUILD.mkdir(parents=True, exist_ok=True)
    out_mod = BUILD / "code_mod.bin"
    out_mod.write_bytes(mod)

    PATCH_DIR.mkdir(exist_ok=True, parents=True)
    out_bps = PATCH_DIR / "code.bps"
    flips_create_bps(base, out_mod, out_bps)

    # Verify by reapplying
    verify = BUILD / "code_mod_verify.bin"
    flips_apply_bps(base, out_bps, verify)
    if verify.read_bytes() != out_mod.read_bytes():
        raise SystemExit("[x] BPS roundtrip mismatch!")

    # Optional: stage into dist
    EXEFS_DIST.mkdir(parents=True, exist_ok=True)
    shutil.copy2(out_bps, EXEFS_DIST / "code.bps")
    print(f"[ok] Wrote {out_bps}")
    print(f"[ok] Staged to {EXEFS_DIST / 'code.bps'}")

if __name__ == "__main__":
    main()
