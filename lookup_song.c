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
/* ================== 3. 楽曲 ================== */

/* 楽曲 id で1曲検索し、リソースを表示 */
static void querysong_by_music_id(sqlite3 *db, sqlite3 *rdb){
    char buf[64];
    while (1) {
        printf("idを入力してください\n");
        if (fgets(buf, sizeof buf, stdin) == NULL) return;
        int music_id = atoi(buf);
        if (music_id <= 0) {
            fprintf(stderr, "入力エラー\n");
            continue;
        }
        sqlite3_stmt *stmt = NULL;
        if (sqlite3_prepare_v2(db,
                "SELECT id,name,bpm,composer,lyricist FROM music_data WHERE id=?",
                -1, &stmt, NULL) != SQLITE_OK) {
            fprintf(stderr, "SQLエラー: %s\n", sqlite3_errmsg(db));
            continue;
        }
        sqlite3_bind_int(stmt, 1, music_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            printf("%d|%s\n", sqlite3_column_int(stmt, 0), sqlite3_column_text(stmt, 1));
            printf("BPM:%d\t作曲:%s\t作詞:%s\n",
                   sqlite3_column_int(stmt, 2),
                   sqlite3_column_text(stmt, 3),
                   sqlite3_column_text(stmt, 4));
            /* 配信停止状態の確認 */
            sqlite3_stmt *estmt = NULL;
            if (sqlite3_prepare_v2(db,
                    "SELECT COUNT(*) FROM music_data_exclude WHERE music_data_id=? AND datetime('now') BETWEEN start_date AND end_date",
                    -1, &estmt, NULL) == SQLITE_OK) {
                sqlite3_bind_int(estmt, 1, music_id);
                if (sqlite3_step(estmt) == SQLITE_ROW && sqlite3_column_int(estmt, 0) > 0) {
                    printf("（この楽曲は現在配信停止中です）\n");
                }
                sqlite3_finalize(estmt);
            }
            char res[256];
            snprintf(res, sizeof res, "l/song_%d.acb", sqlite3_column_int(stmt, 0));
            printf("楽曲:%s\t", res); print_res_hash(rdb, res); printf("\n");
        } else {
            fprintf(stderr, "該当する楽曲がありません\n");
        }
        sqlite3_finalize(stmt);
        return;
    }
}

/* 楽曲の第4階層メニュー：1.idを入力して楽曲リソースを検索 2.戻る */
static void cardsong_id_menu(sqlite3 *db, sqlite3 *rdb){
    char buf[64];
    while (1) {
        printf("1.idを入力して楽曲リソースを検索\t2.戻る\n");
        if (fgets(buf, sizeof buf, stdin) == NULL) return;
        int opt = atoi(buf);
        if (opt == 1) {
            querysong_by_music_id(db, rdb);
            return;
        }
        if (opt == 2) return;
        fprintf(stderr, "入力エラー\n");
    }
}

/* 曲名の部分一致で検索して一覧表示 */
static void querysong_by_name(sqlite3 *db){
    char buf[128];
    while(1){
        printf("曲名を入力してください：\n");
        if (fgets(buf, sizeof buf, stdin) == NULL) return;
        buf[strcspn(buf, "\r\n")] = 0;
        char name_utf8[128];
        gbk_to_utf8(buf, name_utf8, sizeof name_utf8);
        char like[256];
        snprintf(like, sizeof like, "%%%s%%", name_utf8);

        sqlite3_stmt *stmt = NULL;
        if (sqlite3_prepare_v2(db,
                "SELECT id,name,bpm,composer FROM music_data WHERE name LIKE ? ORDER BY id",
                -1, &stmt, NULL) != SQLITE_OK) {
            fprintf(stderr, "SQLエラー:%s\n", sqlite3_errmsg(db));
            return;
        }
        sqlite3_bind_text(stmt, 1, like, -1, SQLITE_TRANSIENT);
        int n = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            printf("| id = %d | name = %s | BPM = %d | 作曲 = %s |\n",
                   sqlite3_column_int(stmt, 0),
                   sqlite3_column_text(stmt, 1),
                   sqlite3_column_int(stmt, 2),
                   sqlite3_column_text(stmt, 3));
            n++;
        }
        sqlite3_finalize(stmt);
        if (n == 0)
            fprintf(stderr, "該当する楽曲が見つかりません\n");
        else{
            break;
        }
    }
}

/* 楽曲検索メニュー：項目を選ぶと自動でこのメニューに戻る。3 で第1階層メニューへ戻る */
int song_Search(sqlite3 *db, sqlite3 *rdb){
    _setmode(_fileno(stdin), _O_BINARY);   // stdin をバイナリモードにし、改行は自前で処理
    char buf[128];
    while (1) {
        printf("入力 1.曲名\t2.楽曲id\t3.戻る\n");
        if (fgets(buf, sizeof buf, stdin) == NULL) return -1;
        int opt = atoi(buf);
        switch (opt) {
        case 1:
            querysong_by_name(db);
            cardsong_id_menu(db, rdb);
            break;
        case 2:
            querysong_by_music_id(db, rdb);
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

