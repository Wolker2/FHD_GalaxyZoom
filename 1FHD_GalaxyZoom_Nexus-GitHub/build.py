#!/usr/bin/env python3
"""Build GalaxyZoom alpha from supplied C/config source, without game files."""
from pathlib import Path
import argparse, hashlib, json, platform, subprocess, sys, zipfile, zlib
ROOT = Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'SOURCE')); sys.path.insert(0,str(ROOT/'scripts'))
from build_dat import create_dat
from dat_codec import decode, encode
from pe_tools import PE
PREFIX = 'Mods/FHD_MODS/FHD_GalaxyZoom/'
def load(path): return json.loads(Path(path).read_text('utf-8'))
def sha(data): return hashlib.sha256(data).hexdigest()

def compile_source(root=ROOT):
    (root/'Native').mkdir(exist_ok=True)
    subprocess.run(['bash',str(root/'SOURCE/Native/build.sh')],cwd=root,check=True,capture_output=True)
    create_dat(root/'SOURCE/CFG/Main.txt',root/'CFG/Main.dat')
    data = (root/'CFG/Main.dat').read_bytes(); assert encode(decode(data,'blockpar')) == data
    (root/'ModuleInfo.txt').write_bytes((root/'SOURCE/ModuleInfo.txt').read_bytes().decode('utf-8').encode('cp1251'))
    return {name:(root/name).read_bytes() for name in ['CFG/Main.dat','ModuleInfo.txt','Native/FHDGalaxyZoom.dll']}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--strict-prepared',action='store_true',help='Require hashes of the verified GCC 13.3 / binutils 2.42 build')
    args = parser.parse_args(); files = compile_source(); original = load(ROOT/'original-file-sha256.json')
    hashes = {name:sha(data) for name,data in sorted(files.items())}
    changed = [name for name in hashes if hashes[name] != original[name]]
    if any(name != 'Native/FHDGalaxyZoom.dll' for name in changed): raise SystemExit('Config or metadata differs from upload')
    if args.strict_prepared and hashes != load(ROOT/'prepared-file-sha256.json'): raise SystemExit('Prepared build hash mismatch: check toolchain versions')
    native = PE(ROOT/'Native/FHDGalaxyZoom.dll').audit()
    out = ROOT/'dist'; out.mkdir(exist_ok=True); archive = out/'FHD_GalaxyZoom_v0.1.5_FROM_SOURCE.zip'
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        for name,data in sorted(files.items()):
            info = zipfile.ZipInfo(PREFIX+name,date_time=(2026,10,28,0,0,0)); info.external_attr=0o644<<16
            z.writestr(info,data,compress_type=zipfile.ZIP_DEFLATED,compresslevel=9)
    with zipfile.ZipFile(archive) as z: assert z.testzip() is None
    report = {'runtime_files':len(files),'source_derived_files':len(files),'prebuilt_runtime_inputs_used':False,
              'all_files_byte_identical_to_upload':not changed,'different_from_upload':changed,
              'reason_for_native_difference':'Unchanged uploaded C compiled with available GCC/binutils; original PE linker is 2.44, available linker is 2.42. Original compiler version is unknown. Code-generation equality is not established.',
              'python':platform.python_version(),'zlib':zlib.ZLIB_RUNTIME_VERSION,
              'gcc':subprocess.check_output(['gcc','--version'],text=True).splitlines()[0],
              'ld':subprocess.check_output(['ld','--version'],text=True).splitlines()[0],
              'native_pe':native,'sha256':hashes,'original_sha256':original,
              'package_sha256':sha(archive.read_bytes()),'live_game_executed':False}
    (out/'build-report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('PASS: 3 runtime files built from source; DAT and metadata match upload')
    print('DLL differs from upload; see build-report.json for hashes and toolchain limits')
    print('Package:',archive)
if __name__ == '__main__': main()
