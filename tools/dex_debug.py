import struct

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

with open('tmp/dex_out2/classes3.dex', 'rb') as f:
    data = f.read()

# DEX header offsets
print(f"file_size: {len(data)}")
print(f"header_size: {struct.unpack_from('<I', data, 32)[0]}")
print(f"endian_tag: {data[40:44]}")
print(f"link_size: {struct.unpack_from('<I', data, 52)[0]}")
print(f"map_off: {struct.unpack_from('<I', data, 52)[0]}")
print(f"string_ids_size: {struct.unpack_from('<I', data, 56)[0]}")
print(f"string_ids_off: 0x{struct.unpack_from('<I', data, 60)[0]:x}")
print(f"type_ids_size: {struct.unpack_from('<I', data, 64)[0]}")
print(f"type_ids_off: 0x{struct.unpack_from('<I', data, 68)[0]:x}")
print(f"proto_ids_size: {struct.unpack_from('<I', data, 72)[0]}")
print(f"proto_ids_off: 0x{struct.unpack_from('<I', data, 76)[0]:x}")
print(f"field_ids_size: {struct.unpack_from('<I', data, 80)[0]}")
print(f"field_ids_off: 0x{struct.unpack_from('<I', data, 84)[0]:x}")
print(f"method_ids_size: {struct.unpack_from('<I', data, 88)[0]}")
print(f"method_ids_off: 0x{struct.unpack_from('<I', data, 92)[0]:x}")
print(f"class_defs_size: {struct.unpack_from('<I', data, 96)[0]}")
print(f"class_defs_off: 0x{struct.unpack_from('<I', data, 100)[0]:x}")

# Now read the onCreate method
target_code_off = 0x6834
print(f"\nonCreate code_off: 0x{target_code_off:x}")
insns_size = struct.unpack_from('<I', data, target_code_off+12)[0]
insns = [struct.unpack_from('<H', data, target_code_off+16 + i*2)[0] for i in range(insns_size)]
print(f"insns_size: {insns_size}")

# PC=10 is invoke-virtual. Method idx = insns[11] | (insns[12] << 16)
# Wait, PC is at insns[pc]. Width is 3 means args at insns[pc+1] and insns[pc+2]
midx = insns[10+1] | (insns[10+2] << 16)
print(f"\nPC=10 opcode: 0x{insns[10]:02x}")
print(f"PC=11 arg1: 0x{insns[11]:04x} ({insns[11]})")
print(f"PC=12 arg2: 0x{insns[12]:04x} ({insns[12]})")
print(f"method_idx: {midx}")

# Read the method_id at this index
mids = struct.unpack_from('<I', data, 88)[0]
mido = struct.unpack_from('<I', data, 92)[0]
print(f"Total methods: {mids}")
if midx < mids:
    off = mido + midx*8
    cidx = struct.unpack_from('<H', data, off)[0]
    nidx = struct.unpack_from('<I', data, off+4)[0]
    print(f"method: class_idx={cidx}, name_idx={nidx}")
    # Now resolve
    tids = struct.unpack_from('<I', data, 64)[0]
    tido = struct.unpack_from('<I', data, 68)[0]
    type_desc_idx = struct.unpack_from('<I', data, tido + cidx*4)[0]
    print(f"type_desc_idx: {type_desc_idx}")
    sids = struct.unpack_from('<I', data, 56)[0]
    sido = struct.unpack_from('<I', data, 60)[0]
    if type_desc_idx < sids:
        str_data_off = struct.unpack_from('<I', data, sido + type_desc_idx*4)[0]
        cls_name = read_mutf8_string(data, str_data_off)
        print(f"class: {cls_name}")
    if nidx < sids:
        str_data_off = struct.unpack_from('<I', data, sido + nidx*4)[0]
        m_name = read_mutf8_string(data, str_data_off)
        print(f"method: {m_name}")
