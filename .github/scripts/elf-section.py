#!/usr/bin/env python3
"""Print, as hex, the contents of the section holding a symbol in an ELF32 object.

armcl places each constant in its own section. The host's binutils cannot read
ARM objects, and armcl's own armobjcopy needs a libtinfo.so.5 that Ubuntu 24.04
does not ship, so this reads the object directly.
"""
import struct
import sys


def main(path, symbol):
    data = open(path, "rb").read()
    if data[:4] != b"\x7fELF" or data[4] != 1:
        sys.exit(f"{path}: not an ELF32 object")
    end = "<" if data[5] == 1 else ">"
    shoff, = struct.unpack_from(end + "I", data, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from(end + "HHH", data, 0x2E)
    sections = [struct.unpack_from(end + "IIIIIIIIII", data, shoff + i * shentsize)
                for i in range(shnum)]
    for sh in sections:
        if sh[1] != 2:  # SHT_SYMTAB
            continue
        strtab = sections[sh[6]]
        for offset in range(sh[4], sh[4] + sh[5], sh[9]):
            name, value, size, _, _, shndx = struct.unpack_from(end + "IIIBBH", data, offset)
            start = strtab[4] + name
            if data[start:data.index(b"\0", start)].decode() == symbol:
                section = sections[shndx]
                print(data[section[4] + value:section[4] + value + size].hex())
                return
    sys.exit(f"{path}: symbol {symbol} not found")


if __name__ == "__main__":
    main(*sys.argv[1:])
