/* AssetRipper.GUI.Free を headless で起動し、Unity プロジェクトのゴミを除いて回収する */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <curl/curl.h>
#include "util.h"
#include "assetripper.h"

static pid_t g_pid = -1;
static int g_port = 0;
static int g_atexit = 0;

static int ends_with(const char *s, const char *suf){
    size_t n = strlen(s), m = strlen(suf);
    if (n < m) return 0;
    return strcasecmp(s + n - m, suf) == 0;
}

static int starts_with(const char *s, const char *pre){
    size_t n = strlen(pre);
    return strncasecmp(s, pre, n) == 0;
}

static int unity_builtin_image(const char *name){
    static const char *names[] = {
        "Background.png", "Checkmark.png", "DropdownArrow.png",
        "InputFieldBackground.png", "Knob.png", "UIMask.png",
        "UISprite.png", "UnitySplash-cube.png"
    };
    for (int i = 0; i < (int)(sizeof names / sizeof names[0]); i++)
        if (strcasecmp(name, names[i]) == 0) return 1;
    return 0;
}

/* 残すファイルなら書き出し名を out に入れて 1 */
static int dest_name_for(const char *name, int mode, char *out, int outn){
    if (name[0] == '.') return 0;
    if (ends_with(name, ".meta") || ends_with(name, ".mat") || ends_with(name, ".cs") ||
        ends_with(name, ".prefab") || ends_with(name, ".asset") || ends_with(name, ".shader"))
        return 0;

    if (mode == RIP_KEEP_SPINE){
        if (ends_with(name, ".skel.bytes")){
            snprintf(out, outn, "%.*s", (int)(strlen(name) - 6), name);
            return 1;
        }
        if (ends_with(name, ".atlas.bytes")){
            snprintf(out, outn, "%.*s", (int)(strlen(name) - 6), name);
            return 1;
        }
        if (ends_with(name, ".atlas.txt")){
            snprintf(out, outn, "%.*s", (int)(strlen(name) - 4), name);
            return 1;
        }
        if (ends_with(name, ".skel") || ends_with(name, ".atlas")){
            snprintf(out, outn, "%s", name);
            return 1;
        }
        if (ends_with(name, ".png") &&
            (ends_with(name, "_tex.png") || ends_with(name, "_tex_A8.png") ||
             starts_with(name, "SPC") || starts_with(name, "SPSprachen_"))){
            snprintf(out, outn, "%s", name);
            return 1;
        }
        if (strcasecmp(name, "SPSprachen_s.json") == 0 || strcasecmp(name, "SPSprachen_N.json") == 0){
            snprintf(out, outn, "%s", name);
            return 1;
        }
        return 0;
    }

    if (mode == RIP_KEEP_IMAGE){
        if ((ends_with(name, ".png") || ends_with(name, ".tga") ||
             ends_with(name, ".jpg") || ends_with(name, ".jpeg")) &&
            !unity_builtin_image(name)){
            snprintf(out, outn, "%s", name);
            return 1;
        }
        return 0;
    }

    /* RIP_KEEP_MODEL: 呼び出し側が拡張子で振り分ける */
    if (ends_with(name, ".glb") || ends_with(name, ".fbx") || ends_with(name, ".obj") ||
        ends_with(name, ".png") || ends_with(name, ".tga")){
        if (unity_builtin_image(name)) return 0;
        snprintf(out, outn, "%s", name);
        return 1;
    }
    return 0;
}

static int copy_file_utf8(const char *src, const char *dst){
    wchar_t ws[4096], wd[4096];
    utf8_to_wide(src, ws, 4096);
    utf8_to_wide(dst, wd, 4096);
    if (!CopyFileW(ws, wd, FALSE)) return 0;
    printf("  -> %s\n", strrchr(dst, '/') ? strrchr(dst, '/') + 1 : dst);
    return 1;
}

