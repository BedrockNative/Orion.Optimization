Orion.Optimization
==================
Windows x64 optimization DLL, built with CMake and clang-cl (Release /O2 /Ob3 /clang:-O3).
First optimization: bounded FIFO cache for successful pack-stack resource queries
made from TextureAtlas::_addElementCollection. Autostart defaults to enabled: an import-table load schedules a worker which calls
OrionOptimizationInitialize() after loader notifications finish. Set
ORION_OPTIMIZATION_AUTOSTART=0 for manual initialization or isolated tests.
The build itself does not inject into a game. ORION_OPTIMIZATION_LOG can name a
Windows path for initialization status and counters every five seconds.
65536 entries / approximately 128 MiB of accounted key storage per thread, plus
container/allocator overhead; oldest entries are evicted incrementally.
Cache hits do not refresh FIFO order (this is FIFO, not LRU).
Keys include filesystem, exact path and ordered extension list. The stack fingerprint
includes manager identity and ordered pack, owner, settings, subpack and revision;
full identity equality also guards hash collisions. On verified 26.51.01 and 26.52.03, an identical
recomposition preserves the generation while pack-root notifications invalidate it.
Unmonitorable roots retain conservative per-recomposition behavior. Unknown builds are rejected.
Cache state is thread-local. Pack references are retained to prevent identity reuse.
See the change-monitor details below; this does not trigger game-side hot reloads.
Negative results are not cached. Returned string_views borrow the current caller's
extension strings, never a previous call's storage.
Activation is gated by the CodeView GUID+age of the two verified game binaries.
The DLL pins itself while hooks are installed and does not support hot unloading.
Exports: OrionOptimizationSetEnabled(bool) for A/B, OrionOptimizationGetStats(CacheStats*)
for aggregate hits/misses/bypasses/invalidations, OrionOptimizationResolveImports()
for isolated resolver tests without installing game hooks.

Source dependencies are pinned Git submodules:
  external/libhat       signature scanning
  external/SafetyHook   hooks
  external/GSL          Microsoft gsl::span
  external/zydis        SafetyHook decoder dependency; nested Zycore submodule

Initialize a checkout with:
  git submodule update --init --recursive

Host requirements:
  CMake >= 3.28, clang-cl, lld-link, make (Linux) or Ninja (Windows),
  Python 3 with matching clang Python bindings/libclang, .NET 8 runtime.
  Windows SDK + MSVC CRT/STL headers/libraries (xwin splat on Linux).
  On Windows use an x64 Visual Studio developer environment, including INCLUDE.

Linux build:
  export XWIN_ROOT=/path/to/xwin
  cmake --preset linux-clang-cl-release
  cmake --build --preset linux-clang-cl-release --parallel

Windows build (provided preset; not tested on Windows yet):
  cmake --preset windows-clang-cl-release
  cmake --build --preset windows-clang-cl-release --parallel

Output:
  build/release/bin/Orion.Optimization.dll
  build/release/bin/Orion.Optimization.pdb

Import pipeline:
  1. tools/generate_imports.py uses native libclang with Windows/MSVC headers
     to extract mangled names and // @signature("...") from include/mc.
     It writes Runtime-Importer metadata and an import library via LLVM's librarian.
     This avoids the upstream SymbolGenerator's dependency on Windows VS discovery.
  2. clang-cl links the DLL against the generated Minecraft.Windows.lib.
  3. CMake downloads Runtime-Importer release manual-38, verifies its pinned
     SHA-256, and runs the official Amethyst.ModuleTweaker.dll with dotnet.
     The post-build verifier requires the resulting .rtih and .rtis sections.
  4. By default a startup worker calls OrionOptimizationInitialize() outside
     DllMain/loader lock. A manual loader can call the same export with autostart
     disabled. Failure returns false.
     The copied resolver scans the main executable with libhat and patches imports.
     DllMain stores the module handle and schedules the worker without waiting;
     it does not scan or install hooks itself.

Amethyst is NOT a dependency/submodule. Only import-resolution sources and the
binary reader were copied to vendor/amethyst; local fixes/provenance are recorded
in vendor/amethyst/ORIGIN.txt, with the original Apache-2.0 license.

