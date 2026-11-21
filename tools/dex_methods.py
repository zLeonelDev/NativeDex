"""Dump just the method names + class from DEX, no class_data parsing."""
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
        # Also decode proto
        proto_off = struct.unpack_from('<I', data, 76 + proto_idx*12)[0]
        # proto_id_item: shorty_idx(4), return_type_idx(4), parameters_off(4)
        return_idx = struct.unpack_from('<I', data, proto_off+4)[0]
        ret_type = types[return_idx] if return_idx < len(types) else '?'
        methods.append((cls, name, ret_type))

    return {'strings': strings, 'types': types, 'methods': methods}

if __name__ == '__main__':
    for path in sys.argv[1:]:
        dex = parse_dex(path)
        if not dex:
            continue
        print(f"\n=== {path}: {len(dex['methods'])} methods ===")
        # Find MainActivity methods
        for i, (cls, name, ret) in enumerate(dex['methods']):
            if 'tareas/MainActivity' in cls or ('MainActivity' in cls and 'MainActivity;' in cls):
                print(f"  [{i:5d}] {cls}->{name} -> {ret}")