static int walk_collect(const char *dir, const char *dest, const char *tex_dest, int mode){
    DIR *d = opendir(dir);
    if (!d) return 0;
    int n = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL){
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        char full[4096];
        snprintf(full, sizeof full, "%s/%s", dir, de->d_name);
        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)){
            n += walk_collect(full, dest, tex_dest, mode);
            continue;
        }
        if (!S_ISREG(st.st_mode)) continue;
        char renamed[1024];
        if (!dest_name_for(de->d_name, mode, renamed, sizeof renamed)) continue;
        const char *folder = dest;
        if (mode == RIP_KEEP_MODEL && (ends_with(renamed, ".png") || ends_with(renamed, ".tga")))
            folder = tex_dest;
        char dst[4096];
        snprintf(dst, sizeof dst, "%s/%s", folder, renamed);
        if (copy_file_utf8(full, dst)) n++;
    }
    closedir(d);
    return n;
}

static void to_posix(char *s){
    for (; s && *s; s++)
        if (*s == '\\') *s = '/';
}

int assetripper_collect(const wchar_t *export_root, const wchar_t *dest, int mode){
    char root[4096], dst[4096], tex[4600];
    wide_to_utf8(export_root, root, sizeof root);
    wide_to_utf8(dest, dst, sizeof dst);
    to_posix(root);
    to_posix(dst);
    snprintf(tex, sizeof tex, "%s/テクスチャ", dst);
    if (mode == RIP_KEEP_MODEL){
        wchar_t wtex[4096];
        utf8_to_wide(tex, wtex, 4096);
        mkdirs(wtex);
    }
    return walk_collect(root, dst, tex, mode);
}

static size_t curl_discard(char *ptr, size_t size, size_t nmemb, void *ud){
    (void)ptr; (void)ud;
    return size * nmemb;
}

static int http_code(const char *url, const char *post, long timeout){
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, timeout < 5 ? timeout : 5L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_discard);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    if (post){
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post);
    }
    CURLcode rc = curl_easy_perform(curl);
    long code = 0;
    if (rc == CURLE_OK)
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);
    return rc == CURLE_OK ? (int)code : -1;
}

static int pick_port(void){
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return 28765;
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    if (bind(s, (struct sockaddr *)&a, sizeof a) != 0){
        close(s);
        return 28765;
    }
    socklen_t n = sizeof a;
    getsockname(s, (struct sockaddr *)&a, &n);
    int port = ntohs(a.sin_port);
    close(s);
    return port > 0 ? port : 28765;
}

static int exe_dir(char *out, int n){
    wchar_t w[1024];
    GetModuleFileNameW(NULL, w, 1024);
    wchar_t *p = wcsrchr(w, L'\\');
    if (p) *p = 0;
    wide_to_utf8(w, out, n);
    to_posix(out);
    return out[0] != 0;
}

