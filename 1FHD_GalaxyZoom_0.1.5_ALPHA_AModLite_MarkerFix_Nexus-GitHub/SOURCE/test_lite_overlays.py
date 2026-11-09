"""Static contract checks for known AModGalaxyLite GAI names and anchor math.
No real Rangers.exe process is started by these checks.
"""
import re
import zipfile
from pathlib import Path


def is_our_overlay(name):
    if re.fullmatch(r"RedRing\d{1,5}", name):
        return True
    if re.fullmatch(r"BlackHole\d{1,5}", name):
        return True
    return bool(re.fullmatch(r"[ldacp]{2,5}\d{1,5}", name))


def transform_star_and_marker(base_star, base_icon, new_star):
    return (new_star[0] + base_icon[0] - base_star[0],
            new_star[1] + base_icon[1] - base_star[1])


for label in ("RedRing0", "RedRing200", "BlackHole152", "lp1", "ldacp5"):
    assert is_our_overlay(label)
for nonmarker in ("Map", "ButExit", "PathCurPos", "Distance", "gs_45", "MarkerScreen"):
    assert not is_our_overlay(nonmarker)

# Both coincident rings and offset war icons remain attached on repeat zoom/pan.
original_star = (710, 180)
for icon in [(710, 180), (720, 170), (693, 183)]:
    for zoomed_star in [(900, 440), (1200, -100), (465, 370), original_star]:
        anchored = transform_star_and_marker(original_star, icon, zoomed_star)
        assert anchored[0] - zoomed_star[0] == icon[0] - original_star[0]
        assert anchored[1] - zoomed_star[1] == icon[1] - original_star[1]

source_path = Path(__file__).resolve().parents[5] / 'AModGalaxyLite.zip'
# Path comes from the external project when present; tests still run without it.
if source_path.exists():
    with zipfile.ZipFile(source_path) as archive:
        cfg = archive.read('Mods/Andromeda/AModGalaxyLite/CFG/Main.txt').decode('cp1251')
    names = re.findall(r'^\s*Name=(\S+)', cfg, re.M)
    overlaid = [n for n in names if is_our_overlay(n)]
    assert sum(n.startswith('RedRing') for n in overlaid) == 201
    assert sum(n.startswith('BlackHole') for n in overlaid) == 153
    assert len(overlaid) > 570
print('AModGalaxyLite overlay recognition and anchor geometry PASS (no live-game assertion)')
