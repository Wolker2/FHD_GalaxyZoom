/* FHD_GalaxyZoom alpha v0.1.5: independent native add-on for SRHD.
 * No changes to Rangers.exe on disk. Fail-closed build guard.
 * Supports three supplied 2.1.2500 EXE variants, including NewEXE 2026-10.
 * All x86 Delphi entry points are based on the decompiled engine.
 * Read README before redistribution or claiming in-game validation.
 * v0.1.5: spatial identity for battle markers; NEVER match map children by
 *           ordinal (layering and script mods can reorder the child list).
 *           RMB drag and clipping untouched from v0.1.4.
 * Fix Delphi mouse event ABI (Point argument is a POINTER, ret 4).
 */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef int i32;
typedef void *ptr;
#define STDCALL __attribute__((stdcall))
#define NAKED __attribute__((naked))
#define MK_RBUTTON 0x0002u
typedef i32 (STDCALL *Protect)(ptr,ptr *,u32 *,u32,u32 *);
typedef i32 (STDCALL *Flush)(ptr,ptr,u32);
typedef i32 (STDCALL *PinModule)(u32,const u16 *,ptr *);
typedef struct { ptr code; ptr data; } Method;
static u8 *game;
ptr originalOpen;
static ptr originalClose;
ptr projectionResume;
static u8 *clearSite;
static u8 originalClearCall[5];
static u8 *galaxyScreen;
static u8 *mapPanel;
static Protect protectMemory;
static Flush flushCache;
static int ready;
static int zoomApplied;
static int busy;
static int dragging;
static i32 dragLastX,dragLastY;
static int markersInMotion;
ptr originalFrameSegment;
ptr originalDrawSegment;
ptr originalWindowMessage;
static ptr originalTimerProcedure;
static int pendingPanRebuild;
static u8 *hookedPolyVmt;
int clippedLineVisible=1;
static int wasDragMoved;
static i32 dragRawX,dragRawY;
static int physicalButtonObserved;
static short (STDCALL *getAsyncKeyState)(int);
static i32 lastRefreshTick;
static u32 (STDCALL *getTickCount)(void);

typedef struct {u8* control;i32 baseX,baseY,i32Width,i32Height,drawX,drawY;u8 tracked,wasActive;int starId,starDX,starDY;} Marker;
static Marker markers[48];

/* At x1, bind native, unnamed star-attached GAI sprites to their closest
 * star (by ID). During zoom, re-created Map children may differ in order.
 * Match by CLASS + SIZE + SPATIAL identity, checking both unzoomed (buggy)
 * and projected (already correct) coordinates; never by child index.
 * Battle rings and battle glyphs are TgaiGI, just like named "gs_<id>" stars.
 * Named controls, labels and map lines are excluded. */
#define MAX_GALAXY_ICONS 8192
#define MAX_GALAXY_STARS 1024
typedef struct {
  i32 id,x,y;
} StarAnchor;
typedef struct {
  ptr classVmt;
  i32 starId,offsetX,offsetY,sizeX,sizeY;
  i32 baseCenterX,baseCenterY;
  u8 matched;
} IconAnchor;
static StarAnchor starsBefore[MAX_GALAXY_STARS];
static StarAnchor starsNow[MAX_GALAXY_STARS];
static IconAnchor iconAnchors[MAX_GALAXY_ICONS];
static int iconAnchorCount;
static int starBeforeCount;
/* AModGalaxyLite stores hundreds of persistent named GAI icons as SIBLINGS
 * of Map: RedRing0..200, BlackHole0..152 and 2-5 faction-letter battle
 * icons. Vanilla built-in icons live INSIDE Map. Both need independent
 * anchoring. The named icons are enabled and positioned by game scripts. */
#define MAX_LITE_OVERLAYS 768
typedef struct {
  u8 *control;
  int starId;
  i32 starDx,starDy;
  i32 baselineX,baselineY;
  u8 hiddenByZoom;
} LiteOverlay;
static LiteOverlay liteOverlays[MAX_LITE_OVERLAYS];
static int liteOverlayCount;
static int liteBaselineReady;
static void clearLiteOverlays(void);
static void captureLiteOverlays(u8 *self);
static void syncLiteOverlays(u8 *self);
static void clearIconAnchors(void);
static void captureIconAnchors(u8 *self);
static void syncIconAnchors(u8 *self);
static float zoomFactor=1.0f;
static double centerX=0.5,centerY=0.5;
static u32 globalGalaxyScreenRva;
static u32 imageSize;
static Method originalMapRightDown,originalMapRightUp;
static const u8 projectionSignature[24]={
  0x55,0x8b,0xec,0x83,0xc4,0xec,0x56,0x57,0x8b,0xf2,0x8d,0x7d,
  0xf4,0xa5,0xa5,0x89,0x4d,0xf0,0x89,0x45,0xfc,0xd9,0x45,0xf4};
static const u8 projectionSignatureNew[24]={
  0x53,0x56,0x57,0x83,0xc4,0xf4,0x8b,0xf2,0x8d,0x3c,0x24,0xa5,
  0xa5,0x8b,0xf1,0x8b,0xd8,0xd9,0x04,0x24,0xd8,0xa3,0xf0,0x00};
