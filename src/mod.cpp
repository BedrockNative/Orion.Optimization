#include <amethyst/runtime/importing/pe32+/PEImporter.hpp>
#include <amethyst/Memory.hpp>
#include <orion/Optimization.hpp>
#include <orion/SupportedBuilds.hpp>
#include <mutex>
#include <cstdio>
namespace {
HMODULE self;
std::once_flag resolveOnce, initializeOnce;
std::unique_ptr<Amethyst::Importing::PE::PEImporter> importer;
bool supportedBuild() {
    const auto base = GetMinecraftBaseAddress();
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    const auto entries = reinterpret_cast<const IMAGE_DEBUG_DIRECTORY*>(base + directory.VirtualAddress);
    for(size_t i=0; i<directory.Size/sizeof(*entries); ++i) {
        if(entries[i].Type != IMAGE_DEBUG_TYPE_CODEVIEW || entries[i].SizeOfData < 24) continue;
        const auto codeview = reinterpret_cast<const uint8_t*>(base + entries[i].AddressOfRawData);
        if(std::memcmp(codeview,"RSDS",4)) continue;
        for(const auto& id : orion::supportedBuilds) if(!std::memcmp(codeview+4,id.data(),id.size())) return true;
    }
    return false;
}
}
extern "C" __declspec(dllexport) bool OrionOptimizationResolveImports() noexcept {
    try {
        std::call_once(resolveOnce, [] {
            importer = Amethyst::Importing::PE::PEImporter::Create(self);
            if (!importer) throw std::runtime_error("Runtime-Importer post-processing is missing");
            importer->ResolveAll();
        });
        return true;
    } catch (const std::exception& e) { Log::Error("Import resolution failed: {}", e.what()); return false; }
}
extern "C" __declspec(dllexport) bool OrionOptimizationInitialize() noexcept {
    try {
        std::call_once(initializeOnce, [] {
            if(!supportedBuild()) throw std::runtime_error("Unsupported game build: verified layouts required");
            if(!OrionOptimizationResolveImports()) throw std::runtime_error("Imports unresolved");
            // Hook callbacks must never outlive the DLL. There is deliberately no hot unload.
            HMODULE pinned;
            if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&OrionOptimizationInitialize),&pinned)) throw std::runtime_error("Cannot pin optimization module");
            orion::installOptimization();
        });
        return true;
    } catch (const std::exception& e) { Log::Error("Initialization failed: {}", e.what()); return false; }
}
extern "C" __declspec(dllexport) bool OrionOptimizationGetStats(orion::CacheStats* out) noexcept {
    if(!out) return false;
    *out = orion::cacheStats(); return true;
}
extern "C" __declspec(dllexport) void OrionOptimizationSetEnabled(bool enabled) noexcept { orion::setCacheEnabled(enabled); }
namespace {
DWORD WINAPI autoStart(void*) {
    const bool initialized = OrionOptimizationInitialize();
    wchar_t path[32768];
    const DWORD length = GetEnvironmentVariableW(L"ORION_OPTIMIZATION_LOG", path, static_cast<DWORD>(std::size(path)));
    FILE* log = nullptr;
    if(length && length < std::size(path)) _wfopen_s(&log, path, L"a");
    if(log) { std::fprintf(log,"pid=%lu initialized=%d\n",GetCurrentProcessId(),initialized); std::fflush(log); }
    if(!initialized) { if(log) std::fclose(log); return 1; }
    // Optional low-volume counters for A/B runs; no logging/allocation in hook hot paths.
    if(log) {
        for(;;) {
            const auto stats = orion::cacheStats();
            std::fprintf(log,"tick=%llu hits=%llu misses=%llu bypasses=%llu invalidations=%llu\n",
                GetTickCount64(),stats.hits,stats.misses,stats.bypasses,stats.invalidations);
            const auto watches=orion::packWatchStats();
            std::fprintf(log,"watched_pack_roots=%llu content_generation=%llu\n",watches.first,watches.second);
            const auto parsed = orion::parsedCacheStats();
            std::fprintf(log,"parsed_hits=%llu parsed_misses=%llu\n",parsed.first,parsed.second);
            std::fflush(log);
            Sleep(5000);
        }
    }
    return 0;
}
}
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        self = module;
        wchar_t setting[2] = {};
        GetEnvironmentVariableW(L"ORION_OPTIMIZATION_AUTOSTART",setting,2);
        if(setting[0] != L'0') {
            // The worker cannot execute until DLL notifications/loader lock finish.
            // Never wait for it here. Import-table loading therefore needs no custom loader.
            if(HANDLE thread = CreateThread(nullptr,0,autoStart,nullptr,0,nullptr)) CloseHandle(thread);
        }
    }
    return TRUE;
}
