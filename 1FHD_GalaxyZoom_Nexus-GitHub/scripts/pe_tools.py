"""Small PE32 reader for exports, section mapping and review metadata."""
from pathlib import Path
import struct

class PE:
    def __init__(self, path):
        self.data = Path(path).read_bytes()
        assert self.data[:2] == b'MZ'
        self.nt = self.u32(0x3c); assert self.data[self.nt:self.nt+4] == b'PE\0\0'
        self.opt = self.nt+24; assert self.u16(self.opt) == 0x10b
        self.machine = self.u16(self.nt+4); self.base = self.u32(self.opt+28)
        self.image_size = self.u32(self.opt+56); self.sections = []
        section_table = self.opt+self.u16(self.nt+20)
        for i in range(self.u16(self.nt+6)):
            at = section_table+40*i
            name = self.data[at:at+8].rstrip(b'\0').decode('ascii')
            vsize, rva, rawsize, raw = struct.unpack_from('<4I', self.data, at+8)
            self.sections.append((name, rva, vsize, raw, rawsize, self.u32(at+36)))
    def u16(self, offset): return struct.unpack_from('<H', self.data, offset)[0]
    def u32(self, offset): return struct.unpack_from('<I', self.data, offset)[0]
    def offset(self, rva):
        if rva < self.u32(self.opt+60): return rva
        for _, base, vsize, raw, rawsize, _ in self.sections:
            if base <= rva < base+max(vsize,rawsize):
                assert rva-base < rawsize, 'RVA has no file-backed bytes'
                return raw+rva-base
        raise ValueError('RVA outside sections: '+hex(rva))
    def at(self, rva, count):
        offset = self.offset(rva); return self.data[offset:offset+count]
    def directory(self, index): return struct.unpack_from('<II', self.data, self.opt+96+8*index)
    def mapped(self):
        image = bytearray(self.image_size); headers = self.u32(self.opt+60)
        image[:headers] = self.data[:headers]
        for _, rva, _, raw, size, _ in self.sections:
            if size: image[rva:rva+size] = self.data[raw:raw+size]
        return image
    def exports(self):
        at = self.offset(self.directory(0)[0]); result = {}
        functions, names, ordinals = (self.u32(at+x) for x in [28,32,36])
        for i in range(self.u32(at+24)):
            start = self.offset(self.u32(self.offset(names)+4*i))
            name = self.data[start:self.data.index(b'\0',start)].decode('ascii')
            ordinal = self.u16(self.offset(ordinals)+2*i)
            result[name] = self.u32(self.offset(functions)+4*ordinal)
        return result
    def symbols(self):
        table, count = self.u32(self.nt+12), self.u32(self.nt+16)
        strings = table+count*18; result = {}; index = 0
        while index < count:
            at = table+18*index
            if self.u32(at) == 0:
                start = strings+self.u32(at+4); end = self.data.index(b'\0',start)
                name = self.data[start:end].decode('ascii')
            else: name = self.data[at:at+8].rstrip(b'\0').decode('ascii')
            value, section = struct.unpack_from('<Ih', self.data, at+8)
            if 0 < section <= len(self.sections): result[name] = self.sections[section-1][1]+value
            index += 1+self.data[at+17]
        return result
    def audit(self):
        assert self.machine == 0x14c, 'DLL must be i386'
        assert self.u16(self.nt+22) & 0x2000, 'Not marked as DLL'
        entry = self.u32(self.opt+16)
        assert any(rva <= entry < rva+max(vsize,rawsize) and flags & 0x20000000
                   for _,rva,vsize,_,rawsize,flags in self.sections), 'Invalid DLL entry'
        assert set(self.exports()) == {'FHDZoom_Init','FHDZoom_Status'}
        imports = self.directory(1)
        if imports != (0,0): assert not any(self.at(*imports)), 'Unexpected static imports'
        assert all(self.directory(5)), 'Missing base relocations'
        return {'machine':'i386 PE32 DLL','exports':sorted(self.exports()),'entry_rva':hex(entry),
                'image_base':hex(self.base),'image_size':hex(self.image_size),
                'base_relocations_present':True,'static_imports':[],
                'linker_version':'.'.join(str(x) for x in self.data[self.opt+2:self.opt+4])}