Game declarations:
  mc/resources/ResourcePackManager.h
  mc/deps/core/resource/ResourceLocation.h
  mc/client/renderer/texture/TextureAtlas.h
They are partial/opaque declarations, not constructible replacement game objects.
No LeviLamina headers or library are included. Forward declarations preserve exact
class/namespace names; no guessed member offsets are exposed.
The lookup/atlas signatures wildcard stack sizes/displacements and were checked for unique
executable-section matches at unwind entries in Minecraft GDK 26.51.01 and 26.52.03.
These offline checks verify resolution. Gameplay performance has not been measured yet.
Run tools/check_signatures.py <game.exe> [<other.exe> ...] --output report.json
to repeat the offline check. Other game binaries are not validated.

Validation:
  cmake -S . -B build/release -DORION_BUILD_TESTS=ON
  cmake --build build/release --parallel
  Run build/release/bin/importer_smoke.exe <absolute DLL path> <mode>
  Modes: positive, missing, ambiguous. All return 0 when behavior matches expected.
  query_cache_tests.exe checks key identity, invalidation, current-caller string_views,
  negative-result bypass, FIFO rotation and both limits.
  hook_abi_tests.exe tests real SafetyHook trampolines for the Windows x64 query/atlas
  member ABIs, including hidden pair return storage and shared_ptr ownership.
  These tests use an artificial host with matching bytes, never call game code,
  and do not inject into Minecraft. All three passed under Wine.
  Static CRT debug PDBs are absent from xwin: LNK4099 warnings concern Microsoft's
  CRT; the mod's own PDB is generated and its PE debug-directory pointer is fixed
  by ModuleTweaker.

Import-table test executable:
  tools/add_mod_import.py source.exe copy.exe (requires Python lief) preserves
  the source and adds Orion.Optimization.dll!OrionOptimizationInitialize to the
  copy. Put the built DLL beside it. This import loads the DLL; the worker performs
  initialization. Source/output SHA-256 hashes are saved alongside the copy.

Parsed texture cache (verified 26.51.01 and 26.52.03)
----------------------------------------------------
The atlas-scoped TextureJSONParser::parseTextureSet hook caches the simple JSON
string form. It deep-copies the parsed resource descriptors using the game's
copy, vector-growth, destruction and allocation routines. It does not copy GPU
handles, skip _addElementCollection side effects, or cache atlas callbacks.
Key: exact texture path, TextureGroup identity, fade bits, color and additive flag.
Entries own a TextureGroup reference and are thread-local, limited to 32768 FIFO
entries; results with more than eight descriptors or paths over 4096 bytes bypass.
On verified 26.51.01 and 26.52.03, identical ordered pack identities survive stack recomposition.
The observed PackInstance revision byte advances even on unchanged world reloads;
for monitored packs, filesystem notifications replace that byte as content validity.
Resource-manager identity, pack ordering/subpack/settings changes, standalone
language changes and the enable toggle invalidate results. Pack roots are monitored
with Win32 change notifications (batched polling at most once per 16 ms while used).
Existing subdirectories receive explicit watches for Wine compatibility. New or
renamed directories rebuild the watch set. Any modification invalidates the entire
stack cache, not just one file: pack precedence can change the selected resource.
Archive packs watch their parent directory conservatively. Missing roots, reparse
points, failed watches and unverified builds fall back to per-recomposition epochs.
These are parsed-data caches; changes still reach the game through its normal reload
mechanism. This mod does not itself trigger an atlas reload or retain GPU atlases.
The parser entry cap is 32768 (variable memory use including owned game strings).
The query cap is 65536 entries / 128 MiB accounted storage, plus allocator overhead.
Complex JSON, empty results, non-atlas calls and unsupported helpers use the game.
ORION_OPTIMIZATION_PARSED_CACHE=0 disables the experiment. Existing optional
optimization logs include parsed_hits and parsed_misses (no per-query logging).
Relocation-wildcard helper signatures are unique-checked in both supported builds.
The game deallocator is resolved from the verified destructor call; no fixed helper
RVAs remain. Missing or ambiguous signatures disable persistent/parser caching.
The exact CodeView build gate still protects structure layouts.
Game startup and repeated atlas rebuilds exercised: parsed cache hits observed,
process remained alive. Visual correctness and performance still need comparison.
