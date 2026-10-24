"""Rewrites the expected bytes of a guest hook table from the decrypted eboot, long enough for far hooks.

usage: python -I hook_bytes.py <eboot.bin ELF> <table.hooks> [--bytes 24] [--write]

Far hooks (libc GuestHooks.cpp, when no block can be allocated near the image) overwrite at least 14 bytes of whole
instructions, so the expected bytes must cover them. For each hook line the script decodes the prologue with the
same instruction whitelist as the installer and reports how many bytes a near (5) and a far (14) hook relocate;
with --write it replaces the expected bytes by the first N bytes at the VA (default 24).
"""
import argparse
import struct


def load(path):
    data = open(path, "rb").read()
    assert data[:4] == b"\x7fELF"
    phoff = struct.unpack_from("<Q", data, 0x20)[0]
    size, count = struct.unpack_from("<HH", data, 0x36)
    segments = []
    for index in range(count):
        kind, flags, offset, va, pa, filesz, memsz, align = struct.unpack_from("<IIQQQQQQ", data, phoff + index * size)
        if kind == 1:
            segments.append((offset, va, filesz))

    def read(va, n):
        for offset, start, filesz in segments:
            if start <= va < start + filesz:
                return data[offset + va - start: offset + va - start + n]
        return None
    return read


def decode(b):
    j = 0
    rex = 0
    if 0x40 <= b[0] <= 0x4f:
        rex = b[0]
        j = 1
    op = b[j]
    wide = (rex & 8) != 0
    if 0x50 <= op <= 0x5f and not wide:
        return j + 1, False
    if 0xb8 <= op <= 0xbf:
        return j + (9 if wide else 5), False
    if op in (0x01, 0x03, 0x09, 0x0b, 0x21, 0x23, 0x29, 0x2b, 0x31, 0x33, 0x39, 0x3b, 0x63, 0x84, 0x85, 0x88, 0x89, 0x8a, 0x8b, 0x8d):
        imm = 0
    elif op in (0x80, 0x83, 0xc6):
        imm = 1
    elif op in (0x81, 0xc7):
        imm = 4
    else:
        return None, False
    modrm = b[j + 1]
    mod, rm = modrm >> 6, modrm & 7
    if mod == 3:
        if op == 0x8d:
            return None, False
        return j + 2 + imm, False
    if mod == 0 and rm == 5:
        return j + 6 + imm, True
    length = j + 2
    if rm == 4:
        if mod == 0 and (b[j + 2] & 7) == 5:
            return None, False
        length += 1
    length += 1 if mod == 1 else 4 if mod == 2 else 0
    return length + imm, False


def cover(code, need):
    covered = 0
    rip = False
    while covered < need:
        length, relative = decode(code[covered:])
        if length is None:
            return None, rip
        rip = rip or relative
        covered += length
    return covered, rip


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("elf")
    ap.add_argument("table")
    ap.add_argument("--bytes", type=int, default=24)
    ap.add_argument("--write", action="store_true")
    args = ap.parse_args()
    read = load(args.elf)
    out = []
    for line in open(args.table, encoding="utf-8").read().splitlines():
        words = line.split()
        if not words or words[0].startswith("#") or words[0] == "patch" or len(words) < 3:
            out.append(line)
            continue
        va = int(words[1], 16)
        code = read(va, 32)
        near, _ = cover(code, 5)
        far, rip = cover(code, 14)
        print(f"{words[0]:20} {words[1]:>10} near {near} far {far}{' RIP-relative' if rip else ''}  {code[:args.bytes].hex()}")
        if args.write and far is not None and not rip:
            words[2] = code[:max(args.bytes, far)].hex()
            out.append(" ".join(words))
        else:
            out.append(line)
    if args.write:
        open(args.table, "w", encoding="utf-8", newline="\n").write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
