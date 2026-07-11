#include "internal.h"

#define PIPE_NAME L"\\\\.\\pipe\\wrd_patcher"

static int ipc_enabled(void) {
    wchar_t configPath[MAX_PATH];
    _snwprintf(configPath, MAX_PATH, L"%ls\\patcher.cfg", g_modsroot);
    FILE *file = _wfopen(configPath, L"rb");
    if (!file) {
        return 0;
    }
    char configText[512];
    size_t bytesRead = fread(configText, 1, sizeof(configText)-1, file);
    configText[bytesRead]=0;
    fclose(file);
    return strstr(configText, "ipc=1") != NULL;
}

// hex -> bytes, allocates *out; returns len or -1
static int hex_decode(const char *hexText, BYTE **out) {
    size_t hexLen = strlen(hexText);
    if (hexLen == 0 || (hexLen & 1)) {
        return -1;
    }
    BYTE *bytes = (BYTE*)malloc(hexLen/2);
    if (!bytes) {
        return -1;
    }
    for (size_t index = 0; index < hexLen; index += 2) {
        int highNibble = hexText[index];
        int lowNibble = hexText[index+1];
        highNibble = (highNibble>='0'&&highNibble<='9')?highNibble-'0':(highNibble|32)>='a'&&(highNibble|32)<='f'?(highNibble|32)-'a'+10:-1;
        lowNibble = (lowNibble>='0'&&lowNibble<='9')?lowNibble-'0':(lowNibble|32)>='a'&&(lowNibble|32)<='f'?(lowNibble|32)-'a'+10:-1;
        if (highNibble < 0 || lowNibble < 0) {
            free(bytes);
            return -1;
        }
        bytes[index/2] = (BYTE)((highNibble<<4)|lowNibble);
    }
    *out = bytes;
    return (int)(hexLen/2);
}

// one command line in, reply (no newline) out into resp[respCapacity]
static void handle_cmd(char *line, char *resp, size_t respCapacity) {
    char *saveptr;
    char *cmd = strtok_s(line, " \t", &saveptr);
    if (!cmd) {
        resp[0]=0;
        return;
    }

    if (_stricmp(cmd, "PING") == 0) {
        strncpy(resp, "PONG", respCapacity);
    }
    else if (_stricmp(cmd, "VERSION") == 0) {
        strncpy(resp, wrg_version_tag(), respCapacity);
    }
    else if (_stricmp(cmd, "LIST") == 0) {
        _snprintf(resp, respCapacity, "mods=%d patches=%d", g_nmods, wrg_has_patches());
    }
    else if (_stricmp(cmd, "REDIRECT") == 0) {
        char *tail = strtok_s(NULL, " \t", &saveptr);
        char *real = strtok_s(NULL, "", &saveptr);   // rest of line, may contain spaces
        if (tail && real) {
            strncpy(resp, wrg_redirect_add(tail, real)==WRG_OK?"OK":"ERR", respCapacity);
        }
        else {
            strncpy(resp, "ERR usage: REDIRECT <tail> <realpath>", respCapacity);
        }
    }
    else if (_stricmp(cmd, "SPLICE") == 0) {
        char *tail = strtok_s(NULL, " \t", &saveptr);
        char *offsetText = strtok_s(NULL, " \t", &saveptr);
        char *hex  = strtok_s(NULL, " \t\r\n", &saveptr);
        BYTE *data = NULL;
        int dataLen;
        if (tail && offsetText && hex && (dataLen = hex_decode(hex, &data)) > 0) {
            unsigned long long offset = _strtoui64(offsetText, NULL, 0);
            WrgResult result = wrg_splice_add(tail, offset, NULL, data, (unsigned int)dataLen);
            if (result == WRG_OK) {
                wrg_install_read_hooks();
            }
            free(data);
            strncpy(resp, result==WRG_OK?"OK":"ERR", respCapacity);
        } else {
            free(data);
            strncpy(resp, "ERR usage: SPLICE <tail> <off> <hex>", respCapacity);
        }
    }
    else if (_stricmp(cmd, "RELOAD") == 0) {
        wrg_manifest_load_all();
        strncpy(resp, "OK", respCapacity);
    }
    else {
        strncpy(resp, "ERR unknown command", respCapacity);
    }
    resp[respCapacity-1] = 0;
}

static DWORD WINAPI ipc_thread(LPVOID arg) {
    (void)arg;
    for (;;) {
        HANDLE pipe = CreateNamedPipeW(PIPE_NAME,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1, 4096, 4096, 0, NULL);
        if (pipe == INVALID_HANDLE_VALUE) {
            Sleep(1000);
            continue;
        }
        if (!ConnectNamedPipe(pipe, NULL) && GetLastError() != ERROR_PIPE_CONNECTED) {
            CloseHandle(pipe);
            continue;
        }
        char buf[4096];
        DWORD bytesRead;
        while (ReadFile(pipe, buf, sizeof(buf)-1, &bytesRead, NULL) && bytesRead > 0) {
            buf[bytesRead] = 0;
            char *saveptr;
            char *line = strtok_s(buf, "\r\n", &saveptr);  // one reply per line
            for (; line; line = strtok_s(NULL, "\r\n", &saveptr)) {
                char resp[1024];
                handle_cmd(line, resp, sizeof(resp));
                strncat(resp, "\n", sizeof(resp)-strlen(resp)-1);
                DWORD bytesWritten;
                WriteFile(pipe, resp, (DWORD)strlen(resp), &bytesWritten, NULL);
            }
        }
        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
    return 0;
}

void wrg_ipc_start(void) {
    if (!ipc_enabled()) {
        return;
    }
    HANDLE thread = CreateThread(NULL, 0, ipc_thread, NULL, 0, NULL);
    if (thread) {
        CloseHandle(thread);
        wrg_log(L"IPC", L"listening on \\\\.\\pipe\\wrd_patcher", NULL);
    }
}
