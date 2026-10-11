#include <amethyst/Memory.hpp>
#include <orion/Optimization.hpp>
#include <safetyhook.hpp>
#include <array>
#include <atomic>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <orion/GameHelpers.hpp>

namespace orion {
namespace {
// Borrowed game ABI, 26.51.01 and 26.52.03. Elements are constructed, cloned and freed
// exclusively by the game's functions; never memcpy owning strings or vectors.
struct Parsed { std::byte* begin{}; std::byte* end{}; std::byte* capacity{}; std::array<std::byte,24> tail{}; };
static_assert(sizeof(Parsed)==48);
using Group = std::shared_ptr<void>;
using Parse = void (*)(Group*, const void*, Parsed*, float, const void*, bool);
using Copy = void* (*)(void*, const void*);
using Grow = void* (*)(void*, void*, const void*);
using Destroy = void (*)(void*);
using Free = void (*)(void*, size_t);
Parse original; Copy copyElement; Grow grow; Destroy destroyElement; Free freeGame;
SafetyHookInline hook;
std::atomic<uint64_t> parsedHits{}, parsedMisses{};
constexpr size_t stride=0x130;
void tidy(Parsed& p) {
    if(!p.begin) return;
    for(auto q=p.begin;q!=p.end;q+=stride) destroyElement(q);
    size_t bytes=static_cast<size_t>(p.capacity-p.begin); void* allocation=p.begin;
    if(bytes>=4096) { allocation=reinterpret_cast<void**>(p.begin)[-1]; bytes+=39; }
    freeGame(allocation,bytes); p={};
}
struct Owned { Parsed data; ~Owned(){tidy(data);} };
void clone(Parsed& dst, const Parsed& src) {
    for(auto q=src.begin;q!=src.end;q+=stride) {
        if(dst.end==dst.capacity) grow(&dst,dst.end,q);
        else { copyElement(dst.end,q); dst.end+=stride; }
    }
    dst.tail=src.tail;
}
struct Entry { Owned value; Group group; };
struct Cache {
    uint64_t epoch{}; uintptr_t manager{};
    std::unordered_map<std::string,std::unique_ptr<Entry>> entries;
    std::deque<std::string> order;
    void clear(){ entries.clear(); order.clear(); }
};
thread_local Cache cache;
thread_local bool busy=false;
void parse(Group* group, const void* json, Parsed* out, float fade, const void* color, bool additive) {
    const auto context=atlasCacheContext();
    // First prototype caches the common JSON string form only. Complex JSON and
    // non-atlas callers retain their original behavior. Content changes invalidate; identical recompositions preserve results.
    if(!context.first || busy || out->begin || static_cast<const unsigned char*>(json)[8]!=4) {
        original(group,json,out,fade,color,additive); return;
    }
    auto holder=*static_cast<const void* const*>(json);
    auto path=holder ? *static_cast<const char* const*>(holder) : nullptr;
    if(!path || strnlen(path,4097)>4096) { original(group,json,out,fade,color,additive); return; }
    struct Guard { Guard(){busy=true;} ~Guard(){busy=false;} } guard;
    std::string key;
    try {
        if(cache.epoch!=context.second || cache.manager!=context.first) {
            cache.clear(); cache.epoch=context.second; cache.manager=context.first;
        }
        key=path;
        key.push_back('\0');
        const void* ptr=group->get(); key.append(reinterpret_cast<const char*>(&ptr),sizeof(ptr));
        key.append(reinterpret_cast<const char*>(&fade),sizeof(fade));
        key.append(static_cast<const char*>(color),16); key.push_back(additive);
        if(auto it=cache.entries.find(key);it!=cache.entries.end()) {
            Owned result; clone(result.data,it->second->value.data);
            *out=result.data; result.data={};
            // Static function takes shared_ptr by value: callee destroys this ABI temporary.
            group->~Group(); parsedHits.fetch_add(1,std::memory_order_relaxed); return;
        }
    } catch(const std::bad_alloc&) { original(group,json,out,fade,color,additive); return; }
    Group owner=*group;
    original(group,json,out,fade,color,additive);
    parsedMisses.fetch_add(1,std::memory_order_relaxed);
    const auto bytes=reinterpret_cast<uintptr_t>(out->end)-reinterpret_cast<uintptr_t>(out->begin);
    if(!bytes || bytes%stride || bytes/stride>8 || context!=atlasCacheContext()) return;
    try {
        auto entry=std::make_unique<Entry>(); clone(entry->value.data,*out); entry->group=std::move(owner);
        while(cache.entries.size()>=32768) {cache.entries.erase(cache.order.front());cache.order.pop_front();}
        cache.order.push_back(key);
        try {cache.entries.emplace(key,std::move(entry));} catch(...) {cache.order.pop_back();throw;}
    } catch(const std::bad_alloc&) { /* Cache failure does not change a successful game parse. */ }
}

}
std::pair<uint64_t,uint64_t> parsedCacheStats(){return {parsedHits.load(),parsedMisses.load()};}
void installParsedCache() {
    // Build gate plus unique helper signatures; fail closed if resolution fails.
    wchar_t disable[2]{}; GetEnvironmentVariableW(L"ORION_OPTIMIZATION_PARSED_CACHE",disable,2);
    if(disable[0]==L'0') return;
    try {
        if(!gameHelpersReady())return;
        const auto& hgame=gameHelpers();
        auto target=reinterpret_cast<Parse>(hgame.parse);
        copyElement=reinterpret_cast<Copy>(hgame.copy); grow=reinterpret_cast<Grow>(hgame.grow);
        destroyElement=reinterpret_cast<Destroy>(hgame.destroy); freeGame=reinterpret_cast<Free>(hgame.free);
        auto h=SafetyHookInline::create(reinterpret_cast<void*>(target),reinterpret_cast<void*>(&parse),SafetyHookInline::StartDisabled);
        if(!h) throw std::runtime_error("Parser hook creation failed");
        original=h->original<Parse>();
        if(!h->enable()) throw std::runtime_error("Parser hook activation failed");
        hook=std::move(*h); OutputDebugStringA("[Orion.Optimization] Experimental parsed texture cache enabled\n");
    } catch(const std::exception& e) {Log::Error("Parsed texture cache skipped: {}",e.what());}
}
}
