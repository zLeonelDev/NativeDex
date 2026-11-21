import struct
import sys

def parse_uleb128(data, offset):
    result = 0
    shift = 0
    while True:
        b = data[offset]
        offset += 1
        result |= (b & 0x7f) << shift
        if (b & 0x80) == 0:
            break
        shift += 7
    return result, offset

def read_mutf8_string(data, offset):
    length, content_off = parse_uleb128(data, offset)
    end = data.find(b'\x00', content_off)
    if end < 0:
        end = content_off + length
    return data[content_off:end].decode('utf-8', errors='replace')

def parse_dex(path):
    with open(path, 'rb') as f:
        data = f.read()
    if data[:4] != b'dex\n':
        return None
    string_ids_size = struct.unpack_from('<I', data, 56)[0]
    string_ids_off = struct.unpack_from('<I', data, 60)[0]
    type_ids_size = struct.unpack_from('<I', data, 64)[0]
    type_ids_off = struct.unpack_from('<I', data, 68)[0]
    method_ids_size = struct.unpack_from('<I', data, 88)[0]
    method_ids_off = struct.unpack_from('<I', data, 92)[0]
    class_defs_size = struct.unpack_from('<I', data, 96)[0]
    class_defs_off = struct.unpack_from('<I', data, 100)[0]

    strings = []
    for i in range(string_ids_size):
        str_data_off = struct.unpack_from('<I', data, string_ids_off + i*4)[0]
        strings.append(read_mutf8_string(data, str_data_off))

    types = []
    for i in range(type_ids_size):
        desc_idx = struct.unpack_from('<I', data, type_ids_off + i*4)[0]
        types.append(strings[desc_idx] if desc_idx < len(strings) else '?')

    methods = []
    for i in range(method_ids_size):
        off = method_ids_off + i*8
        class_idx = struct.unpack_from('<H', data, off)[0]
        proto_idx = struct.unpack_from('<H', data, off+2)[0]
        name_idx = struct.unpack_from('<I', data, off+4)[0]
        cls = types[class_idx] if class_idx < len(types) else '?'
        name = strings[name_idx] if name_idx < len(strings) else '?'
        methods.append((cls, name, proto_idx))

    classes = []
    for i in range(class_defs_size):
        off = class_defs_off + i*32
        if off + 32 > len(data):
            break
        class_idx = struct.unpack_from('<I', data, off)[0]
        if class_idx >= len(types):
            continue
        cls = types[class_idx]
        class_data_off = struct.unpack_from('<I', data, off+24)[0]
        classes.append((cls, class_data_off))

    return {'strings': strings, 'types': types, 'methods': methods, 'classes': classes, 'data': data}

def parse_class_data(data, offset):
    if offset == 0 or offset >= len(data):
        return None
    static_fields_size, offset = parse_uleb128(data, offset)
    instance_fields_size, offset = parse_uleb128(data, offset)
    direct_methods_size, offset = parse_uleb128(data, offset)
    virtual_methods_size, offset = parse_uleb128(data, offset)
    for _ in range(static_fields_size + instance_fields_size):
        parse_uleb128(data, offset); offset = offset
        parse_uleb128(data, offset); offset = offset
    methods = []
    method_idx = 0
    for _ in range(direct_methods_size):
        diff, offset = parse_uleb128(data, offset)
        method_idx += diff
        access, offset = parse_uleb128(data, offset)
        code_off, offset = parse_uleb128(data, offset)
        methods.append((method_idx, access, code_off))
    method_idx = 0
    for _ in range(virtual_methods_size):
        diff, offset = parse_uleb128(data, offset)
        method_idx += diff
        access, offset = parse_uleb128(data, offset)
        code_off, offset = parse_uleb128(data, offset)
        methods.append((method_idx, access, code_off))
    return methods

def parse_code_item(data, offset):
    if offset == 0 or offset + 16 > len(data):
        return None
    registers_size = struct.unpack_from('<H', data, offset)[0]
    ins_size = struct.unpack_from('<H', data, offset+2)[0]
    insns_size = struct.unpack_from('<I', data, offset+12)[0]
    insns = []
    for i in range(insns_size):
        pos = offset + 16 + i*2
        if pos + 2 > len(data):
            break
        insns.append(struct.unpack_from('<H', data, pos)[0])
    return {'regs': registers_size, 'ins': ins_size, 'insns': insns}

