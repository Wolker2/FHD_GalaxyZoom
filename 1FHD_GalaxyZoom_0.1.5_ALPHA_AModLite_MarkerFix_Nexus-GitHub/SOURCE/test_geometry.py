"""Pure-Python sanity checks for native clip/zoom formulas (NOT an in-game test)."""
from math import isclose

def clip_line(a,b,rect):
    x0,y0=a; x1,y1=b
    dx=x1-x0;dy=y1-y0
    left,top,right,bottom=rect[0]+2,rect[1]+2,rect[2]-3,rect[3]-3
    t0,t1=0.,1.
    for p,q in ((-dx,x0-left),(dx,right-x0),(-dy,y0-top),(dy,bottom-y0)):
        if p==0:
            if q<0:return None
            continue
        t=q/p
        if p<0:
            if t>t1:return None
            t0=max(t0,t)
        else:
            if t<t0:return None
            t1=min(t1,t)
    if t0>t1:return None
    return (x0+t0*dx,y0+t0*dy),(x0+t1*dx,y0+t1*dy)

rect=(320,90,1330,760)
assert clip_line((-1000,120),(500,120),rect)
assert clip_line((1300,-500),(1300,950),rect)
assert clip_line((-50,-50),(20,20),rect) is None
assert clip_line((400,300),(500,400),rect) == ((400.,300.),(500.,400.))
assert clip_line((-900,-900),(-500,-500),rect) is None
for a,b in [((-1000,500),(2000,500)),((600,-500),(600,1200)),((1300,130),(1200,800))]:
    out=clip_line(a,b,rect)
    assert out is not None
    for x,y in out:
        assert rect[0]+1<=x<=rect[2]-2 and rect[1]+1<=y<=rect[3]-2
for zoom in (1.0,1.25,2.0,3.814697265625):
    minimum=0.5/zoom;maximum=1-minimum
    for center in (minimum,0.5,maximum):
        x=(0.5-center)*zoom+0.5
        assert -1<=x<=2
print('geometry cases passed; game runtime untested')
