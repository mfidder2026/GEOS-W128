#!/usr/bin/env python3
"""Decode a G64 (unprotected 1541 GCR image) to a plain D64.
    python tools/g64tod64.py in.g64 out.d64"""
import sys

SPT = [21] * 17 + [19] * 7 + [18] * 6 + [17] * 5
GCR = {0x0a: 0, 0x0b: 1, 0x12: 2, 0x13: 3, 0x0e: 4, 0x0f: 5, 0x16: 6, 0x17: 7,
       0x09: 8, 0x19: 9, 0x1a: 10, 0x1b: 11, 0x0d: 12, 0x1d: 13, 0x1e: 14,
       0x15: 15}




def decode(track_bits, pos, nbytes):
    """nbytes of data from GCR bits starting at bit position pos"""
    out = bytearray()
    n = len(track_bits)
    for i in range(nbytes):
        hi = int("".join(track_bits[(pos + i * 10 + k) % n] for k in range(5)), 2)
        lo = int("".join(track_bits[(pos + i * 10 + 5 + k) % n] for k in range(5)), 2)
        if hi not in GCR or lo not in GCR:
            return None
        out.append(GCR[hi] << 4 | GCR[lo])
    return bytes(out)


def main(src, dst):
    g = open(src, "rb").read()
    if g[:8] != b"GCR-1541":
        sys.exit("not a G64")
    ntracks = g[9]
    offs = [int.from_bytes(g[12 + i * 4:16 + i * 4], "little") for i in range(ntracks)]
    d64 = bytearray()
    for t in range(1, 36):
        o = offs[(t - 1) * 2]
        n = g[o] | g[o + 1] << 8
        tb = "".join(f"{b:08b}" for b in g[o + 2:o + 2 + n])
        tb2 = tb + tb[:20000]
        sectors = {}
        p = 0
        while True:
            p = tb2.find("1111111111", p)
            if p < 0 or p >= len(tb):
                break
            while tb2[p] == "1":
                p += 1
            h = decode(tb, p, 8)
            if h and h[0] == 0x08 and h[3] == t and h[2] < SPT[t - 1]:
                q = tb2.find("1111111111", p + 80)
                while tb2[q] == "1":
                    q += 1
                d = decode(tb, q, 258)
                if d and d[0] == 0x07:
                    c = 0
                    for x in d[1:257]:
                        c ^= x
                    if c == d[257]:
                        sectors[h[2]] = d[1:257]
                p = q
            else:
                p += 1
        for s in range(SPT[t - 1]):
            if s not in sectors:
                sys.exit(f"track {t} sector {s}: not readable")
            d64 += sectors[s]
    open(dst, "wb").write(d64)
    print(dst, len(d64), "bytes")


main(sys.argv[1], sys.argv[2])
