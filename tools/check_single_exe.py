"""Enforce the actual release contract, including PE runtime imports."""
from pathlib import Path
import re, subprocess, sys
folder=Path(sys.argv[1])
exe=folder/'Z3.exe'
allowed={'Z3.exe','world.save','world.save.tmp','world.save.map','world.save.map.tmp','world.save.name','active-world.txt','player-skin.png','worlds','build'}
if any(p.name not in allowed for p in folder.iterdir()):
 raise SystemExit('Release folder contains non-game companion files')
if not 0<exe.stat().st_size<10_000_000:
 raise SystemExit('Release executable must be smaller than 10 MB')
pe=subprocess.check_output([sys.argv[2],'-p',str(exe)],text=True)
imports=re.findall(r'DLL Name: (\S+)',pe)
windows={'comdlg32.dll','gdi32.dll','kernel32.dll','ole32.dll','shell32.dll','user32.dll','winmm.dll','vulkan-1.dll'}
extra=[name for name in imports if name.lower() not in windows and not name.lower().startswith('api-ms-win-')]
if extra:raise SystemExit('Unexpected external runtime DLLs: '+', '.join(extra))
if 'Windows GUI' not in pe:raise SystemExit('Release must open as a Windows GUI application')
print(f'PASS single standalone GUI executable: {exe.stat().st_size:,} bytes; only Windows and installed Vulkan driver imports')