static int same(const u8 *a,const u8 *b,u32 length) {
  for(u32 i=0;i<length;i++)if(a[i]!=b[i])return 0;
  return 1;
}
static int asciiEqual(const char *a,const char *b) {
  if(!a||!b)return 0;
  while(*a&&*b)if(*a++!=*b++)return 0;
  return *a==*b;
}
static double limit(double x,double low,double high) {
  return x<low?low:(x>high?high:x);
}
NAKED static ptr pebAddress(void) {__asm__ volatile("movl %fs:0x30,%eax;ret");}
static ptr findModule(const char *match) {
  u8 *peb=(u8 *)pebAddress();if(!peb)return 0;
  u8 *ldr=*(u8 **)(peb+0x0c);if(!ldr)return 0;
  u8 *head=ldr+0x14,*link=*(u8 **)head;
  for(int i=0;link&&link!=head&&i<40;++i) {
    u8 *entry=link-8,*base=*(u8 **)(entry+0x18);
    const u16 *name=*(const u16 **)(entry+0x30);
    if(base&&name) {
      int j=0;
      for(;match[j];j++) {
        u16 c=name[j];if(c>='A'&&c<='Z')c+=32;
        if(c!=(u8)match[j])break;
      }
      if(!match[j]&&!name[j])return base;
    }
    link=*(u8 **)link;
  }
  return 0;
}
static ptr findExport(ptr module,const char *wanted) {
  u8 *p=(u8 *)module;if(!p||*(u16 *)p!=0x5a4d)return 0;
  u8 *nt=p+*(u32 *)(p+0x3c);
  if(*(u32 *)nt!=0x4550||*(u16 *)(nt+24)!=0x10b)return 0;
  u8 *opt=nt+24;u32 rva=*(u32 *)(opt+96),len=*(u32 *)(opt+100);
  if(!rva)return 0;
  u8 *exp=p+rva;
  u32 count=*(u32 *)(exp+20),n=*(u32 *)(exp+24);
  u32 *functions=(u32 *)(p+*(u32 *)(exp+28));
  u32 *names=(u32 *)(p+*(u32 *)(exp+32));
  u16 *ords=(u16 *)(p+*(u32 *)(exp+36));
  for(u32 i=0;i<n;++i)if(asciiEqual((char *)(p+names[i]),wanted)) {
    u16 ord=ords[i];if(ord>=count)return 0;
    u32 f=functions[ord];return (f>=rva&&f<rva+len)?0:p+f;
  }
  return 0;
}
static int replaceBytes(void *location,const u8 *data,u32 len) {
  ptr page=location;u32 size=len,old=0,ignore=0;
  if(!protectMemory||protectMemory((ptr)-1,&page,&size,0x40,&old)<0)return 0;
  for(u32 i=0;i<len;i++)((u8 *)location)[i]=data[i];
  if(flushCache)flushCache((ptr)-1,location,len);
  protectMemory((ptr)-1,&page,&size,old,&ignore);
  return 1;
}
static int replacePtr(void *location,ptr destination) {
  return replaceBytes(location,(const u8 *)&destination,4);
}
static int replaceJump(u8 *site,ptr target) {
  u8 j[6]={0xe9,0,0,0,0,0x90};
  *(i32 *)(j+1)=(i32)((u8 *)target-(site+5));
  return replaceBytes(site,j,6);
}
static u8 *scanProjection(u8 *base,u32 size) {
  u8 *only=0;
  for(u32 i=0x1000;i+24<size;i++)if(same(base+i,projectionSignature,24)) {
    if(only)return 0;
    only=base+i;
  }
  return only;
}
static void delphiCall(ptr function,u8 *self) {
  register ptr eax __asm__("eax")=self;
  __asm__ volatile("call *%1" : "+a"(eax):"m"(function):"edx","ecx","memory","cc");
}
static void writeMethod(u8 *object,u32 offset,ptr code,ptr data){
  *(ptr *)(object+offset)=code;
  *(ptr *)(object+offset+4)=data;
}
static void callMouseEvent(Method method,u8 *sender,u32 keys,const i32 *point){
  if(!method.code)return;
  register ptr eax __asm__("eax")=method.data;
  register ptr edx __asm__("edx")=sender;
  register u32 ecx __asm__("ecx")=keys;
  register ptr ebx __asm__("ebx")=method.code;
  __asm__ volatile(
    ".intel_syntax noprefix;"
    "push %[point];"
    "call ebx;"
    ".att_syntax prefix;"
    :
    : [point]"r"(point), "a"(eax), "d"(edx), "c"(ecx), "b"(ebx)
    : "memory","cc");
}
static void rebuildGalaxy(u8 *self);
static void installMapCallbacks(u8 *self);
static void updateMarkers(u8 *self);
static void clearMarkerTracking(void);
static void hideOverflowIcons(u8 *self);
/* First projection takes place only after fGalaxy2.OnOpen recomputes the
 * original bounds. Adjusting the source-coordinate window at this point
 * makes every star, name, ring, path and inverse hit-test use same mapping.
 */
__attribute__((used)) void applyZoomOnProjection(u8 *self) {
  if(!ready||!self||self!=galaxyScreen||zoomApplied)return;
  float *origin=(float *)(self+0xf0);
  float *extent=(float *)(self+0xf8);
  double w=extent[0],h=extent[1];
  if(w<=0.0||h<=0.0||w>1000000000.0||h>1000000000.0)return;
  zoomApplied=1;
  if(zoomFactor<=1.0001f)return;
  const double inv=1.0/(double)zoomFactor;
  origin[0]=(float)((double)origin[0]+w*(centerX-0.5*inv));
  origin[1]=(float)((double)origin[1]+h*(centerY-0.5*inv));
  extent[0]=(float)(w*inv);extent[1]=(float)(h*inv);
}
NAKED static void projectionHook(void) {
  __asm__ volatile(
    ".intel_syntax noprefix;"
    "pushfd;pushad;push eax;call applyZoomOnProjection;add esp,4;popad;popfd;"
    ".byte 0x55,0x8b,0xec,0x83,0xc4,0xec;"
    "jmp DWORD PTR [projectionResume];"
    ".att_syntax prefix;"
  );
}
NAKED static void projectionHookNew(void) {
  __asm__ volatile(
    ".intel_syntax noprefix;"
    "pushfd;pushad;push eax;call applyZoomOnProjection;add esp,4;popad;popfd;"
    ".byte 0x53,0x56,0x57,0x83,0xc4,0xf4;"
    "jmp DWORD PTR [projectionResume];"
    ".att_syntax prefix;"
  );
}
__attribute__((used)) void resetProjection(void){zoomApplied=0;dragging=0;clearMarkerTracking();}
__attribute__((used)) void handleOpen(u8 *self){
  /* At a new map opening, reset the marker to the vanilla coordinate system. */
  if(ready&&!busy){
    zoomFactor=1.0f;centerX=0.5;centerY=0.5;
    updateMarkers(self);
  }
  resetProjection();
  pendingPanRebuild=0;
  clearIconAnchors();
  delphiCall(originalOpen,self);
  installMapCallbacks(self);
  /* Real modal reopen (not a wheel/pan rebuild): restore any retained
     static sibling markers to x1 after the game has rebuilt its stars,
     then discard the previous map's attachments. */
  if(liteBaselineReady)syncLiteOverlays(self);
  clearLiteOverlays();
  captureIconAnchors(self);
  updateMarkers(self);
  hideOverflowIcons(self);
}
NAKED static void openHook(void) {
  __asm__ volatile(
    ".intel_syntax noprefix;"
    "push eax;call handleOpen;add esp,4;ret;"
    ".att_syntax prefix;"
  );
}
__attribute__((used)) void processWheel(u8 *self,u32 keys,const i32 *mouse,i32 delta) {
  (void)keys;
  if(!ready||busy||self!=galaxyScreen||!mouse||!delta)return;
  u8 *panel=*(u8 **)(self+0xd4);
  if(!panel)return;
  i32 *rect=(i32 *)(panel+0x60); /* HitTestBounds in screen coordinates */
  i32 mx=mouse[0],my=mouse[1];
  if(mx<rect[0]||mx>=rect[2]||my<rect[1]||my>=rect[3])return;
  i32 width=rect[2]-rect[0],height=rect[3]-rect[1];
  if(width<200||height<150)return;
  const double fx=limit((double)(mx-rect[0])/(double)width,0.0,1.0);
  const double fy=limit((double)(my-rect[1])/(double)height,0.0,1.0);
  /* Cache SCRIPT overlays at native x1 just before first wheel event. They
   * may become active after OnOpen, so taking an earlier snapshot is wrong. */
  if(zoomFactor<=1.0001f)captureLiteOverlays(self);
  updateMarkers(self);
  double oldZoom=zoomFactor,newZoom=oldZoom;
  int steps=(delta<0?-delta:delta)/120;
  if(steps<1)steps=1;
  if(steps>4)steps=4;
  for(int i=0;i<steps;i++)newZoom=delta>0?newZoom*1.25:newZoom/1.25;
  newZoom=limit(newZoom,1.0,3.814697265625); /* 1.25 ^ 6 */
  if(newZoom==oldZoom)return;
  const double starX=centerX+(fx-0.5)/oldZoom;
  const double starY=centerY+(fy-0.5)/oldZoom;
  centerX=limit(starX-(fx-0.5)/newZoom,0.5/newZoom,1.0-0.5/newZoom);
  centerY=limit(starY-(fy-0.5)/newZoom,0.5/newZoom,1.0-0.5/newZoom);
  zoomFactor=(float)newZoom;
  rebuildGalaxy(self);
}
NAKED static void wheelHook(void) {
  __asm__ volatile(
    ".intel_syntax noprefix;"
    "push ebp;mov ebp,esp;"
    "push DWORD PTR [ebp+8];push ecx;push edx;push eax;"
    "call processWheel;add esp,16;pop ebp;ret 4;"
    ".att_syntax prefix;"
  );
}
/* The only authoritative source of button state for a pan gesture is the
 * WINDOW's WM_RBUTTONDOWN/UP, not the re-created MapPanel callback. The game
 * sends artificial WM_MOUSEMOVE with wParam=0 during re-opening; that is not
 * a real release. */
