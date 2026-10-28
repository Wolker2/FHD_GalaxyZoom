"""Execute actual PE32 DLL code in Unicorn with controlled UI/Windows fixtures.

Tests original and rebuilt DLLs without modifying their code. Windows APIs and
Delphi UI position/visibility methods are fixtures, not a running game. Private
optimized helpers use the GCC i386 EAX calling convention of these builds.
"""
from pathlib import Path
import argparse, hashlib, json, struct, sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from pe_tools import PE
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EDX, UC_X86_REG_ESP, UC_X86_REG_EIP

PROFILES = [
    (0x68eccc46,0x4d1000,0x48bf14,0x2a2edd,0x2a0160,0x2a2dc0,0x0cfccc),
    (0x6a7afc5c,0x4d2000,0x48a478,0x274c1d,0x271ea0,0x274b00,0x0c1b7c),
    (0x6ac53b97,0x461000,0x41cbb4,0x2776b1,0x274bb8,0x2775cc,0x0c620c)]
OLD_SIG = bytes.fromhex('55 8b ec 83 c4 ec 56 57 8b f2 8d 7d f4 a5 a5 89 4d f0 89 45 fc d9 45 f4')
NEW_SIG = bytes.fromhex('53 56 57 83 c4 f4 8b f2 8d 3c 24 a5 a5 8b f1 8b d8 d9 04 24 d8 a3 f0 00')

