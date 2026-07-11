#include "internal.h"
#include "wp_util.h"

static char g_tag[64] = "unknown";

#define WRG_MAX_VERSION_ALIASES 64
static char g_alias_name[WRG_MAX_VERSION_ALIASES][64];
static char g_alias_tag[WRG_MAX_VERSION_ALIASES][64];
static int  g_nalias = 0;

static char *trim_ascii(char *s) {
    while (*s == ' ' || *s == '\t' || *s == '\r') {
        s++;
    }
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r')) {
        s[--len] = 0;
    }
    return s;
}

static void load_version_aliases(void) {
    wchar_t p[MAX_PATH];
    _snwprintf(p, MAX_PATH, L"%ls\\version_aliases.txt", g_modsroot);
    char *buf = wp::read_file_text(p);
    if (!buf) {
        return;
    }

    char *save;
    char *line = strtok_s(buf, "\n", &save);
    for (; line && g_nalias < WRG_MAX_VERSION_ALIASES; line = strtok_s(NULL, "\n", &save)) {
        char *s = trim_ascii(line);
        if (!s[0] || s[0] == '#') {
            continue;
        }
        char *sp = strchr(s, ' ');
        if (!sp) {
            continue;
        }
        *sp = 0;
        char *name = trim_ascii(s);
        char *tag  = trim_ascii(sp + 1);
        if (!name[0] || !tag[0]) {
            continue;
        }
        strncpy(g_alias_name[g_nalias], name, 63);
        g_alias_name[g_nalias][63] = 0;
        strncpy(g_alias_tag[g_nalias], tag, 63);
        g_alias_tag[g_nalias][63] = 0;
        g_nalias++;
    }
    free(buf);
}

static const char *resolve_version_alias(const char *name) {
    for (int index = 0; index < g_nalias; ++index) {
        if (strcmp(g_alias_name[index], name) == 0) {
            return g_alias_tag[index];
        }
    }
    return NULL;
}

void wrg_version_init(void) {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    wp::HandleGuard fg(realCFW(exe, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL));
    if (!fg) {
        return;
    }
    HANDLE f = fg.get();
    LARGE_INTEGER sz;
    GetFileSizeEx(f, &sz);
    // FNV-1a over first 1 MB
    unsigned int h = 2166136261u;
    BYTE buf[65536];
    DWORD got;
    long long left = 1 << 20;
    while (left > 0) {
        if (!ReadFile(f, buf, sizeof(buf), &got, NULL) || got == 0) {
            break;
        }
        for (DWORD i = 0; i < got; ++i) {
            h ^= buf[i];
            h *= 16777619u;
        }
        left -= got;
    }
    fg.reset();   // must close before writing sidecar
    _snprintf(g_tag, sizeof(g_tag), "%llx-%08x", (unsigned long long)sz.QuadPart, h);

    wchar_t vp[MAX_PATH];
    _snwprintf(vp, MAX_PATH, L"%ls\\patcher_version.txt", g_modsroot);
    FILE *vf = _wfopen(vp, L"w");
    if (vf) {
        fprintf(vf, "%s\n", g_tag);
        fclose(vf);
    }

    load_version_aliases();
}

const char *wrg_version_tag(void) {
    return g_tag;
}

int wrg_version_matches(const char *required) {
    if (!required || !required[0]) {
        return 1;   // no gate -> allow
    }
    if (strcmp(required, g_tag) == 0) {
        return 1;
    }
    const char *aliased = resolve_version_alias(required);
    return aliased && strcmp(aliased, g_tag) == 0;
}
