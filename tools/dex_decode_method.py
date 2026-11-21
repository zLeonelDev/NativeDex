"""Decode bytecode of any method from extracted DEX files.

Usage: python tools/dex_decode_method.py <class_descriptor> <method_name> [dex_dir]

Example:
  python tools/dex_decode_method.py "Lcom/example/app/MainActivity;" onCreate
  python tools/dex_decode_method.py "Lcom/example/app/MainActivity;" onCreate path/to/dex_dir
"""
import struct, sys, os, glob

def parse_uleb128(data, offset):
    result = 0; shift = 0
    while True:
        b = data[offset]; offset += 1
        result |= (b & 0x7f) << shift
        if (b & 0x80) == 0: break
        shift += 7
    return result, offset

def read_mutf8_string(data, offset):
    length, content_off = parse_uleb128(data, offset)
    end = data.find(b'\x00', content_off)
    if end < 0: end = content_off + length
    return data[content_off:end].decode('utf-8', errors='replace')

OPCODE_NAMES = {
    0x00:'nop',0x01:'move',0x02:'move/from16',0x03:'move/16',
    0x04:'move-wide',0x05:'move-wide/from16',0x06:'move-wide/16',
    0x07:'move-object',0x08:'move-object/from16',0x09:'move-object/16',
    0x0a:'move-result',0x0b:'move-result-wide',0x0c:'move-result-object',
    0x0d:'move-exception',0x0e:'return-void',0x0f:'return',0x10:'return-wide',
    0x11:'return-object',0x12:'const/4',0x13:'const/16',0x14:'const',0x15:'const/high16',
    0x16:'const-wide/16',0x17:'const-wide/32',0x18:'const-wide',0x19:'const-wide/high16',
    0x1a:'const-string',0x1b:'const-string/jumbo',0x1c:'const-class',
    0x1d:'monitor-enter',0x1e:'monitor-exit',0x1f:'check-cast',0x20:'instance-of',
    0x21:'array-length',0x22:'new-instance',0x23:'new-array',
    0x24:'filled-new-array',0x25:'filled-new-array/range',0x26:'fill-array-data',
    0x27:'throw',0x28:'goto',0x29:'goto/16',0x2a:'goto/32',
    0x2b:'packed-switch',0x2c:'sparse-switch',
    0x2d:'cmpl-float',0x2e:'cmpg-float',0x2f:'cmpl-double',0x30:'cmpg-double',
    0x31:'cmp-long',
    0x32:'if-eq',0x33:'if-ne',0x34:'if-lt',0x35:'if-ge',0x36:'if-gt',0x37:'if-le',
    0x38:'if-eqz',0x39:'if-nez',0x3a:'if-ltz',0x3b:'if-gez',0x3c:'if-gtz',0x3d:'if-lez',
    0x44:'aput',0x45:'aput-wide',0x46:'aput-byte',0x47:'aput-char',0x48:'aput-short',
    0x49:'aput-object',
    0x4a:'aget',0x4b:'aget-wide',0x4c:'aget-byte',0x4d:'aget-char',0x4e:'aget-short',
    0x4f:'aget-object',
    0x50:'iget',0x51:'iget-wide',0x52:'iget-byte',0x53:'iget-char',0x54:'iget-short',
    0x55:'iget-object',0x56:'iget-boolean',
    0x57:'iput',0x58:'iput-wide',0x59:'iput-byte',0x5a:'iput-char',0x5b:'iput-short',
    0x5c:'iput-object',0x5d:'iput-boolean',
    0x5e:'sget',0x5f:'sget-wide',0x60:'sget-byte',0x61:'sget-char',0x62:'sget-short',
    0x63:'sget-object',0x64:'sget-boolean',
    0x65:'sput',0x66:'sput-wide',0x67:'sput-byte',0x68:'sput-char',0x69:'sput-short',
    0x6a:'sput-object',0x6b:'sput-boolean',
    0x6c:'invoke-virtual',0x6d:'invoke-super',0x6e:'invoke-direct',0x6f:'invoke-static',
    0x70:'invoke-interface',0x71:'invoke-virtual/range',0x72:'invoke-super/range',
    0x73:'invoke-direct/range',0x74:'invoke-static/range',0x75:'invoke-interface/range',
    0x76:'invoke-polymorphic',0x77:'invoke-polymorphic/range',
    0x78:'invoke-custom',0x79:'invoke-custom/range',
}

