#include <amethyst/Memory.hpp>
#include <orion/Optimization.hpp>
#include <orion/QueryCache.hpp>
#include <orion/PackChanges.hpp>
#include <orion/GameHelpers.hpp>
#include <mc/resources/ResourcePackManager.h>
#include <mc/client/renderer/texture/TextureAtlas.h>
#include <safetyhook.hpp>
#include <array>
#include <atomic>
#include <bit>
#include <cstdio>

namespace orion {
namespace {
using Result = std::pair<int, std::string_view>;
using Extensions = gsl::span<const std::string>;
// Raw Windows x64 member ABI: RCX=this, RDX=sret, R8=location, R9=&span.
using QueryFn = Result* (*)(const ResourcePackManager*, Result*, const ResourceLocation*, const Extensions*);
using AtlasFn = void (*)(TextureAtlas*, ResourcePackManager*, const std::shared_ptr<mce::TextureGroup>*, const Json::Value*, ParsedAtlasNode*, int, float, const mce::Color*, bool);
using MutateFn = void (*)(ResourcePackManager*);
SafetyHookInline queryHook, atlasHook, composeHook, languageHook;
QueryFn originalQuery;
AtlasFn originalAtlas;
MutateFn originalCompose, originalLanguage;
std::atomic<uint64_t> epoch{1}, hits{}, misses{}, bypasses{}, invalidations{};
std::atomic<unsigned> mutations{};
std::atomic<uint64_t> forcedContentEpoch{1};
thread_local unsigned composeDepth=0;
std::atomic<bool> enabled{true};
thread_local ResourcePackManager* activeManager = nullptr;
thread_local bool inQuery = false;
thread_local QueryCache cache;

// Validated against the 26.51.01 and 26.52.03 machine code and matching headers.
// These are reads of borrowed game objects, never construction/deletion of game-owned data.
template<class T> T read(const void* object, size_t offset) {
    T value; std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(T)); return value;
}
bool snapshot(const ResourcePackManager* manager, uintptr_t& stack, std::array<PackIdentity,256>& packs, size_t& count) {
    stack = read<uintptr_t>(manager, 0xb8); // mFullStack
    if (!stack) return false;
    auto begin = read<uintptr_t>(reinterpret_cast<void*>(stack), 8); // mStack vector, after vptr
    auto end = read<uintptr_t>(reinterpret_cast<void*>(stack), 16);
    if (end < begin || (end - begin) % 0x128 || (end - begin) / 0x128 > packs.size()) return false;
    count = (end - begin) / 0x128;
    for (size_t i = 0; i < count; ++i) {
        auto p = reinterpret_cast<const void*>(begin + i * 0x128);
        packs[i] = {read<uintptr_t>(p,0x100), read<uintptr_t>(p,0x108), read<uintptr_t>(p,0xf8), read<int32_t>(p,0x120), read<uint8_t>(p,0x124)};
    }
    return true;
}
struct QueryGuard { QueryGuard() { inQuery = true; } ~QueryGuard() { inQuery = false; } };
Result* query(const ResourcePackManager* manager, Result* out, const ResourceLocation* location, const Extensions* extensions) {
    if (!enabled.load(std::memory_order_relaxed) || activeManager != manager || inQuery || mutations.load(std::memory_order_acquire)) {
        bypasses.fetch_add(1, std::memory_order_relaxed); return originalQuery(manager, out, location, extensions);
    }
    QueryGuard guard;
    bool called = false, originalStarted = false;
    try {
        const auto context=atlasCacheContext();
        const uint64_t generation = context.second;
        if(!context.first) {bypasses.fetch_add(1,std::memory_order_relaxed);originalStarted=true;return originalQuery(manager,out,location,extensions);}
        std::array<PackIdentity,256> packs;
        uintptr_t stack; size_t count;
        if (!snapshot(manager, stack, packs, count) || extensions->size() > 64) {
            bypasses.fetch_add(1, std::memory_order_relaxed); originalStarted = true; return originalQuery(manager, out, location, extensions);
        }
        if(contentMonitored())for(size_t i=0;i<count;++i)packs[i].revision=0;
        if (cache.synchronize(reinterpret_cast<uintptr_t>(manager), 0, generation, {packs.data(),count})) invalidations.fetch_add(1, std::memory_order_relaxed);
        const auto& path = *reinterpret_cast<const std::string*>(reinterpret_cast<const std::byte*>(location) + 8);
        const QueryView key{read<int>(location,0), path, {extensions->data(),extensions->size()}};
        if (auto value = cache.find(key); value && context == atlasCacheContext() && !mutations.load(std::memory_order_acquire)) {
            hits.fetch_add(1, std::memory_order_relaxed); *out = *value; return out;
        }
        misses.fetch_add(1, std::memory_order_relaxed);
        originalStarted = true; originalQuery(manager, out, location, extensions); called = true;
        if (context == atlasCacheContext() && !mutations.load(std::memory_order_acquire)) cache.insert(key, *out);
        return out;
    } catch (const std::bad_alloc&) {
        // The optimization may fail without changing the game's lookup result.
        if (originalStarted && !called) throw;
        cache.clear();
        if (called) return out;
    }
    bypasses.fetch_add(1, std::memory_order_relaxed); return originalQuery(manager, out, location, extensions);
}
void atlas(TextureAtlas* self, ResourcePackManager* manager, const std::shared_ptr<mce::TextureGroup>* group, const Json::Value* texture, ParsedAtlasNode* node, int index, float fade, const mce::Color* color, bool additive) {
    struct Scope { ResourcePackManager* old; ~Scope(){activeManager=old;} } scope{activeManager};
    activeManager = manager;
    originalAtlas(self, manager, group, texture, node, index, fade, color, additive);
}
struct Mutation {
    Mutation(){ mutations.fetch_add(1,std::memory_order_acq_rel); epoch.fetch_add(1,std::memory_order_acq_rel); }
    ~Mutation(){ epoch.fetch_add(1,std::memory_order_release); mutations.fetch_sub(1,std::memory_order_release); }
};
void compose(ResourcePackManager* self) {
    struct Depth { Depth(){++composeDepth;} ~Depth(){--composeDepth;} } depth;
    Mutation change; originalCompose(self);
}
void language(ResourcePackManager* self) { Mutation change; originalLanguage(self); if(!composeDepth)forcedContentEpoch.fetch_add(1); }
template<class T> void* methodAddress(T member) {
    static_assert(sizeof(member) == sizeof(uintptr_t));
    // clang-cl emits an import thunk for a PMF; follow its RIP-relative IAT jump.
    return reinterpret_cast<void*>(GetEffectiveAddress(std::bit_cast<uintptr_t>(member)));
}
SafetyHookInline create(void* target, void* detour) {
    auto hook = SafetyHookInline::create(target, detour, SafetyHookInline::StartDisabled);
    if (!hook) throw std::runtime_error("SafetyHook could not create a trampoline");
    return std::move(*hook);
}
}
namespace {
struct PersistentState {
    ResourcePackManager* manager{};
    uint64_t observedEpoch{}, observedForced{}, generation{1}, nextPoll{};
    std::vector<PackIdentity> packs;
    std::vector<std::shared_ptr<void>> owners;
    PackChanges changes;
};
thread_local PersistentState persistent;
std::atomic<uint64_t> monitoredRoots{}, contentGeneration{};
std::wstring packDirectory(uintptr_t resourcePack) {
    // ResourcePack::mImpl -> Impl::mPack -> Pack::mAccessStrategy.
    // Validated against ResourcePack::getIconPath's 26.51 machine code.
    auto impl=read<void*>(reinterpret_cast<void*>(resourcePack),0);
    if(!impl)return {};
    auto pack=read<void*>(impl,8); if(!pack)return {};
    auto strategy=read<void*>(pack,8); if(!strategy)return {};
    auto vtable=read<void**>(strategy,0);
    using Location=const void* (*)(const void*);
    const auto location=reinterpret_cast<Location>(vtable[2])(strategy);
    if(!location)return {};
    // Use the game's resolver: AppPackage paths are relative to data/, not cwd.
    // Verified 26.51/26.52 ABI; functions resolved through unique signatures.
    struct GameString {
        alignas(8) std::byte storage[32]{};
        ~GameString(){
            auto capacity=read<size_t>(storage,24);
            if(capacity<16)return;
            auto data=read<char*>(storage,0);size_t bytes=capacity+1;
            if(bytes>=4096){data=reinterpret_cast<char**>(data)[-1];bytes+=39;}
            reinterpret_cast<void(*)(void*,size_t)>(gameHelpers().free)(data,bytes);
        }
    } resolved;
    using FullPath=void* (*)(const void*,void*);
    reinterpret_cast<FullPath>(gameHelpers().fullPath)(location,resolved.storage);
    const auto& path=*reinterpret_cast<const std::string*>(resolved.storage);
    if(path.empty() || path.size()>32760)return {};
    int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.data(),static_cast<int>(path.size()),nullptr,0);
    if(!size)return {};
    std::wstring wide(size,L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.data(),static_cast<int>(path.size()),wide.data(),size);
    wchar_t absolute[32768];
    DWORD length=GetFullPathNameW(wide.c_str(),32768,absolute,nullptr);
    if(!length || length>=32768)return {};
    wide.assign(absolute,length);
    auto attributes=GetFileAttributesW(wide.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_REPARSE_POINT))return {};
    if(!(attributes&FILE_ATTRIBUTE_DIRECTORY)) {
        auto slash=wide.find_last_of(L"\\/");
        if(slash==std::wstring::npos)return {};
        wide.resize(slash+1); // Packed archive: watch its parent, including replacement/rename.
    }
    return wide;
}
}
std::pair<uintptr_t,uint64_t> atlasCacheContext() {
    if(!activeManager || !enabled.load(std::memory_order_relaxed) || mutations.load(std::memory_order_acquire)) return {};
    if(!gameHelpersReady())return {reinterpret_cast<uintptr_t>(activeManager),epoch.load(std::memory_order_acquire)};
    auto& state=persistent;
    const auto current=epoch.load(std::memory_order_acquire);
    const auto forced=forcedContentEpoch.load(std::memory_order_acquire);
    try {
        if(state.manager!=activeManager || state.observedEpoch!=current) {
            std::array<PackIdentity,256> ids;uintptr_t stack;size_t count;
            if(!snapshot(activeManager,stack,ids,count))return {};
            std::vector<std::wstring> roots;
            std::vector<std::shared_ptr<void>> owners;
            bool monitorable=true;
            for(size_t i=0;i<count;++i) {
                auto path=packDirectory(ids[i].pack);
                if(path.empty()){monitorable=false;break;}
                roots.push_back(std::move(path));
                // This counter advances on every world reload, not only file edits.
                ids[i].revision=0;
                // Retain pack ownership so pointer reuse cannot masquerade as the same pack.
                std::array<uintptr_t,2> pair{ids[i].pack,ids[i].owner};
                owners.push_back(*reinterpret_cast<const std::shared_ptr<void>*>(pair.data()));
            }
            std::sort(roots.begin(),roots.end());roots.erase(std::unique(roots.begin(),roots.end()),roots.end());
            const bool same=state.manager==activeManager && std::ranges::equal(state.packs,std::span(ids.data(),count));
            const bool changed=state.changes.changed();
            const bool hadWatch=state.changes.healthy();
            const bool sameRoots=state.changes.matches(roots);
            const bool watching=monitorable && state.changes.configure(roots);
            if(!same || !sameRoots || changed || !hadWatch || !watching || state.observedForced!=forced) {
                ++state.generation;
            }
            state.observedForced=forced;
            if(!watching)state.changes.clear();
            state.packs.assign(ids.begin(),ids.begin()+count);state.owners=std::move(owners);
            state.manager=activeManager;state.observedEpoch=current;state.nextPoll=0;
            monitoredRoots.store(watching?roots.size():0);
        }
        const auto now=GetTickCount64();
        if(now>=state.nextPoll) {
            if(state.changes.healthy() && state.changes.changed())++state.generation;
            state.nextPoll=now+16;
        }
        if(current!=epoch.load(std::memory_order_acquire) || mutations.load(std::memory_order_acquire))return {};
        contentGeneration.store(state.generation,std::memory_order_relaxed);
        return {reinterpret_cast<uintptr_t>(activeManager),state.changes.healthy()?state.generation:(current | (1ull<<63))};
    } catch(const std::bad_alloc&) {return {};}
}
bool contentMonitored(){return persistent.manager==activeManager && persistent.changes.healthy();}
std::pair<uint64_t,uint64_t> packWatchStats(){return {monitoredRoots.load(),contentGeneration.load()};}
CacheStats cacheStats() { return {hits.load(), misses.load(), bypasses.load(), invalidations.load()}; }
void setCacheEnabled(bool value) { enabled.store(value); forcedContentEpoch.fetch_add(1); epoch.fetch_add(1); }
void installOptimization() {
    if (queryHook) return;
    try {initializeGameHelpers();} catch(const std::exception& e){Log::Error("Persistent cache helpers unavailable: {}",e.what());}
    auto q = create(methodAddress(&ResourcePackManager::getPackStackIndexOfResource), reinterpret_cast<void*>(&query));
    auto a = create(methodAddress(&TextureAtlas::_addElementCollection), reinterpret_cast<void*>(&atlas));
    auto c = create(methodAddress(&ResourcePackManager::_composeFullStack), reinterpret_cast<void*>(&compose));
    auto l = create(methodAddress(&ResourcePackManager::_updateLanguageSubpacks), reinterpret_cast<void*>(&language));
    originalQuery = q.original<QueryFn>(); originalAtlas = a.original<AtlasFn>();
    originalCompose = c.original<MutateFn>(); originalLanguage = l.original<MutateFn>();
    // Rollback is automatic if any enable operation fails. Publish trampolines before activation.
    if (!c.enable() || !l.enable() || !q.enable() || !a.enable()) throw std::runtime_error("SafetyHook could not enable optimization");
    queryHook = std::move(q); atlasHook = std::move(a); composeHook = std::move(c); languageHook = std::move(l);
    installParsedCache();
}
}
