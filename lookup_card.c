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
/* ================== 7. カードイラスト ================== */

/* カード id でカードイラストのリソースをすべて表示 */
static void querycardimg_by_card_id(sqlite3 *db, sqlite3 *rdb){
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
            const char *sizes[6] = {"circle","sm","s","m","l","xl"};
            for (int i = 0; i < 6; i++) {
                snprintf(res, sizeof res, "card_%d_%s.unity3d", card_id, sizes[i]);
                printf("カードイラスト(%s):%s\t", sizes[i], res);
                print_res_hash(rdb, res);
                printf("\n");
            }
            snprintf(res, sizeof res, "card_bg_%d.unity3d", card_id);
            printf("背景:%s\t", res); print_res_hash(rdb, res); printf("\n");
            snprintf(res, sizeof res, "card_bg_%d_01.unity3d", card_id);
            printf("背景(縦):%s\t", res); print_res_hash(rdb, res); printf("\n");
            snprintf(res, sizeof res, "card_bg_%d_s.unity3d", card_id);
            printf("背景(小さい横):%s\t", res); print_res_hash(rdb, res); printf("\n");
            snprintf(res, sizeof res, "idol_3d_%d_l.unity3d", card_id);
            printf("3Dフォト(L):%s\t", res); print_res_hash(rdb, res); printf("\n");
            snprintf(res, sizeof res, "idol_3d_%d_s.unity3d", card_id);
            printf("3Dフォト(S):%s\t", res); print_res_hash(rdb, res); printf("\n");
            snprintf(res, sizeof res, "card_cartoon_%d.unity3d", card_id);
            printf("カードイラストSpinaアニメ(beta):%s\t", res); print_res_hash(rdb, res); printf("\n");
        } else {
            fprintf(stderr, "該当するカードがありません\n");
        }
        sqlite3_finalize(stmt);
        return;
    }
}

/* カードイラストの第4階層メニュー：1.idを入力してカードイラストを検索 2.戻る */
static void cardimg_id_menu(sqlite3 *db, sqlite3 *rdb){
    char buf[64];
    while (1) {
        printf("1.idを入力してカードイラストを検索\t2.戻る\n");
        if (fgets(buf, sizeof buf, stdin) == NULL) return;
        int opt = atoi(buf);
        if (opt == 1) {
            querycardimg_by_card_id(db, rdb);
            return;
        }
        if (opt == 2) return;
        fprintf(stderr, "入力エラー\n");
    }
}

/* カード名の部分一致で検索して一覧表示 */
void querycardimg_by_name(sqlite3 *db){
    char buf[128];
    printf("名称（日本語名）を入力してください：\n");
    if (fgets(buf, sizeof buf, stdin) == NULL) return;
    buf[strcspn(buf, "\r\n")] = 0;
    char name_utf8[128];
    gbk_to_utf8(buf, name_utf8, sizeof name_utf8);
    char like[256];
    snprintf(like, sizeof like, "%%%s%%", name_utf8);

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db,
            "SELECT id,name,open_dress_id FROM card_data WHERE name LIKE ? ORDER BY id",
            -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "SQLエラー:%s\n", sqlite3_errmsg(db));
        return;
    }
    sqlite3_bind_text(stmt, 1, like, -1, SQLITE_TRANSIENT);
    int n = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        printf("| id = %d | name = %s | dress = %d |\n",
               sqlite3_column_int(stmt, 0),
               sqlite3_column_text(stmt, 1),
               sqlite3_column_int(stmt, 2));
        n++;
    }
    sqlite3_finalize(stmt);
    if (n == 0)
        fprintf(stderr, "該当するカードが見つかりません\n");
}

/* キャラ chara_id で検索して一覧表示 */
void querycardimg_by_chara(sqlite3 *db){
    char buf[64];
    printf("キャラid（chara_id）を入力してください：\n");
    if (fgets(buf, sizeof buf, stdin) == NULL) return;
    int chara_id = atoi(buf);
    if (chara_id <= 0) {
        fprintf(stderr, "入力エラー\n");
        return;
    }
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db,
            "SELECT id,name,open_dress_id FROM card_data WHERE chara_id=? ORDER BY id",
            -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "SQLエラー: %s\n", sqlite3_errmsg(db));
        return;
    }
    sqlite3_bind_int(stmt, 1, chara_id);
    int n = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        printf("| id = %d | name = %s | dress = %d |\n",
               sqlite3_column_int(stmt, 0),
               sqlite3_column_text(stmt, 1),
               sqlite3_column_int(stmt, 2));
        n++;
    }
    sqlite3_finalize(stmt);
    if (n == 0)
        fprintf(stderr, "該当するカードが見つかりません\n");
}

/* 衣装 dress_id で検索して一覧表示 */
static void querycardimg_by_dress(sqlite3 *db){
    char buf[64];
    printf("open_dress_id（衣装id）を入力してください：\n");
    if (fgets(buf, sizeof buf, stdin) == NULL) return;
    int dress_id = atoi(buf);
    if (dress_id <= 0) {
        fprintf(stderr, "入力エラー\n");
        return;
    }
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db,
            "SELECT id,name,open_dress_id FROM card_data WHERE open_dress_id=? ORDER BY id",
            -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "SQLエラー: %s\n", sqlite3_errmsg(db));
        return;
    }
    sqlite3_bind_int(stmt, 1, dress_id);
    int n = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        printf("| id = %d | name = %s | dress = %d |\n",
               sqlite3_column_int(stmt, 0),
               sqlite3_column_text(stmt, 1),
               sqlite3_column_int(stmt, 2));
        n++;
    }
    sqlite3_finalize(stmt);
    if (n == 0)
        fprintf(stderr, "該当するカードが見つかりません\n");
}

/* カードイラスト検索メニュー：項目を選ぶと自動でこのメニューに戻る。4 で第1階層メニューへ戻る */
int cardimg_Search(sqlite3 *db, sqlite3 *rdb){
    _setmode(_fileno(stdin), _O_BINARY);   // stdin をバイナリモードにし、改行は自前で処理
    char buf[128];
    while (1) {
        printf("入力 1.カード名\t2.キャラid（chara_id）\t3.キャラdress_id\t4.戻る\n");
        if (fgets(buf, sizeof buf, stdin) == NULL) return -1;
        int opt = atoi(buf);
        switch (opt) {
        case 1:
            querycardimg_by_name(db);
            cardimg_id_menu(db, rdb);
        case 2:
            querycardimg_by_chara(db);
            cardimg_id_menu(db, rdb);
        case 3:
            querycardimg_by_dress(db);
            cardimg_id_menu(db, rdb);
        case 4:
            printf("戻っています...\n");
            return 1;   // lookup_main に第1階層メニューへ戻るよう伝える
        default:
            fprintf(stderr, "入力エラー\n");
            break;
        }
    }
}

