#pragma once
#include "mc/Import.h"
#include <memory>
class ResourcePackManager;
class ParsedAtlasNode;
namespace Json { class Value; }
namespace mce { class TextureGroup; class Color; }
// Partial declaration, not an allocation-compatible replacement for the game class.
class TextureAtlas {
public:
    TextureAtlas() = delete;
    // @signature("55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC ? ? ? ? 48 8D AC 24 ? ? ? ? 0F 29 BD ? ? ? ? 0F 29 B5 ? ? ? ? 48 C7 85 ? ? ? ? FE FF FF FF 4D 89 CA 48 89 D3 48 89 CF 0F B6 8D ? ? ? ?")
    MC void _addElementCollection(ResourcePackManager&, std::shared_ptr<mce::TextureGroup>, const Json::Value&, ParsedAtlasNode&, int, float, const mce::Color&, bool);
};
