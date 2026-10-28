#!/usr/bin/env python3
"""VKMT: comprehensive x18→x28 binary patcher for aarch64 PE binaries.

Unlike vkmt-fix-x18-tls.py (which only patches [x18,...] memory operands),
this patches ALL x18 references: mov, add, str, cmp, cbz, stlxr, stp, etc.

On macOS ARM64, x18 is reserved by the platform. Wine uses x28 for TEB.
FEX's PE code compiled with LLVM's Windows-aarch64 target uses x18 for TEB
access. This binary patcher rewrites all x18 register references to x28.
"""
import os, re, struct, subprocess, sys

BIN = os.environ.get("LLVM_MINGW_BIN", os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
      "..", "llvm-mingw-20250514-ucrt-macos-universal", "bin"))

REG_FIELDS = [
    (0, 5),    # Rd/Rt: bits [4:0]
    (5, 5),    # Rn: bits [9:5]
    (10, 5),   # Rt2/Ra: bits [14:10]
    (16, 5),   # Rm/Rs: bits [20:16]
]


def assemble_words(texts):
    """Assemble each instruction text with clang; returns the encodings (None when it fails)."""
    import tempfile
    res = [None] * len(texts)
    with tempfile.TemporaryDirectory() as d:
        src, obj = os.path.join(d, "a.s"), os.path.join(d, "a.o")
        open(src, "w").write("\n".join(texts) + "\n")
        r = subprocess.run([f"{BIN}/clang", "-target", "aarch64-windows", "-c", "-x", "assembler", src, "-o", obj],
                           capture_output=True, text=True)
        if r.returncode:
            return None
        out = subprocess.run([f"{BIN}/llvm-objdump", "-d", obj], capture_output=True, text=True).stdout
        words = [int(m.group(1), 16) for m in re.finditer(r"^\s*[0-9a-f]+:\s+([0-9a-f]{8})\s", out, re.M)]
        if len(words) != len(texts):
            return None
        return words

def choose_patches(sites):
    """Return {addr: new word}. The register fields are found by assembling the original text
    with x18/w18 replaced by x28/w28: a 5-bit field equal to 18 is not necessarily a register
    (the immediate of ldr/str with an unsigned offset can be 18 too)."""
    texts = [re.sub(r"\b([xw])18\b", r"\g<1>28", d) for _, _, d in sites]
    words = assemble_words(texts)
    out = {}
    if words is None:
        # a site that does not assemble (pc-relative operands) breaks the batch: do them one by one
        words = []
        for t in texts:
            w = assemble_words([t])
            words.append(w[0] if w else None)
    for (addr, raw, d), nw in zip(sites, words):
        if nw is None and re.match(r"\s*(cbz|cbnz|tbz|tbnz)\b", d) and (raw & 31) == 18:
            nw = (raw & ~31) | 28   # compare-and-branch: only Rt is a register, the rest is the target
        if nw is None:
            print(f"  WARNING: could not assemble {d!r} at {addr:#x}, left unpatched")
        else:
            out[addr] = nw
    return out

def patch(path):
    out = subprocess.run([f"{BIN}/llvm-objdump", "-d", path],
                         capture_output=True, text=True).stdout

    # Find all instructions that reference x18 (or w18)
    sites = []
    for line in out.splitlines():
        m = re.match(r"\s*([0-9a-f]+):\s+([0-9a-f]+)\s+(.+)", line)
        if not m:
            continue
        if re.search(r'\b[xw]18\b', m.group(3)):
            addr = int(m.group(1), 16)
            raw = int(m.group(2), 16)
            sites.append((addr, raw, m.group(3).strip()))

    if not sites:
        print(f"{path}: no x18/w18 references found")
        return

    print(f"{path}: found {len(sites)} instructions referencing x18/w18")

    # Parse PE sections to map virtual addresses to file offsets
    hdr = subprocess.run([f"{BIN}/llvm-readobj", "--sections", "--file-headers", path],
                         capture_output=True, text=True).stdout
    image_base = int(re.search(r"ImageBase: (0x[0-9a-fA-F]+)", hdr).group(1), 16)
    secs = []
    for m in re.finditer(r"VirtualAddress: (0x[0-9a-f]+)\n(?:.*\n)*?\s+RawDataSize: (\d+)\n\s+PointerToRawData: (0x[0-9a-f]+)", hdr):
        rva, size, off = int(m.group(1), 16), int(m.group(2)), int(m.group(3), 16)
        secs.append((image_base + rva, off, size))

    data = bytearray(open(path, "rb").read())
    patched = 0
    patches = choose_patches(sites)
    for va, raw, disasm in sites:
        for vma, off, size in secs:
            if vma <= va < vma + size:
                fo = off + (va - vma)
                (w,) = struct.unpack_from("<I", data, fo)
                if w != raw:
                    print(f"  WARNING: mismatch at {va:#x}: file={w:#010x} expected={raw:#010x}")
                    break

                new_w = patches.get(va, w)
                fields_patched = [] if new_w == w else ["assembled"]

                if new_w != w:
                    struct.pack_into("<I", data, fo, new_w)
                    patched += 1
                    print(f"  {va:#x}: {disasm}  [{', '.join(fields_patched)}]")
                break
        else:
            print(f"  WARNING: no section for vaddr {va:#x}")

    open(path, "wb").write(data)
    print(f"{path}: patched {patched}/{len(sites)} instructions (x18→x28)")

for p in sys.argv[1:]:
    patch(p)