class Fixture:
    def __init__(self, pe, relocated=False):
        self.pe = pe; self.base = pe.base+(0x02000000 if relocated else 0)
        self.vm = Uc(UC_ARCH_X86,UC_MODE_32); self.syms = pe.symbols()
        image = pe.mapped()
        if relocated:
            at,size = pe.directory(5); end = at+size
            while at < end:
                page,length = struct.unpack_from('<II',image,at); assert length >= 8
                for pos in range(at+8,at+length,2):
                    entry = struct.unpack_from('<H',image,pos)[0]
                    if entry>>12 == 3:
                        target = page+(entry&4095); old = struct.unpack_from('<I',image,target)[0]
                        struct.pack_into('<I',image,target,(old+self.base-pe.base)&0xffffffff)
                    else: assert entry>>12 == 0
                at += length
        for address,size in [(0,4096),(0x10000,4096),(0x400000,0x4e0000),
                             (self.base,(len(image)+4095)&~4095),(0x10000000,0x20000),(0x20000000,0x20000)]:
            self.vm.mem_map(address,size)
        self.vm.mem_write(self.base,bytes(image))
        self.stop = 0x1001f000; self.noop = self.stop+16
        self.vm.mem_write(self.noop,b'\xc3')
    def put(self,address,value,fmt='I'): self.vm.mem_write(address,struct.pack('<'+fmt,value))
    def get(self,address,fmt='I'): return struct.unpack('<'+fmt,self.vm.mem_read(address,struct.calcsize('<'+fmt)))[0]
    def addr(self,name): return self.base+self.syms[name]
    def glob(self,name,value,fmt='I'): self.put(self.addr(name),value,fmt)
    def value(self,name,fmt='I'): return self.get(self.addr(name),fmt)
    def invoke(self,name,*args,eax=0):
        stack = 0x2001f000
        self.vm.mem_write(stack,struct.pack('<'+'I'*(len(args)+1),self.stop,*[a&0xffffffff for a in args]))
        self.vm.reg_write(UC_X86_REG_ESP,stack); self.vm.reg_write(UC_X86_REG_EAX,eax)
        self.vm.emu_start(self.addr(name),self.stop,timeout=20_000_000,count=200_000_000)
        assert self.vm.reg_read(UC_X86_REG_EIP)==self.stop, 'Emulated call did not return: '+name
        return self.vm.reg_read(UC_X86_REG_EAX)
    def profile(self,index):
        timestamp,size,global_rva,clear,op,close,wheel = PROFILES[index]
        game=0x400000; screen=0x10000000; vmt=game+0x3000
        self.put(0x30,0x10000);self.put(0x10008,game)
        self.vm.mem_write(game,b'MZ');self.put(game+0x3c,0x80)
        self.vm.mem_write(game+0x80,b'PE\0\0');self.put(game+0x88,timestamp)
        self.put(game+0x98,0x10b,'H');self.put(game+0x98+56,size)
        self.put(game+global_rva,screen);self.put(screen,vmt)
        for offset,value in [(0x1c,op),(0x20,close),(0x2c,wheel),(4,0x4000),(0x24,0x4100)]: self.put(vmt+offset,game+value)
        projection = game+(0x277a18 if index==2 else 0x5000)
        self.vm.mem_write(projection,NEW_SIG if index==2 else OLD_SIG)
        self.vm.mem_write(game+clear-2,b'\x8b\x00\xe8\0\0\0\0')
        return game,screen,vmt,projection
    def ui(self):
        screen,panel,parent = 0x10000000,0x10001000,0x10002000
        self.glob('ready',1);self.glob('galaxyScreen',screen);self.glob('mapPanel',panel)
        self.put(screen+0xd4,panel);self.put(panel+0x14,parent)
        self.vm.mem_write(panel+0x60,struct.pack('<4i',320,90,1330,760))
        self.vm.mem_write(panel+0x28,struct.pack('<2i',1010,670))
        self.vm.mem_write(panel+0x70,struct.pack('<2i',320,90))
        self.glob('protectMemory',0)  # No actual OnClose/OnOpen rebuild in math tests.
        return screen,panel,parent
    def os_exports(self,fail_protect=0,pin_ok=True):
        """Minimal PEB loader/export tables and controlled stdcall OS methods."""
        self.put(0x1000c,0x10200);self.put(0x10214,0x10308)
        functions={};self.protection_calls=0
        for entry,base,name,apis,next_link in [
            (0x10300,0x10010000,'ntdll.dll',[('NtProtectVirtualMemory',20),('NtFlushInstructionCache',12)],0x10408),
            (0x10400,0x10014000,'kernelbase.dll',[('GetModuleHandleExW',12),('GetTickCount',0)],0x10214)]:
            self.put(entry+8,next_link);self.put(entry+0x18,base);self.put(entry+0x30,entry+0x200)
            self.vm.mem_write(entry+0x200,(name+'\0').encode('utf-16le'))
            self.vm.mem_write(base,b'MZ');self.put(base+0x3c,0x80);self.vm.mem_write(base+0x80,b'PE\0\0')
            self.put(base+0x98,0x10b,'H');self.put(base+0x98+96,0x400);self.put(base+0x98+100,0x200)
            for offset,value in [(20,len(apis)),(24,len(apis)),(28,0x600),(32,0x700),(36,0x800)]:self.put(base+0x400+offset,value)
            for index,(api,stack_bytes) in enumerate(apis):
                address=base+0x1000+index*0x100;functions[address]=api
                self.put(base+0x600+4*index,address-base);self.put(base+0x700+4*index,0x900+index*0x60)
                self.put(base+0x800+2*index,index,'H');self.vm.mem_write(base+0x900+index*0x60,api.encode()+b'\0')
                self.vm.mem_write(address,b'\xc2'+struct.pack('<H',stack_bytes) if stack_bytes else b'\xc3')
        def api_call(vm,address,size,data):
            api=functions.get(address)
            if not api:return
            stack=vm.reg_read(UC_X86_REG_ESP)
            if api=='NtProtectVirtualMemory':
                self.protection_calls+=1;self.put(self.get(stack+20),0x20)
                result=0xc0000001 if self.protection_calls==fail_protect else 0
            elif api=='GetModuleHandleExW':
                self.put(self.get(stack+12),0x10010000 if pin_ok else 0);result=int(pin_ok)
            elif api=='GetTickCount':result=1000
            else:result=0
            vm.reg_write(UC_X86_REG_EAX,result)
        self.vm.hook_add(UC_HOOK_CODE,api_call,begin=0x10011000,end=0x10015100)

