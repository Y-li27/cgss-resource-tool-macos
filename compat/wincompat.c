#define _POSIX_C_SOURCE 200809L
#include "windows.h"
#include "conio.h"

#include <dirent.h>
#include <fnmatch.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>
#include <time.h>
#include <sys/select.h>
#include <errno.h>
#include <strings.h>
#include <signal.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

static DWORD g_lasterror = 0;

void SetLastError(DWORD e){ g_lasterror = e; }
DWORD GetLastError(void){ return g_lasterror; }

static void path_to_utf8(const wchar_t *w, char *out, size_t n){
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)n, NULL, NULL);
    for (char *p = out; *p; p++){
        if (*p == '\\') *p = '/';
    }
}

static int mkdir_p_utf8(const char *path){
    char tmp[4096];
    snprintf(tmp, sizeof tmp, "%s", path);
    size_t len = strlen(tmp);
    while (len > 1 && tmp[len - 1] == '/') tmp[--len] = 0;
    for (char *p = tmp + 1; *p; p++){
        if (*p == '/'){
            *p = 0;
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) { /* continue anyway */ }
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) == 0 || errno == EEXIST) return 0;
    return -1;
}

static void utf8_to_pathw(const char *in, wchar_t *out, int n){
    MultiByteToWideChar(CP_UTF8, 0, in, -1, out, n);
}

