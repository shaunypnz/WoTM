"""Copies one file from the root of an ISO 9660 image. Usage: iso_extract.py <file.iso> <name> <output>"""
import struct
import sys


def main(iso, name, out):
    with open(iso, "rb") as f:
        f.seek(16 * 2048)
        pvd = f.read(2048)
        if pvd[1:6] != b"CD001":
            sys.exit(f"{iso} is not an ISO 9660 image")
        ext, size = struct.unpack_from("<I", pvd, 158)[0], struct.unpack_from("<I", pvd, 166)[0]
        f.seek(ext * 2048)
        d = f.read(size)
        o = 0
        while o < len(d):
            n = d[o]
            if n == 0:
                o = (o // 2048 + 1) * 2048
                continue
            if d[o + 33:o + 33 + d[o + 32]].split(b";")[0].decode("latin1").upper() == name.upper():
                fe, fs = struct.unpack_from("<I", d, o + 2)[0], struct.unpack_from("<I", d, o + 10)[0]
                f.seek(fe * 2048)
                with open(out, "wb") as w:
                    w.write(f.read(fs))
                return
            o += n
    sys.exit(f"{name} is not in {iso}")


if __name__ == "__main__":
    main(*sys.argv[1:4])
