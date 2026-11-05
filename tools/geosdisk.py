"""GEOS-aware disk image writer for D64, D71 and D81: copies GEOS files (SEQ
and VLIR) from D64 images and CVT files, and marks the disk as a GEOS disk.

Geometry:
  D64: 35 tracks; directory track 18, BAM in 18/0, directory from 18/1
  D71: as D64 per side, 70 tracks; BAM of side 2 in 53/0, its free counts
       in 18/0 from $DD
  D81: 80 tracks of 40 sectors; header 40/0, BAM 40/1 and 40/2, dir from 40/3
"""
import sys

SIG = b"GEOS format V1.0"
OFF_OP_TR_SC = 171      # header: off-page ("border") directory block
OFF_GS_ID = 173         # header: GEOS signature
PAD = bytes([0xa0])

SPT64 = [21] * 17 + [19] * 7 + [18] * 6 + [17] * 5


def off64(t, s):
    return (sum(SPT64[:t - 1]) + s) * 256


class Disk:
    tracks = 0
    dir_track = 18
    dir_start = (18, 1)
    size = 0

    def __init__(self, path):
        self.path = path
        self.img = bytearray(open(path, "rb").read())
        if len(self.img) != self.size:
            sys.exit(f"{path}: not a {type(self).__name__} image")

    def save(self):
        open(self.path, "wb").write(bytes(self.img))

    def spt(self, t):
        raise NotImplementedError

    def off(self, t, s):
        return (sum(self.spt(x) for x in range(1, t)) + s) * 256

    def _bits(self, t):
        """(offset of the free count, offset of the bitmap)"""
        raise NotImplementedError

    def is_free(self, t, s):
        return bool(self.img[self._bits(t)[1] + (s >> 3)] & (1 << (s & 7)))

    def claim(self, t, s):
        c, b = self._bits(t)
        self.img[b + (s >> 3)] &= ~(1 << (s & 7)) & 0xff
        self.img[c] -= 1

    def free_blocks(self):
        return sum(self.img[self._bits(t)[0]]
                   for t in range(1, self.tracks + 1) if t != self.dir_track)

    def _track_order(self):
        return sorted((t for t in range(1, self.tracks + 1)
                       if t != self.dir_track),
                      key=lambda t: abs(t - self.dir_track))

    def alloc(self, prev=None):
        """A free sector: further along the same track, else near the dir."""
        tracks = self._track_order()
        if prev and prev[0] != self.dir_track:
            tracks.insert(0, prev[0])
        for t in tracks:
            n = self.spt(t)
            start = (prev[1] + self.interleave) % n \
                if prev and t == prev[0] else 0
            for i in range(n):
                s = (start + i) % n
                if self.is_free(t, s):
                    self.claim(t, s)
                    return t, s
        sys.exit(f"{self.path}: disk full")

    def make_geos_disk(self):
        """Border block and signature, so that deskTop treats it as GEOS."""
        head = self.off(*self.head)
        if self.img[head + OFF_GS_ID:head + OFF_GS_ID + 16] == SIG:
            return
        t = self.dir_track
        s = next(s for s in range(self.spt(t) - 1, 2, -1)
                 if self.is_free(t, s))
        self.claim(t, s)
        o = self.off(t, s)
        self.img[o:o + 256] = bytes([0, 0xff]) + bytes(254)
        self.img[head + OFF_OP_TR_SC:head + OFF_OP_TR_SC + 2] = bytes((t, s))
        self.img[head + OFF_GS_ID:head + OFF_GS_ID + 16] = SIG

    def _dir_slot(self):
        t, s = self.dir_start
        while True:
            o = self.off(t, s)
            for i in range(8):
                if not self.img[o + i * 32 + 2] & 0x07:
                    return o + i * 32 + 2
            if not self.img[o]:
                n = next(x for x in range(self.spt(t))
                         if self.is_free(self.dir_track, x))
                self.claim(self.dir_track, n)
                self.img[o:o + 2] = bytes((self.dir_track, n))
                p = self.off(self.dir_track, n)
                self.img[p:p + 256] = bytes([0, 0xff]) + bytes(254)
            t, s = self.img[o], self.img[o + 1]

    def _put_chain(self, blocks, prev):
        first = pos = self.alloc(prev)
        for n, b in enumerate(blocks):
            o = self.off(*pos)
            if n + 1 < len(blocks):
                nxt = self.alloc(pos)
                link = bytes(nxt)
            else:
                nxt, link = None, bytes([0, len(b) + 1])
            self.img[o:o + 256] = (link + b).ljust(256, bytes(1))
            pos = nxt or pos
        return first, pos

    def add_seq(self, entry, header, data):
        """entry: 30 byte directory entry, header: 254 bytes, data: bytes"""
        h = self.alloc()
        o = self.off(*h)
        self.img[o:o + 256] = bytes([0, 0xff]) + header
        blocks = [data[i:i + 254] for i in range(0, len(data), 254)] or [b""]
        first, _ = self._put_chain(blocks, h)
        self._entry(entry, first, h)

    def add_vlir(self, entry, header, records):
        """records: list of None (empty) or bytes"""
        h = self.alloc()
        o = self.off(*h)
        self.img[o:o + 256] = bytes([0, 0xff]) + header
        ix = self.alloc(h)
        index = bytearray(254)
        pos = ix
        for n, rec in enumerate(records):
            if rec is None:
                index[n * 2:n * 2 + 2] = bytes([0, 0xff])
            else:
                blocks = [rec[i:i + 254] for i in range(0, len(rec), 254)]
                first, pos = self._put_chain(blocks, pos)
                index[n * 2:n * 2 + 2] = bytes(first)
        o = self.off(*ix)
        self.img[o:o + 256] = bytes([0, 0xff]) + index
        self._entry(entry, ix, h)

    def _entry(self, entry, first, header):
        slot = self._dir_slot()
        e = bytearray(entry)
        e[1:3] = bytes(first)
        e[19:21] = bytes(header)
        self.img[slot:slot + 30] = e

    def add_cvt(self, path):
        """A CVT file. SEQ only: our own applications are all SEQ."""
        d = open(path, "rb").read()
        if d[21] != 0:
            sys.exit(f"{path}: only SEQ CVT files are supported")
        self.add_seq(d[:30], d[254:508], d[508:])

    def check(self):
        """Every GEOS file's chains must add up to its directory size."""
        t, s = self.dir_start
        while t:
            o = self.off(t, s)
            for i in range(8):
                e = self.img[o + i * 32 + 2:o + i * 32 + 32]
                if e[0] & 0x07 and e[22]:
                    self._check_file(e)
            t, s = self.img[o], self.img[o + 1]

    def _chain_len(self, t, s):
        n = 0
        while t and n < 3200:
            o = self.off(t, s)
            t, s = self.img[o], self.img[o + 1]
            n += 1
        return n

    def _check_file(self, e):
        name = bytes(e[3:19]).rstrip(PAD).decode("latin1")
        n = self._chain_len(e[1], e[2])
        if e[21] == 1:                  # VLIR: index + records
            o = self.off(e[1], e[2])
            n = 1
            for i in range(2, 256, 2):
                if self.img[o + i]:
                    n += self._chain_len(self.img[o + i], self.img[o + i + 1])
        blocks = e[28] | e[29] << 8
        if n + 1 != blocks:
            sys.exit(f"broken file on {self.path}: {name}: "
                     f"{n + 1} blocks, directory says {blocks}")


