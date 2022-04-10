#!/usr/bin/python3

from necrc import *
from rc6 import *

DEVICE = "/dev/rc"

with open(DEVICE, "r") as f:
    while True:
        raw = f.readline().strip()
        # Try to decode button data using different encoding methods
        try:
            decoded = nec_decode(raw)
            print("Decoded as NEC:", " ".join(f"{b:02x}" for b in decoded))
            continue
        except:
            pass
        if is_nec_repeat(raw):
            print("Decoded as NEC repeat code")
            continue
        try:
            decoded = rc6_decode(raw)
            print("Decoded as RC6:", decoded)
            continue
        except:
            pass
        print("Can't decode, raw data:", raw)
