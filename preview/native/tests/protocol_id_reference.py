#!/usr/bin/env python3
"""Independent offline specification: Python integers + qualified libcrypto.

No C++ helper is loaded. This script has NOT been run by the source author.
The reviewer supplies the exact OpenSSL DSO selected for the C++ test build.
Only public fixed test keys enter command lines/files; production keys do not.
"""
import argparse
import ctypes
import hashlib
import pathlib
import struct


class AES:
    def __init__(self, library, key):
        self.lib = library
        self.ctx = library.EVP_CIPHER_CTX_new()
        if not self.ctx:
            raise RuntimeError("EVP context allocation failed")
        try:
            if library.EVP_EncryptInit_ex(self.ctx, library.EVP_aes_256_ecb(), None,
                                          key, None) != 1:
                raise RuntimeError("AES initialization failed")
            if library.EVP_CIPHER_CTX_set_padding(self.ctx, 0) != 1:
                raise RuntimeError("AES padding configuration failed")
        except BaseException:
            self.close()
            raise

    def close(self):
        if self.ctx:
            self.lib.EVP_CIPHER_CTX_free(self.ctx)
            self.ctx = None

    def block(self, data):
        if len(data) != 16:
            raise ValueError("AES reference requires exactly one block")
        output = ctypes.create_string_buffer(32)
        size = ctypes.c_int()
        if self.lib.EVP_EncryptUpdate(self.ctx, output, ctypes.byref(size),
                                      data, len(data)) != 1 or size.value != 16:
            raise RuntimeError("AES block evaluation failed")
        return output.raw[:16]


def library(path):
    lib = ctypes.CDLL(str(path.resolve()), mode=ctypes.RTLD_LOCAL)
    lib.EVP_CIPHER_CTX_new.restype = ctypes.c_void_p
    lib.EVP_CIPHER_CTX_new.argtypes = []
    lib.EVP_CIPHER_CTX_free.argtypes = [ctypes.c_void_p]
    lib.EVP_CIPHER_CTX_free.restype = None
    lib.EVP_aes_256_ecb.restype = ctypes.c_void_p
    lib.EVP_aes_256_ecb.argtypes = []
    lib.EVP_EncryptInit_ex.argtypes = [ctypes.c_void_p, ctypes.c_void_p,
                                     ctypes.c_void_p, ctypes.c_char_p, ctypes.c_void_p]
    lib.EVP_EncryptInit_ex.restype = ctypes.c_int
    lib.EVP_CIPHER_CTX_set_padding.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.EVP_CIPHER_CTX_set_padding.restype = ctypes.c_int
    lib.EVP_EncryptUpdate.argtypes = [ctypes.c_void_p, ctypes.c_void_p,
                                    ctypes.POINTER(ctypes.c_int), ctypes.c_char_p,
                                    ctypes.c_int]
    lib.EVP_EncryptUpdate.restype = ctypes.c_int
    return lib


def encoded(purpose, round_index, pair):
    # A separate struct-based encoding, not a translation of C++ array writes.
    return b"U32S" + bytes([1, purpose, 32, 0]) + struct.pack(">HIH", round_index, pair, 0)


def permutation(aes, value, pivots, inverse=False):
    for index in (reversed(range(1024)) if inverse else range(1024)):
        partner = value ^ pivots[index]
        if aes.block(encoded(2, index, max(value, partner)))[0] % 2:
            value = partner
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--libcrypto", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()
    lib = library(args.libcrypto)
    keys = (bytes(range(32)), bytes(32))
    # NIST FIPS197 Appendix C.3, independently checks byte order/AES selection.
    kat = AES(lib, keys[0])
    try:
        if kat.block(bytes.fromhex("00112233445566778899aabbccddeeff")) != bytes.fromhex(
                "8ea2b7ca516745bfeafc49904b496089"):
            raise RuntimeError("AES256 known-answer control failed")
    finally:
        kat.close()
    values = tuple(range(8)) + (0x7ffffffe, 0x7fffffff, 0x80000000, 0xfffffffe, 0xffffffff)
    lines = ["# UNIQUE32-SWAP-OR-NOT-1 width32 rounds1024",
             "# libcrypto_sha256 " + hashlib.sha256(args.libcrypto.read_bytes()).hexdigest()]
    for key in keys:
        aes = AES(lib, key)
        try:
            pivots = [int.from_bytes(aes.block(encoded(1, index, 0))[:4], "big")
                      for index in range(1024)]
            for value in values:
                output = permutation(aes, value, pivots)
                if permutation(aes, output, pivots, inverse=True) != value:
                    raise RuntimeError("Reference inverse control failed")
                lines.append(f"{key.hex()} {value:08x} {output:08x}")
        finally:
            aes.close()
    args.output.write_text("\n".join(lines) + "\n", encoding="ascii")


if __name__ == "__main__":
    main()