class D64(Disk):
    tracks = 35
    size = 174848
    head = (18, 0)
    interleave = 8

    def spt(self, t):
        return SPT64[t - 1]

    def _bits(self, t):
        b = self.off(18, 0) + 4 * t
        return b, b + 1


class D71(D64):
    tracks = 70
    size = 349696

    def spt(self, t):
        return SPT64[(t - 1) % 35]

    def _bits(self, t):
        if t <= 35:
            return D64._bits(self, t)
        return self.off(18, 0) + 0xdd + t - 36, self.off(53, 0) + 3 * (t - 36)

    def _track_order(self):
        # track 53 holds the BAM of side 2
        return [t for t in super()._track_order() if t != 53]


class D81(Disk):
    tracks = 80
    size = 819200
    dir_track = 40
    dir_start = (40, 3)
    head = (40, 0)
    interleave = 1

    def spt(self, t):
        return 40

    def off(self, t, s):
        return ((t - 1) * 40 + s) * 256

    def _bits(self, t):
        base = self.off(40, 1 if t <= 40 else 2)
        e = base + 0x10 + 6 * ((t - 1) if t <= 40 else (t - 41))
        return e, e + 1


FORMATS = {"d64": D64, "d71": D71, "d81": D81}


def blocks_of(entry):
    return entry[28] | entry[29] << 8


def read_d64_files(path):
    """Yield (name, entry, header, data or records) of the GEOS files."""
    img = open(path, "rb").read()

    def chain(t, s):
        out = b""
        n = 0
        while t and n < 800:
            b = img[off64(t, s):off64(t, s) + 256]
            t2, s2 = b[0], b[1]
            out += b[2:] if t2 else b[2:s2 + 1]
            t, s = t2, s2
            n += 1
        return out

    t, s = 18, 1
    while t:
        o = off64(t, s)
        for i in range(8):
            e = img[o + i * 32 + 2:o + i * 32 + 32]
            if not (e[0] & 0x07) or not e[22]:
                continue        # empty or not a GEOS file
            h = img[off64(e[19], e[20]) + 2:off64(e[19], e[20]) + 256]
            name = e[3:19].rstrip(PAD)
            if e[21] == 1:
                ix = img[off64(e[1], e[2]):off64(e[1], e[2]) + 256]
                recs = []
                for j in range(2, 256, 2):
                    if ix[j]:
                        recs.append(chain(ix[j], ix[j + 1]))
                    elif ix[j + 1] == 0xff:
                        recs.append(None)
                    else:
                        break
                yield name, e, h, recs
            else:
                yield name, e, h, chain(e[1], e[2])
        t, s = img[o], img[o + 1]
