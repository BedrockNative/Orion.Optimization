#include <safetyhook.hpp>
#include <gsl/span>
#include <string>
#include <string_view>
#include <memory>
#include <bit>
#include <cstdio>
using Result=std::pair<int,std::string_view>;
using Span=gsl::span<const std::string>;
struct Location { int id; };
struct Manager {
    int id;
    __declspec(noinline) Result lookup(const Location& location, Span ext) const { return {id+location.id,ext[1]}; }
};
using Raw = Result* (*)(const Manager*,Result*,const Location*,const Span*);
Raw original;
unsigned calls;
Result* detour(const Manager* self,Result* out,const Location* location,const Span* ext) {
    ++calls;
    return original(self,out,location,ext);
}
struct Atlas {
    float observed = 0;
    __declspec(noinline) void add(Manager& m, std::shared_ptr<int> group, const Location& loc, int& output, int index, float fade, const float& color, bool additive) {
        observed = fade + color; output = m.id + *group + loc.id + index + (additive ? 1 : 0);
    }
};
using AtlasRaw = void (*)(Atlas*, Manager*, const std::shared_ptr<int>*, const Location*, int*, int, float, const float*, bool);
AtlasRaw originalAtlas;
unsigned atlasCalls;
void atlasDetour(Atlas* self,Manager* manager,const std::shared_ptr<int>* group,const Location* loc,int* output,int index,float fade,const float* color,bool additive) {
    ++atlasCalls;
    originalAtlas(self,manager,group,loc,output,index,fade,color,additive);
}
int main() {
    Manager m{10};Location loc{8};std::string ext[]={".png",".tga"};
    auto method=&Manager::lookup;
    static_assert(sizeof(method)==sizeof(void*));
    auto hook=safetyhook::InlineHook::create(std::bit_cast<void*>(method),reinterpret_cast<void*>(&detour),safetyhook::InlineHook::StartDisabled);
    if(!hook) return 2;
    original=hook->original<Raw>();
    if(!hook->enable())return 3;
    auto result=m.lookup(loc,ext);
    bool ok=calls==1 && result.first==18 && result.second==".tga" && result.second.data()==ext[1].data();
    std::printf("member ABI hook: calls=%u result=%d view=%.*s %s\n",calls,result.first,(int)result.second.size(),result.second.data(),ok?"PASS":"FAIL");
    Atlas atlas; auto group=std::make_shared<int>(3); int output=0; float color=0.5f;
    auto a=safetyhook::InlineHook::create(std::bit_cast<void*>(&Atlas::add),reinterpret_cast<void*>(&atlasDetour),safetyhook::InlineHook::StartDisabled);
    if(!a) return 4;
    originalAtlas=a->original<AtlasRaw>(); if(!a->enable())return 5;
    atlas.add(m,group,loc,output,2,0.25f,color,true);
    bool atlasOk=atlasCalls==1 && output==24 && atlas.observed==0.75f && group.use_count()==1;
    std::printf("atlas ABI hook: calls=%u result=%d shared_owners=%ld %s\n",atlasCalls,output,group.use_count(),atlasOk?"PASS":"FAIL");
    return ok && atlasOk?0:1;
}
