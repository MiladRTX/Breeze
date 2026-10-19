"""PNG -> 3840x2160 BC7 DDS (DX10, no mipmaps), the format the PS5 home screen
reads for sce_sys/pic0.dds and pic1.dds.

usage: python3 bc7.py <in.png> <out.dds> <header.dds>

Every 4x4 block is BC7 mode 6 (one subset, 7-bit RGBA endpoints plus a shared
bit, 4-bit indices), which suits smooth artwork. The endpoints run along the
block's principal colour axis. header.dds is any DDS with the same size and
format, whose 148-byte header is reused (the template's own pic0.dds).
"""
import sys

import numpy as np
from PIL import Image

WIDTH, HEIGHT = 3840, 2160
WEIGHTS = np.array([0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64], np.float32)


def blocks(img):
    a = np.asarray(img.convert("RGB"), np.float32)
    a = a.reshape(HEIGHT // 4, 4, WIDTH // 4, 4, 3).transpose(0, 2, 1, 3, 4)
    return a.reshape(-1, 16, 3)  # block order row-major, pixels row-major


def endpoints(px):
    mean = px.mean(axis=1)
    d = px - mean[:, None, :]
    cov = np.einsum("npi,npj->nij", d, d)
    axis = np.ones((len(px), 3), np.float32)
    for _ in range(6):  # power iteration for the principal axis
        axis = np.einsum("nij,nj->ni", cov, axis)
        axis /= np.maximum(np.linalg.norm(axis, axis=1, keepdims=True), 1e-6)
    t = np.einsum("npi,ni->np", d, axis)
    e0 = mean + axis * t.min(axis=1, keepdims=True)
    e1 = mean + axis * t.max(axis=1, keepdims=True)
    return np.clip(e0, 0, 255), np.clip(e1, 0, 255)


def encode(px):
    e0, e1 = endpoints(px)
    # Shared bit 1 on both endpoints, so alpha (127 << 1 | 1) is exactly 255:
    # colour endpoints are then the odd values 2q + 1.
    q0 = np.clip(np.rint((e0 - 1) / 2), 0, 127).astype(np.int64)
    q1 = np.clip(np.rint((e1 - 1) / 2), 0, 127).astype(np.int64)
    c0 = (q0 * 2 + 1).astype(np.float32)
    c1 = (q1 * 2 + 1).astype(np.float32)

    # The 16 palette colours are on the line c0..c1: the nearest one is the
    # nearest weight to the pixel's projection.
    line = c1 - c0
    len2 = np.einsum("ni,ni->n", line, line)
    proj = np.einsum("npi,ni->np", px - c0[:, None, :], line)
    w = np.where(len2[:, None] > 0, proj * 64 / np.maximum(len2[:, None], 1e-6), 0)
    idx = np.abs(w[:, :, None] - WEIGHTS[None, None, :]).argmin(axis=2).astype(np.int64)

    # The first pixel's index is stored in 3 bits: it must be below 8.
    swap = idx[:, 0] >= 8
    q0[swap], q1[swap] = q1[swap].copy(), q0[swap].copy()
    idx[swap] = 15 - idx[swap]

    u = np.uint64
    lo = np.full(len(px), 1 << 6, np.uint64)  # mode 6
    for shift, v in ((7, q0[:, 0]), (14, q1[:, 0]), (21, q0[:, 1]), (28, q1[:, 1]),
                     (35, q0[:, 2]), (42, q1[:, 2])):
        lo |= v.astype(np.uint64) << u(shift)
    lo |= u(127) << u(49)  # A0
    lo |= u(127) << u(56)  # A1
    lo |= u(1) << u(63)    # P0
    hi = np.ones(len(px), np.uint64)  # P1
    hi |= idx[:, 0].astype(np.uint64) << u(1)
    for i in range(1, 16):
        hi |= idx[:, i].astype(np.uint64) << u(4 + 4 * (i - 1))
    return np.stack([lo, hi], axis=1).astype("<u8").tobytes()


def main():
    src, out, header_from = sys.argv[1:4]
    img = Image.open(src)
    if img.size != (WIDTH, HEIGHT):
        sys.exit(f"{src} is {img.size[0]}x{img.size[1]}, not {WIDTH}x{HEIGHT}")
    header = open(header_from, "rb").read(148)
    assert header[:4] == b"DDS " and header[84:88] == b"DX10", "not a DX10 DDS"
    assert int.from_bytes(header[12:16], "little") == HEIGHT
    assert int.from_bytes(header[16:20], "little") == WIDTH
    assert int.from_bytes(header[128:132], "little") in (98, 99), "not BC7"
    px = blocks(img)
    data = b"".join(encode(px[i:i + 65536]) for i in range(0, len(px), 65536))
    assert len(data) == WIDTH * HEIGHT
    with open(out, "wb") as f:
        f.write(header + data)


if __name__ == "__main__":
    main()
