RC6_T = 444
RC6_PRECITION = 0.5

def rc6_decode(s):
    b = bytes.fromhex(s)
    values = [int.from_bytes(b[i:i+2], byteorder='little') for i in range(0, len(b), 2)]
    if len(values) % 2 != 0: values.append(65535)
    if not (RC6_T * (6 - RC6_PRECITION) <= values[0] <= RC6_T * (6 + RC6_PRECITION)):
        raise ValueError(f"Invalid leading pulse length: {values[0]}")
    if not (RC6_T * (2 - RC6_PRECITION) <= values[1] <= RC6_T * (2 + RC6_PRECITION)):
        raise ValueError(f"Invalid leading gap length: {values[1]}")
    i = 0
    bit = 0
    pos = 2
    while bit <= 20:
        i = i << 1
        bit = bit + 1
        if pos % 2 == 1: i = i | 1
        if values[pos] < RC6_T*3/2 if (pos != 10) else RC6_T*3:
            pos = pos + 2
        else:
            pos = pos + 1
    info = i & 0xFF
    control = (i >> 8) & 0xFF
    toggle = (i >> 16) & 1
    mode = (i >> 17) & 0b111
    return {"info": info, "control": control, "toggle": toggle, "mode": mode}

def rc6_encode(command, control = 0, toggle = 0, mode = 0):
    if type(command) == dict:
        info = command.get("info", 0)
        control = command.get("control", 0)
        toggle = command.get("toggle", 0)
        mode = command.get("mode", 0)
    elif type(command) == tuple:
        info = command[0]
        control = command[1] if len(command) > 1 else 0
        toggle = command[2] if len(command) > 2 else 0
        mode = command[3] if len(command) > 3 else 0
    elif type(command) == int:
        info = command
    else:
        raise ValueError(f"Invalid command type: {type(command)}")

    pulses = []
    bits = []
    data = (1 << 20) | ((mode & 0b111) << 17) | ((toggle & 1) << 16) | ((control & 0xFF) << 8) | (info & 0xFF)
    # Leading pulse
    bits.append((1, RC6_T*6))
    bits.append((0, RC6_T*2))
    pulses.append(RC6_T*6)
    pulses.append(RC6_T*2)
    # Data
    for i in range(20, -1, -1):
        T = RC6_T if i != 16 else RC6_T * 2
        if ((data >> i) & 1 == 0):
            bits.append(i)
            bits.append(0)
            bits.append((0, T))
            bits.append((1, T))
            if (len(pulses) % 2) == 0:
                pulses[len(pulses) - 1] = pulses[len(pulses) - 1] + T
                pulses.append(T)
            else:
                pulses.append(T)
                pulses.append(T)
        else:
            bits.append(i)
            bits.append(1)
            bits.append((1, T))
            bits.append((0, T))
            if (len(pulses) % 2) == 1:
                pulses[len(pulses) - 1] = pulses[len(pulses) - 1] + T
                pulses.append(T)
            else:
                pulses.append(T)
                pulses.append(T)
    if (len(pulses) % 2 == 0):
        pulses = pulses[0:len(pulses) - 1]
    len2str = lambda i: f"{((i >> 8) | (i << 8)) & 0xFFFF:04x}"
    s = "".join([len2str(pulse) for pulse in pulses])
    return s
