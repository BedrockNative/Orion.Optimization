#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace orion {
struct PackIdentity {
    uintptr_t pack, owner, settings;
    int32_t subpack;
    uint8_t revision;
    bool operator==(const PackIdentity&) const = default;
};
inline uint64_t mix(uint64_t h, uint64_t v) { return (h ^ (v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2))); }
struct QueryView { int filesystem; std::string_view path; std::span<const std::string> extensions; };
struct QueryKey {
    int filesystem;
    std::string path;
    std::vector<std::string> extensions;
    operator QueryView() const { return {filesystem, path, extensions}; }
};
struct QueryHash {
    using is_transparent = void;
    size_t operator()(QueryView q) const {
        uint64_t h = mix(static_cast<uint64_t>(q.filesystem), std::hash<std::string_view>{}(q.path));
        h = mix(h, q.extensions.size());
        for (const auto& extension : q.extensions) h = mix(h, std::hash<std::string_view>{}(extension));
        return static_cast<size_t>(h);
    }
};
struct QueryEqual {
    using is_transparent = void;
    bool operator()(QueryView a, QueryView b) const {
        return a.filesystem == b.filesystem && a.path == b.path && std::ranges::equal(a.extensions, b.extensions);
    }
};
struct CachedResult { int pack; size_t extension; size_t bytes; };
class QueryCache {
    uintptr_t manager_ = 0, stack_ = 0;
    uint64_t epoch_ = 0, stackHash_ = 0;
    std::vector<PackIdentity> packs_;
    std::unordered_map<QueryKey, CachedResult, QueryHash, QueryEqual> entries_;
    std::deque<const QueryKey*> order_; // unordered_map node addresses survive rehash.
    size_t bytes_ = 0;
public:
    static constexpr size_t noExtension = static_cast<size_t>(-1);
    static constexpr size_t maxEntries = 65536, maxBytes = 128 * 1024 * 1024;
    bool synchronize(uintptr_t manager, uintptr_t stack, uint64_t epoch, std::span<const PackIdentity> packs) {
        uint64_t hash = mix(mix(manager, stack), packs.size());
        for (const auto& p : packs) {
            hash = mix(hash, p.pack); hash = mix(hash, p.owner); hash = mix(hash, p.settings);
            hash = mix(hash, static_cast<uint32_t>(p.subpack)); hash = mix(hash, p.revision);
        }
        // Equality guards even a collision in the stack fingerprint.
        if (manager == manager_ && stack == stack_ && epoch == epoch_ && hash == stackHash_ && std::ranges::equal(packs_, packs)) return false;
        clear(); manager_ = manager; stack_ = stack; epoch_ = epoch; stackHash_ = hash;
        packs_.assign(packs.begin(), packs.end());
        return true;
    }
    void clear() { order_.clear(); entries_.clear(); bytes_ = 0; }
    size_t size() const { return entries_.size(); }
    std::optional<std::pair<int, std::string_view>> find(QueryView q) const {
        auto it = entries_.find(q);
        if (it == entries_.end()) return {};
        const auto& v = it->second;
        return std::pair<int, std::string_view>{v.pack, v.extension == noExtension ? std::string_view{} : std::string_view(q.extensions[v.extension])};
    }
    bool insert(QueryView q, std::pair<int, std::string_view> result) {
        if (result.first < 0) return false; // Dynamic resources can appear without a stack rebuild.
        size_t index = noExtension;
        if (!result.second.empty() || result.second.data() != nullptr) {
            for (size_t i = 0; i < q.extensions.size(); ++i) {
                if (std::string_view(q.extensions[i]) == result.second) { index = i; break; }
            }
            if (index == noExtension) return false; // Never retain an unknown borrowed string_view.
        }
        size_t bytes = q.path.size() + 1 + sizeof(QueryKey) + sizeof(CachedResult) + 64;
        for (const auto& e : q.extensions) bytes += sizeof(std::string) + e.size() + 1;
        if (bytes > maxBytes) return false;
        if (entries_.find(q) != entries_.end()) return false;
        while (!order_.empty() && (entries_.size() >= maxEntries || bytes_ + bytes > maxBytes)) {
            auto oldest = entries_.find(*order_.front());
            order_.pop_front();
            bytes_ -= oldest->second.bytes;
            entries_.erase(oldest);
        }
        auto [it, added] = entries_.emplace(QueryKey{q.filesystem, std::string(q.path), {q.extensions.begin(), q.extensions.end()}}, CachedResult{result.first,index,bytes});
        if (added) {
            try { order_.push_back(&it->first); }
            catch (...) { entries_.erase(it); throw; }
            bytes_ += bytes;
        }
        return added;
    }
};
}