NAKED static void mapRightDownHook(void){
  __asm__ volatile(
    ".intel_syntax noprefix;"
    "ret 4;"
    ".att_syntax prefix;"
  );
}
NAKED static void mapRightUpHook(void){
  __asm__ volatile(
    ".intel_syntax noprefix;"
    "ret 4;"
    ".att_syntax prefix;"
  );
}
#define WM_MOUSEMOVE 0x0200
#define WM_RBUTTONDOWN 0x0204
#define WM_RBUTTONUP 0x0205
#define WM_CANCELMODE 0x001f
#define WM_KILLFOCUS 0x0008
#define WM_CAPTURECHANGED 0x0215
#define VK_RBUTTON 0x02
__attribute__((used)) void observeWindowMessage(u8 *self,u32 message,u32 keys,u32 lparam){
  if(!ready||busy||self!=galaxyScreen)return;
  i32 mx=(i32)(short)(u16)lparam;
  i32 my=(i32)(short)(u16)(lparam>>16);
  if(message==WM_RBUTTONUP){
    if(dragging&&!wasDragMoved&&mapPanel){
      i32 point[2]={mx,my};
      /* Normal right click continues to show star info while zoomed. */
      callMouseEvent(originalMapRightDown,mapPanel,keys,point);
      callMouseEvent(originalMapRightUp,mapPanel,keys,point);
    }
    if(dragging&&wasDragMoved)pendingPanRebuild=1;
    dragging=0;physicalButtonObserved=0;
    return;
  }
  if(message==WM_KILLFOCUS){
    if(dragging&&wasDragMoved)pendingPanRebuild=1;
    dragging=0;physicalButtonObserved=0;return;
  }
  /* Recreating the map releases Windows/UI mouse capture and may emit
     WM_CAPTURECHANGED or WM_CANCELMODE while RMB is STILL held. Neither
     is an actual release of the physical right mouse button. */
  if(message==WM_CANCELMODE || message==WM_CAPTURECHANGED)return;
  if(message==WM_RBUTTONDOWN){
    if(zoomFactor<=1.0001f){ dragging=0;return; }
    u8 *panel=*(u8 **)(self+0xd4);
    if(!panel){dragging=0;return;}
    i32 *r=(i32 *)(panel+0x60);
    /* WM_RBUTTONDOWN LParam is client coordinates (same coordinate system as
       the map hit rectangle for normal full-screen HD display). */
    if(mx<r[0]||mx>=r[2]||my<r[1]||my>=r[3]){dragging=0;return;}
    dragRawX=mx;dragRawY=my;
    dragging=1;wasDragMoved=0;physicalButtonObserved=1;
    lastRefreshTick=0;
    return;
  }
  if(message!=WM_MOUSEMOVE||!dragging)return;
  /* Ignore synthetic WM_MOUSEMOVE inserted by the engine on OnOpen. Never
   * clear dragging on wParam=0, only a real button-up message can stop pan. */
  if(!(keys&MK_RBUTTON)){
    if(!getAsyncKeyState || !(getAsyncKeyState(VK_RBUTTON)&0x8000))return;
  }
  u8 *panel=*(u8 **)(self+0xd4);
  if(!panel)return;
  i32 *r=(i32 *)(panel+0x60);
  i32 w=r[2]-r[0],h=r[3]-r[1];
  if(w<200||h<150)return;
  int dx=mx-dragRawX,dy=my-dragRawY;
  dragRawX=mx;dragRawY=my;
  if(dx==0&&dy==0)return;
  if(dx>240)dx=240;else if(dx< -240)dx=-240;
  if(dy>240)dy=240;else if(dy< -240)dy=-240;
  const double inv=1.0/(double)zoomFactor;
  double nx=limit(centerX-(double)dx/(double)w*inv,0.5*inv,1.0-0.5*inv);
  double ny=limit(centerY-(double)dy/(double)h*inv,0.5*inv,1.0-0.5*inv);
  if(nx==centerX&&ny==centerY)return;
  centerX=nx;centerY=ny;wasDragMoved=1;
  /* Never OnClose/OnOpen while ProcessWindowMessage is still dispatching.
     Defer rebuilding to the safe post-dispatch ProcessCallbackTimers stage. */
  pendingPanRebuild=1;
}
NAKED static void windowMessageHook(void){
  __asm__ volatile(
    ".intel_syntax noprefix;"
    "pushfd;pushad;"
    "mov eax,DWORD PTR [esp+40];"
    "push eax;"
    "push DWORD PTR [esp+28];"
    "push DWORD PTR [esp+28];"
    "push DWORD PTR [esp+40];"
    "call observeWindowMessage;add esp,16;popad;popfd;"
    "jmp DWORD PTR [originalWindowMessage];"
    ".att_syntax prefix;"
  );
}
/* The original map loop calls this virtual method AFTER processing and
 * displaying the current Windows message. It is safe to redraw there:
 * temporary event handlers and mouse capture are no longer on the stack. */
