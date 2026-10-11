#!/usr/bin/env python3
"""Check annotated signatures for unique executable-section matches at PE unwind entries."""
import argparse,hashlib,json,pathlib,re,struct
p=argparse.ArgumentParser();p.add_argument('executables',nargs='+',type=pathlib.Path);p.add_argument('--output',type=pathlib.Path);a=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[1];reports=[];ok=True
for path in a.executables:
 data=path.read_bytes();pe=struct.unpack_from('<I',data,60)[0]
 if data[:2]!=b'MZ' or data[pe:pe+4]!=b'PE\0\0':raise ValueError('Not a PE: '+str(path))
 opt=pe+24;section=opt+struct.unpack_from('<H',data,pe+20)[0];base=struct.unpack_from('<Q',data,opt+24)[0]
 sections=[]
 for i in range(struct.unpack_from('<H',data,pe+6)[0]):
  at=section+40*i;size,rva,rawsize,raw=struct.unpack_from('<IIII',data,at+8);flags=struct.unpack_from('<I',data,at+36)[0]
  sections.append((rva,size,raw,rawsize,flags))
 exception_rva,exception_size=struct.unpack_from('<II',data,opt+112+3*8)
 entries=set()
 for rva,size,raw,rawsize,flags in sections:
  if rva<=exception_rva<rva+rawsize:
   at=raw+exception_rva-rva
   entries={struct.unpack_from('<I',data,q)[0] for q in range(at,at+exception_size,12)}
 report={'path':str(path.resolve()),'sha256':hashlib.sha256(data).hexdigest(),'signatures':[]}
 for header in sorted((root/'include/mc').rglob('*.h')):
  for match in re.finditer(r'@signature\("([^"]+)"\)',header.read_text()):
   pattern=b''.join(b'.' if x=='?' else re.escape(bytes([int(x,16)])) for x in match[1].split())
   hits=[]
   for rva,size,raw,rawsize,flags in sections:
    if flags & 0x20000000:
     for m in re.finditer(b'(?=('+pattern+b'))',data[raw:raw+min(size,rawsize)],re.DOTALL):
      target=rva+m.start();hits.append({'rva':hex(target),'va':hex(base+target),'unwind_entry':target in entries})
   valid=len(hits)==1 and hits[0]['unwind_entry'];ok &=valid
   report['signatures'].append({'header':str(header.relative_to(root)),'pattern':match[1],'valid':valid,'matches':hits})
   print(path.parent.name,header.name, 'PASS' if valid else 'FAIL',hits)
 reports.append(report)
if a.output:a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(reports,indent=2)+'\n')
raise SystemExit(0 if ok else 1)
