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

// ??????
/* ================== 8. キャラボイス ================== */

/* カード id でボイスリソースを検索（v/card_{カードid}.acb） */
static void queryvoice_by_card_id(sqlite3 *db, sqlite3 *rdb){
    char buf[64];
    while (1) {
        printf("idを入力してください\n");
        if (fgets(buf, sizeof buf, stdin) == NULL) return;
        int card_id = atoi(buf);
        if (card_id <= 0) {
            fprintf(stderr, "入力エラー\n");
            continue;
        }
        sqlite3_stmt *stmt = NULL;
        if (sqlite3_prepare_v2(db,
                "SELECT id,name FROM card_data WHERE id=?",
                -1, &stmt, NULL) != SQLITE_OK) {
            fprintf(stderr, "SQLエラー: %s\n", sqlite3_errmsg(db));
            continue;
        }
        sqlite3_bind_int(stmt, 1, card_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            printf("%d|%s\n", sqlite3_column_int(stmt, 0), sqlite3_column_text(stmt, 1));
            char res[256];
            snprintf(res, sizeof res, "v/card_%d.acb", card_id);
            printf("ボイス:%s\t", res); print_res_hash(rdb, res); printf("\n");
        } else {
            fprintf(stderr, "該当するカードがありません\n");
        }
        sqlite3_finalize(stmt);
        return;
    }
}

/* ボイスの第4階層メニュー：1.idを入力してボイスを検索 2.戻る */
static void cardvoice_id_menu(sqlite3 *db, sqlite3 *rdb){
    char buf[64];
    while (1) {
        printf("1.idを入力してボイスを検索\t2.戻る\n");
        if (fgets(buf, sizeof buf, stdin) == NULL) return;
        int opt = atoi(buf);
        if (opt == 1) {
            queryvoice_by_card_id(db, rdb);
            return;
        }
        if (opt == 2) return;
        fprintf(stderr, "入力エラー\n");
    }
}

/* ボイス検索メニュー：項目を選ぶと自動でこのメニューに戻る。3 で第1階層メニューへ戻る */
int voice_Search(sqlite3 *db, sqlite3 *rdb){
    _setmode(_fileno(stdin), _O_BINARY);   // stdin をバイナリモードにし、改行は自前で処理
    char buf[128];
    while (1) {
        printf("入力 1.カード名\t2.キャラid（chara_id）\t3.戻る\n");
        if (fgets(buf, sizeof buf, stdin) == NULL) return -1;
        int opt = atoi(buf);
        switch (opt) {
        case 1:
            querycardimg_by_name(db);   // カード名一覧を流用
            cardvoice_id_menu(db, rdb);
            break;
        case 2:
            querycardimg_by_chara(db);  // キャラ一覧を流用
            cardvoice_id_menu(db, rdb);
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

