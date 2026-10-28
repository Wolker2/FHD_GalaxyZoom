from pathlib import Path
import hashlib, json, shutil, subprocess, sys, tempfile, unittest, zipfile
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT));sys.path.insert(0,str(ROOT/'tests'))
from build import compile_source, load, PREFIX
from pe_tools import PE
from verify_native import verify

class SourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls): cls.files=compile_source()

    def test_config_metadata_and_prepared_hashes(self):
        original=load(ROOT/'original-file-sha256.json')
        hashes={name:hashlib.sha256(data).hexdigest() for name,data in self.files.items()}
        for name in ['CFG/Main.dat','ModuleInfo.txt']: self.assertEqual(hashes[name],original[name])
        self.assertEqual(hashes,load(ROOT/'prepared-file-sha256.json'))
        self.assertEqual(len(hashes),3)

    def test_pe_contract(self):
        report=PE(ROOT/'Native/FHDGalaxyZoom.dll').audit()
        self.assertEqual(report['exports'],['FHDZoom_Init','FHDZoom_Status'])
        self.assertEqual(report['static_imports'],[])
        self.assertTrue(report['base_relocations_present'])

    def test_actual_dll_machine_code(self):
        report=verify(ROOT/'Native/FHDGalaxyZoom.dll')
        self.assertGreaterEqual(report['checks_passed'],60)
        self.assertTrue(report['actual_dll_machine_code_executed'])

    def test_configuration_source_edit_is_used(self):
        with tempfile.TemporaryDirectory() as folder:
            target=Path(folder);shutil.copytree(ROOT/'SOURCE',target/'SOURCE',ignore=shutil.ignore_patterns('__pycache__','*.o','*.coff'))
            path=target/'SOURCE/CFG/Main.txt';data=path.read_text('utf-8');self.assertIn('unknown init=',data)
            path.write_text(data.replace('unknown init=','unknown initialize=').replace('01=init();','01=initialize();'),encoding='utf-8')
            edited=compile_source(target)
        self.assertEqual([name for name in edited if edited[name]!=self.files[name]],['CFG/Main.dat'])

    def test_isolated_source_only_build(self):
        with tempfile.TemporaryDirectory() as folder:
            target=Path(folder)
            for name in ['SOURCE','scripts']:
                shutil.copytree(ROOT/name,target/name,ignore=shutil.ignore_patterns('__pycache__','*.o','*.coff'))
            for name in ['build.py','original-file-sha256.json','prepared-file-sha256.json']:shutil.copy2(ROOT/name,target/name)
            self.assertFalse(any(p.suffix.lower() in ['.dat','.dll','.exe','.scr'] for p in target.rglob('*') if p.is_file()))
            p=subprocess.run([sys.executable,str(target/'build.py'),'--strict-prepared'],cwd=target,capture_output=True,text=True)
            self.assertEqual(p.returncode,0,p.stdout+'\n'+p.stderr)
            with zipfile.ZipFile(target/'dist/FHD_GalaxyZoom_v0.1.5_FROM_SOURCE.zip') as archive:
                self.assertEqual(len(archive.namelist()),3);self.assertIsNone(archive.testzip())
                for name,data in self.files.items():self.assertEqual(archive.read(PREFIX+name),data)
if __name__=='__main__':unittest.main()
