"""Boundary tests for the native loader; uses tiny synthetic x86-64 images."""
from paths import ROOT
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import os
from link_modules import pack_module_info

EXE = Path(os.environ.get('BB_PROBE', str(ROOT / 'out/bb-probe')))


def package(code, relocs=(), names=(), capabilities=None):
    image = code.ljust(4096, b'\0')
    header = struct.pack('<8sQQQQQ', b'BBPROBE1' if capabilities is None else b'BBPROBE2', len(image), 0, 1, len(relocs), len(names))
    if capabilities is not None:
        header += struct.pack('<Q', capabilities)
    segment = struct.pack('<QQQ', 0, len(image), 5)
    return (header + segment + b''.join(n.encode().ljust(128, b'\0') for n in names)
            + b''.join(struct.pack('<QQqq', *r) for r in relocs) + image)


def native_package(name='fixture-native', binding_address=4112, binding_kind=1,
                   lib_flags=5, init=b'\x31\xc0\xc3', native=b'\xc3', metadata=None):
    # Main calls an import then tail-jumps to the terminal diagnostic import.
    code = b'\x48\x83\xec\x08\xff\x15\x16\0\0\0\x48\x83\xc4\x08\xff\x25\x14\0\0\0'
    image = code.ljust(4096,b'\0') + init.ljust(16,b'\0') + native
    image = image.ljust(12288,b'\0')
    header=struct.pack('<8s6Q',b'BBPROBE3',len(image),0,3,2,2,1)
    meta=metadata or (4096,8192,4096,8192,32,4,1,256)
    return (header + struct.pack('<8Q',*meta) + struct.pack('<3Q',0,binding_address,binding_kind)
            + struct.pack('<9Q',0,4096,5,4096,4096,lib_flags,8192,4096,6)
            + name.encode().ljust(128,b'\0') + b'after-native'.ljust(128,b'\0')
            + struct.pack('<8Q',32,1,0,0,40,1,1,0) + image)


def module_info_package(mutate=None, legacy=False, output_address=8256, check_fields=True):
    """Guest x86 code calls the real NID/bridge and checks rebased EH metadata."""
    code=bytearray(b'\x48\x83\xec\x08')
    def rip(opcode,target):
        code.extend(opcode)
        code.extend(struct.pack('<i',target-(len(code)+4)))
    rip(b'\x48\x8d\x3d',4112) # query an address inside the linked image
    code.extend(b'\x31\xf6')
    rip(b'\x48\x8d\x15',output_address)
    rip(b'\xff\x15',128)
    code.extend(b'\x89\xc3') # keep kernel result in EBX
    if check_fields:
        rip(b'\x48\x8d\x15',output_address)
        rip(b'\x48\x8d\x0d',4608)
        code.extend(bytes.fromhex('48398a500100007405bb01040000')) # EH frame address
        code.extend(bytes.fromhex('83ba08010000017405bb02040000')) # module id1
    code.extend(bytes.fromhex('89df4883c408'))
    rip(b'\xff\x25',136)
    assert len(code)<128
    image=bytearray(12288);image[:len(code)]=code;image[4096:4099]=b'\x31\xc0\xc3'
    struct.pack_into('<Q',image,output_address,424)
    common=dict(size=424,tls_init_size=0,tls_size=0,tls_offset=0,tls_align=0,
                tls_index=0,tls_init_addr=0,init_proc_addr=0,fini_proc_addr=0,
                eh_frame_hdr_addr=0,eh_frame_addr=0,eh_frame_hdr_size=0,eh_frame_size=0)
    main=dict(common,name='eboot.bin',id=0,segments=[(0,4096,5)])
    linked=dict(common,name='fixture.prx',id=1,segments=[(4096,4096,5),(8192,4096,6)])
    linked.update(tls_index=2,tls_init_addr=8192,tls_init_size=4,tls_size=32,tls_align=16,
                  init_proc_addr=4096,eh_frame_hdr_addr=5120,eh_frame_hdr_size=8,
                  eh_frame_addr=4608,eh_frame_size=4)
    descriptions=[main,linked]
    if mutate:mutate(descriptions)
    description_blob=struct.pack('<Q',2)+b''.join(pack_module_info(m) for m in descriptions)
    header=struct.pack('<8s6Q',b'BBPROBE5' if legacy else b'BBPROBE6',12288,0,3,2,2,1)
    metadata=struct.pack('<7Q',256,0,0,0,0,1,4096)+struct.pack('<6Q',8192,4096,8192,32,4,2)+struct.pack('<Q',0)
    # metadata: procparam, four main TLS fields, module count, one seven-Q
    # module record, binding count. Keep the ordinary v5 section unchanged.
    return (header+metadata+(b'' if legacy else description_blob)+
            struct.pack('<9Q',0,4096,5,4096,4096,5,8192,4096,6)+
            b'f7KBOafysXo#p#J'.ljust(128,b'\0')+b'after-module-info'.ljust(128,b'\0')+
            struct.pack('<8Q',128,1,0,0,136,1,1,0)+image)


