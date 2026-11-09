"""Formula-level smoke tests; not an in-game integration test."""
from math import isclose

def move_with_star(old_star, old_marker, new_star):
    return (new_star[0] + old_marker[0] - old_star[0],
            new_star[1] + old_marker[1] - old_star[1])
assert move_with_star((700, 210),(700,210),(900,440)) == (900,440)
assert move_with_star((700,210),(720,195),(900,440)) == (920,425)
for new_star in [(50,50),(1250,740),(-200,200)]:
    assert move_with_star((700,210),(700,210),new_star) == new_star

# WM_MOUSEMOVE with zero flags (synthetic) must preserve the drag latch.
WM_RBUTTONDOWN, WM_MOUSEMOVE, WM_RBUTTONUP = 0x204,0x200,0x205
events=[(WM_RBUTTONDOWN,2),(WM_MOUSEMOVE,2),(WM_MOUSEMOVE,0),
        (WM_MOUSEMOVE,2),(WM_RBUTTONUP,0)]
down=False
out=[]
for event,keys in events:
    if event == WM_RBUTTONDOWN: down=True
    if event == WM_RBUTTONUP: down=False
    out.append(down)
assert out == [True,True,True,True,False]
print("anchor geometry and drag state PASS")
