#ifndef WRG_INTERNAL_H
#define WRG_INTERNAL_H

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "wrg_patcher.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WRG_MAX_MODS    128
#define WRG_MAX_PATCH   512
#define WRG_MAX_TRACK   128
#define WRG_MAX_REDIR   256
#define WRG_MAX_PLUGINS 64

void wrg_log(const wchar_t *tag, const wchar_t *subjectPath, const wchar_t *targetPath);

extern wchar_t g_modsroot[MAX_PATH];   // <gamedir>\mods
extern wchar_t g_gamedir[MAX_PATH];
extern wchar_t g_logpath[MAX_PATH];
extern wchar_t g_mods[WRG_MAX_MODS][128];
extern int     g_nmods;
extern CRITICAL_SECTION g_lock;        // guards track/patch/redir tables

// real API entry points
typedef HANDLE (WINAPI *CreateFileW_t)(LPCWSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE);
typedef HANDLE (WINAPI *CreateFileA_t)(LPCSTR ,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE);
typedef BOOL   (WINAPI *CloseHandle_t)(HANDLE);
typedef BOOL   (WINAPI *ReadFile_t)(HANDLE,LPVOID,DWORD,LPDWORD,LPOVERLAPPED);
extern CreateFileW_t  realCFW;
extern CreateFileA_t  realCFA;
extern CloseHandle_t  realCloseHandle;
extern ReadFile_t     realReadFile;

int  wrg_hook_all(const char *func, void *repl);   // patch import in all modules
void wrg_install_loadlib_rehook(void);             // re-run hooks on LoadLibrary
void wrg_rehook_known(void);                        // re-apply all our hooks

WrgResult wrg_redirect_add(const char *tail, const char *realpath);
// returns 1 + fills out (MAX_PATH wchars) if `name` should be redirected
int  wrg_redirect_resolve(const wchar_t *name, wchar_t *out);
int  wrg_path_wants(const wchar_t *path);   // .dat/.spk filter

typedef struct {
    wchar_t  tail[160];
    unsigned long long off;     // WRG_OFFSET_RESOLVE -> resolve via inner
    char     inner[160];        // inner asset path (resolve mode), else ""
    unsigned int len;
    BYTE    *data;
    int      resolved;          // 1 once off is final
} WrgPatch;

WrgResult wrg_splice_add(const char *tail, uint64_t off, const char *inner,
                         const void *data, uint32_t len);
void wrg_overlay_load_from_mods(void);   // legacy .ovl + manifest splices
const wchar_t *wrg_overlay_tail_for(const wchar_t *name);  // NULL if none
void wrg_track_add(HANDLE handle, const wchar_t *tail);
void wrg_track_set_path(HANDLE handle, const wchar_t *packpath); // resolve patches
void wrg_track_remove(HANDLE handle);                  // CloseHandle eviction
const wchar_t *wrg_track_tail(HANDLE handle);
int  wrg_has_patches(void);
// apply all matching patches to a just-read buffer
int  wrg_splice_apply(const wchar_t *tail, unsigned long long readOffset,
                      void *buf, size_t readLen);
void wrg_install_read_hooks(void);   // NtReadFile + ReadFile (once patches exist)

void wrg_manifest_load_all(void);    // parse mods\<m>\patcher.toml for each mod

void wrg_plugins_load_all(void);     // LoadLibrary mods\<m>\*.dll, call init; compiled out in release
void wrg_plugins_dispatch(WrgEvent ev, const void *data);
const WrgApi *wrg_api(void);          // the singleton passed to plugins

int  wrg_edat_resolve(const wchar_t *packpath, const char *inner,
                      unsigned long long *off);

void        wrg_version_init(void);            // hash WarGame3.exe
const char *wrg_version_tag(void);             // stable id string
int         wrg_version_matches(const char *required);  // manifest gate

void wrg_ipc_start(void);   // spawn named-pipe control thread

#ifdef __cplusplus
}
#endif

#endif
