#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <locale.h>
#include <windows.h>
#include "unpack.h"
#include "preview.h"
#include "paper.h"
#include "browse.h"
#include "cg.h"
#include "auto_updata.h"
#include "util.h"
#include "conio.h"
#define Version 1.61
#define BUILD_VERIANT "db"

int main(void){
    if (!setlocale(LC_ALL, "")) setlocale(LC_ALL, "en_US.UTF-8");
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);   // コンソールを UTF-8 に固定し、入出力を統一
    enable_vt();
    console_cooked();
    /* master.mdb は実行ファイルと同じ場所を見る。起動ディレクトリが違っても届くようにする */
    {
        wchar_t exe[1024];
        GetModuleFileNameW(NULL, exe, 1024);
        wchar_t *slash = wcsrchr(exe, L'\\');
        if (slash) *slash = 0;
        char dir[1024];
        wide_to_utf8(exe, dir, sizeof dir);
        for (char *p = dir; *p; p++) if (*p == '\\') *p = '/';
        if (dir[0]) chdir(dir);
    }
    int rc = updata_main(Version);
    if(rc == -1){
        
        printf("アップデートに失敗しました。プログラムを閉じて再試行してください\n");
        fflush(stdout);
        /* macOS: no pause */
    }
    if(rc == 2){
    /* アップデートスクリプトはすでにバックグラウンド／新しいウィンドウで実行中。本体はすぐに終了する */
    printf("アップデート中です。まもなく終了します...\n");
    fflush(stdout);
    return 0;
    }
                       // ANSIエスケープを有効化(画面クリア/反転)。でないと画面が文字化けする
    /* 検索とダウンロードは1つのモジュール(browse.c)に統合済み。メインメニューの入口は1つだけ */
    def menu[] = {
        {"1.リソース検索とダウンロード", browse_main, 0},
        {"2.アンパック", unpack_main, 0},
        {"3.Spineプレビューを開く(beta)", open_spine_preview, 0},
        {"4.USM/CGアンパック", unpack_usm, 0},
        {"5.終了", NULL, 0},
        {"END", NULL, 0}            /* 番兵は必ず最終行 */
    };

    while(1){
        int rc = pager_pick("メインメニュー", menu, 0);
        if(rc == -1)                /* Esc: メニュー表示を続ける */
            continue;
        if(rc == 4)                 /* "5.終了" は第4項(インデックスは0始まり) */
            break;
        /* 選択項目の func は pager 内で呼び出し済み。ここではそのままループ */
    }

    fflush(stdout);
    return 0;
}
