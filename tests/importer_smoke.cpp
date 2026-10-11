#include <Windows.h>
#include <cstdio>
#include <cstring>
#include "fixture.hpp"
int main(int argc, char** argv) {
    if(argc!=3) return 2;
    const bool negative = std::strcmp(argv[2], "positive") != 0;
    if(std::strcmp(argv[2], "missing")==0) signature0[0]=0;
    if(std::strcmp(argv[2], "ambiguous")==0) std::memcpy(duplicateSignature,signature0,sizeof(signature0));
    HMODULE mod=LoadLibraryA(argv[1]);
    if(!mod) { std::printf("LoadLibrary failed: %lu\n",GetLastError());return 3; }
    auto init=reinterpret_cast<bool(*)()>(GetProcAddress(mod,"OrionOptimizationResolveImports"));
    if(!init) return 4;
    bool first=init(), second=init();
    std::printf("mode=%s first=%d second=%d expected=%d\n",argv[2],first,second,!negative);
    // Imports must remain resolved on repeated initialization; failed resolution must never enable the mod.
    return first==!negative && second==!negative ? 0:1;
}
