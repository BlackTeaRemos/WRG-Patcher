// wrg_patcher.h - public C API for WRG-Patcher plugins.
//
// WRG-Patcher is a proxy version.dll loaded into Wargame: Red Dragon (x86)
// before main(). It is a *platform*: data mods (declarative manifests),
// code mods (plugin DLLs that link this header), and an optional live IPC
// control channel all sit on one robust IAT-hook core.
//
// A plugin is a DLL placed in mods\<ModName>\*.dll. WRG-Patcher LoadLibrary's
// it and calls the exported entry point:
//
//     __declspec(dllexport)
//     int WrgPluginInit(const WrgApi *api, WrgPlugin *self);
//
// Return WRG_OK to stay loaded. The api pointer is valid for the process
// lifetime. Everything a plugin can do to the game goes through *api.
//
// ABI: __cdecl, packed naturally, C linkage. Stable within a major version
// (WRG_API_VERSION). The host refuses a plugin whose api_version major
// differs.

#ifndef WRG_PATCHER_H
#define WRG_PATCHER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WRG_API_VERSION_MAJOR 1
#define WRG_API_VERSION_MINOR 0

// --- result codes ---------------------------------------------------------
typedef enum {
    WRG_OK            = 0,
    WRG_ERR           = -1,
    WRG_ERR_VERSION   = -2,   // api/plugin version mismatch
    WRG_ERR_NOTFOUND  = -3,   // target file/symbol not resolvable
    WRG_ERR_NOMEM     = -4,
    WRG_ERR_RANGE     = -5,   // offset/length out of bounds
} WrgResult;

// --- events the host raises into plugins -----------------------------------
typedef enum {
    WRG_EV_GAME_LOADED = 1,   // first asset open seen (engine up)
    WRG_EV_PACK_OPENED = 2,   // a .dat/.spk was opened (data = path tail)
    WRG_EV_SHUTDOWN    = 3,
} WrgEvent;

typedef void (*WrgEventFn)(WrgEvent ev, const void *data, void *user);

// --- the host API a plugin receives ----------------------------------------
//
// Resolution model. A patch targets an asset by *path tail* (e.g.
// "48574\\ZZ_3a.dat"). Two ways to place bytes:
//   - redirect_pack: serve a whole alternate file for that pack (mesh/NDF).
//   - splice: overlay bytes into the engine's reads of the real pack at a
//     location given EITHER by absolute file offset OR - update-resilient -
//     by an inner asset path the host resolves through the edat trie at
//     runtime (off == WRG_OFFSET_RESOLVE, inner != NULL).
typedef struct WrgApi {
    uint32_t api_version;        // (major<<16)|minor - check before use

    // logging (writes to patcher.log)
    void (*log)(const char *fmt, ...);

    // -- data placement --
    // Redirect every open of `tail` to `realpath` (absolute). Highest
    // registered priority wins; later registrations override earlier.
    WrgResult (*redirect_pack)(const char *tail, const char *realpath);

    // Splice `len` bytes from `data` (host copies it) into reads of `tail`.
    // If `off == WRG_OFFSET_RESOLVE`, `inner` names an asset inside the pack
    // and the host resolves the current file offset at runtime (survives a
    // game repack). Otherwise `off` is an absolute byte offset in the pack.
    WrgResult (*splice)(const char *tail, uint64_t off,
                        const char *inner, const void *data, uint32_t len);

    // -- import hooking (advanced; code mods) --
    // Patch import `func` (from any source DLL) to `repl` in every module.
    // Receives the previous target in *orig for chaining. Re-applied after
    // each LoadLibrary so late-loaded modules are covered.
    WrgResult (*hook_import)(const char *func, void *repl, void **orig);

    // -- process memory (code mods) --
    WrgResult (*read_mem)(uintptr_t addr, void *dst, size_t n);
    WrgResult (*write_mem)(uintptr_t addr, const void *src, size_t n);
    uintptr_t (*module_base)(const char *modname);   // 0 if not loaded

    // -- events --
    WrgResult (*on_event)(WrgEventFn fn, void *user);

    // -- introspection --
    const char *(*game_version)(void);   // hash/tag of WarGame3.exe
} WrgApi;

#define WRG_OFFSET_RESOLVE  ((uint64_t)~0ull)

// --- plugin self-description (filled by the plugin in WrgPluginInit) --------
typedef struct WrgPlugin {
    uint32_t api_version;        // set to (WRG_API_VERSION_MAJOR<<16)|MINOR
    const char *name;
    const char *version;
} WrgPlugin;

typedef int (*WrgPluginInitFn)(const WrgApi *api, WrgPlugin *self);

#ifdef __cplusplus
}
#endif
#endif // WRG_PATCHER_H
