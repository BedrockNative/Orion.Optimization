#pragma once
#include "Support.hpp"
uintptr_t GetMinecraftBaseAddress();
size_t GetMinecraftSize();
uintptr_t SlideAddress(uintptr_t address);
uintptr_t GetEffectiveAddress(uintptr_t address);
void UnprotectMemory(uintptr_t address, size_t size, DWORD* old);
void ProtectMemory(uintptr_t address, size_t size, DWORD protection, DWORD* old);

#include <libhat/signature.hpp>
uintptr_t FindUniqueSignature(hat::signature_view signature, bool executableOnly);
