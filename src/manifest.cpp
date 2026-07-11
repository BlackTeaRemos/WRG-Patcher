#include "internal.h"
#include "wp_util.h"

typedef enum { BLK_NONE, BLK_REDIRECT, BLK_OVERLAY } BlockKind;

typedef struct {
    BlockKind kind;
    char tail[256];
    char file[256];
    char inner[256];
    unsigned long long offset;
    int has_offset;
} Block;

static char *trim(char *text) {
    while (*text == ' ' || *text == '\t' || *text == '\r') {
        text++;
    }
    size_t textLen = strlen(text);
    while (textLen > 0 && (text[textLen-1]==' '||text[textLen-1]=='\t'||text[textLen-1]=='\r'||text[textLen-1]=='\n')) {
        text[--textLen]=0;
    }
    return text;
}

// strip surrounding double quotes, in place
static void unquote(char *text) {
    size_t textLen = strlen(text);
    if (textLen >= 2 && text[0]=='"' && text[textLen-1]=='"') {
        memmove(text, text+1, textLen-2);
        text[textLen-2]=0;
    }
}

static void flush_block(const Block *block, const wchar_t *moddir) {
    if (block->kind == BLK_REDIRECT) {
        if (!block->tail[0] || !block->file[0]) {
            return;
        }
        wchar_t wfile[256];  // mod-relative -> absolute 
        MultiByteToWideChar(CP_UTF8, 0, block->file, -1, wfile, 256);
        wp::normalize_separators(wfile);
        wchar_t absPath[MAX_PATH];
        _snwprintf(absPath, MAX_PATH, L"%ls\\%ls", moddir, wfile);
        absPath[MAX_PATH-1] = 0;   // _snwprintf omits terminator on truncation
        char absPathUtf8[MAX_PATH*2];
        WideCharToMultiByte(CP_UTF8, 0, absPath, -1, absPathUtf8, sizeof(absPathUtf8), NULL, NULL);
        wrg_redirect_add(block->tail, absPathUtf8);
        wrg_log(L"MANIFEST-REDIR", absPath, NULL);
    } else if (block->kind == BLK_OVERLAY) {
        if (!block->tail[0] || !block->file[0]) {
            return;
        }
        if (!block->has_offset && !block->inner[0]) {
            return;   // need offset or inner
        }
        wchar_t wfile[256];
        MultiByteToWideChar(CP_UTF8, 0, block->file, -1, wfile, 256);
        wp::normalize_separators(wfile);
        wchar_t absPath[MAX_PATH];
        _snwprintf(absPath, MAX_PATH, L"%ls\\%ls", moddir, wfile);
        absPath[MAX_PATH-1] = 0;   // _snwprintf omits terminator on truncation
        char *data = NULL;
        long dataLen = 0;
        FILE *file = _wfopen(absPath, L"rb");
        if (file) {
            fseek(file,0,SEEK_END);
            dataLen=ftell(file);
            fseek(file,0,SEEK_SET);
            data=(char*)malloc(dataLen);
            if(data) {
                fread(data,1,dataLen,file);
            }
            fclose(file);
        }
        if (!data || dataLen <= 0) {
            free(data);
            wrg_log(L"MANIFEST-OVL-NOFILE", absPath, NULL);
            return;
        }
        unsigned long long off = block->has_offset ? block->offset : WRG_OFFSET_RESOLVE;
        wrg_splice_add(block->tail, off, block->inner[0] ? block->inner : NULL,
                       data, (unsigned int)dataLen);
        wrg_install_read_hooks();
        free(data);
        wrg_log(L"MANIFEST-OVL", absPath, NULL);
    }
}

static void parse_manifest(const wchar_t *moddir, const wchar_t *modname) {
    wchar_t path[MAX_PATH];
    _snwprintf(path, MAX_PATH, L"%ls\\patcher.toml", moddir);
    char *txt = wp::read_file_text(path);
    if (!txt) {
        return;
    }

    char required[64] = "";
    Block currentBlock = {};
    int gateChecked = 0, gateOk = 1;

    char *saveptr;
    char *line = strtok_s(txt, "\n", &saveptr);
    for (; line; line = strtok_s(NULL, "\n", &saveptr)) {
        char *trimmedLine = trim(line);
        if (!trimmedLine[0] || trimmedLine[0] == '#') {
            continue;
        }

        if (trimmedLine[0] == '[') {
            if (!gateChecked) {   // header complete on first block -> evaluate gate once
                gateChecked = 1;
                gateOk = wrg_version_matches(required);
                if (!gateOk) {
                    wrg_log(L"MANIFEST-SKIP-VERSION", modname, NULL);
                }
            }
            if (currentBlock.kind != BLK_NONE && gateOk) {
                flush_block(&currentBlock, moddir);
            }
            memset(&currentBlock, 0, sizeof(currentBlock));
            if (strncmp(trimmedLine, "[[redirect]]", 12) == 0) {
                currentBlock.kind = BLK_REDIRECT;
            } else if (strncmp(trimmedLine, "[[overlay]]", 11) == 0) {
                currentBlock.kind = BLK_OVERLAY;
            } else {
                currentBlock.kind = BLK_NONE;
            }
            continue;
        }

        char *eq = strchr(trimmedLine, '=');
        if (!eq) {
            continue;
        }
        *eq = 0;
        char *key = trim(trimmedLine);
        char *value = trim(eq + 1);
        unquote(value);

        if (currentBlock.kind == BLK_NONE) {                 // top-level header keys
            if (strcmp(key, "requires_version") == 0) {
                strncpy(required, value, 63);
                required[63]=0;
            }
            continue;                                // name / others
        }
        if (strcmp(key, "tail") == 0) {
            strncpy(currentBlock.tail, value, 255);
            currentBlock.tail[255]=0;
        } else if (strcmp(key, "file") == 0) {
            strncpy(currentBlock.file, value, 255);
            currentBlock.file[255]=0;
        } else if (strcmp(key, "inner") == 0) {
            strncpy(currentBlock.inner, value, 255);
            currentBlock.inner[255]=0;
        } else if (strcmp(key, "offset") == 0) {
            currentBlock.offset = _strtoui64(value, NULL, 0);
            currentBlock.has_offset = 1;
        }
    }
    if (!gateChecked) {
        gateOk = wrg_version_matches(required);  // no blocks
    }
    if (currentBlock.kind != BLK_NONE && gateOk) {
        flush_block(&currentBlock, moddir);
    }
    free(txt);
}

void wrg_manifest_load_all(void) {
    for (int modIndex = 0; modIndex < g_nmods; ++modIndex) {
        wchar_t modDir[MAX_PATH];
        _snwprintf(modDir, MAX_PATH, L"%ls\\%ls", g_modsroot, g_mods[modIndex]);
        parse_manifest(modDir, g_mods[modIndex]);
    }
}