def verify(dll,exe=None):
    pe=PE(dll);pe.audit();checks=0
    def check(condition,message):
        nonlocal checks
        assert condition,message;checks+=1
    # Negative Init paths use real DLL machine code, including FS:0x30 PEB lookup.
    for relocated in [False,True]:
        f=Fixture(pe,relocated)
        check(f.invoke('FHDZoom_Status')==2,'Initial status')
        check(f.invoke('FHDZoom_Init')==1,'Missing PEB')
        f.put(0x30,0x10000);f.put(0x10008,0x400000)
        f.vm.mem_write(0x400000,b'MZ');f.put(0x40003c,0x80)
        f.vm.mem_write(0x400080,b'PE\0\0');f.put(0x400098,0x10b,'H')
        check(f.invoke('FHDZoom_Init')==2,'Unsupported build rejected')
        # Current profile avoids scanning millions of zero bytes for each negative case.
        _,screen,vmt,projection=f.profile(2)
        f.put(0x400000+PROFILES[2][2],0)
        check(f.invoke('FHDZoom_Init')==6,'Absent galaxy class rejected')
        f.put(0x400000+PROFILES[2][2],screen);f.put(vmt+0x1c,0)
        check(f.invoke('FHDZoom_Init')==3,'Wrong VMT rejected')
        f.profile(2);f.vm.mem_write(projection,b'\x90')
        check(f.invoke('FHDZoom_Init')==3,'Changed projection rejected')
        f.profile(2);f.vm.mem_write(0x400000+PROFILES[2][3],b'\x90')
        check(f.invoke('FHDZoom_Init')==3,'Changed clear call rejected')
        for index in range(3):
            f=Fixture(pe,relocated);_,_,vmt,projection=f.profile(index)
            before=bytes(f.vm.mem_read(vmt,0x30));signature=bytes(f.vm.mem_read(projection,24))
            check(f.invoke('FHDZoom_Init')==4,'Valid synthetic profile reaches missing-OS guard')
            check(bytes(f.vm.mem_read(vmt,0x30))==before and bytes(f.vm.mem_read(projection,24))==signature,'Rejected Init leaves hooks untouched')
        for failure in [0,1,3,5,7,9]:
            f=Fixture(pe,relocated);_,_,vmt,projection=f.profile(2)
            baseline=bytes(f.vm.mem_read(vmt,0x30));signature=bytes(f.vm.mem_read(projection,24))
            f.os_exports(fail_protect=failure)
            check(f.invoke('FHDZoom_Init')==(7 if failure else 0),'Hook installation or injected write failure')
            if failure:
                check(bytes(f.vm.mem_read(vmt,0x30))==baseline and bytes(f.vm.mem_read(projection,24))==signature,'Prior hooks rolled back after one injected write failure')
                check(f.invoke('FHDZoom_Status')==2,'Failure leaves module unready')
            else:
                check(f.invoke('FHDZoom_Status')==0 and f.get(projection,'B')==0xe9,'Successful install status and projection hook')
                for offset in [4,0x1c,0x24,0x2c]:check(f.base<=f.get(vmt+offset)<f.base+pe.image_size,'VMT points to installed DLL hook')
                calls=f.protection_calls;check(f.invoke('FHDZoom_Init')==0 and f.protection_calls==calls,'Repeated Init is idempotent')
        f=Fixture(pe,relocated);_,_,vmt,projection=f.profile(2);f.os_exports(pin_ok=False)
        check(f.invoke('FHDZoom_Init')==5 and f.get(projection,'B')==NEW_SIG[0],'Pin failure rejected before hooks')
    # Projection modifies native coordinate window once and respects screen identity.
    f=Fixture(pe);screen,panel,parent=f.ui()
    f.glob('zoomFactor',2,'f');f.glob('centerX',0.6,'d');f.glob('centerY',0.4,'d')
    f.vm.mem_write(screen+0xf0,struct.pack('<4f',10,20,150,100))
    f.invoke('applyZoomOnProjection',screen)
    coords=struct.unpack('<4f',f.vm.mem_read(screen+0xf0,16))
    check(all(abs(a-b)<0.0001 for a,b in zip(coords,[62.5,35,75,50])),'Zoom projection')
    f.invoke('applyZoomOnProjection',screen)
    check(struct.unpack('<4f',f.vm.mem_read(screen+0xf0,16))==coords,'Projection applies once')
    # Wheel updates its real zoom/center globals; rebuild is blocked by the OS fixture.
    f=Fixture(pe);screen,panel,parent=f.ui();mouse=0x10003000
    f.vm.mem_write(mouse,struct.pack('<2i',900,400))
    fx=(900-320)/1010;fy=(400-90)/670
    f.invoke('processWheel',screen,0,mouse,120)
    zoom=f.value('zoomFactor','f');cx=f.value('centerX','d');cy=f.value('centerY','d')
    check(abs(zoom-1.25)<1e-6,'One wheel step')
    check(abs(cx+(fx-.5)/zoom-fx)<1e-9 and abs(cy+(fy-.5)/zoom-fy)<1e-9,'Cursor anchor preserved')
    for _ in range(8): f.invoke('processWheel',screen,0,mouse,120)
    check(abs(f.value('zoomFactor','f')-3.814697265625)<1e-6,'Maximum zoom')
    for _ in range(8): f.invoke('processWheel',screen,0,mouse,-480)
    check(f.value('zoomFactor','f')==1,'Minimum zoom')
    f.vm.mem_write(mouse,struct.pack('<2i',100,100));f.invoke('processWheel',screen,0,mouse,120)
    check(f.value('zoomFactor','f')==1,'Outside-map wheel ignored')
    # Actual window-message handler: synthetic moves/capture changes preserve drag.
    f.glob('zoomFactor',2,'f');f.glob('centerX',.5,'d');f.glob('centerY',.5,'d')
    point=lambda x,y:(x&65535)|((y&65535)<<16)
    f.invoke('observeWindowMessage',screen,0x204,2,point(700,400))
    check(f.value('dragging')==1,'RMB starts pan')
    f.invoke('observeWindowMessage',screen,0x200,2,point(750,420))
    check(f.value('pendingPanRebuild')==1 and f.value('centerX','d')<.5,'Pan queues rebuild and updates center')
    for message in [0x200,0x1f,0x215]:
        f.invoke('observeWindowMessage',screen,message,0,point(0,0))
        check(f.value('dragging')==1,'Synthetic/capture message preserves drag')
    f.invoke('observeWindowMessage',screen,0x205,0,point(750,420))
    check(f.value('dragging')==0,'RMB release stops pan')
    # Real clipping routine, with native line owner fields and map bounds.
    line,segment,rect=0x10004000,0x10005000,0x10006000;f.put(line+0x14,panel)
    for a,b,visible in [((-1000,120),(500,120),1),((1300,-500),(1300,950),1),
                         ((-50,-50),(20,20),0),((400,300),(500,400),1)]:
        f.vm.mem_write(segment+0x28,struct.pack('<4i',*a,*b));f.vm.mem_write(rect,struct.pack('<4i',0,0,1920,1080))
        check(f.invoke('clipGalaxySegment',line,segment,rect)==visible,'Clipping visibility')
        if visible:
            x0,y0,x1,y1=struct.unpack('<4i',f.vm.mem_read(segment+0x28,16))
            check(all(322<=x<=1327 and 92<=y<=757 for x,y in [(x0,y0),(x1,y1)]),'Clipped endpoints within frame')
    # Capture actual Lite siblings through processWheel; sync uses controlled UI methods.
    f=Fixture(pe);screen,panel,parent=f.ui();star=0x10003000;vmt=0x10004000
    f.put(panel+4,star);f.put(parent+4,panel)
    f.put(star+0x78,0x10005000);f.vm.mem_write(0x10005000,'gs_45\0'.encode('utf-16le'))
    f.vm.mem_write(star+0x70,struct.pack('<2i',710,180))
    f.put(vmt+0xc,f.noop);f.put(vmt+0x28,f.noop+16);f.vm.mem_write(f.noop+16,b'\xc3')
    icons=[]
    names=['RedRing0','BlackHole152','ldacp5','RedRing1234','RedRing12345','ButExit']
    for index,name in enumerate(names):
        obj=0x10006000+index*0x100;nameptr=0x10008000+index*0x100
        f.put(obj,vmt);f.put(obj+0x78,nameptr);f.vm.mem_write(nameptr,(name+'\0').encode('utf-16le'))
        f.put(obj+0x14,parent);f.vm.mem_write(obj+0x41,b'\x01')
        f.vm.mem_write(obj+0x20,struct.pack('<2i',700,170));f.vm.mem_write(obj+0x28,struct.pack('<2i',20,20))
        f.vm.mem_write(obj+0x60,struct.pack('<4i',700,170,720,190))
        f.put((panel if index==0 else icons[-1])+0x10,obj);icons.append(obj)
    mouse=0x10009000;f.vm.mem_write(mouse,struct.pack('<2i',900,400));f.invoke('processWheel',screen,0,mouse,120)
    check(f.value('liteOverlayCount')==4,'Actual Lite recognition accepts 1-4 digit suffixes')
    def ui_method(vm,address,size,data):
        if address==f.noop:
            obj=vm.reg_read(UC_X86_REG_EAX);pos=vm.reg_read(UC_X86_REG_EDX)
            x,y=struct.unpack('<2i',vm.mem_read(pos,8));oldx,oldy=struct.unpack('<2i',vm.mem_read(obj+0x20,8))
            box=list(struct.unpack('<4i',vm.mem_read(obj+0x60,16)));box=[box[0]+x-oldx,box[1]+y-oldy,box[2]+x-oldx,box[3]+y-oldy]
            vm.mem_write(obj+0x20,struct.pack('<2i',x,y));vm.mem_write(obj+0x60,struct.pack('<4i',*box))
        elif address==f.noop+16:
            obj=vm.reg_read(UC_X86_REG_EAX);vm.mem_write(obj+0x41,bytes([bool(vm.reg_read(UC_X86_REG_EDX))]))
    f.vm.hook_add(UC_HOOK_CODE,ui_method,begin=f.noop,end=f.noop+16)
    for x,y in [(900,440),(1400,440),(710,180)]:
        f.vm.mem_write(star+0x70,struct.pack('<2i',x,y));f.invoke('syncLiteOverlays',eax=screen)
        for icon in icons[:4]:
            check(struct.unpack('<2i',f.vm.mem_read(icon+0x20,8))==(x-10,y-10),'Lite marker stays with star')
            check(f.get(icon+0x41,'B')==(0 if x==1400 else 1),'Lite visibility restored at frame boundary')
    for icon in icons[4:]: check(struct.unpack('<2i',f.vm.mem_read(icon+0x20,8))==(700,170),'Unrecognized control left unchanged')
    target=None
    if exe is not None:
        target_pe=PE(exe);digest=hashlib.sha256(target_pe.data).hexdigest()
        check(digest=='e43a4b15503c705eebe2d8ce5778c2a2fb090db515a3fd86d319e5c5aa692ebf','Known target EXE hash')
        check(target_pe.u32(target_pe.nt+8)==PROFILES[2][0] and target_pe.image_size==PROFILES[2][1],'Actual EXE profile')
        check(target_pe.at(0x277a18,24)==NEW_SIG,'Actual projection signature')
        check(target_pe.at(0x2776b1-2,3)==b'\x8b\x00\xe8','Actual clear call prefix')
        pattern=struct.pack('<II',0x400000+0x274bb8,0x400000+0x2775cc)
        mapped=target_pe.mapped();hits=[];at=0
        while True:
            at=mapped.find(pattern,at)
            if at<0: break
            if struct.unpack_from('<I',mapped,at+16)[0]==0x400000+0x0c620c: hits.append(at-0x1c)
            at+=1
        check(len(hits)==1,'Unique actual Galaxy VMT')
        f=Fixture(pe);f.vm.mem_write(0x400000,bytes(mapped));f.put(0x30,0x10000);f.put(0x10008,0x400000)
        f.put(0x400000+0x41cbb4,0x10000000);f.put(0x10000000,0x400000+hits[0])
        check(f.invoke('FHDZoom_Init')==4,'Actual EXE reaches OS guard with synthetic live instance')
        target={'sha256':digest,'actual_exe_bytes_checked':True,'game_instance_is_synthetic':True}
    return {'checks_passed':checks,'dll_sha256':hashlib.sha256(pe.data).hexdigest(),
            'actual_dll_machine_code_executed':True,'relocation_tested':True,
            'init_success_and_hook_rollback_tested':True,'windows_and_ui_methods':'controlled fixtures',
            'live_game_executed':False,'optional_target':target}

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--dll',type=Path,default=ROOT/'Native/FHDGalaxyZoom.dll')
    parser.add_argument('--exe',type=Path);args=parser.parse_args()
    print(json.dumps(verify(args.dll,args.exe),ensure_ascii=False,indent=2))