__attribute__((used)) void handleCallbackTimers(u8 *self){
  delphiCall(originalTimerProcedure,self);
  if(!ready||busy||self!=galaxyScreen||!pendingPanRebuild)return;
  i32 tick=getTickCount?(i32)getTickCount():0;
  if(getTickCount && tick-lastRefreshTick<32 && tick-lastRefreshTick>=0)return;
  pendingPanRebuild=0;
  lastRefreshTick=tick;
  rebuildGalaxy(self);
}
NAKED static void callbackTimersHook(void){
  __asm__ volatile(
    ".intel_syntax noprefix;"
    "push eax;call handleCallbackTimers;add esp,4;ret;"
    ".att_syntax prefix;"
  );
}
/* UI markers from FHD QuestMarker/GalaxySearch are positioned by InterfacePos,
 * not by GalaxyPointToMapPoint. Their panel is a SIBLING of the engine Map.
 * Traverse that sibling sub-tree, remember each unzoomed anchor and recompute
 * positions against the same moving source window as game-created star sprites.
 * Do not touch marker controls generated directly in Map (they redraw natively).
 */
static int markerName(const u16 *name){
  if(!name)return 0;
  int n=0;
  for(;n<90&&name[n];++n){
    u16 x=name[n];
    if(x=='M'||x=='m'){
      u16 a=name[n+1],b=name[n+2],c=name[n+3];
      if((a=='a'||a=='A')&&(b=='r'||b=='R')&&(c=='k'||c=='K'))return 1;
    }
    if(x==0x043c||x==0x041c){
      if((name[n+1]==0x0435||name[n+1]==0x0415)&&
         (name[n+2]==0x0442||name[n+2]==0x0422)&&
         (name[n+3]==0x043a||name[n+3]==0x041a))return 1;
    }
  }
  return 0;
}
static void clearMarkerTracking(void){
  for(int i=0;i<48;i++){
    markers[i].control=0;markers[i].tracked=0;markers[i].wasActive=0;
    markers[i].starId=-1;
  }
}
static Marker* markerTrack(u8 *item){
  Marker *vacant=0;
  for(int i=0;i<48;i++){
    if(markers[i].control==item)return &markers[i];
    if(!markers[i].control&&!vacant)vacant=&markers[i];
  }
  if(vacant)vacant->control=item;
  return vacant;
}
static i32 round2(double x){return x>=0?(i32)(x+0.5):(i32)(x-0.5);}
static void setControlPos(u8 *object,i32 x,i32 y){
  ptr fn=*(ptr *)(*(u8 **)object+0xc); /* TObjectGI.SetPosition VMT slot */
  i32 pos[2]={x,y};
  register ptr eax __asm__("eax")=object;
  register ptr edx __asm__("edx")=pos;
  __asm__ volatile("call *%2":"+a"(eax),"+d"(edx):"m"(fn):"ecx","memory","cc");
}
/* Native star controls have stable names "gs_<id>" and are rebuilt at
 * each zoom. Prefer snapping overlay marker anchors to the same star sprite
 * rather than projecting rounded pixels independently. */
static int starIdFromName(const u16 *name){
  if(!name||name[0]!='g'||name[1]!='s'||name[2]!='_')return -1;
  int id=0,at=3,ndigits=0;
  while(ndigits<8&&name[at]>='0'&&name[at]<='9'){
    id=id*10+(name[at++]-'0');++ndigits;
  }
  return ndigits&&!name[at]?id:-1;
}
static u8 *starGraphicById(u8 *panel,int id){
  u8 *child=*(u8 **)(panel+4);
  for(int i=0;child&&i<20000;i++){
    if(starIdFromName(*(const u16 **)(child+0x78))==id)return child;
    child=*(u8 **)(child+0x10);
  }
  return 0;
}
static void findMarkerStar(Marker *track,u8 *panel){
  track->starId=-1;
  i32 *size=(i32 *)(panel+0x28),*abs=(i32 *)(panel+0x70);
  if(size[0]<=0||size[1]<=0)return;
  double zx=zoomFactor;
  double mx=(track->baseX+track->i32Width*0.5)*zx+
    size[0]*(0.5-centerX*zx)+abs[0];
  double my=(track->baseY+track->i32Height*0.5)*zx+
    size[1]*(0.5-centerY*zx)+abs[1];
  int best=1600,bestid=-1,bdx=0,bdy=0;
  u8 *child=*(u8 **)(panel+4);
  for(int i=0;child&&i<20000;i++){
    int id=starIdFromName(*(const u16 **)(child+0x78));
    if(id>=0){
      i32 *ap=(i32 *)(child+0x70);
      int dx=round2(mx)-ap[0],dy=round2(my)-ap[1];
      int ds=dx*dx+dy*dy;
      if(ds<best){best=ds;bestid=id;bdx=dx;bdy=dy;}
    }
    child=*(u8 **)(child+0x10);
  }
  if(bestid>=0){track->starId=bestid;track->starDX=bdx;track->starDY=bdy;}
}
static void setControlActive(u8 *object,int active){
  ptr fn=*(ptr *)(*(u8 **)object+0x28);
  register ptr eax __asm__("eax")=object;
  register ptr edx __asm__("edx")=(ptr)(active?1:0);
  __asm__ volatile("call *%2":"+a"(eax),"+d"(edx):"m"(fn):"ecx","memory","cc");
}
static void syncMarker(u8 *item,u8 *panel){
  if(!item||!panel)return;
  Marker *track=markerTrack(item);if(!track)return;
  if(!item[0x41]&&!track->wasActive)return; /* respect script visibility */
  i32 *sz=(i32 *)(item+0x28),*local=(i32 *)(item+0x20);
  i32 *abs=(i32 *)(item+0x70),*mapAbs=(i32 *)(panel+0x70);
  if(sz[0]<1||sz[0]>400||sz[1]<1||sz[1]>400)return;
  if(!track->tracked||((local[0]!=track->drawX||local[1]!=track->drawY)&&!markersInMotion&&!track->wasActive)){
    /* Scripts reassign targets in ORIGINAL map screen coordinates. */
    track->baseX=abs[0]-mapAbs[0];
    track->baseY=abs[1]-mapAbs[1];
    track->i32Width=sz[0];track->i32Height=sz[1];
    track->tracked=1;
    findMarkerStar(track,panel);
  }
  u8 *parent=*(u8 **)(item+0x14);if(!parent)return;
  i32 *parentAbs=(i32 *)(parent+0x70);
  i32 *mapSize=(i32 *)(panel+0x28);
  if(mapSize[0]<=0||mapSize[1]<=0)return;
  double x=(track->baseX+track->i32Width*0.5)*zoomFactor+
      mapSize[0]*(0.5-centerX*zoomFactor)-track->i32Width*0.5;
  double y=(track->baseY+track->i32Height*0.5)*zoomFactor+
      mapSize[1]*(0.5-centerY*zoomFactor)-track->i32Height*0.5;
  i32 absoluteX=round2(x)+mapAbs[0];
  i32 absoluteY=round2(y)+mapAbs[1];
  if(track->starId>=0){
    u8 *star=starGraphicById(panel,track->starId);
    if(star){
      i32 *ap=(i32 *)(star+0x70);
      absoluteX=ap[0]+track->starDX-sz[0]/2;
      absoluteY=ap[1]+track->starDY-sz[1]/2;
    }
  }
  i32 targetX=absoluteX-parentAbs[0],targetY=absoluteY-parentAbs[1];
  track->drawX=targetX;track->drawY=targetY;
  if(targetX!=local[0]||targetY!=local[1])setControlPos(item,targetX,targetY);
  i32 *rect=(i32 *)(panel+0x60);
  int visible=(absoluteX>=rect[0]+1 && absoluteY>=rect[1]+1 &&
               absoluteX+sz[0]<=rect[2]-1 && absoluteY+sz[1]<=rect[3]-1);
  if(!visible&&item[0x41]){setControlActive(item,0);track->wasActive=1;}
  else if(visible&&track->wasActive){setControlActive(item,1);track->wasActive=0;}
}
/* Some vanilla GAI icons (including war symbols) draw with a full-screen
 * viewport, not MapPanel clip. Do not draw such a sprite if its rectangle
 * would cross the galaxy map border; native map re-open recreates each one. */