OP_WIDTHS = {
    0x00: 1, 0x01: 1, 0x02: 2, 0x03: 3, 0x04: 1, 0x05: 2, 0x06: 3,
    0x07: 1, 0x08: 2, 0x09: 3, 0x0a: 1, 0x0b: 1, 0x0c: 1, 0x0d: 1,
    0x0e: 1, 0x0f: 1, 0x10: 1, 0x11: 1,
    0x12: 1, 0x13: 2, 0x14: 3, 0x15: 2,
    0x16: 2, 0x17: 2, 0x18: 5, 0x19: 2,
    0x1a: 2, 0x1b: 3, 0x1c: 2,
    0x1d: 1, 0x1e: 1,
    0x1f: 2, 0x20: 2,
    0x21: 1, 0x22: 2, 0x23: 2, 0x24: 3, 0x25: 3,
    0x26: 3, 0x27: 1,
    0x28: 1, 0x29: 2, 0x2a: 3, 0x2b: 3, 0x2c: 3,
    0x2d: 1, 0x2e: 1, 0x2f: 1, 0x30: 1, 0x31: 1,
    0x32: 2, 0x33: 2, 0x34: 2, 0x35: 2, 0x36: 2, 0x37: 2,
    0x38: 2, 0x39: 2, 0x3a: 2, 0x3b: 2, 0x3c: 2, 0x3d: 2,
    0x44: 2, 0x45: 2, 0x46: 2, 0x47: 2, 0x48: 2, 0x49: 2, 0x4a: 2, 0x4b: 2,
    0x4c: 2, 0x4d: 2, 0x4e: 2, 0x4f: 2, 0x50: 2, 0x51: 2, 0x52: 2, 0x53: 2, 0x54: 2, 0x55: 2, 0x56: 2, 0x57: 2, 0x58: 2, 0x59: 2,
    0x5a: 2, 0x5b: 2, 0x5c: 2, 0x5d: 2, 0x5e: 2, 0x5f: 2, 0x60: 2, 0x61: 2, 0x62: 2, 0x63: 2, 0x64: 2, 0x65: 2, 0x66: 2, 0x67: 2,
    0x6e: 3, 0x6f: 3, 0x70: 3, 0x71: 3, 0x72: 3, 0x73: 3,
    0x74: 3, 0x75: 3, 0x76: 3, 0x77: 3, 0x78: 3, 0x79: 3,
    0x7b: 1, 0x7c: 1, 0x7d: 1, 0x7e: 1, 0x7f: 1, 0x80: 1,
    0x81: 1, 0x82: 1, 0x83: 1, 0x84: 1, 0x85: 1, 0x86: 1, 0x87: 1, 0x88: 1, 0x89: 1, 0x8a: 1, 0x8b: 1, 0x8c: 1, 0x8d: 1, 0x8e: 1, 0x8f: 1,
    0x90: 1, 0x91: 1, 0x92: 1, 0x93: 1, 0x94: 1, 0x95: 1, 0x96: 1, 0x97: 1, 0x98: 1, 0x99: 1, 0x9a: 1, 0x9b: 1, 0x9c: 1, 0x9d: 1, 0x9e: 1, 0x9f: 1,
    0xa0: 1, 0xa1: 1, 0xa2: 1, 0xa3: 1, 0xa4: 1, 0xa5: 1, 0xa6: 1, 0xa7: 1, 0xa8: 1, 0xa9: 1, 0xaa: 1, 0xab: 1, 0xac: 1, 0xad: 1, 0xae: 1, 0xaf: 1,
    0xb0: 1, 0xb1: 1, 0xb2: 1, 0xb3: 1, 0xb4: 1, 0xb5: 1, 0xb6: 1, 0xb7: 1, 0xb8: 1, 0xb9: 1, 0xba: 1, 0xbb: 1, 0xbc: 1, 0xbd: 1, 0xbe: 1, 0xbf: 1,
    0xd0: 2, 0xd1: 2, 0xd2: 2, 0xd3: 2, 0xd4: 2, 0xd5: 2, 0xd6: 2, 0xd7: 2,
    0xd8: 2, 0xd9: 2, 0xda: 2, 0xdb: 2, 0xdc: 2, 0xdd: 2, 0xde: 2, 0xdf: 2,
    0xe0: 2, 0xe1: 2, 0xe2: 2, 0xe3: 2, 0xe4: 2, 0xe5: 2, 0xe6: 2, 0xe7: 2,
    0xe8: 2, 0xe9: 2, 0xea: 2, 0xeb: 2, 0xec: 2, 0xed: 2, 0xee: 2, 0xef: 2,
    0xfa: 3, 0xfb: 3, 0xfc: 3, 0xfd: 3, 0xfe: 3, 0xff: 3,
}

