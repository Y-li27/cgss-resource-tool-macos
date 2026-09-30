#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "util.h"

// util.c: 共通ツール

void utf8_to_wide(const char *in, wchar_t *out, int n){
    MultiByteToWideChar(CP_UTF8, 0, in, -1, out, n);
}


void wide_to_utf8(const wchar_t *in, char *out, int n){
    WideCharToMultiByte(CP_UTF8, 0, in, -1, out, n, NULL, NULL);
}

void wpath_join(wchar_t *out, int n, const wchar_t *a, const wchar_t *b){
    if (!out || n <= 0) return;
    if (!a) a = L"";
    if (!b) b = L"";
    int i = 0;
    for (; *a && i < n - 1; a++) out[i++] = *a;
    while (i > 0 && (out[i - 1] == L'/' || out[i - 1] == L'\\')) i--;
    if (*b == L'/' || *b == L'\\') b++;
    if (i > 0 && *b && i < n - 1) out[i++] = L'/';
    for (; *b && i < n - 1; b++) out[i++] = *b;
    out[i] = 0;
}

/* 現在の本体プログラムのディレクトリを取得 */
void get_dl_root(wchar_t *buf, int n){
    GetModuleFileNameW(NULL, buf, n);
    wchar_t *p = wcsrchr(buf, L'\\');
    wchar_t *q = wcsrchr(buf, L'/');
    if (q && (!p || q > p)) p = q;
    if (p) *p = 0;
    wcscat(buf, L"/CGSS_DOWN");
}

/* ディレクトリを再帰的に作成 */

void mkdirs(const wchar_t *path){
    wchar_t tmp[1024];
    wcscpy(tmp, path);
    wchar_t *start = tmp;
    if (tmp[0] && tmp[1] == L':') start = tmp + 2;
    if (*start == L'\\' || *start == L'/') start++;
    for (wchar_t *p = start; *p; p++){
        if (*p == L'\\' || *p == L'/'){
            wchar_t save = *p;
            *p = 0;
            if (!CreateDirectoryW(tmp, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
                printf("ディレクトリ作成失敗 %ls err=%lu\n", tmp, (unsigned long)GetLastError());
            *p = save;
        }
    }
    if (!CreateDirectoryW(tmp, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        printf("ディレクトリ作成失敗 %ls err=%lu\n", tmp, (unsigned long)GetLastError());
}

/* リソース名からディレクトリ部分を除く（l/song_1.acb -> song_1.acb） */
const char *base_name(const char *name){
    const char *p = strrchr(name, '/');
    return p ? p + 1 : name;
}

/* 現在のディレクトリの manifest_*.db を走査し、バージョン最大のファイル名を返す（静的バッファ、NULL=なし） */

const char *find_manifest(void){
    static char path[260];
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA("manifest_*.db", &fd);
    long long best = -1;
    if (h == INVALID_HANDLE_VALUE) return NULL;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        long long v = 0;
        if (sscanf(fd.cFileName, "manifest_%lld.db", &v) == 1 && v > best){
            best = v;
            snprintf(path, sizeof path, "%s", fd.cFileName);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return best > 0 ? path : NULL;
}

/* ================== LZ4 ブロック展開（cgss_lz4.py から移植） ================== */


int parse_multi(const char *line, int *sel, int max){
    int n = 0;
    const char *p = line;
    while (*p){
        if (*p == 'a' || *p == 'A') return -1;
        if (*p >= '0' && *p <= '9'){
            int v = 0;
            while (*p >= '0' && *p <= '9'){ v = v * 10 + (*p - '0'); p++; }
            if (v > 0 && v <= max && n < 64) sel[n++] = v;
        } else p++;
    }
    if(n == 0) fprintf(stderr,"無効な入力\n");
    return n;
}


int selected(const int *sel, int n, int v){
    for (int i = 0; i < n; i++)
        if (sel[i] == v) return 1;
    return 0;
}

/* ResItem 一覧をキャラディレクトリ配下の各サブディレクトリへダウンロード */
