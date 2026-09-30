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

// ????
/* ================== 5. 譜面 ================== */

/* 楽曲 id で譜面を検索（live_data → musicscores_m{live_id}.bdb） */
static void querychart_by_music_id(sqlite3 *db, sqlite3 *rdb){
    char buf[64];
    while (1) {
        printf("楽曲idを入力してください\n");
        if (fgets(buf, sizeof buf, stdin) == NULL) return;
        int music_id = atoi(buf);
        if (music_id <= 0) {
            fprintf(stderr, "入力エラー\n");
            continue;
        }
        sqlite3_stmt *stmt = NULL;
        if (sqlite3_prepare_v2(db,
                "SELECT id,name FROM music_data WHERE id=?",
                -1, &stmt, NULL) != SQLITE_OK) {
            fprintf(stderr, "SQLエラー: %s\n", sqlite3_errmsg(db));
            continue;
        }
        sqlite3_bind_int(stmt, 1, music_id);
        if (sqlite3_step(stmt) != SQLITE_ROW) {
            fprintf(stderr, "該当する楽曲がありません\n");
            sqlite3_finalize(stmt);
            return;
        }
        printf("%d|%s\n", sqlite3_column_int(stmt, 0), sqlite3_column_text(stmt, 1));
        sqlite3_finalize(stmt);

        sqlite3_stmt *lstmt = NULL;
        if (sqlite3_prepare_v2(db,
                "SELECT id,difficulty_1,difficulty_2,difficulty_3,difficulty_4 FROM live_data WHERE music_data_id=? ORDER BY id",
                -1, &lstmt, NULL) != SQLITE_OK) {
            fprintf(stderr, "SQLエラー: %s\n", sqlite3_errmsg(db));
            return;
        }
        sqlite3_bind_int(lstmt, 1, music_id);
        int n = 0;
        while (sqlite3_step(lstmt) == SQLITE_ROW) {
            int live_id = sqlite3_column_int(lstmt, 0);
            printf("live %d | diff:%d/%d/%d/%d\n", live_id,
                   sqlite3_column_int(lstmt, 1),
                   sqlite3_column_int(lstmt, 2),
                   sqlite3_column_int(lstmt, 3),
                   sqlite3_column_int(lstmt, 4));
            if (sqlite3_column_int(lstmt, 1) == 0) {
                printf("（MV/イベントバリアント、譜面なし）\n");
                continue;
            }
            char res[256];
            snprintf(res, sizeof res, "musicscores_m%d.bdb", live_id);
            printf("譜面:%s\t", res); print_res_hash(rdb, res); printf("\n");
            n++;
        }
        sqlite3_finalize(lstmt);
        if (n == 0)
            fprintf(stderr, "該当する live がありません\n");
        return;
    }
}

/* 譜面の第4階層メニュー：1.楽曲idを入力して譜面を検索 2.戻る */
static void cardchart_id_menu(sqlite3 *db, sqlite3 *rdb){
    char buf[64];
    while (1) {
        printf("1.楽曲idを入力して譜面を検索\t2.戻る\n");
        if (fgets(buf, sizeof buf, stdin) == NULL) return;
        int opt = atoi(buf);
        if (opt == 1) {
            querychart_by_music_id(db, rdb);
            return;
        }
        if (opt == 2) return;
        fprintf(stderr, "入力エラー\n");
    }
}

/* 譜面検索メニュー：項目を選ぶと自動でこのメニューに戻る。3 で第1階層メニューへ戻る */
int chart_Search(sqlite3 *db, sqlite3 *rdb){
    _setmode(_fileno(stdin), _O_BINARY);   // stdin をバイナリモードにし、改行は自前で処理
    char buf[128];
    while (1) {
        printf("入力 1.曲名\t2.楽曲id\t3.戻る\n");
        if (fgets(buf, sizeof buf, stdin) == NULL) return -1;
        int opt = atoi(buf);
        switch (opt) {
        case 1:
            queryaction_by_name(db);   // 曲名一覧を流用
            cardchart_id_menu(db, rdb);
            break;
        case 2:
            querychart_by_music_id(db, rdb);
            break;
        case 3:
            printf("戻っています...\n");
            return 1;   // lookup_main に第1階層メニューへ戻るよう伝える
        default:
            fprintf(stderr, "入力エラー\n");
            break;
        }
    }
}

