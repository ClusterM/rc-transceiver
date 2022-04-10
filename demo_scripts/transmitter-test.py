#!/usr/bin/python3

from necrc import *
from rc6 import *
from time import sleep

DEVICE = "/dev/rc"

with open(DEVICE, "w") as f:
    # Send '0' button code for Philips TV
    raw = rc6_encode(command=0, control=0, toggle=0, mode=0)
    f.write(raw + "\n")
    f.flush()

    sleep(0.1)

    # Send NEC code
    raw = nec_encode([0x00, 0xef, 0x03, 0xfc])
    f.write(raw + "\n")
    f.flush()
