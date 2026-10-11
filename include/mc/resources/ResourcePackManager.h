#pragma once
#include "mc/Import.h"
#include <gsl/span>
#include <string>
#include <string_view>
#include <utility>
#include "mc/deps/core/resource/ResourceLocation.h"
// Partial declaration for Minecraft GDK 26.51.01 / 26.52.03; no object layout is claimed.
class ResourcePackManager {
public:
    ResourcePackManager() = delete;
    // @signature("55 41 56 56 57 53 48 83 EC 30 48 8D 6C 24 30 48 C7 45 F8 FE FF FF FF 48 89 CE 48 8B 79 60 48 8B 1F 48 39 FB 74 2F 41 B6 01 0F 1F 80 00 00 00 00 48")
    MC void _composeFullStack();
    // @signature("55 41 56 56 57 53 48 83 EC 60 48 8D 6C 24 60 48 C7 45 F8 FE FF FF FF 48 89 CE 8B 05 ? ? ? ? 8B 0D ? ? ? ? 65 48 8B 14 25 58 00 00 00 48 8B 0C CA 3B 81 04 00 00 00")
    MC void _updateLanguageSubpacks();

    // @signature("55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC ? ? ? ? 48 8D AC 24 ? ? ? ? 0F 29 B5 ? ? ? ? 48 C7 85 ? ? ? ? FE FF FF FF 4D 89 CF 4C 89 C6 48 89 55 ? 48 8B 81 ? ? ? ? 48 89 45 ?")
    MC std::pair<int, std::string_view> getPackStackIndexOfResource(const ResourceLocation&, gsl::span<const std::string>) const;
};
static_assert(sizeof(gsl::span<const std::string>) == 16);
static_assert(sizeof(std::string) == 32);
