#include "amethyst/runtime/importing/data/pe32+/PECanonicalDataSymbol.hpp"
#include <amethyst/Memory.hpp>
#include <libhat/scanner.hpp>

namespace Amethyst::Importing::PE {
	std::string PECanonicalDataSymbol::GetKind() const {
		return "data";
	}

	std::string PECanonicalDataSymbol::ToString() const {
		std::stringstream fields;
		if (IsVirtualTable) {
			fields << "VirtualTable, ";
		}
		if (IsSignature) {
			fields << std::format("Signature[{} elements]", Signature.size());
		} else {
			fields << std::format("Address[{:x}]", Address);
		}
		return std::format("{} -> PECanonicalDataSymbol[{}]", CanonicalSymbol::ToString(), fields.str());
	}

	uintptr_t PECanonicalDataSymbol::Compute(const ResolutionContext& ctx) {
		if (ctx.ModuleHandle == nullptr) {
			Log::Error("Cannot resolve data symbol '{}' without a valid module handle", DisplayName());
			return 0x0;
		}

		if (IsSignature) {
			const auto result = FindUniqueSignature(Signature, false);
            if (!result) {
                Log::Error("Missing or ambiguous signature for '{}'", DisplayName());
                return 0;
            }
            return GetEffectiveAddress(result);
		}

		return SlideAddress(Address);
	}

	bool PECanonicalDataSymbol::Resolve(const ResolutionContext& ctx) {
		if (IsShadow) {
			return true;
		}

		if (TargetOffset == 0x0) {
			Log::Error("Data symbol '{}' has no target offset to write to", DisplayName());
			return false;
		}

		uintptr_t base = reinterpret_cast<uintptr_t>(ctx.ModuleHandle);
		uintptr_t computedAddress = Compute(ctx);
		if (computedAddress == 0x0) {
			Log::Error("Failed to compute address for data symbol '{}'", DisplayName());
			return false;
		}

		if (HasStorage) {
			if (StorageOffset == 0x0) {
				Log::Error("Data symbol '{}' has storage enabled but no storage offset", DisplayName());
				return false;
			}

			// First, write the computed address to the storage
			uintptr_t* storage = reinterpret_cast<uintptr_t*>(base + StorageOffset);
			InterlockedExchange(storage, computedAddress);

			// Now, write the address of the storage to the target
			computedAddress = reinterpret_cast<uintptr_t>(storage);
		}

		DWORD oldProtection;
		UnprotectMemory(base + TargetOffset, sizeof(void*), &oldProtection);
		uintptr_t* target = reinterpret_cast<uintptr_t*>(base + TargetOffset);
		InterlockedExchange(target, computedAddress);
		ProtectMemory(base + TargetOffset, sizeof(void*), oldProtection, nullptr);
		return true;
	}
}