/* Child order within the engine Map is deterministic for the same static
 * galaxy during OnOpen. Record original star associations once, before any
 * zooming. A sprite already in the correct place remains unchanged. */
static void clearIconAnchors(void){
  iconAnchorCount=0;starBeforeCount=0;
}
static StarAnchor *anchorById(StarAnchor *arr,int count,int id){
  for(int i=0;i<count;i++)if(arr[i].id==id)return &arr[i];
  return 0;
}
static int collectStars(u8 *panel,StarAnchor *arr,int capacity){
  int count=0;
  u8 *child=*(u8 **)(panel+4);
  for(int i=0;child&&i<MAX_GALAXY_ICONS;i++){
    int id=starIdFromName(*(const u16 **)(child+0x78));
    if(id>=0&&count<capacity){
      i32 *a=(i32 *)(child+0x70);
      arr[count].id=id;arr[count].x=a[0];arr[count].y=a[1];
      count++;
    }
    child=*(u8 **)(child+0x10);
  }
  return count;
}
static int isUnnamedIcon(u8 *child,ptr starVmt){
  if(!child||*(ptr *)child!=starVmt)return 0;
  const u16 *name=*(const u16 **)(child+0x78);
  if(name&&name[0])return 0;
  i32 *size=(i32 *)(child+0x28);
  return size[0]>=8&&size[0]<=250&&size[1]>=8&&size[1]<=250;
}
static ptr nativeStarSpriteVmt(u8 *panel){
  u8 *child=*(u8 **)(panel+4);
  for(int i=0;child&&i<MAX_GALAXY_ICONS;i++){
    if(starIdFromName(*(const u16 **)(child+0x78))>=0)return *(ptr *)child;
    child=*(u8 **)(child+0x10);
  }
  return 0;
}
static void captureIconAnchors(u8 *self){
  if(!self||zoomFactor>1.0001f)return;
  u8 *panel=*(u8 **)(self+0xd4);
  if(!panel)return;
  starBeforeCount=collectStars(panel,starsBefore,MAX_GALAXY_STARS);
  iconAnchorCount=0;
  if(!starBeforeCount)return;
  ptr starVmt=nativeStarSpriteVmt(panel);
  if(!starVmt)return;
  u8 *child=*(u8 **)(panel+4);
  for(int i=0;child&&i<MAX_GALAXY_ICONS;i++){
    if(isUnnamedIcon(child,starVmt)){
      i32 *box=(i32 *)(child+0x60);
      i32 *sz=(i32 *)(child+0x28);
      i32 cx=(box[0]+box[2])/2,cy=(box[1]+box[3])/2;
      int near=-1,best=65*65;
      for(int j=0;j<starBeforeCount;j++){
        i32 dx=cx-starsBefore[j].x,dy=cy-starsBefore[j].y;
        i32 ds=dx*dx+dy*dy;
        if(ds<best){best=ds;near=j;}
      }
      if(near>=0&&iconAnchorCount<MAX_GALAXY_ICONS){
        IconAnchor *a=&iconAnchors[iconAnchorCount++];
        a->classVmt=starVmt;
        a->starId=starsBefore[near].id;
        a->offsetX=cx-starsBefore[near].x;
        a->offsetY=cy-starsBefore[near].y;
        a->sizeX=sz[0];a->sizeY=sz[1];
        a->baseCenterX=cx;a->baseCenterY=cy;
        a->matched=0;
      }
    }
    child=*(u8 **)(child+0x10);
  }
}
/* Current sprite center can be either at the ORIGINAL unzoomed position
 * (observed in 0.bmp -> 1.bmp) or at the proper zoomed star position.
 * Match whichever is nearer. Each baseline sprite can be used only once.
 * Preserve offsets: swords deliberately appear beside the combat ring. */
static void syncIconAnchors(u8 *self){
  if(!self||!iconAnchorCount||zoomFactor<=1.0001f)return;
  u8 *panel=*(u8 **)(self+0xd4);
  if(!panel)return;
  int n=collectStars(panel,starsNow,MAX_GALAXY_STARS);
  if(n<=0)return;
  ptr starVmt=nativeStarSpriteVmt(panel);
  if(!starVmt)return;
  for(int i=0;i<iconAnchorCount;i++)iconAnchors[i].matched=0;
  u8 *child=*(u8 **)(panel+4);
  for(int i=0;child&&i<MAX_GALAXY_ICONS;i++){
    u8 *next=*(u8 **)(child+0x10);
    if(isUnnamedIcon(child,starVmt)){
      i32 *box=(i32 *)(child+0x60),*sz=(i32 *)(child+0x28);
      i32 cx=(box[0]+box[2])/2,cy=(box[1]+box[3])/2;
      int chosen=-1;
      /* A threshold smaller than star separation to avoid grabbing an
         unrelated icon when mods create additional animated UI elements. */
      i32 best=36*36;
      for(int j=0;j<iconAnchorCount;j++){
        IconAnchor *a=&iconAnchors[j];
        if(a->matched||a->classVmt!=starVmt||
           a->sizeX!=sz[0]||a->sizeY!=sz[1])continue;
        StarAnchor *star=anchorById(starsNow,n,a->starId);
        if(!star)continue;
        i32 tx=star->x+a->offsetX,ty=star->y+a->offsetY;
        i32 d0x=cx-a->baseCenterX,d0y=cy-a->baseCenterY;
        i32 d1x=cx-tx,d1y=cy-ty;
        i32 orig=d0x*d0x+d0y*d0y;
        i32 proj=d1x*d1x+d1y*d1y;
        i32 dist=orig<proj?orig:proj;
        if(dist<best){best=dist;chosen=j;}
      }
      if(chosen>=0){
        IconAnchor *a=&iconAnchors[chosen];
        a->matched=1;
        StarAnchor *star=anchorById(starsNow,n,a->starId);
        i32 tx=star->x+a->offsetX,ty=star->y+a->offsetY;
        i32 dx=tx-cx,dy=ty-cy;
        if((dx||dy)&&dx<15000&&dx> -15000&&dy<15000&&dy> -15000){
          i32 *pos=(i32 *)(child+0x20);
          setControlPos(child,pos[0]+dx,pos[1]+dy);
        }
      }
    }
    child=next;
  }
}
/* Deliberately narrow name recognizer: only the exact overlay families in
 * the supplied AModGalaxyLite Main.txt. Never transform interface buttons,
 * panels or unrelated mod labels. */
