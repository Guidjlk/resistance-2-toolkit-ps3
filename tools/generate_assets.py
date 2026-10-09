#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 Guidjlk
"""Generate empty unlock containers from a user-supplied original v1.60 ELF.

Only generated marker bytes are embedded. No game executable, game key,
account/console metadata or source save is copied into the package.
The containers have valid NPD/CMAC fields but unsigned ECDSA headers.
"""
from pathlib import Path
import argparse
import hashlib
import struct
from Crypto.Cipher import AES
from Crypto.Hash import CMAC

ROOT = Path(__file__).resolve().parent.parent
NAMES = ['rachel', 'fsoldier', 'ravager', 'cloven', 'ranger', 'blackops']
OMAC2 = bytes.fromhex('6ba52976efda16ef3c339fb2971e256b')
OMAC3 = bytes.fromhex('9b515feacf75064981aa604d91a54e97')
EDAT_KEY = bytes.fromhex('be959ca8308defa2e5e180c63712a9ae')


def cmac(key, data):
    return CMAC.new(key, ciphermod=AES).update(data).digest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('elf', type=Path)
    args = parser.parse_args()
    elf = args.elf.read_bytes()
    if hashlib.sha256(elf).hexdigest() != '697bd1df98f79ce099e54e0e60e0a1744d3950ea43a2a06978f69a91e51b3d41':
        raise ValueError('Original BCUS98120 1.60 ELF required')
    game_key = elf[0xE03214-0x10000:0xE03224-0x10000]
    header_key = AES.new(EDAT_KEY, AES.MODE_CBC, bytes(16)).decrypt(game_key)
    lines = ['/* SPDX-License-Identifier: GPL-2.0-only */',
             '/* Copyright (C) 2026 Guidjlk. Generated empty unlock markers. */',
             '#ifndef R2TK_UNLOCK_DATA_H', '#define R2TK_UNLOCK_DATA_H']
    (ROOT/'assets/unlocks').mkdir(parents=True, exist_ok=True)
    for name in NAMES:
        filename = name+'_unlock.edat'
        cid = ('UP9000-BCUS98120_00-'+(name.upper()+'UNLOCK').ljust(16, '0')).encode()
        assert len(cid) == 36
        data = bytearray(272)
        struct.pack_into('>4sIII', data, 0, b'NPD\0', 3, 3, 0)
        data[0x10:0x40] = cid.ljust(48, b'\0')
        data[0x40:0x50] = b'R2TK by Guidjlk'.ljust(16, b'\0')
        data[0x50:0x60] = cmac(OMAC3, data[0x10:0x40]+filename.encode())
        data[0x60:0x70] = cmac(bytes(x ^ y for x, y in zip(game_key, OMAC2)), data[:0x60])
        struct.pack_into('>IIQ', data, 0x80, 0x0C, 16384, 0)
        data[0x90:0xA0] = cmac(header_key, b'')
        data[0xA0:0xB0] = cmac(header_key, data[:0xA0])
        data[0x100:] = b'R2TK 0.1.0'.ljust(16, b'\0')
        assert len(data) == 272
        assert cmac(OMAC3, data[0x10:0x40]+filename.encode()) == data[0x50:0x60]
        assert cmac(bytes(x ^ y for x, y in zip(game_key, OMAC2)), data[:0x60]) == data[0x60:0x70]
        assert cmac(header_key, data[:0xA0]) == data[0xA0:0xB0]
        (ROOT/'assets/unlocks'/filename).write_bytes(data)
        lines.append('static const unsigned char edat_'+name+'[272] = {')
        lines.extend(' '+','.join('0x%02x' % b for b in data[i:i+16])+',' for i in range(0, len(data), 16))
        lines.append('};')
    lines.append('#endif')
    (ROOT/'include/unlock_data.h').write_text('\n'.join(lines)+'\n')
    for name in ['wraith','malikov','grim']:
        (ROOT/'assets/unlocks'/(name+'_unlock.dat')).write_bytes(b'')
    print('Generated nine empty marker files; no game executable copied.')


if __name__ == '__main__':
    main()
