#!/usr/bin/env python3
"""Make a separate game executable importing Orion.Optimization.dll (requires lief)."""
import argparse,hashlib,json,pathlib
import lief
p=argparse.ArgumentParser();p.add_argument('source',type=pathlib.Path);p.add_argument('output',type=pathlib.Path);a=p.parse_args()
if a.source.resolve()==a.output.resolve():raise SystemExit('Refusing to overwrite source executable')
if a.output.exists():raise SystemExit('Output already exists; choose a new filename')
def digest(path):
 with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
original=digest(a.source)
pe=lief.PE.parse(str(a.source))
if pe is None:raise SystemExit('Cannot parse PE')
pe.add_import('Orion.Optimization.dll').add_entry('OrionOptimizationInitialize')
config=lief.PE.Builder.config_t();config.imports=True
builder=lief.PE.Builder(pe,config);builder.build();builder.write(str(a.output))
check=lief.PE.parse(str(a.output))
assert check and any(i.name=='Orion.Optimization.dll' and any(e.name=='OrionOptimizationInitialize' for e in i.entries) for i in check.imports)
assert digest(a.source)==original
record={'source':str(a.source.resolve()),'source_sha256':original,'output':str(a.output.resolve()),'output_sha256':digest(a.output),'lief':lief.__version__,'import':'Orion.Optimization.dll!OrionOptimizationInitialize'}
a.output.with_suffix('.imports.json').write_text(json.dumps(record,indent=2)+'\n')
print(json.dumps(record,indent=2))
