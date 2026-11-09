#!/usr/bin/env python3
"""Pure Python BlockPar DAT writer: unsorted new format (rangers-utils encoding).
No BlockParEditor or Wine runtime required; round-trip validation included.
Reference algorithm: murgesku/rangers-utils, MIT.
"""
from pathlib import Path
import struct
import zlib
import sys

THIS = Path(__file__).resolve().parent
ROOT = THIS.parent
KIT = THIS.parents[4] / 'kit' / 'SRHD-XenoModKit-master'
# Release package does not include the user's XenoModKit. A simple parser for
# small own TXT structure means rebuild remains possible without external kit.

def read_blocks(source: str):
    class Node:
        def __init__(self, name=''):
            self.name = name
            self.entries = []
    root=Node()
    stack=[root]
    for number,line in enumerate(source.splitlines(),1):
        line=line.strip()
        if not line or line.startswith('//'):continue
        if line == '}':
            if len(stack)==1:raise ValueError(f'Unexpected closing at {number}')
            stack.pop()
        elif line.endswith(('^{','~{')):
            node=Node(line[:-2].strip())
            if not node.name:raise ValueError(f'No block name at {number}')
            stack[-1].entries.append(node)
            stack.append(node)
        elif '=' in line:
            k,v=line.split('=',1)
            stack[-1].entries.append((k.strip(),v))
        else:raise ValueError(f'Unrecognized line {number}: {line!r}')
    if len(stack)!=1:raise ValueError('Missing closing brace')
    return root

def encode_block(node):
    # BlockPar root/blocks: bool sorted=False, uint32 count, then elements.
    result=bytearray(struct.pack('<BI',0,len(node.entries)))
    def wchar(s):return s.encode('utf-16le')+b'\x00\x00'
    for item in node.entries:
        if isinstance(item,tuple):
            k,v=item
            result.append(1)
            result.extend(wchar(k))
            result.extend(wchar(v))
        else:
            result.append(2)
            result.extend(wchar(item.name))
            result.extend(encode_block(item))
    return result

def r31(seed):
    while True:
        hi,lo=divmod(seed,0x1f31d)
        seed=lo*0x41a7-hi*0xb14
        if seed<1:seed+=0x7fffffff
        yield (seed-1)&255

def cipher(data:bytes,seed):
    return bytes(c^k for c,k in zip(data,r31(seed)))

def create_dat(source:Path,output:Path):
    root=read_blocks(source.read_text('utf-8'))
    raw=encode_block(root)
    packed=b'ZL01'+struct.pack('<I',len(raw))+zlib.compress(raw,9)
    SEED=0x51a2c3d4
    header=struct.pack('<II',zlib.crc32(packed)&0xffffffff, SEED^0xb1e8c689)
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_bytes(header+cipher(packed,SEED))
    assert read_dat(output)==to_simple(root)
    return len(raw),len(packed)

def to_simple(node):
    return [(k,v) if isinstance((k,v) if isinstance(item,tuple) else item,tuple) else None for item in []] if False else [
        ('P',item[0],item[1]) if isinstance(item,tuple) else ('B',item.name,to_simple(item))
        for item in node.entries
    ]

def read_dat(path):
    import io
    data=path.read_bytes(); crc,xseed=struct.unpack_from('<II',data)
    packed=cipher(data[8:],xseed^0xb1e8c689)
    assert crc == (zlib.crc32(packed)&0xffffffff), 'DAT CRC does not match'
    assert packed[:4]==b'ZL01'
    raw=zlib.decompress(packed[8:]);assert len(raw)==struct.unpack_from('<I',packed,4)[0]
    buf=io.BytesIO(raw)
    def readw():
        bs=bytearray()
        while True:
            pair=buf.read(2)
            if len(pair)!=2:raise ValueError('Truncated WideString')
            if pair==b'\0\0':return bs.decode('utf-16le')
            bs.extend(pair)
    def rec():
        sorted_flag=buf.read(1)
        if sorted_flag not in (b'\x00',b'\x01'):raise ValueError('Bad sorted flag')
        count=struct.unpack('<I',buf.read(4))[0]
        items=[]
        for _ in range(count):
            if sorted_flag==b'\x01':
                idx,dup=struct.unpack('<II',buf.read(8))
            kind=buf.read(1)
            if kind==b'\x01':items.append(('P',readw(),readw()))
            elif kind==b'\x02':items.append(('B',readw(),rec()))
            else:raise ValueError(f'Unknown item kind {kind}')
        return items
    elements=rec()
    assert buf.read()==b'', 'trailing DAT bytes'
    return elements

if __name__=='__main__':
    p=ROOT/'CFG/Main.dat'
    print('built',p,'sizes',create_dat(ROOT/'SOURCE/CFG/Main.txt',p))
    print('tree:',read_dat(p))
