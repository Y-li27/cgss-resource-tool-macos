// lookup ??????
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>
#include <windows.h>
#include "data.h"
#include "sqlite3.h"
#include "lookup_table.h"
#include "GBKswapUTF8.h"
#include "util.h"
#include "paper.h"

#define DB_PATH "master.mdb"

// ?????
int lookup_main(void){
    sqlite3 *db = NULL;    // ゲームのメインDB
    sqlite3 *rdb = NULL;   // リソースマニフェストDB
    const char *mp = find_manifest();
    if (GetFileAttributesA(DB_PATH) == INVALID_FILE_ATTRIBUTES){
        fprintf(stderr, "master.mdb がありません。プログラムと同じディレクトリに置いてください\n");
        return -1;
    }
    if (!mp){
        fprintf(stderr, "manifest_*.db（リソースマニフェストDB）がありません。先に check_update.exe を実行して取得してください\n");
        return -1;
    }
    if (sqlite3_open(DB_PATH, &db) != SQLITE_OK) {
        fprintf(stderr, "master.mdb を開けませんでした（%s）\n", sqlite3_errmsg(db));
        return -1;
    }
    if (sqlite3_open(mp, &rdb) != SQLITE_OK) {
        fprintf(stderr, "%s を開けませんでした（%s）\n", mp, sqlite3_errmsg(rdb));
        sqlite3_close(db);
        return -1;
    }
    char buf[128];
    dbdef menu[]={
        {"1.3Dモデル",td_Search,0},
        {"2.2DSpineSDキャラ(beta)",spina_Search,0},
        {"3.楽曲",song_Search,0},
        {"4.楽曲モーション",action_Search,0},
        {"5.譜面",chart_Search,0},
        {"6.3Dステージ",stage_Search,0},
        {"7.カードイラスト",cardimg_Search,0},
        {"8.キャラボイス（テキスト付き）",voice_Search,0},
        {"9.BGM(beta)",NULL,0},
        {"10.CG(beta)",NULL,0},
        {"11.終了",NULL,0},
        {"END",NULL,0}          /* 番兵は必須。pager はこれで項目数を数える */
    };
    while (1) {
        int rc =pager_picks("検索",menu,db,rdb,0);
        if(rc == -1)
            continue;
        else if(rc == 10)       /* "11.終了" は 0 始まりでインデックス 10 */
            break;
    }
    sqlite3_close(rdb);
    sqlite3_close(db);
    return 0;
}

