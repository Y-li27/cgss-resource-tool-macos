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

// 3D ??????
static void md_body_name(sqlite3 *rdb, int dress_id, char *out, int n){
    sqlite3_stmt *stmt = NULL;
    snprintf(out, n, "3d_md_body%04d_hq.unity3d", dress_id);
    if (sqlite3_prepare_v2(rdb, "SELECT 1 FROM manifests WHERE name=?", -1, &stmt, NULL) == SQLITE_OK){
        sqlite3_bind_text(stmt, 1, out, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) != SQLITE_ROW)
            snprintf(out, n, "3d_md_body%04d.unity3d", dress_id);
        sqlite3_finalize(stmt);
    }
}

/* 3d_chara_head_{chara_id}_{dress_id} も _hq を優先（頭部メッシュとテクスチャを含む）。マニフェストになければ通常版 */
static void head_name(sqlite3 *rdb, int chara_id, int dress_id, char *out, int n){
    sqlite3_stmt *stmt = NULL;
    snprintf(out, n, "3d_chara_head_%04d_%04d_hq.unity3d", chara_id, dress_id);
    if (sqlite3_prepare_v2(rdb, "SELECT 1 FROM manifests WHERE name=?", -1, &stmt, NULL) == SQLITE_OK){
        sqlite3_bind_text(stmt, 1, out, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) != SQLITE_ROW)
            snprintf(out, n, "3d_chara_head_%04d_%04d.unity3d", chara_id, dress_id);
        sqlite3_finalize(stmt);
    }
}

/* カード1枚分のモデルリソース名を表示（物理/頭部/ボディ/テクスチャ）。各行に hash を付ける
   card_id カードid  name カード名  chara_id キャラid  dress_id 衣装id */
static void print_model_names(sqlite3 *rdb, int card_id, const char *name, int chara_id, int dress_id){
    char res[256];
    printf("%d|%sモデル名\n", card_id, name);
    snprintf(res, sizeof res, "3d_chara_body_%04d.unity3d", dress_id);
    printf("物理：%s", res); print_res_hash(rdb, res); printf("\n");
    head_name(rdb, chara_id, dress_id, res, sizeof res);
    printf("頭部：%s", res); print_res_hash(rdb, res); printf("\n");
    md_body_name(rdb, dress_id, res, sizeof res);
    printf("ボディ：%s", res); print_res_hash(rdb, res); printf("\n");
    const char *tx[3] = {"hq","multi","spec"};
    for(int i = 0; i < 3; i++){
        snprintf(res, sizeof res, "3d_tx_body%04d_%s.unity3d", dress_id, tx[i]);
        printf("テクスチャ：%s", res); print_res_hash(rdb, res); printf("\n");
    }
}

/* カード id で1枚検索し、モデルリソースを表示
   入力が不正なときだけ聞き直す。1回検索したら戻る（上位メニューへ） */
static void query3d_model_by_card_id(sqlite3 *db, sqlite3 *rdb){
    char buf[64];
    while (1) {
        printf("idを入力してください：\n");
        if (fgets(buf, sizeof buf, stdin) == NULL) return;
        int id = atoi(buf);
        if (id <= 0) {
            fprintf(stderr, "入力エラー\n");
            continue;
        }
        sqlite3_stmt *stmt = NULL;
        if (sqlite3_prepare_v2(db,
                "SELECT id,name,chara_id,open_dress_id FROM card_data WHERE id=?",
                -1, &stmt, NULL) != SQLITE_OK) {
            fprintf(stderr, "SQLエラー: %s\n", sqlite3_errmsg(db));
            return;
        }
        sqlite3_bind_int(stmt, 1, id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int dress_id = sqlite3_column_int(stmt, 3);
            if (dress_id == 0) {
                fprintf(stderr, "専用衣装がありません\n");
            } else {
                print_model_names(rdb,
                                  sqlite3_column_int(stmt, 0),
                                  (const char*)sqlite3_column_text(stmt, 1),
                                  sqlite3_column_int(stmt, 2), dress_id);
            }
        } else {
            fprintf(stderr, "該当するモデルがありません\n");
        }
        sqlite3_finalize(stmt);
        return;
    }
}

/* 第4階層メニュー：1.idを入力してモデルを検索 2.戻る。選び終わると第3階層メニューへ戻る */
static void card3d_id_menu(sqlite3 *db, sqlite3 *rdb){
    char buf[64];
    while (1) {
        printf("1.idを入力してモデルを検索\t2.戻る\n");
        if (fgets(buf, sizeof buf, stdin) == NULL) return;
        int opt = atoi(buf);
        if (opt == 1) {
            query3d_model_by_card_id(db, rdb);
            return;
        }
        if (opt == 2) return;
        fprintf(stderr, "入力エラー\n");
    }
}

