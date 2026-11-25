import struct
import tempfile
import unittest
from pathlib import Path

from pe_exports import MACHINES, dll_exports


def fixture(machine, ordinal=0):
    pe_offset, optional_size = 0x80, 0xF0
    data = bytearray(0x600)
    data[0:2] = b"MZ"
    struct.pack_into("<I", data, 60, pe_offset)
    data[pe_offset:pe_offset + 4] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", data, pe_offset + 4, machine, 2, 0, 0, 0, optional_size, 0x2002)
    optional = pe_offset + 24
    struct.pack_into("<H", data, optional, 0x20B)
    struct.pack_into("<III", data, optional + 108, 16, 0x2000, 0x100)
    sections = optional + optional_size
    struct.pack_into("<IIII", data, sections + 8, 0x200, 0x1000, 0x200, 0x200)
    struct.pack_into("<I", data, sections + 36, 0x60000020)
    struct.pack_into("<IIII", data, sections + 48, 0x200, 0x2000, 0x200, 0x400)
    struct.pack_into("<I", data, sections + 76, 0x40000040)
    struct.pack_into("<IIIIIII", data, 0x40C, 0x2080, 1, 3, 3, 0x2030, 0x2040, 0x2050)
    struct.pack_into("<III", data, 0x430, 0x1000, 0x20A0, 0x2190)
    struct.pack_into("<III", data, 0x440, 0x20C0, 0x20D0, 0x20E0)
    struct.pack_into("<HHH", data, 0x450, ordinal, 1, 2)
    for offset, name in [(0x480, b"torrent-rasterbar.dll"), (0x4A0, b"KERNEL32.Sleep"),
                         (0x4C0, b"valid"), (0x4D0, b"forwarded"), (0x4E0, b"data")]:
        data[offset:offset + len(name)] = name
    return data


class PeExports(unittest.TestCase):
    def parse(self, data, processor):
        with tempfile.TemporaryDirectory() as work:
            path = Path(work) / "torrent-rasterbar.dll"
            path.write_bytes(data)
            return dll_exports(path, processor)

    def test_only_local_executable_exports(self):
        for processor in ("x86_64", "arm64"):
            machine = MACHINES[processor]
            self.assertEqual(self.parse(fixture(machine), processor),
                             ("torrent-rasterbar.dll", machine, {"valid"}))

    def test_foreign_machine_rejected(self):
        with self.assertRaisesRegex(ValueError, "selected architecture"):
            self.parse(fixture(MACHINES["x86_64"]), "arm64")

    def test_out_of_bounds_ordinal_rejected(self):
        with self.assertRaisesRegex(ValueError, "ordinal out of range"):
            self.parse(fixture(MACHINES["x86_64"], ordinal=12), "x86_64")

    def test_truncated_file_rejected(self):
        with self.assertRaisesRegex(ValueError, "Truncated PE"):
            self.parse(fixture(MACHINES["x86_64"])[:0x500], "x86_64")


if __name__ == "__main__":
    unittest.main()