static int liteDigits(const u16 *name,int start){
  if(!name||name[start]<'0'||name[start]>'9')return 0;
  for(int i=start;i<start+5;i++){
    u16 x=name[i];
    if(x==0)return 1;
    if(x<'0'||x>'9')return 0;
  }
  return 0;
}
static int literalPrefix(const u16 *name,const char *text){
  if(!name||!text)return -1;
  int i=0;
  for(;text[i];++i)if(name[i]!=(u16)(u8)text[i])return -1;
  return i;
}
static int isLiteName(const u16 *name){
  if(!name)return 0;
  int n=literalPrefix(name,"RedRing");
  if(n>=0)return liteDigits(name,n);
  n=literalPrefix(name,"BlackHole");
  if(n>=0)return liteDigits(name,n);
  int k=0;
  /* Valid faction-letter pairs, triples, quadruples and five-way battles. */
  for(;k<6;++k){
    u16 ch=name[k];
    if(ch>='0'&&ch<='9')break;
    if(ch!='l'&&ch!='d'&&ch!='a'&&ch!='c'&&ch!='p')return 0;
  }
  return k>=2&&k<=5&&liteDigits(name,k);
}
static void clearLiteOverlays(void){
  liteOverlayCount=0;
  liteBaselineReady=0;
}
static void captureLiteAt(u8 *child,StarAnchor *stars,int starCount){
  if(!child||liteOverlayCount>=MAX_LITE_OVERLAYS||!child[0x41])return;
  if(!isLiteName(*(const u16 **)(child+0x78)))return;
  i32 *size=(i32 *)(child+0x28),*r=(i32 *)(child+0x60);
  if(size[0]<=0||size[1]<=0||size[0]>128||size[1]>128)return;
  i32 cx=(r[0]+r[2])/2,cy=(r[1]+r[3])/2;
  int match=-1,dist=50*50;
  for(int i=0;i<starCount;i++){
    i32 dx=cx-stars[i].x,dy=cy-stars[i].y,ds=dx*dx+dy*dy;
    if(ds<dist){dist=ds;match=i;}
  }
  if(match<0)return;  /* Mark not on a known/visible star: leave intact. */
  LiteOverlay *a=&liteOverlays[liteOverlayCount++];
  a->control=child;
  a->starId=stars[match].id;
  a->starDx=cx-stars[match].x;
  a->starDy=cy-stars[match].y;
  a->baselineX=cx;
  a->baselineY=cy;
  a->hiddenByZoom=0;
}
static void captureLiteOverlays(u8 *self){
  if(!self||self!=galaxyScreen||zoomFactor>1.0001f)return;
  u8 *panel=*(u8 **)(self+0xd4);
  if(!panel)return;
  StarAnchor original[MAX_GALAXY_STARS];
  int count=collectStars(panel,original,MAX_GALAXY_STARS);
  if(!count)return;
  /* Map and the named GAI assets are DIRECT SIBLINGS in AModGalaxyLite.
     This captures only those siblings and never traverses live Map icons. */
  u8 *parent=*(u8 **)(panel+0x14);
  if(!parent)return;
  liteOverlayCount=0;
  u8 *child=*(u8 **)(parent+4);
  for(int i=0;child&&i<5000;i++){
    if(child!=panel)captureLiteAt(child,original,count);
    child=*(u8 **)(child+0x10);
  }
  liteBaselineReady=1;
}
static void syncLiteOverlays(u8 *self){
  if(!liteBaselineReady||!liteOverlayCount||!self||self!=galaxyScreen)return;
  u8 *panel=*(u8 **)(self+0xd4);
  if(!panel)return;
  StarAnchor current[MAX_GALAXY_STARS];
  int count=collectStars(panel,current,MAX_GALAXY_STARS);
  if(!count)return;
  i32 *mapRect=(i32 *)(panel+0x60);
  for(int i=0;i<liteOverlayCount;i++){
    LiteOverlay *a=&liteOverlays[i];
    u8 *icon=a->control;
    if(!icon)continue;
    StarAnchor *star=anchorById(current,count,a->starId);
    if(!star)continue;
    /* Use the pixel offset established at x1, not the icon's present
       coordinates: those may have been recreated by a script with the
       vanilla projection rather than the zoomed projection. */
    i32 wantX=star->x+a->starDx;
    i32 wantY=star->y+a->starDy;
    i32 *r=(i32 *)(icon+0x60),*sz=(i32 *)(icon+0x28);
    i32 cx=(r[0]+r[2])/2,cy=(r[1]+r[3])/2;
    i32 dx=wantX-cx,dy=wantY-cy;
    if((dx||dy)&&dx<15000&&dx> -15000&&dy<15000&&dy> -15000){
      i32 *pos=(i32 *)(icon+0x20);
      setControlPos(icon,pos[0]+dx,pos[1]+dy);
    }
    /* Unlike built-in star sprites these GAI are siblings of Map and
       the parent viewport is larger. Hide out-of-frame graphics here. */
    int inside=wantX-sz[0]/2>=mapRect[0]+1&&
               wantY-sz[1]/2>=mapRect[1]+1&&
               wantX+(sz[0]+1)/2<=mapRect[2]-1&&
               wantY+(sz[1]+1)/2<=mapRect[3]-1;
    if(!inside&&icon[0x41]){setControlActive(icon,0);a->hiddenByZoom=1;}
    if(inside&&a->hiddenByZoom){setControlActive(icon,1);a->hiddenByZoom=0;}
  }
}
static void hideOverflowIcons(u8 *self){
  if(!self||!mapPanel||zoomFactor<=1.0001f)return;
  i32 *rect=(i32 *)(mapPanel+0x60);
  u8 *child=*(u8 **)(mapPanel+4);
  u8 *starLinks=*(u8 **)(self+0x108);
  for(int i=0;child&&i<20000;i++){
    u8 *next=*(u8 **)(child+0x10);
    if(child!=starLinks&&child[0x41]){
      i32 *size=(i32 *)(child+0x28);
      i32 *box=(i32 *)(child+0x60);
      if(size[0]>0&&size[1]>0&&size[0]<280&&size[1]<280&&
         (box[0]<rect[0]+1||box[1]<rect[1]+1||
          box[2]>rect[2]-1||box[3]>rect[3]-1)){
        setControlActive(child,0);
      }
    }
    child=next;
  }
}
static void walkMarkers(u8 *obj,u8 *mapPanel,int depth,int *visited){
  if(!obj||depth>14||*visited>2000)return;
  u8 *child=*(u8 **)(obj+4);
  for(int i=0;child&&i<1000&&*visited<2000;++i){
    u8 *next=*(u8 **)(child+0x10);
    ++*visited;
    if(child!=mapPanel){
      if(markerName(*(const u16 **)(child+0x78)))syncMarker(child,mapPanel);
      walkMarkers(child,mapPanel,depth+1,visited);
    }
    child=next;
  }
}
static void updateMarkers(u8 *self){
  if(!ready||markersInMotion||!self||self!=galaxyScreen)return;
  u8 *panel=*(u8 **)(self+0xd4);if(!panel)return;
  u8 *container=*(u8 **)(panel+0x14);if(!container)return;
  int visited=0;
  walkMarkers(container,panel,0,&visited);
}
/* Galaxy PolyLineGI.DrawUpdateRects (animated branch) sends a SCREEN-sized
 * clip rect to DX, ignoring MapPanel. Some gradients don't use clip rect at
 * all. Clip BOTH the incoming rect and the physical segment endpoints. */