/* カード名の部分一致で検索して一覧表示 */
static void query3d_by_name(sqlite3 *db){
    char buf[128];
    printf("名称（日本語名）を入力してください：");
    if (fgets(buf, sizeof buf, stdin) == NULL) return;
    buf[strcspn(buf, "\r\n")] = 0;
    char name_utf8[256];
    gbk_to_utf8(buf, name_utf8, sizeof(name_utf8));
    char like[160];
    snprintf(like, sizeof(like), "%%%s%%", name_utf8);

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db,
            "SELECT id,name,rarity,open_dress_id FROM card_data WHERE name LIKE ? ORDER BY id",
            -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "SQLエラー: %s\n", sqlite3_errmsg(db));
        return;
    }
    sqlite3_bind_text(stmt, 1, like, -1, SQLITE_TRANSIENT);
    int n = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        printf("%d | %s | rarity（レアリティ）=%d | dress（衣装id）=%d\n",
               sqlite3_column_int(stmt, 0),
               sqlite3_column_text(stmt, 1),
               sqlite3_column_int(stmt, 2),
               sqlite3_column_int(stmt, 3));
        n++;
    }
    sqlite3_finalize(stmt);
    if (n == 0) fprintf(stderr, "該当するデータが見つかりません\n");
}

/* キャラ chara_id で検索して一覧表示 */
static void query3d_by_chara(sqlite3 *db){
    char buf[64];
    printf("キャラid(chara_id)を入力してください\n");
    if (fgets(buf, sizeof buf, stdin) == NULL) return;
    int chara_id = atoi(buf);
    if (chara_id <= 0) {
        fprintf(stderr, "入力エラー\n");
        return;
    }
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db,
            "SELECT id,name,rarity,open_dress_id FROM card_data WHERE chara_id=? ORDER BY id",
            -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "SQLエラー: %s\n", sqlite3_errmsg(db));
        return;
    }
    sqlite3_bind_int(stmt, 1, chara_id);
    int n = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        printf("%d | %s | rarity（レアリティ）=%d | dress（衣装id）=%d\n",
               sqlite3_column_int(stmt, 0),
               sqlite3_column_text(stmt, 1),
               sqlite3_column_int(stmt, 2),
               sqlite3_column_int(stmt, 3));
        n++;
    }
    sqlite3_finalize(stmt);
    if (n == 0) fprintf(stderr, "idの入力が不正です。検索結果は0件です\n");
}

/* 衣装 dress_id で検索し、モデルリソースを直接表示 */
static void query3d_by_dress(sqlite3 *db, sqlite3 *rdb){
    char buf[64];
    printf("open_dress_id(衣装id)を入力してください：\n");
    if (fgets(buf, sizeof buf, stdin) == NULL) return;
    int dress_id = atoi(buf);
    if (dress_id <= 0) {
        fprintf(stderr, "入力エラー\n");
        return;
    }
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db,
            "SELECT id,name,chara_id,open_dress_id FROM card_data WHERE open_dress_id=?",
            -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "SQLエラー: %s\n", sqlite3_errmsg(db));
        return;
    }
    sqlite3_bind_int(stmt, 1, dress_id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        print_model_names(rdb,
                          sqlite3_column_int(stmt, 0),
                          (const char*)sqlite3_column_text(stmt, 1),
                          sqlite3_column_int(stmt, 2),
                          sqlite3_column_int(stmt, 3));
    } else {
        fprintf(stderr, "該当するモデルがありません\n");
    }
    sqlite3_finalize(stmt);
}

/* 3Dモデル検索メニュー：項目を選ぶと自動でこのメニューに戻る。4 で第1階層メニューへ戻る */
int td_Search(sqlite3 *db, sqlite3 *rdb){
    _setmode(_fileno(stdin), _O_BINARY);   // stdin をバイナリモードにし、改行は自前で処理
    char buf[128];
    while (1) {
        printf("入力 1.カード名\t2.キャラchara_id（キャラid）\t3.キャラdress_id\t4.戻る\n");
        if (fgets(buf, sizeof buf, stdin) == NULL) return -1;
        int opt = atoi(buf);
        switch (opt) {
        case 1:
            query3d_by_name(db);
            card3d_id_menu(db, rdb);
            break;
        case 2:
            query3d_by_chara(db);
            card3d_id_menu(db, rdb);
            break;
        case 3:
            query3d_by_dress(db, rdb);
            break;
        case 4:
            printf("戻っています...\n");
            return 1;   // lookup_main に第1階層メニューへ戻るよう伝える
        default:
            fprintf(stderr, "入力エラー\n");
            break;
        }
    }
}