W = {}
for op, w in [(0,1),(1,1),(4,1),(7,1),(0xa,1),(0xb,1),(0xc,1),(0xd,1),
    (0xe,1),(0xf,1),(0x10,1),(0x11,1),(0x12,1),(0x13,2),(0x14,3),(0x15,2),
    (0x16,2),(0x17,2),(0x18,5),(0x19,2),(0x1a,2),(0x1b,3),(0x1c,2),
    (0x1d,1),(0x1e,1),(0x1f,2),(0x20,2),(0x21,1),(0x22,2),(0x23,2),(0x24,3),(0x25,3),
    (0x26,3),(0x27,1),(0x28,1),(0x29,2),(0x2a,3),(0x2b,3),(0x2c,3),
    (0x32,2),(0x33,2),(0x34,2),(0x35,2),(0x36,2),(0x37,2),
    (0x38,2),(0x39,2),(0x3a,2),(0x3b,2),(0x3c,2),(0x3d,2)]:
    W[op] = w
for op in range(0x44,0x4c): W[op] = 2
for op in range(0x4c,0x5a): W[op] = 2
for op in range(0x5a,0x68): W[op] = 2
for op in range(0x68,0x7a): W[op] = 3
for op in range(0x7b,0x82): W[op] = 1
for op in range(0x82,0x90): W[op] = 1
for op in range(0x90,0xd0): W[op] = 1
for op in range(0xd0,0xf0): W[op] = 2
for op in range(0xf0,0x100): W[op] = 3

def parse_dex(path):
    with open(path, 'rb') as f:
        data = f.read()
    if data[:4] != b'dex\n': return None
    sids = struct.unpack_from('<I', data, 56)[0]
    sido = struct.unpack_from('<I', data, 60)[0]
    tids = struct.unpack_from('<I', data, 64)[0]
    tido = struct.unpack_from('<I', data, 68)[0]
    pids = struct.unpack_from('<I', data, 72)[0]
    pido = struct.unpack_from('<I', data, 76)[0]
    fids = struct.unpack_from('<I', data, 80)[0]
    fido = struct.unpack_from('<I', data, 84)[0]
    mids = struct.unpack_from('<I', data, 88)[0]
    mido = struct.unpack_from('<I', data, 92)[0]
    cds = struct.unpack_from('<I', data, 96)[0]
    cdo = struct.unpack_from('<I', data, 100)[0]

    strings = [read_mutf8_string(data, struct.unpack_from('<I', data, sido + i*4)[0]) for i in range(sids)]
    types = [strings[struct.unpack_from('<I', data, tido + i*4)[0]] for i in range(tids)]
    protos = []
    for i in range(pids):
        off = pido + i * 12
        shorty = struct.unpack_from('<I', data, off)[0]
        ret = struct.unpack_from('<I', data, off+4)[0]
        params_off = struct.unpack_from('<I', data, off+8)[0]
        protos.append((strings[shorty], types[ret], params_off))
    methods = []
    for i in range(mids):
        off = mido + i*8
        cidx = struct.unpack_from('<H', data, off)[0]
        pidx = struct.unpack_from('<H', data, off+2)[0]
        nidx = struct.unpack_from('<I', data, off+4)[0]
        methods.append((types[cidx], strings[nidx], protos[pidx] if pidx < len(protos) else ('?','?',0)))
    return {'strings': strings, 'types': types, 'methods': methods, 'classes': (cds, cdo), 'data': data}

def format_proto(proto):
    shorty, ret, _ = proto
    return f"{shorty} -> {ret}"

