// unpack_res.c: キャラリソースのアンパック（カードイラスト/背景/カードイラストSpinaアニメ/3Dフォト/spine -> png/データ）
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <windows.h>
#include "util.h"
#include "unpack.h"
#include "assetripper.h"
#include "spine_convert.h"

#define MAX_RES_ITEMS 1024

typedef struct {
    wchar_t path[1100];
    wchar_t dir[1100];
    char name[256];
    char folder_name[256];
    char sub[64];
    int done;
    int spine_sub;   /* カードイラストSpinaアニメ/live2d: 独立した spine サブフォルダへアンパック */
} ResUnpackItem;

static void scan_res_dir(const wchar_t *chara_dir, const wchar_t *sub, const char *sub_u8,
                         ResUnpackItem *items, int *n, const char *folder_name, int spine_sub){
    wchar_t pat[1300];
    swprintf(pat, 1300, L"%ls\\%ls\\*.unity3d", chara_dir, sub);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (*n >= MAX_RES_ITEMS) break;
        swprintf(items[*n].dir, 1100, L"%ls\\%ls", chara_dir, sub);
        swprintf(items[*n].path, 1100, L"%ls\\%ls\\%ls", chara_dir, sub, fd.cFileName);
        wide_to_utf8(fd.cFileName, items[*n].name, sizeof items[*n].name);
        snprintf(items[*n].folder_name, sizeof items[*n].folder_name, "%s", folder_name);
        snprintf(items[*n].sub, sizeof items[*n].sub, "%s", sub_u8);
        items[*n].spine_sub = spine_sub;
        wchar_t marker[1300];
        swprintf(marker, 1300, L"%ls\\%ls\\%ls.done", chara_dir, sub, fd.cFileName);
        items[*n].done = (GetFileAttributesW(marker) != INVALID_FILE_ATTRIBUTES);
        (*n)++;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

/* 共有スケルトンに同梱のテンプレートスキン（卯月/杏の例）をサブフォルダへ移し、カード自身のSDキャラと混ざらないようにする */
static void move_shared_template_samples(const wchar_t *dir){
    /* 移動するファイル（共有スケルトン SPSprachen_N/s のテクスチャとアトラス。スケルトン本体は残す） */
    static const wchar_t *samples[] = {
        L"SPSprachen_N.png", L"SPSprachen_N.atlas", L"SPSprachen_N.atlas.asset",
        L"SPSprachen_N.atlas.atlas", L"SPSprachen_N_Atlas.json", L"SPSprachen_N_SkeletonData.json",
        L"SPSprachen_N.skel", L"SPSprachen_N.skel.asset", L"SPSprachen_N.json", L"SPSprachen_N_v38.json",
        L"SPSprachen_s.png", L"SPSprachen_s.atlas", L"SPSprachen_s.atlas.asset",
        L"SPSprachen_s.atlas.atlas", L"SPSprachen_s_Atlas.json", L"SPSprachen_s_SkeletonData.json",
    };
    wchar_t subdir[1300];
    swprintf(subdir, 1300, L"%ls\\テンプレート例(卯月杏)", dir);
    mkdirs(subdir);
    int moved = 0;
    for (int i = 0; i < (int)(sizeof samples / sizeof samples[0]); i++){
        wchar_t src[1300], dst[1300];
        swprintf(src, 1300, L"%ls\\%ls", dir, samples[i]);
        if (GetFileAttributesW(src) == INVALID_FILE_ATTRIBUTES) continue;
        swprintf(dst, 1300, L"%ls\\%ls", subdir, samples[i]);
        if (MoveFileW(src, dst)) moved++;
    }
    /* N スケルトンは旧カードのSDキャラ用（旧カードで s を使うと「大頭」になる）。spine ルートにも1部残して使いやすくする */
    static const wchar_t *keepN[] = {
        L"SPSprachen_N.skel", L"SPSprachen_N.skel.asset",
        L"SPSprachen_N.json", L"SPSprachen_N_v38.json"
    };
    for (int i = 0; i < (int)(sizeof keepN / sizeof keepN[0]); i++){
        wchar_t src[1300], dst[1300];
        swprintf(src, 1300, L"%ls\\%ls", subdir, keepN[i]);
        swprintf(dst, 1300, L"%ls\\%ls", dir, keepN[i]);
        if (GetFileAttributesW(src) != INVALID_FILE_ATTRIBUTES &&
            GetFileAttributesW(dst) == INVALID_FILE_ATTRIBUTES)
            CopyFileW(src, dst, FALSE);
    }
    if (moved > 0)
        printf("  共有スケルトンのテンプレート例(卯月/杏)を テンプレート例(卯月杏)\\ サブフォルダへ移しました\n");
}

/* キャラリソースを1つアンパックし、png/データファイルを元のディレクトリへ書き出す */

static int extract_res_one(const ResUnpackItem *it, int idx){
    wchar_t exedir[1024], outdir[1200];
    GetModuleFileNameW(NULL, exedir, 1024);
    wchar_t *p = wcsrchr(exedir, L'\\');
    if (p) *p = 0;
    swprintf(outdir, 1200, L"%ls\\AssetStudio_out\\r%03d", exedir, idx);
    wipe_dir(outdir);
    mkdirs(outdir);

    printf("アンパック %s\\%s ...\n", it->sub, it->name);
    if (!assetripper_export(it->path, outdir))
        return 0;

    int n = 0;
    /* 書き出し先: spine パックは探しやすいよう spine サブフォルダに分ける */
    wchar_t destdir[1300];
    wcscpy(destdir, it->dir);
    if (it->spine_sub){
        swprintf(destdir, 1300, L"%ls\\spine", it->dir);
        mkdirs(destdir);
    }
    int spine = it->spine_sub || strcmp(it->sub, "spine") == 0;
    n += assetripper_collect(outdir, destdir, spine ? RIP_KEEP_SPINE : RIP_KEEP_IMAGE);
    wipe_dir(outdir);
    if (spine){
        int cn = convert_skels_in_dir(destdir);
        if (cn > 0)
            printf("  skel %d 個を json に変換（プレビューのボーン用）\n", cn);
    }

    /* 共有スケルトン同梱のテンプレートスキン（卯月/杏の例）をサブフォルダへ移す */
    if (!it->spine_sub && strcmp(it->sub, "spine") == 0)
        move_shared_template_samples(destdir);

    wchar_t wname[256], marker[1300];
    utf8_to_wide(it->name, wname, 256);
    swprintf(marker, 1300, L"%ls\\%ls.done", it->dir, wname);
    FILE *mf = _wfopen(marker, L"wb");
    if (mf){ fputs("done", mf); fclose(mf); }

    if (n == 0) printf("  コピーできるファイルがありません（パックに画像/データがない可能性）\n");
    printf("  完了。%d 個のファイルを書き出し\n", n);
    return n;
}

/* ダウンロード側のフォルダ名に合わせる。Spine / Live2D / Spine_Live も対象 */
static int res_folder_kind(const char *name){
    if (strcmp(name, "カードイラスト") == 0 || strcmp(name, "背景") == 0 ||
        strcmp(name, "3Dフォト") == 0)
        return 1;
    if (strcasecmp(name, "spine") == 0)
        return 2;
    if (strcasecmp(name, "live2d") == 0 || strcasecmp(name, "spine_live") == 0 ||
        strcmp(name, "カードイラストSpinaアニメ") == 0)
        return 3;
    return 0;
}

int unpack_resources_main(void){
    wchar_t wroot[1024];
    GetModuleFileNameW(NULL, wroot, 1024);
    wchar_t *p = wcsrchr(wroot, L'\\');
    if (p) *p = 0;
    wcscat(wroot, L"\\CGSS_DOWN");

    ResUnpackItem *items = (ResUnpackItem*)malloc(sizeof(ResUnpackItem) * MAX_RES_ITEMS);
    if (!items){ fprintf(stderr, "メモリ不足\n"); return 1; }
    int n = 0;

    wchar_t pat[1300];
    swprintf(pat, 1300, L"%ls\\*", wroot);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h != INVALID_HANDLE_VALUE){
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (fd.cFileName[0] == L'.') continue;
            char chara_name[256];
            wide_to_utf8(fd.cFileName, chara_name, sizeof chara_name);
            wchar_t chara_dir[1300];
            swprintf(chara_dir, 1300, L"%ls\\%ls", wroot, fd.cFileName);
            wchar_t spat[1300];
            swprintf(spat, 1300, L"%ls\\*", chara_dir);
            WIN32_FIND_DATAW sd;
            HANDLE hs = FindFirstFileW(spat, &sd);
            if (hs == INVALID_HANDLE_VALUE) continue;
            do {
                if (!(sd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                if (sd.cFileName[0] == L'.') continue;
                char subname[256];
                wide_to_utf8(sd.cFileName, subname, sizeof subname);
                int kind = res_folder_kind(subname);
                if (!kind) continue;
                const char *sub_u8 = (kind == 2) ? "spine" : (kind == 3) ? "live2d" : subname;
                scan_res_dir(chara_dir, sd.cFileName, sub_u8, items, &n, chara_name, kind == 3);
            } while (FindNextFileW(hs, &sd) && n < MAX_RES_ITEMS);
            FindClose(hs);
        } while (FindNextFileW(h, &fd) && n < MAX_RES_ITEMS);
        FindClose(h);
    }

    if (n == 0){
        printf("CGSS_DOWN の Spine / Live2D / カードイラスト / 背景 / 3Dフォト に unity3d がありません\n");
        printf("Enterで戻る\n");
        fflush(stdout);
        char line[16];
        if (fgets(line, sizeof line, stdin) == NULL) { /* 画面が消える前に読めるようにする */ }
        free(items);
        return 1;
    }

    int ndone = 0;
    for (int i = 0; i < n; i++){
        printf("[%d] %s : %s\\%s %s\n", i + 1, items[i].folder_name, items[i].sub, items[i].name,
               items[i].done ? "[アンパック済]" : "[未アンパック]");
        if (items[i].done) ndone++;
    }
    printf("リソースパック %d 個、アンパック済 %d 個（a=未処理をすべて、番号で強制再アンパック、0=戻る）: ", n, ndone);
    char buf[128];
    if (fgets(buf, sizeof buf, stdin) == NULL){ free(items); return 1; }
    int *sel = (int*)malloc(sizeof(int) * n);
    if (!sel){ free(items); return 1; }
    int nsel = parse_multi(buf, sel, n);
    if (nsel < 0){
        nsel = 0;
        for (int i = 0; i < n; i++)
            if (!items[i].done) sel[nsel++] = i + 1;
        if (nsel == 0){ printf("すべてアンパック済みです。処理は不要です\n"); free(sel); free(items); return 0; }
    }
    if (nsel == 0){ free(sel); free(items); return 1; }

    for (int s = 0; s < nsel; s++){
        extract_res_one(&items[sel[s] - 1], s);
    }
    printf("すべて完了\n");
    printf("Enterで戻る\n");
    fflush(stdout);
    if (fgets(buf, sizeof buf, stdin) == NULL) { /* 結果を読めるように待つ */ }
    assetripper_stop();
    free(sel);
    free(items);
    return 0;
}
