#!/usr/bin/env python3
import argparse
import hashlib
from pathlib import Path

import yaml

# === Repo layout
REPO  = Path(__file__).resolve().parents[1]
BASES = REPO / "patch" / "bases"
ADDRS = REPO / "addresses"
BUILD = REPO / "build"

# Fates code.bin runtime base
CODE_BASE   = 0x00100000
STOLEN_SIZE = 8  # bytes we steal from each site (2 ARM instructions)

# Keystone (ARM – Fates code.bin is ARM, not Thumb)
try:
    from keystone import Ks, KS_ARCH_ARM, KS_MODE_ARM
except Exception as e:
    raise SystemExit(
        "[x] keystone-engine not available.\n"
        "    Install with:  python -m pip install keystone-engine pyyaml\n"
        f"    Import error: {e}"
    )


def ks_arm() -> Ks:
    return Ks(KS_ARCH_ARM, KS_MODE_ARM)


# --- SHA-1 helpers --------------------------------------------------------


def sha1(p: Path) -> str:
    h = hashlib.sha1()
    with open(p, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def read_expected(region: str) -> str:
    t = (BASES / f"{region}.sha1").read_text().splitlines()
    for line in t:
        line = line.strip()
        if line and not line.startswith("#"):
            return line.lower()
    raise SystemExit(f"[x] Missing base sha1 in {BASES / (region + '.sha1')}")


# --- AOB with byte/nibble wildcards --------------------------------------


def _hex_nibble(ch: str):
    if ch == "?":
        return None
    if ch in "0123456789abcdefABCDEF":
        return int(ch, 16)
    raise ValueError("bad nibble")


def _parse_token(tok: str):
    tok = tok.strip()
    if len(tok) != 2:
        raise ValueError("token must be 2 chars")
    hi = _hex_nibble(tok[0])
    lo = _hex_nibble(tok[1])
    val = 0
    msk = 0
    if hi is None:
        if lo is None:
            return 0x00, 0x00        # '??'
        val |= lo
        msk |= 0x0F
        return val, msk             # '?X'
    val |= (hi << 4)
    msk |= 0xF0                     # 'X?'
    if lo is None:
        return val, msk
    val |= lo
    msk |= 0x0F
    return val, msk                 # 'XY'


def aob_to_pat_mask(aob: str):
    toks = [t for t in aob.replace("\t", " ").split(" ") if t]
    pat = bytearray()
    msk = bytearray()
    for t in toks:
        v, m = _parse_token(t)
        pat.append(v)
        msk.append(m)
    return bytes(pat), bytes(msk)


def scan(buf: bytes, pat: bytes, msk: bytes) -> int:
    n = len(pat)
    for i in range(0, len(buf) - n + 1):
        w = buf[i:i + n]
        ok = True
        for a, b, m in zip(w, pat, msk):
            if m and ((a & m) != (b & m)):
                ok = False
                break
        if ok:
            return i
    return -1


# --- find code cave -------------------------------------------------------


def find_code_cave(buf: bytearray, need: int) -> int:
    run = 0
    for i in range(len(buf) - 1, -1, -1):
        if buf[i] in (0x00, 0xFF):
            run += 1
            if run >= need:
                return i
        else:
            run = 0
    return -1


# --- asm helpers ----------------------------------------------------------


def asm_abs_jump_va(to_va: int, origin_off: int) -> bytes:
    """
    Assemble an absolute ARM jump using:
        ldr   pc, [pc, #-4]
        .word to_va

    Total size = 8 bytes, suitable for ARM code.
    origin_off is a file offset; add CODE_BASE to get the runtime VA.
    """
    ks = ks_arm()
    origin_va = CODE_BASE + origin_off
    asm = (
        "ldr pc, [pc, #-4]\n"
        f".word 0x{to_va:08X}\n"
    )
    enc, _ = ks.asm(asm, origin_va)
    b = bytes(bytearray(enc or []))
    if len(b) != 8:
        raise SystemExit(f"[x] detour assembled to {len(b)} bytes, expected 8")
    return b


# --- main -----------------------------------------------------------------


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--region", default="na_v11")
    ap.add_argument(
        "--in",
        dest="infile",
        default=str(REPO / "original" / "code.bin"),
    )
    ap.add_argument(
        "--out",
        dest="outfile",
        default=str(BUILD / "code_mod.bin"),
    )
    # IMPORTANT: write header where plugin includes it:
    ap.add_argument(
        "--gen",
        dest="genhdr",
        default=str(REPO / "plugin" / "include" / "hooks_table.hpp"),
    )
    ap.add_argument(
        "--patch-sites",
        action="store_true",
        help="Also patch hook sites to jump to trampolines (inline/legacy mode).",
    )
    args = ap.parse_args()

    base = Path(args.infile)
    out = Path(args.outfile)
    gen = Path(args.genhdr)
    BUILD.mkdir(parents=True, exist_ok=True)

    exp = read_expected(args.region)
    act = sha1(base)
    if act.lower() != exp:
        raise SystemExit(f"[x] SHA1 mismatch:\n  expected {exp}\n  actual   {act}")

    y = yaml.safe_load((ADDRS / f"{args.region}.yml").read_text())
    hooks = y.get("hooks", {})

    buf = bytearray(base.read_bytes())
    mod = bytearray(buf)

    # Each trampoline gets a fixed-size slot in the code cave
    per_tramp = 0x60

    # Count how many hook *sites* (addr can be a list).
    num_sites = 0
    for spec in hooks.values():
        addrs = spec.get("addr")
        if isinstance(addrs, list):
            num_sites += len(addrs)
        elif addrs is not None:
            num_sites += 1
        elif spec.get("aob"):
            num_sites += 1

    arena_need = per_tramp * max(1, num_sites)
    arena_off = find_code_cave(mod, arena_need)

    if arena_off < 0:
        arena_off = len(mod)
        mod += bytearray(b"\x00" * arena_need)

    cur = arena_off

    rows = []
    hook_ids = []

    def scan_aob(a: str) -> int:
        pat, msk = aob_to_pat_mask(a)
        return scan(buf, pat, msk)

    # Build trampolines and kHooks rows.
    for name, spec in hooks.items():
        aob = spec.get("aob")
        addrs = spec.get("addr")
        guard = spec.get("guard", None)

        # addr can be a single value or a list of values
        if isinstance(addrs, list):
            targets = addrs
        else:
            targets = [addrs] if addrs is not None else [None]

        for addr in targets:
            # Resolve site either from addr or AOB
            if addr is not None:
                try:
                    site = int(str(addr), 0)
                except Exception:
                    site = None
            elif aob:
                site = scan_aob(aob)
            else:
                site = None

            if site is None or site < 0:
                print(f"[skip] {name} (no match)")
                continue

            # Guard snapshot
            guard_len = 0
            guard_bytes = b""
            if guard:
                gpat, gmsk = aob_to_pat_mask(guard)
                guard_len = len(gpat)
                guard_bytes = bytes(buf[site:site + guard_len])

            # Steal first STOLEN_SIZE bytes from site
            stolen = bytes(mod[site:site + STOLEN_SIZE])

            # Trampoline: [stolen] + absolute jump back to resume
            tramp_off = cur
            tramp = bytearray()
            tramp += stolen

            resume_off = site + STOLEN_SIZE
            resume_va = CODE_BASE + resume_off
            tramp += asm_abs_jump_va(resume_va, tramp_off + len(tramp))

            end_tramp = tramp_off + len(tramp)
            mod[tramp_off:end_tramp] = tramp
            cur += per_tramp

            # Optionally patch the site to jump to the trampoline
            if args.patch_sites:
                tramp_va = CODE_BASE + tramp_off
                detour = asm_abs_jump_va(tramp_va, site)
                if len(detour) != STOLEN_SIZE:
                    raise SystemExit(
                        f"[x] detour size {len(detour)} != STOLEN_SIZE {STOLEN_SIZE}"
                    )
                mod[site:site + STOLEN_SIZE] = detour

            rows.append(
                {
                    "name": name,
                    "site_off": site,
                    "tramp_off": tramp_off,
                    "guard": guard_bytes,
                    "guard_len": guard_len,
                }
            )
            hook_ids.append(name)
            print(f"[ok] {name}: site=0x{site:X} tramp=0x{tramp_off:X}")

    # Write patched code
    out.write_bytes(mod)

    # Emit header the plugin includes
    with open(gen, "w", newline="\n") as f:
        f.write("// Auto-generated. Do not edit.\n#pragma once\n#include <stdint.h>\n\n")
        f.write(f"#define CODE_BASE 0x{CODE_BASE:08X}u\n\n")

        # Deduplicate names for the enum, but keep kNumHooks = total rows.
        unique_names = []
        for n in hook_ids:
            if n not in unique_names:
                unique_names.append(n)

        f.write("enum HookId {\n")
        for i, n in enumerate(unique_names):
            sep = "," if i < len(unique_names) - 1 else ""
            f.write(f"  HookId_{n}{sep}\n")
        f.write("};\n\n")

        f.write(
            "struct HookEntry { const char* name; uint32_t site_off; "
            "uint32_t tramp_off; uint8_t guard[8]; uint8_t guard_len; };\n"
        )
        f.write(f"static constexpr uint32_t kNumHooks = {len(rows)};\n")
        f.write("static constexpr HookEntry kHooks[] = {\n")
        for r in rows:
            g = r["guard"]
            pad = [0] * 8
            for i, b in enumerate(g[:8]):
                pad[i] = b
            ghex = ", ".join(f"0x{b:02X}" for b in pad)
            f.write(
                f'  {{"{r["name"]}", 0x{r["site_off"]:X}, 0x{r["tramp_off"]:X}, '
                f'{{ {ghex} }}, {r["guard_len"]} }},\n'
            )
        f.write("};\n")

    print(f"[ok] Wrote {out}")
    print(f"[ok] Wrote header {gen}")


if __name__ == "__main__":
    main()