def decode_method(dex, target_class, target_method):
    cds, cdo = dex['classes']
    data = dex['data']
    for i in range(cds):
        off = cdo + i*32
        if off + 32 > len(data): break
        cidx = struct.unpack_from('<I', data, off)[0]
        if cidx >= len(dex['types']): continue
        cls = dex['types'][cidx]
        if cls == target_class:
            target_cdata = struct.unpack_from('<I', data, off+24)[0]
            if target_cdata == 0: return None, None
            o = target_cdata
            sf, o = parse_uleb128(data, o)
            if_, o = parse_uleb128(data, o)
            dm, o = parse_uleb128(data, o)
            vm, o = parse_uleb128(data, o)
            for _ in range(sf + if_):
                _, o = parse_uleb128(data, o); _, o = parse_uleb128(data, o)
            midx = 0
            for _ in range(dm):
                d, o = parse_uleb128(data, o); midx += d
                access, o = parse_uleb128(data, o)
                code_off, o = parse_uleb128(data, o)
                if midx < len(dex['methods']) and dex['methods'][midx][1] == target_method:
                    return code_off, dex['methods'][midx]
            midx = 0
            for _ in range(vm):
                d, o = parse_uleb128(data, o); midx += d
                access, o = parse_uleb128(data, o)
                code_off, o = parse_uleb128(data, o)
                if midx < len(dex['methods']) and dex['methods'][midx][1] == target_method:
                    return code_off, dex['methods'][midx]
            return None, None
    return None, None

def disasm(dex, code_off):
    data = dex['data']
    if code_off == 0 or code_off >= len(data):
        return []
    regs = struct.unpack_from('<H', data, code_off)[0]
    ins = struct.unpack_from('<H', data, code_off+2)[0]
    outs = struct.unpack_from('<H', data, code_off+4)[0]
    tries = struct.unpack_from('<H', data, code_off+6)[0]
    insns_size = struct.unpack_from('<I', data, code_off+12)[0]
    insns = [struct.unpack_from('<H', data, code_off+16 + i*2)[0] for i in range(insns_size)]

    result = []
    result.append(f"  registers={regs} inputs={ins} outputs={outs} tries={tries} insns={insns_size}")
    pc = 0
    while pc < len(insns):
        inst = insns[pc]
        op = inst & 0xFF
        width = W.get(op, 1)
        name = OPCODE_NAMES.get(op, f'0x{op:02x}')
        extras = ''

        if op == 0x1a and width >= 2:
            idx = insns[pc+1]
            if idx < len(dex['strings']):
                extras = f'"{dex["strings"][idx]}"'
        elif op == 0x1c and width >= 2:
            idx = insns[pc+1]
            if idx < len(dex['types']):
                extras = dex['types'][idx]
        elif op in range(0x6c, 0x7a) and width >= 2:
            idx = insns[pc+1]
            if idx < len(dex['methods']):
                m_cls, m_name, m_proto = dex['methods'][idx]
                extras = f'{m_cls}->{m_name} {m_proto[0]}'
        elif op in (0x22,) and width >= 2:
            idx = insns[pc+1]
            if idx < len(dex['types']):
                extras = dex['types'][idx]
        elif op == 0x12:
            a = (inst >> 8) & 0xF
            b = (inst >> 12) & 0xF
            extras = f'v{a}, #{b}'
        elif op == 0x13:
            a = (inst >> 8) & 0xFF
            extras = f'v{a}, #{insns[pc+1]}'
        elif op == 0x14:
            a = (inst >> 8) & 0xFF
            val = (insns[pc+2] << 16) | insns[pc+1]
            extras = f'v{a}, #0x{val:08x}'
        elif op == 0x15:
            a = (inst >> 8) & 0xFF
            extras = f'v{a}, #{insns[pc+1]}0000'
        elif op == 0x12:
            a = (inst >> 8) & 0xF
            extras = f'v{a}'
        elif op == 0x28:
            off = (inst >> 8) & 0xFF
            if off & 0x80: off -= 0x100
            extras = f'+{off}'
        elif op == 0x29:
            extras = f'+{insns[pc+1]}'
        elif op in range(0x32, 0x3e) and width >= 2:
            a = (inst >> 8) & 0xF
            b = (inst >> 12) & 0xF
            off = insns[pc+1]
            if off & 0x8000: off -= 0x10000
            extras = f'v{a}, v{b}, +{off}'
        elif op in range(0x38, 0x3e) and width >= 2:
            a = (inst >> 8) & 0xFF
            off = insns[pc+1]
            if off & 0x8000: off -= 0x10000
            extras = f'v{a}, +{off}'
        elif op in (0x01, 0x07):
            a = (inst >> 8) & 0xF
            b = (inst >> 12) & 0xF
            extras = f'v{a}, v{b}'
        elif op in range(0x50, 0x5e) and width >= 2:
            a = (inst >> 8) & 0xF
            b = (inst >> 12) & 0xF
            idx = insns[pc+1]
            extras = f'v{a}, v{b}, field@{idx}'
        elif op in range(0x5e, 0x6c) and width >= 2:
            a = (inst >> 8) & 0xFF
            idx = insns[pc+1]
            extras = f'v{a}, field@{idx}'
        elif op == 0x0e:
            extras = ''
        elif op == 0x0f:
            a = (inst >> 8) & 0xFF
            extras = f'v{a}'
        elif op == 0x11:
            a = (inst >> 8) & 0xFF
            extras = f'v{a}'

        result.append(f"  {pc:4d}: {name:24s} {extras}")
        pc += width
    return result

