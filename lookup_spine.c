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

// 2D Spine ????
/* ================== 2D Spine SDキャラ ================== */

/* カード id で1枚検索し、Spina リソースを表示（card_spine_{カードid}.unity3d） */
static void queryspina_by_card_id(sqlite3 *db,sqlite3 *rdb){
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
        char res[256];
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            printf("%d|%s\n", sqlite3_column_int(stmt, 0), sqlite3_column_text(stmt, 1));
            snprintf(res,sizeof res,"card_spine_%d.unity3d",sqlite3_column_int(stmt,0));
            printf("Spine:%s\t",res);print_res_hash(rdb,res); printf("\n");
            printf("共有スケルトン:spine_sprachen_petit_chara_common.unity3d\t"); print_res_hash(rdb, "spine_sprachen_petit_chara_common.unity3d"); printf("\n");
            snprintf(res, sizeof res, "card_live_%d.unity3d", sqlite3_column_int(stmt, 0));
            printf("Spine_Live:%s\t", res); print_res_hash(rdb, res); printf("\n");
            
        } else {
            fprintf(stderr, "該当するカードがありません\n");
        }
        sqlite3_finalize(stmt);
        return;
    }
}

/* Spina 検索の第4階層メニュー：1.idを入力してSpinaリソースを検索 2.戻る */
static void cardspina_id_menu(sqlite3 *db,sqlite3 *rdb){
    char buf[64];
    while (1) {
        printf("1.idを入力してSpineリソースを検索\t2.戻る\n");
        if (fgets(buf, sizeof buf, stdin) == NULL) return;
        int opt = atoi(buf);
        if (opt == 1) {
            queryspina_by_card_id(db,rdb);
            return;
        }
        if (opt == 2) return;
        fprintf(stderr, "入力エラー\n");
    }
}

/* Spina の SDキャラ名を部分一致で検索して一覧表示 */
static void queryspina_by_name(sqlite3 *db){
    char buf[128];
    printf("キャラ名（日本語名）を入力してください：\n");
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
    int n = 0;  // 出力回数を記録
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        printf("| id = %d | name = %s | dress = %d |\n",
               sqlite3_column_int(stmt, 0),
               sqlite3_column_text(stmt, 1),
               sqlite3_column_int(stmt, 2));
        n++;
    }
    sqlite3_finalize(stmt);
    if (n == 0)
        fprintf(stderr, "該当するリソースが見つかりません\n");
}

/* Spina をキャラ chara_id で検索して一覧表示 */
static void queryspina_by_chara(sqlite3 *db){
    char buf[64];
    while (1)
    {
            printf("キャラid（chara_id）を入力してください：\n");
         if (fgets(buf, sizeof buf, stdin) == NULL) return;
         int chara_id = atoi(buf);
         if (chara_id <= 0) {
            fprintf(stderr, "入力エラー\n");
            break;
        }
        sqlite3_stmt *stmt = NULL;
        if (sqlite3_prepare_v2(db,
                "SELECT id,name,open_dress_id FROM card_data WHERE chara_id=? ORDER BY id",
                -1, &stmt, NULL) != SQLITE_OK) {
            fprintf(stderr, "SQLエラー: %s\n", sqlite3_errmsg(db));
            break;
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
            fprintf(stderr, "該当するリソースが見つかりません\n");
        else
            break;
    }
    return ;
}

/* Spina を衣装 dress_id で検索して一覧表示 */
static void queryspina_by_dress(sqlite3 *db){
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
        fprintf(stderr, "該当するリソースが見つかりません\n");
}

/* Spine モデル検索メニュー：項目を選ぶと自動でこのメニューに戻る。4 で第1階層メニューへ戻る */
int spina_Search(sqlite3 *db,sqlite3 *rdb){
    _setmode(_fileno(stdin), _O_BINARY);   // stdin をバイナリモードにし、改行は自前で処理
    char buf[128];
    while (1) {
        printf("入力 1.カード名\t2.キャラid（chara_id）\t3.キャラdress_id\t4.戻る\n");
        if (fgets(buf, sizeof buf, stdin) == NULL) return -1;
        int opt = atoi(buf);
        switch (opt) {
        case 1:
            queryspina_by_name(db);
            cardspina_id_menu(db,rdb);
            break;
        case 2:
            queryspina_by_chara(db);
            cardspina_id_menu(db,rdb);
            break;
        case 3:
            queryspina_by_dress(db);
            cardspina_id_menu(db,rdb);
            break;
        case 4:
            printf("戻っています...\n");
            return 1;   // lookup_main に第1階層メニューへ戻るよう伝える
        default:
            fprintf(stderr, "入力エラー\n");
            break;
        }
    }
    return 0;
}


