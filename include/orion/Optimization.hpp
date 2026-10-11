#pragma once
#include <cstdint>
#include <utility>
namespace orion {
struct CacheStats { uint64_t hits, misses, bypasses, invalidations; };
void installOptimization();
void installParsedCache();
std::pair<uint64_t,uint64_t> packWatchStats();
bool contentMonitored();
std::pair<uintptr_t,uint64_t> atlasCacheContext();
std::pair<uint64_t,uint64_t> parsedCacheStats();
CacheStats cacheStats();
void setCacheEnabled(bool enabled);
}