def msync_package():
    code=bytearray(b'\x48\x83\xec\x08')
    def rip(opcode,target):
        code.extend(opcode);code.extend(struct.pack('<i',target-(len(code)+4)))
    rip(b'\x48\x8d\x3d',8192) #4KiB aligned, not16KiB aligned
    code.extend(b'\xbe'+struct.pack('<I',4096)+b'\xba'+struct.pack('<I',1))
    rip(b'\xff\x15',128)
    code.extend(bytes.fromhex('89c74883c408'))
    rip(b'\xff\x25',136)
    image=bytes(code).ljust(16384,b'\0')
    return (struct.pack('<8s6Q',b'BBPROBE2',len(image),0,1,2,2,1)+
            struct.pack('<3Q',0,len(image),5)+b'tZY4+SZNFhA#p#J'.ljust(128,b'\0')+
            b'after-msync'.ljust(128,b'\0')+struct.pack('<8Q',128,1,0,0,136,1,1,0)+image)


@unittest.skipUnless(EXE.exists(), 'build native probe first')
class LoaderTests(unittest.TestCase):
    def test_invalid_content_profile_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'content.bin'
            for data in (b'BBCONT01',struct.pack('<8s5I',b'BBCONT01',2,0,0,0,0),
                         struct.pack('<8s5I',b'BBCONT01',3,0,0,0,0)+b'extra'):
                with self.subTest(data=data):
                    path.write_bytes(data)
                    r=self.run_image(package(b'\xc3'),'--content-profile',str(path))
                    self.assertEqual(r.returncode,1,r.stdout+r.stderr)
                    self.assertIn('content profile',r.stderr)

    def run_image(self, data, *options):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'boot.bin'
            path.write_bytes(data)
            return subprocess.run([str(EXE.resolve()), str(path), '--cpu-only', *options],
                                  capture_output=True, text=True, timeout=5)

    def test_original_instruction_reaches_named_import(self):
        # jmp [rip+2]; two padding bytes; relocated function pointer at +8
        r = self.run_image(package(b'\xff\x25\x02\0\0\0\x90\x90', [(8, 1, 0, 0)], ['fixture-import']))
        self.assertEqual(r.returncode, 20, r.stderr)
        self.assertIn('first unsupported PS4 import: fixture-import', r.stdout)

    def test_ps4_msync_import_accepts_unwind_page_probe(self):
        r=self.run_image(msync_package())
        self.assertEqual(r.returncode,20,r.stdout+r.stderr)
        self.assertIn('first unsupported PS4 import: after-msync',r.stdout)
        self.assertIn('first argument: 0x0',r.stdout)
        self.assertIn('guest msync calls=1',r.stdout)

    def test_native_initializer_and_export_return(self):
        r=self.run_image(native_package())
        self.assertEqual(r.returncode,20,r.stdout+r.stderr)
        self.assertIn('Module 0 initializer returned 0',r.stdout)
        self.assertIn('first unsupported PS4 import: after-native',r.stdout)

    def test_v6_module_info_has_rebased_unwind_metadata_through_real_import(self):
        r=self.run_image(module_info_package())
        self.assertEqual(r.returncode,20,r.stdout+r.stderr)
        self.assertIn('Loaded 2 module-info records',r.stdout)
        self.assertIn('first unsupported PS4 import: after-module-info',r.stdout)
        self.assertIn('first argument: 0x0',r.stdout)

    def test_legacy_v5_does_not_claim_unwind_metadata_ready(self):
        r=self.run_image(module_info_package(legacy=True,check_fields=False))
        self.assertEqual(r.returncode,20,r.stdout+r.stderr)
        self.assertIn('first unsupported PS4 import: after-module-info',r.stdout)
        self.assertIn('first argument: 0x8002004e',r.stdout)
        self.assertIn('module unwind metadata unavailable',r.stderr)

    def test_module_info_readonly_guest_output_returns_fault(self):
        r=self.run_image(module_info_package(output_address=2048,check_fields=False))
        self.assertEqual(r.returncode,20,r.stdout+r.stderr)
        self.assertIn('first argument: 0x8002000e',r.stdout)

    def test_v6_module_info_ranges_and_identity_are_validated(self):
        cases=[
            (lambda m:m[1].update(id=0),'invalid module-info metadata'),
            (lambda m:m[1].update(size=416),'invalid module-info metadata'),
            (lambda m:m[1].update(segments=[(0,4096,5)]),'outside its image'),
            (lambda m:m[1].update(segments=[(4096,4096,5)]),'incomplete module-info'),
            (lambda m:m[1].update(segments=[(4096,4096,5),(4096,4096,5)]),'incomplete module-info'),
            (lambda m:m[1].update(eh_frame_addr=12284,eh_frame_size=8),'unmapped module-info unwind'),
            (lambda m:m[1].update(eh_frame_addr=512,eh_frame_size=8),'unmapped module-info unwind'),
            (lambda m:m[1].update(tls_index=7),'module-info TLS metadata mismatch'),
        ]
        for mutate,message in cases:
            with self.subTest(message=message):
                r=self.run_image(module_info_package(mutate))
                self.assertEqual(r.returncode,1,r.stdout+r.stderr)
                self.assertIn(message,r.stderr)

    def test_v6_module_info_count_bounds_and_truncation(self):
        data=bytearray(module_info_package());table=56+14*8
        for count in (0,18):
            with self.subTest(count=count):
                bad=bytearray(data);struct.pack_into('<Q',bad,table,count)
                r=self.run_image(bad)
                self.assertEqual(r.returncode,1,r.stdout+r.stderr)
                self.assertIn('invalid module-info count',r.stderr)
        bad=bytearray(data);struct.pack_into('<I',bad,table+8+424+416,5)
        r=self.run_image(bad)
        self.assertEqual(r.returncode,1,r.stdout+r.stderr)
        self.assertIn('invalid module-info metadata',r.stderr)
        r=self.run_image(data[:table+8+423])
        self.assertEqual(r.returncode,1,r.stdout+r.stderr)
        self.assertIn('truncated module-info table',r.stderr)

    def test_host_contract_takes_priority_over_native_export(self):
        r=self.run_image(native_package(name='bzQExy189ZI#q#q',native=b'\x0f\x0b'))
        self.assertEqual(r.returncode,20,r.stdout+r.stderr)
        self.assertIn('_init_env returned',r.stdout)
        self.assertIn('first unsupported PS4 import: after-native',r.stdout)

    def test_strict_mode_skips_native_initialization_and_binding(self):
        r=self.run_image(native_package(init=b'\x0f\x0b'), '--strict-imports')
        self.assertEqual(r.returncode,20,r.stdout+r.stderr)
        self.assertIn('first unsupported PS4 import: fixture-native',r.stdout)
        self.assertNotIn('Starting native libc',r.stdout)

    def test_native_metadata_and_bindings_are_validated(self):
        for kwargs,message in [
            ({'binding_address':12288},'invalid native binding'),
            ({'binding_kind':2},'native export kind/range mismatch'),
            ({'lib_flags':4},'native function is not executable'),
            ({'metadata':(4096,8192,4096,8192,32,33,1,256)},'invalid linked module metadata'),
            ({'metadata':(4096,8192,4096,8192,32,4,1,12280)},'unmapped procparam'),
        ]:
            with self.subTest(kwargs=kwargs):
                r=self.run_image(native_package(**kwargs))
                self.assertEqual(r.returncode,1,r.stdout+r.stderr)
                self.assertIn(message,r.stderr)

    def test_failed_native_initializer_does_not_enter_game(self):
        r=self.run_image(native_package(init=b'\xb8\x01\0\0\0\xc3'))
        self.assertEqual(r.returncode,1,r.stdout+r.stderr)
        self.assertIn('module initializer failed',r.stderr)
        self.assertNotIn('Entering original',r.stdout)

    def test_base_relative_address_is_relocated(self):
        # jump via a relative relocation to code at +16, then to an import at +24
        code = b'\xff\x25\x02\0\0\0\x90\x90' + b'\0'*8 + b'\xff\x25\x02\0\0\0\x90\x90'
        r = self.run_image(package(code, [(8, 0, 16, 0), (24, 1, 0, 0)], ['after-relative-jump']))
        self.assertEqual(r.returncode, 20, r.stderr)
        self.assertIn('after-relative-jump', r.stdout)

    def test_out_of_bounds_relocation_is_rejected(self):
        r = self.run_image(package(b'\xc3', [(4092, 0, 0, 0)]))
        self.assertEqual(r.returncode, 1)
        self.assertIn('bad relocation', r.stderr)

    def test_truncated_image_is_rejected(self):
        r = self.run_image(package(b'\xc3')[:-1])
        self.assertEqual(r.returncode, 1)
        self.assertIn('incorrect memory image size', r.stderr)

    def test_illegal_instruction_reports_guest_offset(self):
        r = self.run_image(package(b'\x0f\x0b'))
        if os.environ.get('BB_PROBE_TRANSLATOR') == '1':
            self.assertEqual(r.returncode, 30)
            self.assertIn('STOP: bbcpu:', r.stderr)
            return
        self.assertEqual(r.returncode, 132)
        self.assertIn('at guest offset 0x0,', r.stderr)

    def test_runtime_returns_from_verified_init_env(self):
        # Align stack, call _init_env through +32, then tail-jump to unknown +40.
        code = b'\x48\x83\xec\x08\xff\x15\x16\0\0\0\x48\x83\xc4\x08\xff\x25\x14\0\0\0'
        data = package(code, [(32,1,0,0),(40,1,1,0)], ['bzQExy189ZI#q#q','after-init'], 1)
        r = self.run_image(data)
        self.assertEqual(r.returncode,20,r.stderr)
        self.assertIn('first unsupported PS4 import: after-init',r.stdout)
        self.assertIn('_init_env=1',r.stdout)
        strict = self.run_image(data,'--strict-imports')
        self.assertEqual(strict.returncode,20,strict.stderr)
        self.assertIn('first unsupported PS4 import: bzQExy189ZI#q#q',strict.stdout)

    def test_unverified_runtime_stays_disabled(self):
        r = self.run_image(package(b'\xff\x25\x02\0\0\0\x90\x90',[(8,1,0,0)],['bzQExy189ZI#q#q'],0))
        self.assertEqual(r.returncode,20,r.stderr)
        self.assertIn('_init_env=0',r.stdout)

    def test_unknown_capabilities_rejected(self):
        r = self.run_image(package(b'\xc3',capabilities=2))
        self.assertEqual(r.returncode,1)
        self.assertIn('unknown runtime capabilities',r.stderr)


if __name__ == '__main__':
    unittest.main()
