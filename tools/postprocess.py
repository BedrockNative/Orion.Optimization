#!/usr/bin/env python3
import argparse,pathlib,struct,subprocess
p=argparse.ArgumentParser();p.add_argument('--dotnet',required=True);p.add_argument('--tool',required=True);p.add_argument('--module',required=True);p.add_argument('--input',required=True);p.add_argument('--output',required=True);a=p.parse_args()
subprocess.run([a.dotnet,a.tool,'--module',a.module,'--input',a.input,'--output',a.output],check=True)
# Upstream can log an error and return exit code zero. Require actual patched sections.
b=pathlib.Path(a.module).read_bytes();pe=struct.unpack_from('<I',b,0x3c)[0];count=struct.unpack_from('<H',b,pe+6)[0];section=pe+24+struct.unpack_from('<H',b,pe+20)[0]
names={b[section+40*i:section+40*i+8].rstrip(b'\0') for i in range(count)}
if not {b'.rtih',b'.rtis'}<=names:raise SystemExit('Runtime-Importer did not produce .rtih/.rtis sections')
print('Runtime-Importer output verified')
