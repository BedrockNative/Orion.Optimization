import json,pathlib,sys
functions=json.loads(pathlib.Path(sys.argv[1]).read_text())['functions']
out='#pragma once\n#pragma section(".orion", read, write, execute)\n'
for i,f in enumerate(functions):
 b=[int(x,16) if x!='?' else 0 for x in f['signature'].split()]
 out+=f'__declspec(allocate(".orion")) unsigned char signature{i}[] = {{'+','.join(map(str,b))+'};\n'
out+='__declspec(allocate(".orion")) unsigned char duplicateSignature[256] = {};\n'
pathlib.Path(sys.argv[2]).write_text(out)