static int belongsToGalaxyMap(u8 *line){
  if(!ready||!line||!mapPanel||!galaxyScreen)return 0;
  if(line==*(u8 **)(galaxyScreen+0x108))return 1;
  u8 *owner=*(u8 **)(line+0x14);
  for(int i=0;owner&&i<12;i++){
    if(owner==mapPanel)return 1;
    owner=*(u8 **)(owner+0x14);
  }
  return 0;
}
/* Liang-Barsky line clipping with no dependencies or change to original
 * segment allocation. Unlike rect-only scissoring this works for gradient
 * line rasterizers that ignore the clip parameter. */
static int clampLine(i32 *a,i32 *b,const i32 *r){
  double x0=a[0],y0=a[1],x1=b[0],y1=b[1];
  double dx=x1-x0,dy=y1-y0;
  double left=r[0]+2,top=r[1]+2,right=r[2]-3,bottom=r[3]-3;
  double t0=0.0,t1=1.0;
  double p[4]={-dx,dx,-dy,dy};
  double q[4]={x0-left,right-x0,y0-top,bottom-y0};
  for(int i=0;i<4;i++){
    if(p[i]==0.0){if(q[i]<0.0)return 0;continue;}
    double t=q[i]/p[i];
    if(p[i]<0){if(t>t1)return 0;if(t>t0)t0=t;}
    else {if(t<t0)return 0;if(t<t1)t1=t;}
  }
  if(t0>t1)return 0;
  a[0]=round2(x0+t0*dx);a[1]=round2(y0+t0*dy);
  b[0]=round2(x0+t1*dx);b[1]=round2(y0+t1*dy);
  return 1;
}
__attribute__((used)) int clipGalaxySegment(u8 *line,u8 *segment,i32 *rect){
  if(!line||!segment||!rect||!belongsToGalaxyMap(line))return 1;
  i32 *mapRect=(i32 *)(mapPanel+0x60);
  if(mapRect[2]-mapRect[0]<200||mapRect[3]-mapRect[1]<150)return 1;
  if(rect[0]<mapRect[0])rect[0]=mapRect[0];
  if(rect[1]<mapRect[1])rect[1]=mapRect[1];
  if(rect[2]>mapRect[2])rect[2]=mapRect[2];
  if(rect[3]>mapRect[3])rect[3]=mapRect[3];
  if(rect[2]<=rect[0]+4||rect[3]<=rect[1]+4)return 0;
  return clampLine((i32 *)(segment+0x28),(i32 *)(segment+0x30),rect);
}
NAKED static void frameSegmentHook(void){
  __asm__ volatile(
    ".intel_syntax noprefix;"
    "pushfd;pushad;"
    "push ecx;push edx;push eax;call clipGalaxySegment;add esp,12;"
    "mov DWORD PTR [clippedLineVisible],eax;"
    "popad;popfd;"
    "cmp DWORD PTR [clippedLineVisible],0;je 1f;"
    "jmp DWORD PTR [originalFrameSegment];"
    "1:ret;"
    ".att_syntax prefix;"
  );
}
NAKED static void drawSegmentHook(void){
  __asm__ volatile(
    ".intel_syntax noprefix;"
    "pushfd;pushad;"
    "push ecx;push edx;push eax;call clipGalaxySegment;add esp,12;"
    "mov DWORD PTR [clippedLineVisible],eax;"
    "popad;popfd;"
    "cmp DWORD PTR [clippedLineVisible],0;je 1f;"
    "jmp DWORD PTR [originalDrawSegment];"
    "1:ret;"
    ".att_syntax prefix;"
  );
}
static void installLineClip(u8 *self){
  u8 *starLinks=*(u8 **)(self+0x108);
  if(!starLinks)return;
  u8 *vmt=*(u8 **)starLinks;
  if(!vmt||(u32)vmt<(u32)game||(u32)vmt>=(u32)(game+imageSize))return;
  if(vmt==hookedPolyVmt)return;
  ptr oldFrame=*(ptr *)(vmt+0xcc);
  ptr oldDraw=*(ptr *)(vmt+0xc8);
  if((u32)oldFrame<(u32)(game+0x1000)||(u32)oldFrame>=(u32)(game+imageSize))return;
  if((u32)oldDraw<(u32)(game+0x1000)||(u32)oldDraw>=(u32)(game+imageSize))return;
  originalFrameSegment=oldFrame;
  originalDrawSegment=oldDraw;
  if(replacePtr(vmt+0xcc,(ptr)frameSegmentHook)){
    if(replacePtr(vmt+0xc8,(ptr)drawSegmentHook))hookedPolyVmt=vmt;
    else replacePtr(vmt+0xcc,oldFrame);
  }
}

