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

// 3D ????
/* ================== 6. 3Dステージ ================== */

/* 楽曲 id でステージを検索（live_data.live_bg → 3d_stage_{live_bg}） */
static void querystage_by_music_id(sqlite3 *db, sqlite3 *rdb){
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
                "SELECT id, live_bg FROM live_data WHERE music_data_id=? ORDER BY id",
                -1, &lstmt, NULL) != SQLITE_OK) {
            fprintf(stderr, "SQLエラー: %s\n", sqlite3_errmsg(db));
            return;
        }
        sqlite3_bind_int(lstmt, 1, music_id);
        int n = 0;
        while (sqlite3_step(lstmt) == SQLITE_ROW) {
            int live_id = sqlite3_column_int(lstmt, 0);
            int live_bg = sqlite3_column_int(lstmt, 1);
            printf("live %d | ステージbg:%d\n", live_id, live_bg);
            char res[256];
            snprintf(res, sizeof res, "3d_stage_%d.unity3d", live_bg);
            printf("ステージ:%s\t", res); print_res_hash(rdb, res); printf("\n");
            snprintf(res, sizeof res, "3d_stage_%d_hq.unity3d", live_bg);
            printf("ステージHQ:%s\t", res); print_res_hash(rdb, res); printf("\n");
            n++;
        }
        sqlite3_finalize(lstmt);
        if (n == 0)
            fprintf(stderr, "該当する live がありません\n");
        return;
    }
}

/* ステージの第4階層メニュー：1.楽曲idを入力してステージを検索 2.戻る */
static void cardstage_id_menu(sqlite3 *db, sqlite3 *rdb){
    char buf[64];
    while (1) {
        printf("1.楽曲idを入力してステージを検索\t2.戻る\n");
        if (fgets(buf, sizeof buf, stdin) == NULL) return;
        int opt = atoi(buf);
        if (opt == 1) {
            querystage_by_music_id(db, rdb);
            return;
        }
        if (opt == 2) return;
        fprintf(stderr, "入力エラー\n");
    }
}

/* ステージ検索メニュー：項目を選ぶと自動でこのメニューに戻る。3 で第1階層メニューへ戻る */
int stage_Search(sqlite3 *db, sqlite3 *rdb){
    _setmode(_fileno(stdin), _O_BINARY);   // stdin をバイナリモードにし、改行は自前で処理
    char buf[128];
    while (1) {
        printf("入力 1.曲名\t2.楽曲id\t3.戻る\n");
        if (fgets(buf, sizeof buf, stdin) == NULL) return -1;
        int opt = atoi(buf);
        switch (opt) {
        case 1:
            queryaction_by_name(db);   // 曲名一覧を流用
            cardstage_id_menu(db, rdb);
            break;
        case 2:
            querystage_by_music_id(db, rdb);
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
/* 第1階層の検索メニュー */
