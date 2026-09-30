// unpack_fbx.c: モデルを FBX にアンパック（AssetStudio.CLI を呼び出し）
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "util.h"
#include "unpack.h"
#include "assetripper.h"

#define MAX_FBX_ITEMS 256
typedef struct {
    wchar_t path[1100];
    wchar_t dir[1100];
    char name[256];
    char folder_name[256];
    int done;
} FbxItem;

static void scan_fbx_dir(const wchar_t *dir, FbxItem *items, int *n, const char *folder_name){
    wchar_t pat[1300];
    swprintf(pat, 1300, L"%ls\\*.unity3d", dir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (*n >= MAX_FBX_ITEMS) break;
        swprintf(items[*n].dir, 1100, L"%ls", dir);
        swprintf(items[*n].path, 1100, L"%ls\\%ls", dir, fd.cFileName);
        wide_to_utf8(fd.cFileName, items[*n].name, sizeof items[*n].name);
        snprintf(items[*n].folder_name, sizeof items[*n].folder_name, "%s", folder_name);
        items[*n].done = is_done(dir, fd.cFileName);
        (*n)++;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

/* outdir\sub\*.ext を dest にコピー */

static int extract_one(const FbxItem *it, int idx){
    wchar_t exedir[1024], outdir[1200];
    GetModuleFileNameW(NULL, exedir, 1024);
    wchar_t *p = wcsrchr(exedir, L'\\');
    if (p) *p = 0;
    /* 出力ディレクトリは ASCII 必須。日本語パスを .NET CLI に渡すと失敗する */
    swprintf(outdir, 1200, L"%ls\\AssetStudio_out\\p%03d", exedir, idx);
    wipe_dir(outdir);
    mkdirs(outdir);

    printf("アンパック %s ...\n", it->name);
    if (!assetripper_export(it->path, outdir))
        return 0;

    int n = assetripper_collect(outdir, it->dir, RIP_KEEP_MODEL);
    wipe_dir(outdir);
    printf("  AssetRipper 無償版は FBX ではなく glb を書き出します\n");

    /* アンパック済みマーカーを書く */
    wchar_t wname[256], marker[1300];
    utf8_to_wide(it->name, wname, 256);
    swprintf(marker, 1300, L"%ls\\%ls.done", it->dir, wname);
    FILE *mf = _wfopen(marker, L"wb");
    if (mf){ fputs("done", mf); fclose(mf); }

    if (n == 0) printf("  コピーできるファイルがありません（パックにメッシュ/テクスチャ/アニメーションがない可能性）\n");
    printf("  完了。%d 個のファイルをコピー\n", n);
    return n;
}


int unpack_fbx_main(void){
    /* CGSS_DOWN\*\3dモデル\*.unity3d を走査 */
    wchar_t wroot[1024];
    GetModuleFileNameW(NULL, wroot, 1024);
    wchar_t *p = wcsrchr(wroot, L'\\');
    if (p) *p = 0;
    wcscat(wroot, L"\\CGSS_DOWN");  // wroot を X:XXX\XXX\CGSS_DOWN にする

    FbxItem items[MAX_FBX_ITEMS];
    int n = 0;
    wchar_t pat[1300];
    swprintf(pat, 1300, L"%ls\\*", wroot);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h != INVALID_HANDLE_VALUE){
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (fd.cFileName[0] == L'.') continue;
            char chara_name[512];
            wide_to_utf8(fd.cFileName, chara_name, sizeof chara_name);
            wchar_t mdir[1300];
            swprintf(mdir, 1300, L"%ls\\%ls\\3Dモデル", wroot, fd.cFileName);
            if (GetFileAttributesW(mdir) == INVALID_FILE_ATTRIBUTES)
                swprintf(mdir, 1300, L"%ls\\%ls\\3dモデル", wroot, fd.cFileName);
            if (GetFileAttributesW(mdir) == INVALID_FILE_ATTRIBUTES) continue;
            scan_fbx_dir(mdir, items, &n, chara_name);
        } while (FindNextFileW(h, &fd) && n < MAX_FBX_ITEMS);
        FindClose(h);
    }

    if (n == 0){
        printf("CGSS_DOWN に 3dモデル フォルダがありません。手動でパスを入力します\n");
        char path[1024];
        printf(".unity3d ファイルのパスを入力: ");
        if (fgets(path, sizeof path, stdin) == NULL) return 1;
        path[strcspn(path, "\r\n")] = 0;
        if (!path[0]) return 1;
        FbxItem it;
        memset(&it, 0, sizeof it);
        utf8_to_wide(path, it.path, 1100);
        wcscpy(it.dir, it.path);
        wchar_t *ws = wcsrchr(it.dir, L'\\');
        if (ws) *ws = 0;
        const char *bn = strrchr(path, '\\');
        snprintf(it.name, sizeof it.name, "%s", bn ? bn + 1 : path);
        snprintf(it.folder_name, sizeof it.folder_name, "手動入力");
        print_gui_guide(it.dir);
        extract_one(&it, 0);
        assetripper_stop();
        return 0;
    }

    print_gui_guide(items[0].dir);
    int ndone = 0;
    for (int i = 0; i < n; i++){
        printf("[%d] %s : %s %s\n", i + 1, items[i].folder_name, items[i].name,
               items[i].done ? "[アンパック済]" : "[未アンパック]");
        if (items[i].done) ndone++;
    }
    printf("モデルパック %d 個、アンパック済 %d 個（a=未処理をすべて、番号で強制再アンパック、0=戻る）: ", n, ndone);
    char buf[128];
    if (fgets(buf, sizeof buf, stdin) == NULL) return 1;
    int sel[MAX_FBX_ITEMS], nsel = parse_multi(buf, sel, n);
    if (nsel < 0){
        nsel = 0;
        for (int i = 0; i < n; i++)
            if (!items[i].done) sel[nsel++] = i + 1;
        if (nsel == 0){ printf("すべてアンパック済みです。処理は不要です\n"); return 0; }
    }
    if (nsel == 0) return 1;

    for (int s = 0; s < nsel; s++){
        extract_one(&items[sel[s] - 1], s);
    }
    printf("すべて完了\n");
    assetripper_stop();
    return 0;
}

