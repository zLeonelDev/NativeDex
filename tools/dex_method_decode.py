"""Decode DEX invoke instructions correctly."""
import struct
import sys

def parse_uleb128(data, offset):
    result = 0
    shift = 0
    while True:
        b = data[offset]
        offset += 1
        result |= (b & 0x7f) << shift
        if (b & 0x80) == 0: break
        shift += 7
    return result, offset

def read_mutf8_string(data, offset):
    length, content_off = parse_uleb128(data, offset)
    end = data.find(b'\x00', content_off)
    if end < 0: end = content_off + length
    return data[content_off:end].decode('utf-8', errors='replace')

def parse_dex(path):
    with open(path, 'rb') as f:
        data = f.read()
    if data[:4] != b'dex\n': return None
    sids = struct.unpack_from('<I', data, 56)[0]
    sido = struct.unpack_from('<I', data, 60)[0]
    tids = struct.unpack_from('<I', data, 64)[0]
    tido = struct.unpack_from('<I', data, 68)[0]
    mids = struct.unpack_from('<I', data, 88)[0]
    mido = struct.unpack_from('<I', data, 92)[0]
    cds = struct.unpack_from('<I', data, 96)[0]
    cdo = struct.unpack_from('<I', data, 100)[0]

    strings = [read_mutf8_string(data, struct.unpack_from('<I', data, sido + i*4)[0]) for i in range(sids)]
    types = [strings[struct.unpack_from('<I', data, tido + i*4)[0]] for i in range(tids)]
    methods = []
    for i in range(mids):
        off = mido + i*8
        cidx = struct.unpack_from('<H', data, off)[0]
        nidx = struct.unpack_from('<I', data, off+4)[0]
        methods.append((types[cidx], strings[nidx]))
    return {'strings': strings, 'types': types, 'methods': methods, 'classes': (cds, cdo), 'data': data}

W = {
    0x00: 1, 0x01: 1, 0x04: 1, 0x07: 1, 0x0a: 1, 0x0b: 1, 0x0c: 1, 0x0d: 1,
    0x0e: 1, 0x0f: 1, 0x10: 1, 0x11: 1,
    0x12: 1, 0x13: 2, 0x14: 3, 0x15: 2, 0x16: 2, 0x17: 2, 0x18: 5, 0x19: 2,
    0x1a: 2, 0x1b: 3, 0x1c: 2, 0x1d: 1, 0x1e: 1,
    0x1f: 2, 0x20: 2, 0x21: 1, 0x22: 2, 0x23: 2, 0x24: 3, 0x25: 3,
    0x26: 3, 0x27: 1, 0x28: 1, 0x29: 2, 0x2a: 3, 0x2b: 3, 0x2c: 3,
    0x32: 2, 0x33: 2, 0x34: 2, 0x35: 2, 0x36: 2, 0x37: 2,
    0x38: 2, 0x39: 2, 0x3a: 2, 0x3b: 2, 0x3c: 2, 0x3d: 2,
    0x44: 2, 0x45: 2, 0x46: 2, 0x47: 2, 0x48: 2, 0x49: 2, 0x4a: 2, 0x4b: 2,
    0x4c: 2, 0x4d: 2, 0x4e: 2, 0x4f: 2, 0x50: 2, 0x51: 2, 0x52: 2, 0x53: 2,
    0x54: 2, 0x55: 2, 0x56: 2, 0x57: 2, 0x58: 2, 0x59: 2,
    0x5a: 2, 0x5b: 2, 0x5c: 2, 0x5d: 2, 0x5e: 2, 0x5f: 2, 0x60: 2, 0x61: 2,
    0x62: 2, 0x63: 2, 0x64: 2, 0x65: 2, 0x66: 2, 0x67: 2,
}
for op in range(0x90, 0xb0): W[op] = 1
for op in range(0xb0, 0xd0): W[op] = 1
for op in range(0xd0, 0xf0): W[op] = 2
for op in range(0xf0, 0x100): W[op] = 3

def main():
    target_class_name = 'Lcom/leoneldev/tareas/MainActivity;'
    target_method_name = 'onCreate'

    for path in ['tmp/dex_out2/classes.dex', 'tmp/dex_out2/classes2.dex',
                 'tmp/dex_out2/classes3.dex', 'tmp/dex_out2/classes4.dex']:
        dex = parse_dex(path)
        if not dex: continue
        cds, cdo = dex['classes']
        data = dex['data']

        target_cdata = None
        for i in range(cds):
            off = cdo + i*32
            if off + 32 > len(data): break
            cidx = struct.unpack_from('<I', data, off)[0]
            if cidx >= len(dex['types']): continue
            cls = dex['types'][cidx]
            if cls == target_class_name:
                target_cdata = struct.unpack_from('<I', data, off+24)[0]
                break

        if target_cdata is None or target_cdata == 0: continue

        o = target_cdata
        sf, o = parse_uleb128(data, o)
        if_, o = parse_uleb128(data, o)
        dm, o = parse_uleb128(data, o)
        vm, o = parse_uleb128(data, o)
        for _ in range(sf + if_):
            _, o = parse_uleb128(data, o); _, o = parse_uleb128(data, o)

        midx = 0
        target_code_off = None
        for _ in range(dm):
            d, o = parse_uleb128(data, o); midx += d
            access, o = parse_uleb128(data, o)
            code_off, o = parse_uleb128(data, o)
            if midx < len(dex['methods']) and dex['methods'][midx][1] == target_method_name:
                target_code_off = code_off; break
        if target_code_off is None:
            midx = 0
            for _ in range(vm):
                d, o = parse_uleb128(data, o); midx += d
                access, o = parse_uleb128(data, o)
                code_off, o = parse_uleb128(data, o)
                if midx < len(dex['methods']) and dex['methods'][midx][1] == target_method_name:
                    target_code_off = code_off; break

        if target_code_off is None or target_code_off == 0: continue

        print(f"=== {target_class_name}->{target_method_name} in {path} (code_off=0x{target_code_off:x}) ===")
        regs = struct.unpack_from('<H', data, target_code_off)[0]
        ins = struct.unpack_from('<H', data, target_code_off+2)[0]
        insns_size = struct.unpack_from('<I', data, target_code_off+12)[0]
        print(f"regs={regs}, ins={ins}, insns_size={insns_size}")
        insns = [struct.unpack_from('<H', data, target_code_off+16 + i*2)[0] for i in range(insns_size)]

        pc = 0
        while pc < len(insns):
            inst = insns[pc]
            op = inst & 0xFF
            width = W.get(op, 1)
            extras = ''
            if op == 0x1a and width >= 2:
                idx = insns[pc+1]
                if idx < len(dex['strings']): extras = f'"{dex["strings"][idx]}"'
            elif op == 0x22 and width >= 2:
                idx = insns[pc+1]
                if idx < len(dex['types']): extras = dex['types'][idx]
            elif op in (0x6e, 0x6f, 0x70, 0x71, 0x72, 0x74, 0x75, 0x76, 0x77, 0x78):
                # Empirically, for invoke-kind (35c/3rc), insns[pc+1] is the 16-bit method_idx
                idx = insns[pc+1]
                if idx < len(dex['methods']):
                    m_cls, m_name = dex['methods'][idx]
                    extras = f'{m_cls}->{m_name}'
            print(f"  PC={pc:4d}  0x{op:02x}  {extras}")
            pc += width
        break

if __name__ == '__main__':
    main()
