"""Compares the frame dumps of two runs by frame index (frame_NNN.bmp in two folders) and, for scale, the
dumps within one folder against their predecessor: per pair the share of pixels that differ and the largest
channel difference.

usage: python -I frame_compare.py <folder A> <folder B> [first index] [last index]
"""
import os
import sys

from PIL import Image, ImageChops


def load(folder, index):
    path = os.path.join(folder, f"frame_{index:03d}.bmp")
    return Image.open(path).convert("RGB") if os.path.exists(path) else None


def compare(a, b):
    if a is None or b is None or a.size != b.size:
        return None
    diff = ImageChops.difference(a, b)
    histogram = diff.convert("L").histogram()
    total = a.size[0] * a.size[1]
    same = histogram[0]
    extrema = diff.getextrema()
    return 1.0 - same / total, max(channel[1] for channel in extrema)


def main():
    folderA, folderB = sys.argv[1], sys.argv[2]
    first = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    last = int(sys.argv[4]) if len(sys.argv) > 4 else 400
    print(f"{'frame':>5} {'A vs B differing':>17} {'max':>4} | {'A vs prev A':>12} | {'B vs prev B':>12}")
    previousA = previousB = None
    identical = compared = 0
    for index in range(first, last + 1):
        a, b = load(folderA, index), load(folderB, index)
        if a is None and b is None:
            continue
        ab = compare(a, b)
        aa = compare(previousA, a)
        bb = compare(previousB, b)
        previousA, previousB = a or previousA, b or previousB
        cell = lambda r: f"{100 * r[0]:11.3f} % {r[1]:4d}" if r else f"{'-':>18}"
        if ab is not None:
            compared += 1
            identical += ab[0] == 0.0
        print(f"{index:5d} {cell(ab)} | {cell(aa)[:14]} | {cell(bb)[:14]}")
    print(f"{compared} pairs compared, {identical} bit-identical")


main()