def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)

    target_class = sys.argv[1]
    target_method = sys.argv[2]
    dex_dir = sys.argv[3] if len(sys.argv) > 3 else 'tmp'

    # Find DEX files
    dex_files = sorted(glob.glob(os.path.join(dex_dir, '**', 'classes*.dex'), recursive=True))
    if not dex_files:
        print(f"No DEX files found in {dex_dir}")
        sys.exit(1)

    print(f"Searching for {target_class}->{target_method} in {len(dex_files)} DEX files...")
    found = False
    for path in dex_files:
        dex = parse_dex(path)
        if not dex: continue
        code_off, method_info = decode_method(dex, target_class, target_method)
        if code_off is not None:
            found = True
            proto_str = format_proto(method_info[2])
            print(f"\n=== FOUND in {os.path.basename(path)} ===")
            print(f"Method: {method_info[0]}->{method_info[1]} {proto_str}")
            print(f"Code offset: 0x{code_off:x}")
            lines = disasm(dex, code_off)
            for line in lines:
                print(line)

            # Also find and decode the superclass method if it's invoke-super
            print(f"\n--- Searching superclass methods ---")
            super_class = None
            if target_class.startswith('L') and target_class.endswith(';'):
                base = target_class[1:-1]
                # Common superclass chain
                candidates = [
                    'Landroidx/appcompat/app/AppCompatActivity;',
                    'Landroidx/fragment/app/FragmentActivity;',
                    'Landroidx/activity/ComponentActivity;',
                    'Landroid/app/Activity;',
                    'Landroid/content/ContextWrapper;',
                    'Landroid/content/Context;',
                    'Ljava/lang/Object;',
                ]
                for cand in candidates:
                    for p2 in dex_files:
                        dex2 = parse_dex(p2)
                        if not dex2: continue
                        co2, mi2 = decode_method(dex2, cand, target_method)
                        if co2 is not None:
                            proto2 = format_proto(mi2[2])
                            print(f"\n  Super: {cand}->{target_method} {proto2} in {os.path.basename(p2)}")
                            print(f"  Code offset: 0x{co2:x}")
                            lines2 = disasm(dex2, co2)
                            for line in lines2:
                                print(line)
                            super_class = cand
                            break
                    if super_class: break
                if not super_class:
                    print("  (superclass method not found)")
            break

    if not found:
        print(f"Method {target_class}->{target_method} not found in any DEX file")
        # List similar class names
        for path in dex_files:
            dex = parse_dex(path)
            if not dex: continue
            for t in dex['types']:
                if target_class.split('/')[-1].replace(';','') in t:
                    print(f"  Similar: {t} in {os.path.basename(path)}")

if __name__ == '__main__':
    main()
