"""Read native PE32+ DLL identity and non-forwarded executable exports."""

import struct


MACHINES = {"x86_64": 0x8664, "amd64": 0x8664, "aarch64": 0xAA64, "arm64": 0xAA64}


def dll_exports(path, processor):
    data = path.read_bytes()

    def unpack(fmt, offset):
        if offset < 0 or offset + struct.calcsize(fmt) > len(data):
            raise ValueError("Truncated PE image")
        return struct.unpack_from(fmt, data, offset)

    if not data.startswith(b"MZ"):
        raise ValueError("Expected DOS header on Windows SDK")
    pe, = unpack("<I", 60)
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("Missing expected PE signature")
    machine, count, _, _, _, optional_size, flags = unpack("<HHIIIHH", pe + 4)
    if machine != MACHINES[processor] or not 1 <= count <= 96 or flags & 0x2002 != 0x2002:
        raise ValueError("The Windows SDK must be a native DLL for the selected architecture")
    optional = pe + 24
    if optional_size < 120 or unpack("<H", optional)[0] != 0x020B or not unpack("<I", optional + 108)[0]:
        raise ValueError("Expected PE32+ optional header and export directory")
    exports_rva, exports_size = unpack("<II", optional + 112)
    if not exports_rva or exports_size < 40:
        raise ValueError("Missing PE export directory")

    sections = []
    for i in range(count):
        start = optional + optional_size + 40 * i
        virtual_size, rva, length, raw = unpack("<IIII", start + 8)
        characteristics, = unpack("<I", start + 36)
        if length and raw + length > len(data):
            raise ValueError("Truncated PE section data")
        sections.append((rva, max(virtual_size, length), length, raw, characteristics))

    def locate(rva, size=1):
        matches = [(raw + (rva - begin), raw + length, attributes)
                   for begin, _, length, raw, attributes in sections
                   if rva >= begin and rva - begin + size <= length]
        if len(matches) != 1:
            raise ValueError("PE address is missing or ambiguous")
        return matches[0]

    def string(rva):
        begin, end, _ = locate(rva)
        stop = data.find(b"\0", begin, min(end, begin + 4096))
        if stop <= begin:
            raise ValueError("Invalid PE export name")
        return data[begin:stop].decode("ascii")

    export_offset, _, _ = locate(exports_rva, exports_size)
    name_rva, _, functions_count, names_count, functions, names, ordinals = unpack("<IIIIIII", export_offset + 12)
    if not 0 < names_count <= 100000 or not 0 < functions_count <= 100000:
        raise ValueError("Unexpected PE export table dimensions")
    functions_offset, _, _ = locate(functions, 4 * functions_count)
    names_offset, _, _ = locate(names, 4 * names_count)
    ordinals_offset, _, _ = locate(ordinals, 2 * names_count)
    exported = set()
    for i in range(names_count):
        name = string(unpack("<I", names_offset + 4 * i)[0])
        ordinal, = unpack("<H", ordinals_offset + 2 * i)
        if ordinal >= functions_count:
            raise ValueError("PE export ordinal out of range")
        target, = unpack("<I", functions_offset + 4 * ordinal)
        if target == 0 or exports_rva <= target < exports_rva + exports_size:
            continue
        containing = [attributes for begin, size, _, _, attributes in sections if begin <= target < begin + size]
        if len(containing) != 1:
            raise ValueError("PE export address is missing or ambiguous")
        if containing[0] & 0x20000000:
            locate(target)
            exported.add(name)
    return string(name_rva), machine, exported
