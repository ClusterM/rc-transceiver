NEC_LEADING_PULSE = 9000
NEC_LEADING_GAP = 4500
NEC_GAP = 562
NEC_PULSE_0 = 562
NEC_PULSE_1 = 1600
NEC_PRECITION = 0.25
NEC_REPEAT_GAP = 2250

def nec_decode(s):
    b = bytes.fromhex(s)
    values = [int.from_bytes(b[i:i+2], byteorder='little') for i in range(0, len(b), 2)]
    if not (NEC_LEADING_PULSE * (1 - NEC_PRECITION) <= values[0] <= NEC_LEADING_PULSE * (1 + NEC_PRECITION)):
        raise ValueError(f"Invalid leading pulse length: {values[0]}")
    if not (NEC_LEADING_GAP * (1 - NEC_PRECITION) <= values[1] <= NEC_LEADING_GAP * (1 + NEC_PRECITION)):
        raise ValueError(f"Invalid leading gap length: {values[1]}")
    if not (NEC_GAP * (1 - NEC_PRECITION) <= values[2] <= NEC_GAP * (1 + NEC_PRECITION)):
        raise ValueError(f"Invalid first pulse length: {values[2]}")
    i = 0
    for p in range(3, 3 + 2 * 32, 2):
        v = values[p]
        v = 1 if v  > (NEC_PULSE_0 + NEC_PULSE_1) / 2 else 0
        i = (i >> 1) | (v << 31)
    return int.to_bytes(i, length=4, byteorder='little')

def nec_encode(h):
    # Type check
    if type(h) == int:
        h = int.to_bytes(h, length=4, byteorder='little')
    elif type(h) is list:
        h = bytes(h)
    elif type(h) is bytes:
        pass
    else:
        raise TypeError(f"Invalid type of input data: {type(h)}")
    # Length check
    if len(h) == 2:
        h = bytes([h[0], h[0] ^ 0xFF, h[1], h[1] ^ 0xFF])
    elif len(h) == 3:
        h = bytes([h[0], h[1], h[2], h[2] ^ 0xFF])
    elif len(h) == 4:
        pass
    else:
        raise ValueError(f"Invalid length of input data: {len(h)}")
    # Encoding
    len2str = lambda i: f"{((i >> 8) | (i << 8)) & 0xFFFF:04x}"
    i = int.from_bytes(h, byteorder='little')
    pulses = [NEC_PULSE_1 if i & (1 << bit) > 0 else NEC_PULSE_0 for bit in range(32)]
    s = (
        len2str(NEC_LEADING_PULSE) +
        len2str(NEC_LEADING_GAP) + 
        len2str(NEC_GAP) +
        len2str(NEC_GAP).join([len2str(pulse) for pulse in pulses]) +
        len2str(NEC_GAP)
    )
    return s

def is_nec_repeat(s):
    b = bytes.fromhex(s)
    values = [int.from_bytes(b[i:i+2], byteorder='little') for i in range(0, len(b), 2)]
    if len(values) > 3: return False
    if not (NEC_LEADING_PULSE * (1 - NEC_PRECITION) <= values[0] <= NEC_LEADING_PULSE * (1 + NEC_PRECITION)):
        return False
    if not (NEC_REPEAT_GAP * (1 - NEC_PRECITION) <= values[1] <= NEC_REPEAT_GAP * (1 + NEC_PRECITION)):
        return False
    if not (NEC_GAP * (1 - NEC_PRECITION) <= values[2] <= NEC_GAP * (1 + NEC_PRECITION)):
        return False
    return True
