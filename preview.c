// preview.c: Spine のブラウザプレビューを開く（skel->json を自動で追加変換し、既定のブラウザで preview.html を開く）
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "util.h"
#include "spine_convert.h"
#include "texture_merge.h"
#include "preview.h"

typedef struct {
    wchar_t folder[1100];   /* キャラフォルダの絶対パス */
    char name[256];         /* キャラフォルダ名（UTF-8） */
    int skels;              /* そのフォルダ内の skel 数 */
} PrevCard;

static int count_skels(const wchar_t *live2d){
    wchar_t pat[1300];
    swprintf(pat, 1300, L"%ls\\*.skel*", live2d);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int n = 0;
    do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) n++; } while (FindNextFileW(h, &fd));
    FindClose(h);
    return n;
}

int open_spine_preview(void){
    wchar_t exedir[1024];
    GetModuleFileNameW(NULL, exedir, 1024);
    wchar_t *p = wcsrchr(exedir, L'\\');
    if (p) *p = 0;

    wchar_t html[1200];
    swprintf(html, 1200, L"%ls\\spine_preview\\preview.html", exedir);
    if (GetFileAttributesW(html) == INVALID_FILE_ATTRIBUTES){
        printf("spine_preview\\preview.html が見つかりません（プログラムと同じディレクトリに置いてください）。先にプレビューページを取得してください\n");
        return -1;
    }

    /* CGSS_DOWN 以下の全キャラの live2d ディレクトリを走査 */
    wchar_t wroot[1024];
    swprintf(wroot, 1024, L"%ls\\CGSS_DOWN", exedir);
    PrevCard cards[512];
    int n = 0;
    wchar_t pat[1300];
    swprintf(pat, 1300, L"%ls\\*", wroot);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h != INVALID_HANDLE_VALUE){
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (fd.cFileName[0] == L'.') continue;
            /* 新しいディレクトリを優先: カードイラストSpinaアニメ\spine または live2d\spine。旧ディレクトリ直置きも互換 */
            wchar_t live2d[1300];
            live2d[0] = 0;
            const wchar_t *cands[5];
            wchar_t a1[1300], a2[1300], a3[1300], a4[1300], a5[1300];
            swprintf(a1, 1300, L"%ls\\%ls\\カードイラストSpinaアニメ\\spine", wroot, fd.cFileName);
            swprintf(a2, 1300, L"%ls\\%ls\\live2d\\spine", wroot, fd.cFileName);
            swprintf(a3, 1300, L"%ls\\%ls\\live2d", wroot, fd.cFileName);
            swprintf(a4, 1300, L"%ls\\%ls\\カードイラストSpinaアニメ", wroot, fd.cFileName);
            swprintf(a5, 1300, L"%ls\\%ls\\spine", wroot, fd.cFileName);
            cands[0] = a1; cands[1] = a2; cands[2] = a3; cands[3] = a4; cands[4] = a5;
            int sk = 0;
            for (int c = 0; c < 5 && sk == 0; c++)
                sk = count_skels(cands[c]);
            /* skel があるディレクトリを確定（上のループは数だけ記録したので、ここで改めて決める） */
            for (int c = 0; c < 5; c++){
                if (count_skels(cands[c]) > 0){ wcscpy(live2d, cands[c]); break; }
            }
            if (sk > 0 && n < 512){
                swprintf(cards[n].folder, 1100, L"%ls", live2d);
                wide_to_utf8(fd.cFileName, cards[n].name, sizeof cards[n].name);
                cards[n].skels = sk;
                n++;
            }
        } while (FindNextFileW(h, &fd) && n < 512);
        FindClose(h);
    }

    if (n == 0){
        printf("CGSS_DOWN に Spine(live2d) リソースのあるキャラが見つかりません。先にカードイラストSpinaアニメをダウンロードしてアンパックしてください\n");
        return 0;
    }
    printf("Spine リソースのあるキャラ:\n");
    for (int i = 0; i < n; i++)
        printf("[%d] %s（%d 個の skel）\n", i + 1, cards[i].name, cards[i].skels);
    printf("プレビューするキャラを選択（数字は複数選択/カンマ区切り、a=すべて、0=戻る）:");
    char buf[256];
    if (fgets(buf, sizeof buf, stdin) == NULL) return 0;
    int *sel = (int*)malloc(sizeof(int) * n);
    if (!sel) return -1;
    int nsel = parse_multi(buf, sel, n);
    if (nsel < 0){
        nsel = 0;
        for (int i = 0; i < n; i++) sel[nsel++] = i + 1;
    }
    if (nsel == 0){ free(sel); return 0; }

    for (int s = 0; s < nsel; s++){
        PrevCard *c = &cards[sel[s] - 1];
        printf("処理 %s ...\n", c->name);
        int cn = convert_skels_in_dir(c->folder);
        printf("  %d 個の skel を json に変換済み\n", cn);
        int mn = merge_a8_textures_in_dir(c->folder);
        if (mn > 0)
            printf("  %d 枚のテクスチャを合成済み（3.8.75 エディタ用）\n", mn);
    }
    free(sel);

    HINSTANCE hr = ShellExecuteW(NULL, L"open", html, NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)hr <= 32){
        printf("ブラウザを開けませんでした（エラーコード %d）\n", (int)(INT_PTR)hr);
        return -1;
    }
    printf("\nブラウザで preview.html を開きました。ページ内で選択:\n");
    printf("  スケルトン: 上のキャラの spine ディレクトリ内のすべての .skel（ページが自動で JSON へ変換。.json も対応）\n");
    printf("  アトラス: SP3S301290_tex.atlas（対応するカードのファイル名に変更）\n");
    printf("  テクスチャ: 対応する tex.png と tex_A8.png（A8 はアルファチャンネル。合成後は黒縁なし）\n");
    printf("  Spine 3.8.75 エディタへ読み込む場合: *_v38.json + *_v38.atlas + *_merged.png を開く\n");
    printf("  SDキャラのヒント: 旧カードの SDキャラ が\"大頭\"になる場合は、スケルトンを SPSprachen_N.json（spine ルートディレクトリ内）に替えてください\n");
    return 0;
}