/* ---- UTF-8 <-> wchar_t (UTF-32 on macOS/Linux) ---- */
int MultiByteToWideChar(UINT cp, DWORD flags, const char *src, int slen, wchar_t *dst, int dlen){
    (void)cp; (void)flags;
    if (!src) return 0;
    if (slen < 0) slen = (int)strlen(src) + 1;
    int need = 0;
    const unsigned char *p = (const unsigned char *)src;
    const unsigned char *end = p + slen;
    while (p < end){
        unsigned char c = *p;
        if (c == 0){ need++; break; }
        int adv = 1;
        if ((c & 0x80) == 0) adv = 1;
        else if ((c & 0xE0) == 0xC0) adv = 2;
        else if ((c & 0xF0) == 0xE0) adv = 3;
        else if ((c & 0xF8) == 0xF0) adv = 4;
        p += adv;
        if (p > end) break;
        need++;
    }
    if (!dst || dlen == 0) return need;
    int i = 0;
    p = (const unsigned char *)src;
    while (p < end && i < dlen){
        unsigned char c = *p++;
        uint32_t cpnt;
        if (c == 0){ dst[i++] = 0; break; }
        if ((c & 0x80) == 0) cpnt = c;
        else if ((c & 0xE0) == 0xC0 && p < end){ cpnt = ((c & 0x1F) << 6) | (*p++ & 0x3F); }
        else if ((c & 0xF0) == 0xE0 && p + 1 < end){
            cpnt = ((c & 0x0F) << 12) | ((*p & 0x3F) << 6) | (p[1] & 0x3F); p += 2;
        } else if ((c & 0xF8) == 0xF0 && p + 2 < end){
            cpnt = ((c & 0x07) << 18) | ((*p & 0x3F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); p += 3;
        } else cpnt = 0xFFFD;
        dst[i++] = (wchar_t)cpnt;
    }
    if (i < dlen) dst[i] = 0;
    return i;
}

int WideCharToMultiByte(UINT cp, DWORD flags, const wchar_t *src, int slen, char *dst, int dlen,
                        const char *defchar, BOOL *used){
    (void)cp; (void)flags; (void)defchar;
    if (used) *used = FALSE;
    if (!src) return 0;
    if (slen < 0){
        slen = 0;
        while (src[slen]) slen++;
        slen++; /* include NUL */
    }
    int need = 0;
    for (int i = 0; i < slen; i++){
        uint32_t c = (uint32_t)src[i];
        if (c == 0){ need++; break; }
        if (c < 0x80) need += 1;
        else if (c < 0x800) need += 2;
        else if (c < 0x10000) need += 3;
        else need += 4;
    }
    if (!dst || dlen == 0) return need;
    int o = 0;
    for (int i = 0; i < slen && o < dlen; i++){
        uint32_t c = (uint32_t)src[i];
        if (c == 0){ dst[o++] = 0; break; }
        if (c < 0x80){
            dst[o++] = (char)c;
        } else if (c < 0x800 && o + 2 <= dlen){
            dst[o++] = (char)(0xC0 | (c >> 6));
            dst[o++] = (char)(0x80 | (c & 0x3F));
        } else if (c < 0x10000 && o + 3 <= dlen){
            dst[o++] = (char)(0xE0 | (c >> 12));
            dst[o++] = (char)(0x80 | ((c >> 6) & 0x3F));
            dst[o++] = (char)(0x80 | (c & 0x3F));
        } else if (o + 4 <= dlen){
            dst[o++] = (char)(0xF0 | (c >> 18));
            dst[o++] = (char)(0x80 | ((c >> 12) & 0x3F));
            dst[o++] = (char)(0x80 | ((c >> 6) & 0x3F));
            dst[o++] = (char)(0x80 | (c & 0x3F));
        } else break;
    }
    if (o < dlen) dst[o] = 0;
    return o;
}

DWORD GetModuleFileNameW(HMODULE mod, wchar_t *buf, DWORD n){
    (void)mod;
    char path[PATH_MAX];
    path[0] = 0;
#ifdef __APPLE__
    uint32_t sz = sizeof path;
    if (_NSGetExecutablePath(path, &sz) != 0) path[0] = 0;
    char real[PATH_MAX];
    if (path[0] && realpath(path, real)) snprintf(path, sizeof path, "%s", real);
#elif defined(__linux__)
    ssize_t r = readlink("/proc/self/exe", path, sizeof path - 1);
    if (r > 0) path[r] = 0;
    else path[0] = 0;
#endif
    if (!path[0]){
        if (getcwd(path, sizeof path) == NULL) snprintf(path, sizeof path, ".");
        size_t L = strlen(path);
        if (L + 8 < sizeof path) memcpy(path + L, "/CGSS", 6);
    }
    utf8_to_pathw(path, buf, (int)n);
    for (wchar_t *p = buf; *p; p++){
        if (*p == L'/') *p = L'\\';
    }
    return (DWORD)wcslen(buf);
}

BOOL CreateDirectoryW(LPCWSTR path, SECURITY_ATTRIBUTES *sa){
    (void)sa;
    char u8[4096];
    path_to_utf8(path, u8, sizeof u8);
    if (mkdir_p_utf8(u8) == 0){ SetLastError(ERROR_ALREADY_EXISTS); return TRUE; }
    if (errno == EEXIST){ SetLastError(ERROR_ALREADY_EXISTS); return TRUE; }
    SetLastError((DWORD)errno);
    return FALSE;
}

BOOL RemoveDirectoryW(LPCWSTR path){
    char u8[PATH_MAX];
    path_to_utf8(path, u8, sizeof u8);
    return rmdir(u8) == 0 ? TRUE : FALSE;
}

BOOL DeleteFileW(LPCWSTR path){
    char u8[PATH_MAX];
    path_to_utf8(path, u8, sizeof u8);
    return unlink(u8) == 0 ? TRUE : FALSE;
}

BOOL CopyFileW(LPCWSTR src, LPCWSTR dst, BOOL fail_if_exists){
    char s[PATH_MAX], d[PATH_MAX];
    path_to_utf8(src, s, sizeof s);
    path_to_utf8(dst, d, sizeof d);
    if (fail_if_exists && access(d, F_OK) == 0) return FALSE;
    FILE *in = fopen(s, "rb");
    if (!in) return FALSE;
    FILE *out = fopen(d, "wb");
    if (!out){ fclose(in); return FALSE; }
    char buf[64 * 1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0){
        if (fwrite(buf, 1, n, out) != n){ fclose(in); fclose(out); return FALSE; }
    }
    fclose(in); fclose(out);
    return TRUE;
}

BOOL MoveFileExW(LPCWSTR src, LPCWSTR dst, DWORD flags){
    (void)flags;
    return MoveFileW(src, dst);
}

BOOL MoveFileW(LPCWSTR src, LPCWSTR dst){
    char s[PATH_MAX], d[PATH_MAX];
    path_to_utf8(src, s, sizeof s);
    path_to_utf8(dst, d, sizeof d);
    if (rename(s, d) == 0) return TRUE;
    if (CopyFileW(src, dst, FALSE)) return DeleteFileW(src);
    return FALSE;
}

DWORD GetFileAttributesA(LPCSTR path){
    wchar_t w[PATH_MAX];
    if (!path) return INVALID_FILE_ATTRIBUTES;
    MultiByteToWideChar(CP_UTF8, 0, path, -1, w, PATH_MAX);
    return GetFileAttributesW(w);
}

DWORD GetFileAttributesW(LPCWSTR path){
    char u8[PATH_MAX];
    path_to_utf8(path, u8, sizeof u8);
    struct stat st;
    if (stat(u8, &st) != 0){
        SetLastError(ERROR_FILE_NOT_FOUND);
        return INVALID_FILE_ATTRIBUTES;
    }
    DWORD a = FILE_ATTRIBUTE_NORMAL;
    if (S_ISDIR(st.st_mode)) a = FILE_ATTRIBUTE_DIRECTORY;
    return a;
}

typedef struct {
    DIR *dir;
    char dirpath[PATH_MAX];
    char pattern[256];
    int is_wide;
} FindCtx;

static void split_pattern(const char *pat, char *dir, size_t dsz, char *glob, size_t gsz){
    snprintf(dir, dsz, "%s", pat);
    for (char *p = dir; *p; p++) if (*p == '\\') *p = '/';
    char *slash = strrchr(dir, '/');
    if (slash){
        snprintf(glob, gsz, "%s", slash + 1);
        *slash = 0;
        if (!dir[0]) snprintf(dir, dsz, "/");
    } else {
        snprintf(glob, gsz, "%s", dir);
        snprintf(dir, dsz, ".");
    }
    if (!glob[0]) snprintf(glob, gsz, "*");
}

static int fill_wfd(const char *dir, const char *name, WIN32_FIND_DATAW *fd){
    memset(fd, 0, sizeof *fd);
    utf8_to_pathw(name, fd->cFileName, MAX_PATH);
    char full[PATH_MAX];
    snprintf(full, sizeof full, "%s/%s", dir, name);
    struct stat st;
    if (stat(full, &st) == 0 && S_ISDIR(st.st_mode))
        fd->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
    else
        fd->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
    return 1;
}

static int fill_afd(const char *dir, const char *name, WIN32_FIND_DATAA *fd){
    memset(fd, 0, sizeof *fd);
    snprintf(fd->cFileName, MAX_PATH, "%s", name);
    char full[PATH_MAX];
    snprintf(full, sizeof full, "%s/%s", dir, name);
    struct stat st;
    if (stat(full, &st) == 0 && S_ISDIR(st.st_mode))
        fd->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
    else
        fd->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
    return 1;
}

static HANDLE find_open(const char *pat_u8){
    FindCtx *ctx = (FindCtx*)calloc(1, sizeof *ctx);
    if (!ctx) return INVALID_HANDLE_VALUE;
    split_pattern(pat_u8, ctx->dirpath, sizeof ctx->dirpath, ctx->pattern, sizeof ctx->pattern);
    ctx->dir = opendir(ctx->dirpath);
    if (!ctx->dir){ free(ctx); return INVALID_HANDLE_VALUE; }
    return (HANDLE)ctx;
}

static int find_next_name(FindCtx *ctx, char *name, size_t n){
    struct dirent *de;
    while ((de = readdir(ctx->dir)) != NULL){
        if (fnmatch(ctx->pattern, de->d_name, FNM_NOESCAPE) == 0){
            snprintf(name, n, "%s", de->d_name);
            return 1;
        }
    }
    return 0;
}

HANDLE FindFirstFileW(LPCWSTR pattern, WIN32_FIND_DATAW *fd){
    char u8[PATH_MAX];
    path_to_utf8(pattern, u8, sizeof u8);
    HANDLE h = find_open(u8);
    if (h == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;
    FindCtx *ctx = (FindCtx*)h;
    char name[256];
    if (!find_next_name(ctx, name, sizeof name)){
        closedir(ctx->dir); free(ctx);
        return INVALID_HANDLE_VALUE;
    }
    fill_wfd(ctx->dirpath, name, fd);
    return h;
}

BOOL FindNextFileW(HANDLE h, WIN32_FIND_DATAW *fd){
    if (!h || h == INVALID_HANDLE_VALUE) return FALSE;
    FindCtx *ctx = (FindCtx*)h;
    char name[256];
    if (!find_next_name(ctx, name, sizeof name)) return FALSE;
    fill_wfd(ctx->dirpath, name, fd);
    return TRUE;
}

HANDLE FindFirstFileA(const char *pattern, WIN32_FIND_DATAA *fd){
    HANDLE h = find_open(pattern);
    if (h == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;
    FindCtx *ctx = (FindCtx*)h;
    char name[256];
    if (!find_next_name(ctx, name, sizeof name)){
        closedir(ctx->dir); free(ctx);
        return INVALID_HANDLE_VALUE;
    }
    fill_afd(ctx->dirpath, name, fd);
    return h;
}

BOOL FindNextFileA(HANDLE h, WIN32_FIND_DATAA *fd){
    if (!h || h == INVALID_HANDLE_VALUE) return FALSE;
    FindCtx *ctx = (FindCtx*)h;
    char name[256];
    if (!find_next_name(ctx, name, sizeof name)) return FALSE;
    fill_afd(ctx->dirpath, name, fd);
    return TRUE;
}

BOOL FindClose(HANDLE h){
    if (!h || h == INVALID_HANDLE_VALUE) return FALSE;
    FindCtx *ctx = (FindCtx*)h;
    if (ctx->dir) closedir(ctx->dir);
    free(ctx);
    return TRUE;
}

HANDLE CreateFileW(LPCWSTR path, DWORD access, DWORD share, SECURITY_ATTRIBUTES *sa,
                   DWORD disp, DWORD flags, HANDLE tmpl){
    (void)share; (void)sa; (void)flags; (void)tmpl;
    char u8[4096];
    path_to_utf8(path, u8, sizeof u8);
    const char *mode = "rb+";
    if (access & GENERIC_WRITE){
        mode = (disp == CREATE_ALWAYS) ? "wb" : "ab";
        char parent[4096];
        snprintf(parent, sizeof parent, "%s", u8);
        char *slash = strrchr(parent, '/');
        if (slash && slash != parent){
            *slash = 0;
            mkdir_p_utf8(parent);
        }
    }
    FILE *f = fopen(u8, mode);
    if (!f){
        SetLastError(errno ? (DWORD)errno : ERROR_FILE_NOT_FOUND);
        fprintf(stderr, "CreateFileW path=%s errno=%d (%s)\n", u8, errno, strerror(errno));
        return INVALID_HANDLE_VALUE;
    }
    return (HANDLE)f;
}

BOOL WriteFile(HANDLE h, LPCVOID buf, DWORD n, LPDWORD written, void *ovl){
    (void)ovl;
    if (!h || h == INVALID_HANDLE_VALUE) return FALSE;
    size_t w = fwrite(buf, 1, n, (FILE*)h);
    if (written) *written = (DWORD)w;
    return w == n;
}

#define PROC_MAGIC 0x50524F43u
typedef struct { uint32_t magic; int refs; pid_t pid; int status; int done; } ProcBox;

BOOL CloseHandle(HANDLE h){
    if (!h || h == INVALID_HANDLE_VALUE) return FALSE;
    ProcBox *box = (ProcBox*)h;
    if (box->magic == PROC_MAGIC){
        if (--box->refs <= 0) free(box);
        return TRUE;
    }
    fclose((FILE*)h);
    return TRUE;
}

BOOL CreateProcessW(LPCWSTR app, LPWSTR cmdline, SECURITY_ATTRIBUTES *pa, SECURITY_ATTRIBUTES *ta,
                    BOOL inherit, DWORD flags, LPVOID env, LPCWSTR cwd,
                    STARTUPINFOW *si, PROCESS_INFORMATION *pi){
    (void)pa; (void)ta; (void)inherit; (void)flags; (void)env; (void)si;
    char cmd[4096];
    if (app && app[0]){
        char a[PATH_MAX], c[3500];
        path_to_utf8(app, a, sizeof a);
        if (cmdline) path_to_utf8(cmdline, c, sizeof c);
        else c[0] = 0;
        snprintf(cmd, sizeof cmd, "\"%s\" %s", a, c);
    } else if (cmdline){
        path_to_utf8(cmdline, cmd, sizeof cmd);
    } else {
        return FALSE;
    }
    char cd[PATH_MAX];
    cd[0] = 0;
    if (cwd) path_to_utf8(cwd, cd, sizeof cd);

    pid_t pid = fork();
    if (pid < 0) return FALSE;
    if (pid == 0){
        if (cd[0]) chdir(cd);
        execl("/bin/sh", "sh", "-c", cmd, (char*)NULL);
        _exit(127);
    }
    ProcBox *box = (ProcBox*)calloc(1, sizeof *box);
    box->magic = PROC_MAGIC;
    box->refs = 2;
    box->pid = pid;
    if (pi){
        memset(pi, 0, sizeof *pi);
        pi->hProcess = box;
        pi->hThread = box;
        pi->dwProcessId = (DWORD)pid;
    }
    return TRUE;
}

DWORD WaitForSingleObject(HANDLE h, DWORD ms){
    (void)ms;
    if (!h) return 0;
    ProcBox *box = (ProcBox*)h;
    if (box->done) return 0;
    if (waitpid(box->pid, &box->status, 0) >= 0) box->done = 1;
    return 0;
}

BOOL GetExitCodeProcess(HANDLE h, DWORD *code){
    if (!h || !code) return FALSE;
    ProcBox *box = (ProcBox*)h;
    if (!box->done){
        if (waitpid(box->pid, &box->status, 0) >= 0) box->done = 1;
    }
    *code = (DWORD)(WIFEXITED(box->status) ? WEXITSTATUS(box->status) : 1);
    return TRUE;
}

HINSTANCE ShellExecuteW(HWND hwnd, LPCWSTR op, LPCWSTR file, LPCWSTR params, LPCWSTR dir, INT show){
    (void)hwnd; (void)op; (void)params; (void)dir; (void)show;
    char path[PATH_MAX];
    path_to_utf8(file, path, sizeof path);
    char cmd[PATH_MAX + 32];
#ifdef __APPLE__
    snprintf(cmd, sizeof cmd, "open \"%s\"", path);
#else
    snprintf(cmd, sizeof cmd, "xdg-open \"%s\" >/dev/null 2>&1", path);
#endif
    int rc = system(cmd);
    return (HINSTANCE)(intptr_t)(rc == 0 ? 33 : 2);
}

UINT GetConsoleCP(void){ return CP_UTF8; }
UINT GetConsoleOutputCP(void){ return CP_UTF8; }
UINT GetACP(void){ return CP_UTF8; }
BOOL SetConsoleCP(UINT cp){ (void)cp; return TRUE; }
BOOL SetConsoleOutputCP(UINT cp){ (void)cp; return TRUE; }
HANDLE GetStdHandle(DWORD id){ (void)id; return (HANDLE)(intptr_t)1; }
BOOL GetConsoleMode(HANDLE h, DWORD *mode){ (void)h; if (mode) *mode = 0; return TRUE; }
BOOL SetConsoleMode(HANDLE h, DWORD mode){ (void)h; (void)mode; return TRUE; }

BOOL GetConsoleScreenBufferInfo(HANDLE h, CONSOLE_SCREEN_BUFFER_INFO *info){
    (void)h;
    if (!info) return FALSE;
    memset(info, 0, sizeof *info);
    struct winsize ws;
    int rows = 24, cols = 80;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0){
        rows = ws.ws_row;
        cols = ws.ws_col;
    }
    info->srWindow.Top = 0;
    info->srWindow.Bottom = (SHORT)(rows - 1);
    info->srWindow.Left = 0;
    info->srWindow.Right = (SHORT)(cols - 1);
    return TRUE;
}

void Sleep(DWORD ms){
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

DWORD GetCurrentProcessId(void){ return (DWORD)getpid(); }

FILE *_wfopen(const wchar_t *path, const wchar_t *mode){
    char p[PATH_MAX], m[16];
    path_to_utf8(path, p, sizeof p);
    WideCharToMultiByte(CP_UTF8, 0, mode, -1, m, sizeof m, NULL, NULL);
    return fopen(p, m);
}

int _wcsicmp(const wchar_t *a, const wchar_t *b){
    if (!a) a = L"";
    if (!b) b = L"";
    while (*a && *b){
        wchar_t ca = *a, cb = *b;
        if (ca >= L'A' && ca <= L'Z') ca += 32;
        if (cb >= L'A' && cb <= L'Z') cb += 32;
        if (ca != cb) return ca < cb ? -1 : 1;
        a++; b++;
    }
    if (*a) return 1;
    if (*b) return -1;
    return 0;
}

int _stricmp(const char *a, const char *b){
    return strcasecmp(a ? a : "", b ? b : "");
}

/* ---- raw console input ---- */
static struct termios g_orig;
static int g_have_orig = 0;
static int g_raw = 0;
static int g_pending = -1;
static int g_cooked_atexit = 0;

static void remember_orig(void){
    if (g_have_orig || !isatty(STDIN_FILENO)) return;
    tcgetattr(STDIN_FILENO, &g_orig);
    /* 前回異常終了で ICRNL が落ちていると、Enter が ^M のままになる */
    g_orig.c_iflag |= (ICRNL | IXON | BRKINT);
    g_orig.c_iflag &= ~(IGNCR | INLCR);
    g_orig.c_oflag |= (OPOST | ONLCR);
    g_orig.c_lflag |= (ICANON | ECHO | ECHOE | ECHOK | IEXTEN);
    g_have_orig = 1;
}

void console_cooked(void){
    if (!isatty(STDIN_FILENO)){ g_raw = 0; return; }
    remember_orig();
    if (g_have_orig)
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig);
    g_raw = 0;
}

static void cooked_atexit(void){ console_cooked(); }

static void on_signal(int sig){
    console_cooked();
    signal(sig, SIG_DFL);
    raise(sig);
}

static void raw_on(void){
    if (!isatty(STDIN_FILENO)) return;
    remember_orig();
    if (!g_cooked_atexit){
        atexit(cooked_atexit);
        signal(SIGINT, on_signal);
        signal(SIGTERM, on_signal);
        g_cooked_atexit = 1;
    }
    if (g_raw) return;
    struct termios t = g_orig;
    t.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    t.c_lflag &= ~(ECHO | ECHONL | ICANON | IEXTEN);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &t);
    g_raw = 1;
}

static int read_byte(void){
    unsigned char c;
    for (;;){
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n == 1) return c;
        if (n < 0 && errno == EINTR) continue;
        return -1;
    }
}

