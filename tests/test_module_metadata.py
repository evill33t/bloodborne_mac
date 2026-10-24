"""Prepared unwind metadata from synthetic ELF program headers, no game files."""
from paths import ROOT
import copy
import struct
import unittest
from link_modules import module_info, pack_module_info


def fixture():
    elf=bytearray(256)
    # Two bounded EH records and a terminating zero; then the indexed header.
    elf[64:96]=struct.pack('<I',12)+bytes(12)+struct.pack('<I',8)+bytes(8)+bytes(4)
    elf[128:140]=b'\x01\x1b\x03\x3b'+struct.pack('<iI',64-132,1)
    return dict(elf=elf,tags={12:0,13:16},ph=[
        dict(type=1,vaddr=0,memsz=256,filesz=256,offset=0,flags=5,align=16),
        dict(type=7,vaddr=200,memsz=16,filesz=0,offset=200,flags=4,align=16),
        dict(type=0x6474e550,vaddr=128,memsz=12,filesz=12,offset=128,flags=4,align=4)])


class ModuleMetadataTests(unittest.TestCase):
    def test_exact_record_extent_and_relocated_fields(self):
        info=module_info(fixture(),'fixture.prx',0x10000,1,2)
        self.assertEqual(info['eh_frame_hdr_addr'],0x10080)
        self.assertEqual(info['eh_frame_addr'],0x10040)
        self.assertEqual(info['eh_frame_size'],32)
        self.assertEqual(info['init_proc_addr'],0x10000)
        self.assertEqual(info['tls_init_addr'],0x100c8)
        self.assertEqual(info['tls_index'],2)
        blob=pack_module_info(info)
        self.assertEqual(len(blob),424)
        self.assertEqual(struct.unpack_from('<QQII',blob,328),(0x10080,0x10040,12,32))
        self.assertEqual(struct.unpack_from('<QIi',blob,352),(0x10000,256,5))
        self.assertEqual(struct.unpack_from('<II',blob,416),(1,1))

    def test_known_image_without_eh_header_reports_absence(self):
        m=fixture();m['ph'].pop()
        info=module_info(m,'assembly.prx',0x10000,1,2)
        self.assertEqual((info['eh_frame_addr'],info['eh_frame_size']),(0,0))

    def test_absolute_and_data_relative_pointer_encodings(self):
        for encoding,value in [(4,64),(0x34,64-128)]:
            m=fixture();m['ph'][-1]['memsz']=16
            m['elf'][129]=encoding
            # datarel sdata8 supports negative offsets, unsigned absolute does not.
            if encoding==0x34:
                m['elf'][129]=0x3c
                struct.pack_into('<q',m['elf'],132,value)
            else:struct.pack_into('<Q',m['elf'],132,value)
            info=module_info(m,'fixture.prx',0x10000,1,2)
            self.assertEqual(info['eh_frame_addr'],0x10040)

    def test_invalid_header_pointer_and_record_are_rejected(self):
        mutations=[
            (lambda m:m['elf'].__setitem__(128,2),'unsupported EH frame pointer'),
            (lambda m:m['elf'].__setitem__(129,0x91),'unsupported EH frame pointer'),
            (lambda m:struct.pack_into('<i',m['elf'],132,4096),'unmapped EH frame pointer'),
            (lambda m:struct.pack_into('<I',m['elf'],64,128),'invalid EH frame record'),
            (lambda m:m['ph'][-1].update(vaddr=256),'unmapped module unwind'),
        ]
        for mutate,message in mutations:
            with self.subTest(message=message):
                m=fixture();mutate(m)
                with self.assertRaisesRegex(ValueError,message):module_info(m,'fixture.prx',0,0,1)

    def test_segment_and_name_limits(self):
        m=fixture();m['ph'].extend(copy.deepcopy(m['ph'][:1])*4)
        with self.assertRaisesRegex(ValueError,'segment count'):module_info(m,'fixture.prx',0,0,1)
        with self.assertRaisesRegex(ValueError,'name/segment'):module_info(fixture(),'x'*256,0,0,1)


if __name__=='__main__':unittest.main()
