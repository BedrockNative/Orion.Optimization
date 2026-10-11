#include <amethyst/Memory.hpp>
#include <Zydis/Zydis.h>
uintptr_t GetMinecraftBaseAddress() { return reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)); }
size_t GetMinecraftSize() {
    const auto base = GetMinecraftBaseAddress();
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    return nt->OptionalHeader.SizeOfImage;
}
uintptr_t SlideAddress(uintptr_t address) { return GetMinecraftBaseAddress() + address - 0x140000000ull; }
void ProtectMemory(uintptr_t address, size_t size, DWORD protection, DWORD* old) {
    DWORD ignored;
    if (!VirtualProtect(reinterpret_cast<void*>(address), size, protection, old ? old : &ignored))
        throw std::runtime_error("VirtualProtect failed during import resolution");
}
void UnprotectMemory(uintptr_t address, size_t size, DWORD* old) {
    ProtectMemory(address, size, PAGE_EXECUTE_READWRITE, old);
}
uintptr_t GetEffectiveAddress(uintptr_t address) {
	ZydisDecoder decoder;
	ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);

	ZydisDecodedInstruction instr;
	ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
	if (ZYAN_SUCCESS(ZydisDecoderDecodeFull(&decoder, reinterpret_cast<const void*>(address), ZYDIS_MAX_INSTRUCTION_LENGTH, &instr, operands))) {
		// lea ...
		if (instr.mnemonic == ZYDIS_MNEMONIC_LEA) {
			// ... reg, [rip + disp32]
			if (instr.operand_count_visible == 2 &&
				operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER &&
				operands[1].type == ZYDIS_OPERAND_TYPE_MEMORY &&
				operands[1].mem.base == ZYDIS_REGISTER_RIP &&
				operands[1].mem.disp.has_displacement &&
				operands[1].mem.index == ZYDIS_REGISTER_NONE)
				return address + instr.length + operands[1].mem.disp.value;
		}

		// call|jmp ...
		if (instr.mnemonic == ZYDIS_MNEMONIC_CALL || instr.mnemonic == ZYDIS_MNEMONIC_JMP) {
			// ... rel32
			if (instr.operand_count_visible == 1 &&
				operands[0].type == ZYDIS_OPERAND_TYPE_IMMEDIATE)
				return address + instr.length + operands[0].imm.value.s;

			// ... [rip + disp32]
			if (instr.operand_count_visible == 1 &&
				operands[0].type == ZYDIS_OPERAND_TYPE_MEMORY &&
				operands[0].mem.base == ZYDIS_REGISTER_RIP &&
				operands[0].mem.disp.has_displacement &&
				operands[0].mem.index == ZYDIS_REGISTER_NONE) {
				uintptr_t ptr = address + instr.length + operands[0].mem.disp.value;
				return *reinterpret_cast<uintptr_t*>(ptr);
			}
		}
	}

	return address;
}

#include <libhat/scanner.hpp>
uintptr_t FindUniqueSignature(hat::signature_view signature, bool executableOnly) {
    if (signature.empty()) return 0;
    const auto base = GetMinecraftBaseAddress();
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    const auto sections = IMAGE_FIRST_SECTION(nt);
    uintptr_t found = 0;
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const auto& section = sections[i];
        if (!(section.Characteristics & IMAGE_SCN_MEM_READ) ||
            (executableOnly && !(section.Characteristics & IMAGE_SCN_MEM_EXECUTE))) continue;
        auto start = base + section.VirtualAddress;
        auto end = start + std::min<size_t>(section.Misc.VirtualSize, nt->OptionalHeader.SizeOfImage - section.VirtualAddress);
        while (start < end) {
            MEMORY_BASIC_INFORMATION memory{};
            if (!VirtualQuery(reinterpret_cast<void*>(start), &memory, sizeof(memory))) return 0;
            auto next = std::min(end, reinterpret_cast<uintptr_t>(memory.BaseAddress) + memory.RegionSize);
            if (next <= start) return 0;
            if (memory.State == MEM_COMMIT && !(memory.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
                auto cursor = reinterpret_cast<std::byte*>(start);
                const auto limit = reinterpret_cast<std::byte*>(next);
                while (cursor < limit) {
                    auto match = hat::find_pattern(cursor, limit, signature);
                    if (!match.has_result()) break;
                    if (found) return 0; // Never choose arbitrarily between two targets.
                    found = reinterpret_cast<uintptr_t>(match.get());
                    cursor = match.get() + 1;
                }
            }
            start = next;
        }
    }
    return found;
}