static int read_byte_timeout(int ms){
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (long)(ms % 1000) * 1000;
    int r = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
    if (r <= 0) return -1;
    return read_byte();
}

int _getch(void){
    raw_on();
    if (g_pending >= 0){
        int v = g_pending;
        g_pending = -1;
        return v;
    }
    int c = read_byte();
    if (c < 0) return -1;
    if (c == '\n' || c == '\r') return '\r';
    if (c != 27) return c;

    /* テンキー Enter は ESC O M。素の Esc と区別するため少しだけ待つ */
    int c2 = read_byte_timeout(50);
    if (c2 < 0) return 27;
    if (c2 == 'O'){
        int c3 = read_byte_timeout(50);
        if (c3 == 'M') return '\r';
        return 27;
    }
    if (c2 != '[') return 27;

    int c3 = read_byte_timeout(50);
    if (c3 < 0) return 27;
    int scan = 0;
    switch (c3){
        case 'A': scan = 0x48; break; /* up */
        case 'B': scan = 0x50; break; /* down */
        case 'C': scan = 0x4D; break; /* right */
        case 'D': scan = 0x4B; break; /* left */
        case 'H': scan = 0x47; break; /* home */
        case 'F': scan = 0x4F; break; /* end */
        case '5': { read_byte_timeout(50); scan = 0x49; break; } /* PgUp */
        case '6': { read_byte_timeout(50); scan = 0x51; break; } /* PgDn */
        default:
            /* kitty などの CSI u。Enter は ESC [ 13 u */
            if (c3 >= '0' && c3 <= '9'){
                int code = c3 - '0';
                for (;;){
                    int d = read_byte_timeout(50);
                    if (d >= '0' && d <= '9') code = code * 10 + (d - '0');
                    else break;
                }
                if (code == 13 || code == 10) return '\r';
                return -1;
            }
            return -1;
    }
    g_pending = scan;
    return 0xE0;
}

int _wremove(const wchar_t *path){
    return DeleteFileW(path) ? 0 : -1;
}
