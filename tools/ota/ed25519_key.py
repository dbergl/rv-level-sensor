"""Checks that an Ed25519 public key is a canonical point of large order (RFC 8032 arithmetic)."""
P = 2**255 - 19
D = -121665 * pow(121666, -1, P) % P
IDENTITY = (0, 1, 1, 0)  # extended coordinates (X, Y, Z, T), x = X/Z, y = Y/Z, xy = T/Z


def add(a, b):
    (x1, y1, z1, t1), (x2, y2, z2, t2) = a, b
    A, B, C = (y1 - x1) * (y2 - x2) % P, (y1 + x1) * (y2 + x2) % P, 2 * D * t1 * t2 % P
    Dd = 2 * z1 * z2 % P
    E, F, G, H = B - A, Dd - C, Dd + C, B + A
    return E * F % P, G * H % P, F * G % P, E * H % P


def scalar_mult(n, pt):
    r = IDENTITY
    while n:
        if n & 1:
            r = add(r, pt)
        pt, n = add(pt, pt), n >> 1
    return r


def decode_point(b):
    """The point for a 32-byte encoding, or None if it is not canonical or not on the curve."""
    v = int.from_bytes(b, "little")
    y, sign = v & (2**255 - 1), v >> 255
    if y >= P:
        return None
    u, w = (y * y - 1) % P, (D * y * y + 1) % P
    x = u * pow(w, 3, P) * pow(u * pow(w, 7, P), (P - 5) // 8, P) % P
    if w * x * x % P != u:
        x = x * pow(2, (P - 1) // 4, P) % P
        if w * x * x % P != u:
            return None
    if x == 0 and sign:
        return None
    if x & 1 != sign:
        x = P - x
    return x, y, 1, x * y % P


def encode_point(pt):
    x, y, z, _ = pt
    zi = pow(z, -1, P)
    return ((y * zi % P) | ((x * zi % P & 1) << 255)).to_bytes(32, "little")


def check_public_key(key: bytes) -> str:
    """Empty if the key is usable, otherwise why not."""
    if len(key) != 32:
        return f"expected 32 bytes, found {len(key)}"
    pt = decode_point(key)
    if pt is None:
        return "not a valid Ed25519 public key"
    if encode_point(scalar_mult(8, pt)) == encode_point(IDENTITY):
        return "a small-order point, which would let anyone forge signatures"
    return ""