static int file_exists(const char *path){
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static void find_binary(char *out, int n){
    out[0] = 0;
    char dir[1024];
    if (!exe_dir(dir, sizeof dir)) return;
    const char *rels[] = {
        "/AssetRipper/AssetRipper.GUI.Free",
        "/AssetRipper.GUI.Free",
        "/AssetRipper/AssetRipper.GUI.Free.app/Contents/MacOS/AssetRipper.GUI.Free",
        "/AssetRipper.app/Contents/MacOS/AssetRipper.GUI.Free"
    };
    for (int i = 0; i < 4; i++){
        snprintf(out, n, "%s%s", dir, rels[i]);
        if (file_exists(out)) return;
    }
    out[0] = 0;
    FILE *p = popen("command -v AssetRipper.GUI.Free 2>/dev/null", "r");
    if (p){
        if (fgets(out, n, p)){
            out[strcspn(out, "\r\n")] = 0;
            if (!file_exists(out)) out[0] = 0;
        } else out[0] = 0;
        pclose(p);
    }
}

void assetripper_stop(void){
    if (g_pid <= 0) return;
    kill(-g_pid, SIGTERM);
    for (int i = 0; i < 20; i++){
        int st;
        if (waitpid(g_pid, &st, WNOHANG) == g_pid){
            g_pid = -1;
            return;
        }
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 100000000L;
        nanosleep(&ts, NULL);
    }
    kill(-g_pid, SIGKILL);
    waitpid(g_pid, NULL, 0);
    g_pid = -1;
}

static void stop_atexit(void){ assetripper_stop(); }

static int ensure_server(void){
    if (g_pid > 0){
        int st;
        if (waitpid(g_pid, &st, WNOHANG) == 0) return 1;
        g_pid = -1;
    }
    char bin[1024];
    find_binary(bin, sizeof bin);
    if (!bin[0]){
        char dir[1024];
        exe_dir(dir, sizeof dir);
        printf("AssetRipper が見つかりません。\n");
        printf("https://github.com/AssetRipper/AssetRipper/releases の macOS 版を展開し、\n");
        printf("AssetRipper.GUI.Free を次のどちらかに置いてください。\n");
        printf("  %s/AssetRipper/AssetRipper.GUI.Free\n", dir);
        printf("  PATH 上の AssetRipper.GUI.Free\n");
        return 0;
    }
    if (access(bin, X_OK) != 0) chmod(bin, 0755);

    char dir[1024], logpath[1200];
    exe_dir(dir, sizeof dir);
    snprintf(logpath, sizeof logpath, "%s/AssetRipper_out", dir);
    wchar_t wlogdir[1200];
    utf8_to_wide(logpath, wlogdir, 1200);
    mkdirs(wlogdir);
    snprintf(logpath, sizeof logpath, "%s/AssetRipper_out/assetripper.log", dir);

    g_port = pick_port();
    char portbuf[16];
    snprintf(portbuf, sizeof portbuf, "%d", g_port);

    pid_t pid = fork();
    if (pid < 0){
        printf("AssetRipper の起動に失敗しました\n");
        return 0;
    }
    if (pid == 0){
        setsid();
        int fd = open(logpath, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd >= 0){
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            if (fd > 2) close(fd);
        }
        char *slash = strrchr(bin, '/');
        if (slash){
            *slash = 0;
            chdir(bin);
            *slash = '/';
        }
        execl(bin, bin, "--headless", "--port", portbuf, (char *)NULL);
        _exit(127);
    }
    g_pid = pid;
    if (!g_atexit){
        atexit(stop_atexit);
        g_atexit = 1;
    }

    char url[64];
    snprintf(url, sizeof url, "http://127.0.0.1:%d/", g_port);
    for (int i = 0; i < 90; i++){
        int st;
        if (waitpid(g_pid, &st, WNOHANG) == g_pid){
            g_pid = -1;
            printf("AssetRipper がすぐ終了しました。ログ: %s\n", logpath);
            return 0;
        }
        if (http_code(url, NULL, 2) > 0) return 1;
        sleep(1);
    }
    printf("AssetRipper の起動を確認できません。ログ: %s\n", logpath);
    assetripper_stop();
    return 0;
}

static int post_path(const char *route, const char *path, long timeout){
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    char *esc = curl_easy_escape(curl, path, 0);
    char post[8192];
    snprintf(post, sizeof post, "Path=%s&CreateSubfolder=false", esc ? esc : "");
    if (esc) curl_free(esc);
    curl_easy_cleanup(curl);
    char url[128];
    snprintf(url, sizeof url, "http://127.0.0.1:%d%s", g_port, route);
    return http_code(url, post, timeout);
}

int assetripper_export(const wchar_t *unity_path, const wchar_t *out_dir){
    if (!ensure_server()) return 0;
    char in[4096], out[4096];
    wide_to_utf8(unity_path, in, sizeof in);
    wide_to_utf8(out_dir, out, sizeof out);
    to_posix(in);
    to_posix(out);
    mkdirs(out_dir);

    int lc = post_path("/LoadFile", in, 600);
    if (lc < 200 || lc >= 400){
        printf("AssetRipper の読み込みに失敗しました (HTTP %d)\n", lc);
        post_path("/Reset", "", 30);
        return 0;
    }
    int ec = post_path("/Export/PrimaryContent", out, 900);
    post_path("/Reset", "", 60);
    if (ec < 200 || ec >= 400){
        printf("AssetRipper の書き出しに失敗しました (HTTP %d)\n", ec);
        return 0;
    }
    return 1;
}