static void installMapCallbacks(u8 *self){
  if(!self||self!=galaxyScreen)return;
  mapPanel=*(u8 **)(self+0xd4);
  if(!mapPanel)return;
  originalMapRightDown.code=*(ptr *)(mapPanel+0xb8);
  originalMapRightDown.data=*(ptr *)(mapPanel+0xbc);
  originalMapRightUp.code=*(ptr *)(mapPanel+0xc0);
  originalMapRightUp.data=*(ptr *)(mapPanel+0xc4);
  if(zoomFactor>1.0001f){
    writeMethod(mapPanel,0xb8,(ptr)mapRightDownHook,self);
    writeMethod(mapPanel,0xc0,(ptr)mapRightUpHook,self);
  }
  installLineClip(self);
}
static void rebuildGalaxy(u8 *self){
  if(!self||busy)return;
  u8 savedMode=self[0xd8];
  static const u8 nops[5]={0x90,0x90,0x90,0x90,0x90};
  busy=1;
  if(!replaceBytes(clearSite,nops,5)){busy=0;return;}
  /* Do NOT set CapturePreviewOnOpen: that invokes preview-only render/close
     and breaks the active map.  Rebuild as a normal map opening. */
  delphiCall(originalClose,self);
  replaceBytes(clearSite,originalClearCall,5);
  self[0xd8]=savedMode;
  zoomApplied=0;
  delphiCall(originalOpen,self);
  installMapCallbacks(self);
  syncIconAnchors(self);
  syncLiteOverlays(self);
  busy=0;
  updateMarkers(self);
  hideOverflowIcons(self);
}
/* Returned status: 0 installed, 1 no process, 2 wrong executable,
   3 signatures mismatch, 4 OS imports unavailable, 5 unable to pin module,
   6 galaxy class not ready, 7 patch write failed. */
u32 FHDZoom_Init(void) {
  if(ready)return 0;
  u8 *peb=(u8 *)pebAddress();if(!peb)return 1;
  game=*(u8 **)(peb+8);
  if(!game||*(u16 *)game!=0x5a4d)return 1;
  u8 *nt=game+*(u32 *)(game+0x3c);
  if(*(u32 *)nt!=0x4550||*(u16 *)(nt+24)!=0x10b)return 1;
  u32 ts=*(u32 *)(nt+8);
  imageSize=*(u32 *)(nt+24+56);
  u32 clearRva=0,expectedOpen=0,expectedClose=0,expectedWheel=0;
  int newExe=0;
  if(ts==0x68eccc46&&imageSize==0x4d1000) {
    globalGalaxyScreenRva=0x48bf14;
    clearRva=0x2a2edd;
    expectedOpen=0x2a0160;expectedClose=0x2a2dc0;expectedWheel=0x0cfccc;
  } else if(ts==0x6a7afc5c&&imageSize==0x4d2000) {
    globalGalaxyScreenRva=0x48a478;
    clearRva=0x274c1d;
    expectedOpen=0x271ea0;expectedClose=0x274b00;expectedWheel=0x0c1b7c;
  } else if(ts==0x6ac53b97&&imageSize==0x461000) {
    globalGalaxyScreenRva=0x41cbb4;
    clearRva=0x2776b1;
    expectedOpen=0x274bb8;expectedClose=0x2775cc;expectedWheel=0x0c620c;
    newExe=1;
  } else return 2;
  /* VMT entry verification also protects against wrong offsets/mod conflicts. */
  galaxyScreen=*(u8 **)(game+globalGalaxyScreenRva);
  if(!galaxyScreen)return 6;
  u8 *vmt=*(u8 **)galaxyScreen;
  if(!vmt||(u32)vmt<(u32)game||(u32)vmt>=(u32)(game+imageSize))return 6;
  if(*(u32 *)(vmt+0x1c)!=(u32)(game+expectedOpen) ||
     *(u32 *)(vmt+0x20)!=(u32)(game+expectedClose) ||
     *(u32 *)(vmt+0x2c)!=(u32)(game+expectedWheel))return 3;
  u8 *projection = newExe ? game+0x277a18 : scanProjection(game,imageSize);
  if(!projection)return 3;
  if(newExe && !same(projection,projectionSignatureNew,24))return 3;
  clearSite=game+clearRva;
  const u8 expectedPrefix[]={0x8b,0x00,0xe8};
  if(clearSite[0]!=0xe8||!same(clearSite-2,expectedPrefix,2))return 3;
  for(int i=0;i<5;i++)originalClearCall[i]=clearSite[i];
  ptr ntdll=findModule("ntdll.dll");
  protectMemory=(Protect)findExport(ntdll,"NtProtectVirtualMemory");
  flushCache=(Flush)findExport(ntdll,"NtFlushInstructionCache");
  if(!protectMemory||!flushCache)return 4;
  ptr kb=findModule("kernelbase.dll"),k32=findModule("kernel32.dll");
  PinModule pin=(PinModule)findExport(kb,"GetModuleHandleExW");
  if(!pin)pin=(PinModule)findExport(k32,"GetModuleHandleExW");
  ptr pinned=0;
  if(!pin||!pin(1|4,(const u16 *)(ptr)FHDZoom_Init,&pinned)||!pinned)return 5;
  originalOpen=(ptr)(game+expectedOpen);
  originalClose=(ptr)(game+expectedClose);
  projectionResume=(ptr)(projection+6);
  if(!replaceJump(projection,newExe?(ptr)projectionHookNew:(ptr)projectionHook))return 7;
  if(!replacePtr(vmt+0x1c,(ptr)openHook)) {
    replaceBytes(projection,newExe?projectionSignatureNew:projectionSignature,6);return 7;
  }
  if(!replacePtr(vmt+0x2c,(ptr)wheelHook)) {
    replacePtr(vmt+0x1c,originalOpen);
    replaceBytes(projection,newExe?projectionSignatureNew:projectionSignature,6);return 7;
  }
  originalWindowMessage=*(ptr *)(vmt+0x04);
  if(!originalWindowMessage || (u32)originalWindowMessage < (u32)(game+0x1000) ||
      (u32)originalWindowMessage >= (u32)(game+imageSize) ||
      !replacePtr(vmt+0x04,(ptr)windowMessageHook)){
    replacePtr(vmt+0x2c,(ptr)(game+expectedWheel));
    replacePtr(vmt+0x1c,originalOpen);
    replaceBytes(projection,newExe?projectionSignatureNew:projectionSignature,6);return 7;
  }
  originalTimerProcedure=*(ptr *)(vmt+0x24);
  if(!originalTimerProcedure ||
     (u32)originalTimerProcedure < (u32)(game+0x1000) ||
     (u32)originalTimerProcedure >= (u32)(game+imageSize) ||
     !replacePtr(vmt+0x24,(ptr)callbackTimersHook)){
    replacePtr(vmt+0x04,originalWindowMessage);
    replacePtr(vmt+0x2c,(ptr)(game+expectedWheel));
    replacePtr(vmt+0x1c,originalOpen);
    replaceBytes(projection,newExe?projectionSignatureNew:projectionSignature,6);
    return 7;
  }
  ptr tmod=findModule("kernelbase.dll");
  getTickCount=(u32 (STDCALL *)(void))findExport(tmod,"GetTickCount");
  if(!getTickCount)getTickCount=(u32 (STDCALL *)(void))findExport(findModule("kernel32.dll"),"GetTickCount");
  getAsyncKeyState=(short (STDCALL *)(int))findExport(findModule("user32.dll"),"GetAsyncKeyState");
  ready=1;
  return 0;
}
u32 FHDZoom_Status(void) {return ready?0:2;}
int STDCALL DllMain(ptr module,u32 reason,ptr reserved) {
  (void)module;(void)reason;(void)reserved;return 1;
}