OP_NAMES = {
    0x00: 'nop', 0x01: 'move', 0x07: 'move-object',
    0x0a: 'move-result', 0x0c: 'move-result-object', 0x0d: 'move-exception',
    0x0e: 'return-void', 0x0f: 'return', 0x11: 'return-object',
    0x12: 'const/4', 0x13: 'const/16', 0x14: 'const',
    0x1a: 'const-string', 0x1c: 'const-class',
    0x1d: 'monitor-enter', 0x1e: 'monitor-exit',
    0x22: 'new-instance', 0x23: 'new-array', 0x27: 'throw',
    0x28: 'goto',
    0x32: 'if-eq', 0x33: 'if-ne', 0x34: 'if-lt', 0x35: 'if-ge', 0x36: 'if-gt', 0x37: 'if-le',
    0x38: 'if-eqz', 0x39: 'if-nez', 0x3a: 'if-ltz', 0x3b: 'if-gez', 0x3c: 'if-gtz', 0x3d: 'if-lez',
    0x52: 'iget', 0x54: 'iget-object', 0x59: 'iput', 0x5b: 'sget-object',
    0x6e: 'invoke-virtual', 0x6f: 'invoke-super', 0x70: 'invoke-direct', 0x71: 'invoke-static', 0x72: 'invoke-interface',
    0x74: 'invoke-virtual/range', 0x76: 'invoke-direct/range', 0x77: 'invoke-static/range',
    0x82: 'long-to-float', 0x87: 'double-to-int',
}

def main():
    dex_files = [
        'tmp/dex_out2/classes.dex',
        'tmp/dex_out2/classes2.dex',
        'tmp/dex_out2/classes3.dex',
        'tmp/dex_out2/classes4.dex',
    ]
    for path in dex_files:
        dex = parse_dex(path)
        if not dex:
            continue
        for cls, class_data_off in dex['classes']:
            if 'MainActivity;' not in cls or 'tareas/' not in cls:
                continue
            print(f"\n=== {cls} in {path} ===")
            methods_in_class = parse_class_data(dex['data'], class_data_off)
            if methods_in_class is None:
                continue
            for method_idx, access, code_off in methods_in_class:
                if method_idx >= len(dex['methods']):
                    continue
                cls_name, method_name, _ = dex['methods'][method_idx]
                code = parse_code_item(dex['data'], code_off)
                if not code:
                    continue
                print(f"\n--- Method: {cls_name}->{method_name} (regs={code['regs']}, ins={code['ins']}) ---")
                pc = 0
                while pc < len(code['insns']):
                    inst = code['insns'][pc]
                    opcode = inst & 0xFF
                    name = OP_NAMES.get(opcode, f'op_{opcode:02x}')
                    width = OP_WIDTHS.get(opcode, 1)
                    extras = []
                    if opcode == 0x1a and width >= 2:
                        idx = code['insns'][pc+1]
                        if idx < len(dex['strings']):
                            extras.append(f'"{dex["strings"][idx]}"')
                    elif opcode == 0x22 and width >= 2:
                        idx = code['insns'][pc+1]
                        if idx < len(dex['types']):
                            extras.append(f'{dex["types"][idx]}')
                    elif opcode in (0x6e, 0x6f, 0x70, 0x71, 0x72):
                        method_idx_val = code['insns'][pc+1] | (code['insns'][pc+2] << 16)
                        if method_idx_val < len(dex['methods']):
                            m_cls, m_name, _ = dex['methods'][method_idx_val]
                            extras.append(f'{m_cls}->{m_name}')
                    elif opcode in (0x74, 0x76, 0x77):
                        method_idx_val = code['insns'][pc+1] | (code['insns'][pc+2] << 16)
                        if method_idx_val < len(dex['methods']):
                            m_cls, m_name, _ = dex['methods'][method_idx_val]
                            extras.append(f'{m_cls}->{m_name}')
                    line = f"  PC={pc:4d}  {name:30s}"
                    if extras:
                        line += f"  {', '.join(extras)}"
                    print(line)
                    pc += width

if __name__ == '__main__':
    main()
