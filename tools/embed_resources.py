"""Bake required resources into the executable; runtime never extracts them."""
from pathlib import Path
import sys
root,build=map(Path,sys.argv[1:3])
files=[*sorted((root/'assets').rglob('*.png')),root/'fonts/english.png',root/'fonts/default-skin.png']
entries=[(p.relative_to(root).as_posix(),p) for p in files]
entries += [('build/world.'+stage+'.spv',build/('world.'+stage+'.spv')) for stage in ('vert','frag')]
output=['#include "resources.hpp"\n']
for index,(name,path) in enumerate(entries):
 data=path.read_bytes()
 output.append('alignas(4) static const unsigned char resource'+str(index)+'[]={'+','.join(map(str,data))+'};\n')
output.append('std::span<const unsigned char> embeddedFile(std::string_view path) {\n')
for index,(name,path) in enumerate(entries):
 output.append('if(path=="'+name+'")return resource'+str(index)+';\n')
output.append('return {};\n}\n')
(build/'embedded_resources.cpp').write_text(''.join(output),encoding='utf-8')
print('Embedded',len(entries),'resources,',sum(p.stat().st_size for _,p in entries),'bytes')
