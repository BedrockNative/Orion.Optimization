#!/usr/bin/env python3
"""Generate relocation-wildcard helper signatures and validate both known game PEs."""
import pathlib,struct,re,subprocess,json
root=pathlib.Path(__file__).resolve().parents[1]
import argparse
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('executables', nargs=2, type=pathlib.Path, metavar='EXE', help='26.51.01 and 26.52.03 executables, in that order')
paths=parser.parse_args().executables
def pe(path):
 d=path.read_bytes();o=struct.unpack_from('<I',d,60)[0];s=o+24+struct.unpack_from('<H',d,o+20)[0];sections=[]
 for i in range(struct.unpack_from('<H',d,o+6)[0]):
  at=s+40*i;vs,va,rs,raw=struct.unpack_from('<IIII',d,at+8);flags=struct.unpack_from('<I',d,at+36)[0]
  sections.append((va,d[raw:raw+min(vs,rs)],flags))
 er,es=struct.unpack_from('<II',d,o+24+112+24)
 entries=set()
 for va,b,f in sections:
  if va<=er<va+len(b):entries={struct.unpack_from('<I',b,k)[0] for k in range(er-va,er-va+es,12)}
 return sections,entries
images=[pe(p) for p in paths]
result={}; declarations=[]
for name,rva in [('parse',0x182c690),('copy',0x184b300),('grow',0x184ae90),('destroy',0x182e750),('fullPath',0x275110)]:
 text=subprocess.check_output(['objdump','-d','-Mintel','--insn-width=16','--start-address='+hex(0x140000000+rva),'--stop-address='+hex(0x140000000+rva+240),str(paths[0])],text=True)
 pattern=[];valid=False
 for line in text.splitlines():
  m=re.match(r'\s*([0-9a-f]+):\s*((?:[0-9a-f]{2} )+)\s*(.*)',line)
  if not m:continue
  vals=m[2].split();op=m[3]
  if vals[0] in ('e8','e9') or ('rip+' in op or 'rip-' in op): vals[-4:]=['?']*4

  if op.startswith('sub    rsp,'): vals[-(4 if len(vals)==7 else 1):]=['?']*(4 if len(vals)==7 else 1)
  pattern+=vals
  if len(pattern)<48:continue
  regex=re.compile(b''.join(b'.' if v=='?' else re.escape(bytes([int(v,16)])) for v in pattern),re.S)
  matches=[]
  for sections,entries in images:
   hits=[va+m.start() for va,b,f in sections if f&0x20000000 for m in regex.finditer(b)]
   matches.append(hits)
  if all(len(h)==1 and h[0] in images[i][1] for i,h in enumerate(matches)):
   valid=True;break
  if len(pattern)>180:break
 if not valid:raise RuntimeError((name,matches))
 sig=' '.join(v.upper() for v in pattern)
 declarations.append('inline constexpr auto '+name+' = "'+sig+'";')
 result[name]={'signature':sig,'rvas':[hex(h[0]) for h in matches],'function_entry':True}
 print(name,result[name]['rvas'])
(root/'src/parser-signatures.hpp').write_text('#pragma once\n// Unique function-entry matches validated on 26.51.01 and 26.52.03.\nnamespace orion::parser_signatures {\n'+'\n'.join(declarations)+'\n}\n')
(root/'build/helper-signature-validation.json').write_text(json.dumps(result,indent=2)+'\n')
