#include <orion/QueryCache.hpp>
#include <cstdio>
#include <stdexcept>
static void check(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
int main() {
 try {
    orion::QueryCache c;
    std::vector<orion::PackIdentity> packs{{1,2,3,4,5},{6,7,8,9,10}};
    std::vector<std::string> extensions{".png", ".tga"};
    orion::QueryView q{0,"textures/test",extensions};
    check(c.synchronize(1,2,1,packs),"initial stack");
    check(c.insert(q,{18,extensions[1]}),"insert");
    check(!c.synchronize(1,2,1,packs),"stable stack retained");
    auto copied=extensions; q.extensions=copied;
    auto hit=c.find(q);
    check(hit && hit->first==18 && hit->second.data()==copied[1].data(),"result must borrow current caller strings");
    check(!c.find({1,q.path,copied}),"filesystem separates keys");
    std::swap(copied[0],copied[1]);
    check(!c.find(q),"ordered extensions separate keys");
    std::swap(copied[0],copied[1]);
    check(!c.insert({0,"missing",extensions},{-1,{}}),"negative result bypass");
    check(!c.insert({0,"foreign",extensions},{1,"not an extension"}),"unowned view bypass");
    ++packs[0].revision;
    check(c.synchronize(1,2,1,packs) && !c.find(q),"revision invalidation");
    c.insert(q,{18,copied[1]});
    std::swap(packs[0],packs[1]);
    check(c.synchronize(1,2,1,packs) && !c.find(q),"pack order invalidation");
    c.insert(q,{18,copied[1]});
    check(c.synchronize(1,2,2,packs) && !c.find(q),"explicit generation invalidation");
    for (size_t i=0;i<=orion::QueryCache::maxEntries;++i) c.insert({0,std::to_string(i),extensions},{1,extensions[0]});
    check(c.size()==orion::QueryCache::maxEntries,"entry bound");
    check(!c.find({0,"0",extensions}),"FIFO removes oldest");
    check(c.find({0,"1",extensions}).has_value(),"FIFO does not flush whole cache");
    check(c.find({0,std::to_string(orion::QueryCache::maxEntries),extensions}).has_value(),"FIFO retains newest");
    c.clear();
    std::string large(8192,'a');
    const int inserts=static_cast<int>(orion::QueryCache::maxBytes/large.size())+100;
    for(int i=0;i<inserts;++i) c.insert({0,large+std::to_string(i),extensions},{1,extensions[0]});
    check(c.size()<static_cast<size_t>(inserts) && c.size()>1,"byte budget rotates entries");
    check(!c.find({0,large+"0",extensions}) && c.find({0,large+std::to_string(inserts-1),extensions}),"byte budget retains recent entries");
    std::puts("PASS: current-string lifetime, key identity, invalidation, misses, FIFO entry/byte bounds");
    return 0;
 } catch (const std::exception& e) { std::printf("FAIL: %s\n",e.what()); return 1; }
}
