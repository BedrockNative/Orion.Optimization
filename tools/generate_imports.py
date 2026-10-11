#!/usr/bin/env python3
"""Generate only annotation metadata/import library. Runtime-Importer patches the DLL."""
import argparse,json,os,pathlib,re,subprocess
from clang import cindex
p=argparse.ArgumentParser();p.add_argument('--root',type=pathlib.Path,required=True);p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--sdk',default='');p.add_argument('--librarian',required=True);a=p.parse_args()
root=a.root.resolve();out=a.out.resolve();platform=out/'win-client';symbols=platform/'symbols';symbols.mkdir(parents=True,exist_ok=True)
headers=sorted((root/'include/mc').rglob('*.h'))
source='\n'.join(f'#include "{h.as_posix()}"' for h in headers)
args=['-x','c++','-std=c++23','--target=x86_64-pc-windows-msvc','-fms-compatibility','-fms-extensions','-fparse-all-comments','-D_ITERATOR_DEBUG_LEVEL=0','-DNDEBUG','-I'+str(root/'include'),'-I'+str(root/'external/GSL/include')]
if a.sdk:
 for d in ('crt/include','sdk/include/ucrt','sdk/include/shared','sdk/include/um'):
  args+=['-isystem',str(pathlib.Path(a.sdk)/d)]
else:
 for d in os.environ.get('INCLUDE','').split(';'):
  if d:args+=['-isystem',d]
tu=cindex.Index.create().parse('orion_imports.cpp',args=args,unsaved_files=[('orion_imports.cpp',source)])
errors=[str(d) for d in tu.diagnostics if d.severity>=cindex.Diagnostic.Error]
if errors:raise SystemExit('\n'.join(errors))
functions=[]
def visit(cur):
 for c in cur.get_children():
  if c.location.file and pathlib.Path(c.location.file.name).resolve() not in headers:continue
  comment=c.raw_comment or ''
  if '@signature' in comment:
   m=re.search(r'@signature\("([0-9A-Fa-f? ]+)"\)',comment)
   if not m or not c.mangled_name:raise RuntimeError('Invalid signature declaration: '+c.spelling)
   functions.append({'name':c.mangled_name,'signature':m.group(1)})
  if c.kind in (cindex.CursorKind.CLASS_DECL,cindex.CursorKind.STRUCT_DECL,cindex.CursorKind.NAMESPACE):visit(c)
visit(tu.cursor)
if not functions:raise SystemExit('No annotated imports found')
(symbols/'orion.symbols.json').write_text(json.dumps({'functions':functions},indent=2)+'\n')
def_file=platform/'Minecraft.Windows.def';def_file.write_text('LIBRARY "Minecraft.Windows.exe"\nEXPORTS\n'+''.join('    '+x['name']+'\n' for x in functions))
cmd=[a.librarian]
if 'lld-link' in pathlib.Path(a.librarian).name:cmd+=['/lib']
subprocess.run(cmd+['/nologo','/machine:x64','/def:'+str(def_file),'/out:'+str(platform/'Minecraft.Windows.lib')],check=True)
print(f'Generated {len(functions)} signature imports')
