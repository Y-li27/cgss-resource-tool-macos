/* browse.c: リソース検索 + ダウンロード 統合モジュール
 *
 * 流れ(2段階):
 *   1. 対象を探す: 名称を入力(部分一致)または id -> pager で処理対象を複数選択
 *   2. リソースを選ぶ: その対象の全リソースを自動で組み立て -> pager で複数選択 -> 一括ダウンロード
 *
 * 以前分かれていた「検索」と「ダウンロード」の2メニューを置き換える:
 *  - 汎用/BGM/譜面/ステージ/モーション/モデル/Spine/ステッカー: manifest のリソース名を直接検索
 *  - 楽曲: 曲名の部分一致 または 楽曲id
 *  - カード: カード名/キャラ名の部分一致 または カードid/キャラid
 *  - ボイス: カード名の部分一致 または カードid
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <windows.h>
#include "sqlite3.h"
#include "paper.h"
#include "net.h"
#include "util.h"
#include "cg.h"
#include "sticker.h"
#include "conio.h"

#define DB_PATH "master.mdb"
#define MAX_ITEMS 1024

static void browse_fail(const char *msg){
    fprintf(stderr, "%s\n", msg);
    fprintf(stderr, "Enterでメニューに戻る\n");
    fflush(stderr);
    _getch();
}

/* ダウンロード可能なリソース1件 */
typedef struct {
    char disp[128];      /* pager に表示する名前 */
    char name[256];      /* マニフェスト内のリソース名 */
    char hash[64];
    wchar_t sub[64];     /* CGSS_DOWN 以下のサブディレクトリ */
} BItem;

/* リソース名に対応する hash を照会。成功なら 0 を返す */
static int get_hash(sqlite3 *rdb, const char *name, char *hash_out, int n){
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(rdb, "SELECT hash FROM manifests WHERE name=?",
                           -1, &stmt, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    int rc = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW){
        snprintf(hash_out, n, "%s", (const char*)sqlite3_column_text(stmt, 0));
        rc = 0;
    }
    sqlite3_finalize(stmt);
    return rc;
}

/* 既知のリソース名 -> 候補リストへ追加(マニフェストに無ければスキップを表示) */
static void add_res(sqlite3 *rdb, BItem *items, int *n,
                    const char *name, const wchar_t *sub){
    if (*n >= MAX_ITEMS) return;
    if (get_hash(rdb, name, items[*n].hash, 64) != 0){
        printf("マニフェストに %s がありません\n", name);
        return;
    }
    snprintf(items[*n].name, sizeof items[*n].name, "%s", name);
    snprintf(items[*n].disp, sizeof items[*n].disp, "%s", name);
    wcscpy(items[*n].sub, sub);
    (*n)++;
}

/* 候補リストを pager 用の dbdef 配列へ変換(tmp は n+1 要素、最終行は END) */
static void make_menu(dbdef *tmp, BItem *items, int n){
    for (int i = 0; i < n; i++){
        snprintf(tmp[i].name, sizeof tmp[i].name, "%s", items[i].disp);
        tmp[i].func = NULL;
        tmp[i].state = 0;
    }
    snprintf(tmp[n].name, sizeof tmp[n].name, "END");
    tmp[n].func = NULL;
    tmp[n].state = 0;
}

/* pager で選択されたインデックスを集め、個数を返す */
static int collect(const dbdef *tmp, int n, int *picked){
    int c = 0;
    for (int i = 0; i < n; i++)
        if (tmp[i].state) picked[c++] = i;
    return c;
}

static int is_sticker_resource(const char *name)
{
    static const char prefix[] = "spine_motion_sticker_";
    return name && strncmp(name, prefix, sizeof(prefix) - 1) == 0;
}

/* 1件を wroot\sub へダウンロード。ステッカーは成功後すぐに共通の後処理へ入る。 */
static void download_item(const BItem *it, const wchar_t *wroot){
    wchar_t wsub[1300];

    if (is_sticker_resource(it->name)) {
        wchar_t sticker_root[1300], raw_dir[1300];
        wchar_t spine_dir[1300], png_dir[1300];

        wpath_join(sticker_root, 1300, wroot, L"ステッカー");
        wpath_join(raw_dir, 1300, sticker_root, L"元ファイルunity3d");
        wpath_join(spine_dir, 1300, sticker_root, L"spineファイル");
        wpath_join(png_dir, 1300, sticker_root, L"ステッカーPNG");
        mkdirs(raw_dir);
        mkdirs(spine_dir);
        mkdirs(png_dir);

        printf("ダウンロード %s ...\n", it->name);
        if (dl_one(it->name, it->hash, raw_dir) == 0) {
            sticker_unpack_file(it->name, raw_dir, spine_dir, png_dir, 0);
        }
        return;
    }

    wpath_join(wsub, 1300, wroot, it->sub);
    mkdirs(wsub);
    printf("ダウンロード %s ...\n", it->name);
    dl_one(it->name, it->hash, wsub);
}

/* 以下の種別は「楽曲選択/カード選択」を再利用する。先に宣言(定義は後ろ) */
static int choose_songs(sqlite3 *db, int *ids, int max);
static int choose_cards(sqlite3 *db, int *ids, int max);

/* 曲名を照会 */
static void get_song_name(sqlite3 *db, int id, char *out, int n){
    out[0] = 0;
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, "SELECT name FROM music_data WHERE id=?",
                           -1, &stmt, NULL) == SQLITE_OK){
        sqlite3_bind_int(stmt, 1, id);
        if (sqlite3_step(stmt) == SQLITE_ROW)
            snprintf(out, n, "%s", (const char*)sqlite3_column_text(stmt, 0));
        sqlite3_finalize(stmt);
    }
}

/* CGSS_DOWN\<id><名前> ディレクトリを作成 */
static void make_dl_folder(int id, const char *name, wchar_t *wfolder, int n){
    char folder[512];
    snprintf(folder, sizeof folder, "%d%s", id, name);
    wchar_t wroot[1024], wfoldername[512];
    get_dl_root(wroot, 1024);
    utf8_to_wide(folder, wfoldername, 512);
    wpath_join(wfolder, n, wroot, wfoldername);
    mkdirs(wfolder);
}

/* 共通の締め: 複数選択 -> wfolder へ一括ダウンロード */
static int pick_and_download(const char *title, sqlite3 *db, sqlite3 *rdb,
                             BItem *items, int n, const wchar_t *wfolder){
    if (n == 0){ printf("ダウンロードできるリソースがありません\n"); return 0; }
    static dbdef tmp[MAX_ITEMS + 1];
    static int picked[MAX_ITEMS];
    make_menu(tmp, items, n);
    int rc = pager_picks(title, tmp, db, rdb, 1);
    if (rc <= 0){ if (rc == -1) printf("キャンセルしました\n"); return 0; }
    int c = collect(tmp, n, picked);
    for (int i = 0; i < c; i++)
        download_item(&items[picked[i]], wfolder);
    printf("合計 %d 件をダウンロード -> %ls\n", c, wfolder);
    return c;
}

/* ================== 汎用: manifest のリソース名を検索 ================== */

static void browse_manifest(sqlite3 *rdb, const char *title,
                            const char *extra, const wchar_t *subdir){
    char buf[256];
    printf("キーワードを入力(空欄=すべて): ");
    if (fgets(buf, sizeof buf, stdin) == NULL) return;
    buf[strcspn(buf, "\r\n")] = 0;

    char like[300];
    snprintf(like, sizeof like, "%%%s%%", buf);

    char sql[1000];
    if (extra && extra[0])
        snprintf(sql, sizeof sql,
            "SELECT name,hash FROM manifests WHERE name LIKE ? %s ORDER BY name LIMIT %d",
            extra, MAX_ITEMS);
    else
        snprintf(sql, sizeof sql,
            "SELECT name,hash FROM manifests WHERE name LIKE ? ORDER BY name LIMIT %d",
            MAX_ITEMS);

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(rdb, sql, -1, &stmt, NULL) != SQLITE_OK){
        fprintf(stderr, "SQLエラー: %s\n", sqlite3_errmsg(rdb));
        return;
    }
    sqlite3_bind_text(stmt, 1, like, -1, SQLITE_TRANSIENT);

    static BItem items[MAX_ITEMS];
    int n = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && n < MAX_ITEMS){
        snprintf(items[n].name, sizeof items[n].name, "%s",
                 (const char*)sqlite3_column_text(stmt, 0));
        snprintf(items[n].hash, sizeof items[n].hash, "%s",
                 (const char*)sqlite3_column_text(stmt, 1));
        snprintf(items[n].disp, sizeof items[n].disp, "%s", items[n].name);
        wcscpy(items[n].sub, subdir);
        n++;
    }
    sqlite3_finalize(stmt);

    if (n == 0){ printf("一致するリソースがありません\n"); return; }
    printf("一致 %d 件(Spaceで選択, Aで全選択, Enterでダウンロード):\n", n);

    static dbdef tmp[MAX_ITEMS + 1];
    make_menu(tmp, items, n);
    int rc = pager_picks(title, tmp, NULL, rdb, 1);
    if (rc <= 0){ if (rc == -1) printf("キャンセルしました\n"); return; }

    static int picked[MAX_ITEMS] ;
    int c = collect(tmp, n, picked);    // 選択数を返す
    wchar_t wroot[1024];
    get_dl_root(wroot, 1024);
    for (int i = 0; i < c; i++)
        download_item(&items[picked[i]], wroot);
    printf("合計 %d 件をダウンロード -> %ls\n", c, wroot);
}

/* 種別ごとの薄いラッパー(シグネチャは dbdef.func と一致している必要がある) */
static int browse_all(sqlite3 *db, sqlite3 *rdb){
    (void)db;
    browse_manifest(rdb, "汎用リソース検索", NULL, L"カスタム");
    return 0;
}
static int browse_bgm(sqlite3 *db, sqlite3 *rdb){
    (void)db;
    browse_manifest(rdb, "BGM検索", "AND name LIKE '%bgm%'", L"BGM");
    return 0;
}
static int browse_sticker(sqlite3 *db, sqlite3 *rdb){
    (void)db;
    browse_manifest(rdb, "ステッカー検索",
                    "AND name LIKE 'spine_motion_sticker%'", L"ステッカー");
    return 0;
}

/* 譜面: 先に曲名/id で楽曲を探し、その曲の全譜面を列挙 */
static int browse_chart(sqlite3 *db, sqlite3 *rdb){
    int ids[32];
    int nids = choose_songs(db, ids, 32);
    if (nids <= 0) return 0;
    for (int s = 0; s < nids; s++){
        int id = ids[s];
        char sname[128];
        get_song_name(db, id, sname, sizeof sname);
        printf("\n========== %d|%s の譜面 ==========\n", id, sname);

        static BItem items[MAX_ITEMS];
        int n = 0;
        char res[256];
        sqlite3_stmt *lstmt = NULL;
        if (sqlite3_prepare_v2(db,
                "SELECT id,difficulty_1,difficulty_2,difficulty_3,difficulty_4 "
                "FROM live_data WHERE music_data_id=? ORDER BY id",
                -1, &lstmt, NULL) == SQLITE_OK){
            sqlite3_bind_int(lstmt, 1, id);
            while (sqlite3_step(lstmt) == SQLITE_ROW && n < MAX_ITEMS){
                int live_id = sqlite3_column_int(lstmt, 0);
                snprintf(res, sizeof res, "musicscores_m%d.bdb", live_id);
                add_res(rdb, items, &n, res, L"譜面");
                if (n > 0)
                    snprintf(items[n-1].disp, sizeof items[n-1].disp,
                             "live %d | diff %d/%d/%d/%d", live_id,
                             sqlite3_column_int(lstmt, 1),
                             sqlite3_column_int(lstmt, 2),
                             sqlite3_column_int(lstmt, 3),
                             sqlite3_column_int(lstmt, 4));
            }
            sqlite3_finalize(lstmt);
        }
        wchar_t wfolder[1024];
        make_dl_folder(id, sname, wfolder, 1024);
        pick_and_download("譜面リソース(Spaceで選択, Enterでダウンロード)", db, rdb,
                          items, n, wfolder);
    }
    return 0;
}

/* ステージ: 先に曲名/id で楽曲を探し、その曲のステージを列挙 */
static int browse_stage(sqlite3 *db, sqlite3 *rdb){
    int ids[32];
    int nids = choose_songs(db, ids, 32);
    if (nids <= 0) return 0;
    for (int s = 0; s < nids; s++){
        int id = ids[s];
        char sname[128];
        get_song_name(db, id, sname, sizeof sname);
        printf("\n========== %d|%s のステージ ==========\n", id, sname);

        static BItem items[MAX_ITEMS];
        int n = 0;
        char res[256];
        sqlite3_stmt *lstmt = NULL;
        if (sqlite3_prepare_v2(db,
                "SELECT id, live_bg FROM live_data WHERE music_data_id=? ORDER BY id",
                -1, &lstmt, NULL) == SQLITE_OK){
            sqlite3_bind_int(lstmt, 1, id);
            while (sqlite3_step(lstmt) == SQLITE_ROW && n < MAX_ITEMS){
                int live_id = sqlite3_column_int(lstmt, 0);
                int live_bg = sqlite3_column_int(lstmt, 1);
                if (live_bg > 0){
                    snprintf(res, sizeof res, "3d_stage_%d.unity3d", live_bg);
                    add_res(rdb, items, &n, res, L"ステージ");
                    if (n > 0)
                        snprintf(items[n-1].disp, sizeof items[n-1].disp,
                                 "live %d | ステージ %d", live_id, live_bg);
                    snprintf(res, sizeof res, "3d_stage_%d_hq.unity3d", live_bg);
                    add_res(rdb, items, &n, res, L"ステージ");
                }
            }
            sqlite3_finalize(lstmt);
        }
        wchar_t wfolder[1024];
        make_dl_folder(id, sname, wfolder, 1024);
        pick_and_download("ステージリソース(Spaceで選択, Enterでダウンロード)", db, rdb,
                          items, n, wfolder);
    }
    return 0;
}

/* モーション: 先に曲名/id で楽曲を探し、その曲のモーションを列挙 */
static int browse_action(sqlite3 *db, sqlite3 *rdb){
    int ids[32];
    int nids = choose_songs(db, ids, 32);
    if (nids <= 0) return 0;
    for (int s = 0; s < nids; s++){
        int id = ids[s];
        char sname[128];
        get_song_name(db, id, sname, sizeof sname);
        printf("\n========== %d|%s のモーション ==========\n", id, sname);

        static BItem items[MAX_ITEMS];
        int n = 0;
        char like[64];
        snprintf(like, sizeof like, "3d_cutt_an_chr_son%d%%", id);
        sqlite3_stmt *mstmt = NULL;
        if (sqlite3_prepare_v2(rdb,
                "SELECT name,hash FROM manifests WHERE name LIKE ? ORDER BY name",
                -1, &mstmt, NULL) == SQLITE_OK){
            sqlite3_bind_text(mstmt, 1, like, -1, SQLITE_TRANSIENT);
            while (sqlite3_step(mstmt) == SQLITE_ROW && n < MAX_ITEMS){
                snprintf(items[n].name, sizeof items[n].name, "%s",
                         (const char*)sqlite3_column_text(mstmt, 0));
                snprintf(items[n].hash, sizeof items[n].hash, "%s",
                         (const char*)sqlite3_column_text(mstmt, 1));
                snprintf(items[n].disp, sizeof items[n].disp, "%s", items[n].name);
                wcscpy(items[n].sub, L"モーション");
                n++;
            }
            sqlite3_finalize(mstmt);
        }
        wchar_t wfolder[1024];
        make_dl_folder(id, sname, wfolder, 1024);
        pick_and_download("モーションリソース(Spaceで選択, Enterでダウンロード)", db, rdb,
                          items, n, wfolder);
    }
    return 0;
}

/* 3Dモデル: カード名/キャラ名/id でカードを探し、モデルリソースを列挙 */
static int browse_model(sqlite3 *db, sqlite3 *rdb){
    int ids[64];
    int nids = choose_cards(db, ids, 64);
    if (nids <= 0) return 0;
    for (int s = 0; s < nids; s++){
        int card_id = ids[s];
        sqlite3_stmt *stmt = NULL;
        if (sqlite3_prepare_v2(db,
                "SELECT id,name,chara_id,open_dress_id FROM card_data WHERE id=?",
                -1, &stmt, NULL) != SQLITE_OK)
            continue;
        sqlite3_bind_int(stmt, 1, card_id);
        if (sqlite3_step(stmt) != SQLITE_ROW){ sqlite3_finalize(stmt); continue; }
        char cname[128];
        snprintf(cname, sizeof cname, "%s",
                 (const char*)sqlite3_column_text(stmt, 1));
        int chara_id = sqlite3_column_int(stmt, 2);
        int dress_id = sqlite3_column_int(stmt, 3);
        sqlite3_finalize(stmt);
        printf("\n========== %d|%s の3Dモデル ==========\n", card_id, cname);

        static BItem items[MAX_ITEMS];
        int n = 0;
        char res[256];
        if (dress_id > 0){
            snprintf(res, sizeof res, "3d_chara_body_%04d.unity3d", dress_id);
            add_res(rdb, items, &n, res, L"3Dモデル");
            snprintf(res, sizeof res, "3d_chara_head_%04d_%04d_hq.unity3d",
                     chara_id, dress_id);
            if (get_hash(rdb, res, items[n].hash, 64) != 0)
                snprintf(res, sizeof res, "3d_chara_head_%04d_%04d.unity3d",
                         chara_id, dress_id);
            add_res(rdb, items, &n, res, L"3Dモデル");
            snprintf(res, sizeof res, "3d_md_body%04d_hq.unity3d", dress_id);
            if (get_hash(rdb, res, items[n].hash, 64) != 0)
                snprintf(res, sizeof res, "3d_md_body%04d.unity3d", dress_id);
            add_res(rdb, items, &n, res, L"3Dモデル");
            const char *tx[3] = {"hq","multi","spec"};
            for (int i = 0; i < 3; i++){
                snprintf(res, sizeof res, "3d_tx_body%04d_%s.unity3d",
                         dress_id, tx[i]);
                add_res(rdb, items, &n, res, L"3Dモデル");
            }
        } else {
            printf("このカードには専用衣装がなく、3Dモデルもありません\n");
        }
        wchar_t wfolder[1024];
        make_dl_folder(card_id, cname, wfolder, 1024);
        pick_and_download("3Dモデルリソース(Spaceで選択, Enterでダウンロード)", db, rdb,
                          items, n, wfolder);
    }
    return 0;
}

/* Spine: カード名/キャラ名/id でカードを探し、Spine リソースを列挙 */
static int browse_spine(sqlite3 *db, sqlite3 *rdb){
    int ids[64];
    int nids = choose_cards(db, ids, 64);
    if (nids <= 0) return 0;
    for (int s = 0; s < nids; s++){
        int card_id = ids[s];
        char cname[128] = "";
        sqlite3_stmt *stmt = NULL;
        if (sqlite3_prepare_v2(db, "SELECT id,name FROM card_data WHERE id=?",
                               -1, &stmt, NULL) == SQLITE_OK){
            sqlite3_bind_int(stmt, 1, card_id);
            if (sqlite3_step(stmt) == SQLITE_ROW)
                snprintf(cname, sizeof cname, "%s",
                         (const char*)sqlite3_column_text(stmt, 1));
            sqlite3_finalize(stmt);
        }
        printf("\n========== %d|%s のSpine ==========\n", card_id, cname);

        static BItem items[MAX_ITEMS];
        int n = 0;
        char res[256];
        snprintf(res, sizeof res, "card_spine_%d.unity3d", card_id);
        add_res(rdb, items, &n, res, L"Spine");
        add_res(rdb, items, &n, "spine_sprachen_petit_chara_common.unity3d", L"Spine");
        snprintf(res, sizeof res, "card_cartoon_%d.unity3d", card_id);
        add_res(rdb, items, &n, res, L"Spine_Live");

        wchar_t wfolder[1024];
        make_dl_folder(card_id, cname, wfolder, 1024);
        pick_and_download("Spineリソース(Spaceで選択, Enterでダウンロード)", db, rdb,
                          items, n, wfolder);
    }
    return 0;
}

/* ================== CGムービー: usm を検索し、同じ movie の音声を自動で付ける ================== */

static int browse_cg(sqlite3 *db, sqlite3 *rdb){
    char buf[128];
    printf("CGキーワードを入力(例 anivcount / movie_0029, 空欄=すべて): ");
    if (fgets(buf, sizeof buf, stdin) == NULL) return 0;
    buf[strcspn(buf, "\r\n")] = 0;

    char like[300];
    snprintf(like, sizeof like, "%%%s%%", buf);

    static dbdef tmp[513];
    static char row_name[513][256];   /* 選択した usm の元リソース名 */
    int n = 0;
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(rdb,
            "SELECT name FROM manifests WHERE name LIKE '%.usm' AND name LIKE ? "
            "ORDER BY name LIMIT 512",
            -1, &stmt, NULL) != SQLITE_OK){
        fprintf(stderr, "SQLエラー: %s\n", sqlite3_errmsg(rdb));
        return 0;
    }
    sqlite3_bind_text(stmt, 1, like, -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW && n < 512){
        snprintf(row_name[n], sizeof row_name[n], "%s",
                 (const char*)sqlite3_column_text(stmt, 0));
        /* 表示名: movie_XXXX[_alt]。2drich なら 2drich<id> | 曲名 */
        const char *p = strstr(row_name[n], "movie_");
        if (p){
            snprintf(tmp[n].name, sizeof tmp[n].name, "%s", p);
        } else if (strncmp(row_name[n], "m/live/high/2drich", 18) == 0){
            const char *d = row_name[n] + 18;
            char sid[16] = "";
            int k = 0;
            while (d[k] && isdigit((unsigned char)d[k]) && k < 14){
                sid[k] = d[k];
                k++;
            }
            sid[k] = 0;
            char sname[128] = "";
            if (sid[0]){
                sqlite3_stmt *sstmt = NULL;
                if (sqlite3_prepare_v2(db,
                        "SELECT name FROM music_data WHERE id=?",
                        -1, &sstmt, NULL) == SQLITE_OK){
                    sqlite3_bind_int(sstmt, 1, atoi(sid));
                    if (sqlite3_step(sstmt) == SQLITE_ROW)
                        snprintf(sname, sizeof sname, "%s",
                                 (const char*)sqlite3_column_text(sstmt, 0));
                    sqlite3_finalize(sstmt);
                }
            }
            if (sname[0])
                snprintf(tmp[n].name, sizeof tmp[n].name,
                         "2drich%s | %s", sid, sname);
            else
                snprintf(tmp[n].name, sizeof tmp[n].name, "2drich%s", sid);
        } 
        /* 数が少ない本物の2Dアニメーションには、対応する楽曲を表示 */
        else if(strncmp(row_name[n], "m/live/high/movie", 17) == 0){
            const char *d = row_name[n] + 17;
            char sid[16] = "";
            int k = 0;
            while (d[k] && isdigit((unsigned char)d[k]) && k < 14){
                sid[k] = d[k];
                k++;
            }
            sid[k] = 0;
            char sname[128] = "";
            if (sid[0]){
                sqlite3_stmt *sstmt = NULL;
                if (sqlite3_prepare_v2(db,
                        "SELECT name FROM music_data WHERE id=?",
                        -1, &sstmt, NULL) == SQLITE_OK){
                    sqlite3_bind_int(sstmt, 1, atoi(sid));
                    if (sqlite3_step(sstmt) == SQLITE_ROW)
                        snprintf(sname, sizeof sname, "%s",
                                 (const char*)sqlite3_column_text(sstmt, 0));
                    sqlite3_finalize(sstmt);
                }
            }
            if (sname[0])
                snprintf(tmp[n].name, sizeof tmp[n].name,
                         "2dmovie%s | %s", sid, sname);
            else
                snprintf(tmp[n].name, sizeof tmp[n].name, "2dmovie%s", sid);
        }
        
        else {
            snprintf(tmp[n].name, sizeof tmp[n].name, "%s", row_name[n]);
        }
        tmp[n].func = NULL;
        tmp[n].state = 0;
        n++;
    }
    sqlite3_finalize(stmt);
    if (n == 0){ printf("一致するCGがありません\n"); return 0; }

    static int picked_idx[512];
    int np = 0;
    if (n == 1){
        picked_idx[np++] = 0;
    } else {
        snprintf(tmp[n].name, sizeof tmp[n].name, "END");
        tmp[n].func = NULL;
        tmp[n].state = 0;
        int r = pager_picks("CGムービー(Spaceで選択, Enterで確定)", tmp, NULL, rdb, 1);
        if (r <= 0){ if (r == -1) printf("キャンセルしました\n"); return 0; }
        for (int i = 0; i < n; i++)
            if (tmp[i].state) picked_idx[np++] = i;
    }

    for (int s = 0; s < np; s++){
        int idx = picked_idx[s];
        const char *usm_name = row_name[idx];
        int is2dmovie = 0;
        /* movie_XXXX を抽出 */
        char movie[64] = "";
        if(strstr(usm_name, "movie_")){
            const char *pm = strstr(usm_name,"movie_"); 
            if (pm){
                const char *d = pm + 6;
                int k = 0;
                while (d[k] && isdigit((unsigned char)d[k]) && k < 60){
                    movie[k] = d[k];
                    k++;
                }
                movie[k] = 0;
            }
            is2dmovie = 1;
        }else if(strstr(usm_name,"movie")){
            const char *pm = strstr(usm_name,"movie"); 
            if (pm){
                const char *d = pm + 5;
                int k = 0;
                while (d[k] && isdigit((unsigned char)d[k]) && k < 60){
                    movie[k] = d[k];
                    k++;
                }
                movie[k] = 0;
            }
            is2dmovie = 1;
        }
        printf("\n========== %s ==========\n", usm_name);

        /* 自動で組み合わせ: 選択した usm + 正確にペアの音声。2つ目の選択メニューは出さない */
        static BItem items[16];
        int n2 = 0;
        char hash[64];
        if (get_hash(rdb, usm_name, hash, 64) == 0){
            snprintf(items[n2].name, sizeof items[n2].name, "%s", usm_name);
            snprintf(items[n2].hash, sizeof items[n2].hash, "%s", hash);
            snprintf(items[n2].disp, sizeof items[n2].disp, "%s", usm_name);
            wcscpy(items[n2].sub, L"ムービー");
            n2++;
        }
        /* ペア音声: m/AnivCount/<dir>/movie_<id>.usm
         *        -> m/bgm_anivcount_<dir>_movie_<id>.acb (同じ dir+id) */
        char dirnum[16] = "";
        {
            const char *a = strstr(usm_name, "AnivCount/");
            if (a){
                const char *d = a + 10;
                int k = 0;
                while (d[k] && isdigit((unsigned char)d[k]) && k < 14){
                    dirnum[k] = d[k];
                    k++;
                }
                dirnum[k] = 0;
            }
        }
        if (dirnum[0] && movie[0]){
            char cand[512];
            snprintf(cand, sizeof cand, "m/bgm_anivcount_%s_movie_%s.acb",
                     dirnum, movie);
            if (get_hash(rdb, cand, hash, 64) == 0 && n2 < 16){
                snprintf(items[n2].name, sizeof items[n2].name, "%s", cand);
                snprintf(items[n2].hash, sizeof items[n2].hash, "%s", hash);
                snprintf(items[n2].disp, sizeof items[n2].disp, "%s", cand);
                wcscpy(items[n2].sub, L"音声");
                n2++;
            }
        } else if (strncmp(usm_name, "m/live/high/2drich", 18) == 0){
            /* 2D rich MV: 2drich<曲id>.usm -> l/song_<曲id>.acb */
            const char *d = usm_name + 18;
            char sid[16] = "";
            int k = 0;
            while (d[k] && isdigit((unsigned char)d[k]) && k < 14){
                sid[k] = d[k];
                k++;
            }
            sid[k] = 0;
            if (sid[0]){
                char cand[512];
                snprintf(cand, sizeof cand, "l/song_%s.acb", sid);
                if (get_hash(rdb, cand, hash, 64) == 0 && n2 < 16){
                    snprintf(items[n2].name, sizeof items[n2].name, "%s", cand);
                    snprintf(items[n2].hash, sizeof items[n2].hash, "%s", hash);
                    snprintf(items[n2].disp, sizeof items[n2].disp, "%s", cand);
                    wcscpy(items[n2].sub, L"音声");
                    n2++;
                }
            }
        }
        else if (strncmp(usm_name, "m/live/high/movie", 17) == 0) {
            /* m/live/high/movie5044.usm のようにアンダースコアが無い場合 */
            const char *d = usm_name + 17;   /* "m/live/high/movie" をスキップ */
            char sid[16] = "";
            int k = 0;
            while (d[k] && isdigit((unsigned char)d[k]) && k < 14) {
                sid[k] = d[k];
                k++;
            }
            sid[k] = 0;
            if (sid[0]) {
                char cand[512];
                snprintf(cand, sizeof cand, "l/song_%s.acb", sid);
                if (get_hash(rdb, cand, hash, 64) == 0 && n2 < 16) {
                    snprintf(items[n2].name, sizeof items[n2].name, "%s", cand);
                    snprintf(items[n2].hash, sizeof items[n2].hash, "%s", hash);
                    snprintf(items[n2].disp, sizeof items[n2].disp, "%s", cand);
                    wcscpy(items[n2].sub, L"音声");
                    n2++;
                }
            }
        } 
        else if (movie[0]){
            /* フォールバック: 同じ movie id の全 acb */
            char mlike[128];
            if(!is2dmovie)
                snprintf(mlike, sizeof mlike, "%%movie_%s%%.acb", movie);
            else
                snprintf(mlike,sizeof mlike,"%%song_%s%%.acb",movie);
            sqlite3_stmt *mstmt = NULL;
            if (sqlite3_prepare_v2(rdb,
                    "SELECT name,hash FROM manifests WHERE name LIKE ? ORDER BY name",
                    -1, &mstmt, NULL) == SQLITE_OK){
                sqlite3_bind_text(mstmt, 1, mlike, -1, SQLITE_TRANSIENT);
                while (sqlite3_step(mstmt) == SQLITE_ROW && n2 < 16){
                    const char *nm = (const char*)sqlite3_column_text(mstmt, 0);
                    snprintf(items[n2].name, sizeof items[n2].name, "%s", nm);
                    snprintf(items[n2].hash, sizeof items[n2].hash, "%s",
                             (const char*)sqlite3_column_text(mstmt, 1));
                    snprintf(items[n2].disp, sizeof items[n2].disp, "%s", nm);
                    wcscpy(items[n2].sub, L"音声");
                    n2++;
                }
                sqlite3_finalize(mstmt);
            }
        }
        if (n2 == 0){ printf("関連ファイルがありません\n"); continue; }

        /* ディレクトリ: CGSS_DOWN\CG\movie_XXXX または CG\2drichXXXX */
        char folder[512];
        if (movie[0]){
            snprintf(folder, sizeof folder, "CG\\movie_%s", movie);
        } else {
            char b2[256];
            snprintf(b2, sizeof b2, "%s", base_name(usm_name));
            char *dot = strrchr(b2, '.');
            if (dot) *dot = 0;
            snprintf(folder, sizeof folder, "CG\\%s", b2);
        }
        wchar_t wroot[1024], wfolder[1024], wfoldername[512];
        get_dl_root(wroot, 1024);
        utf8_to_wide(folder, wfoldername, 512);
        wpath_join(wfolder, 1024, wroot, wfoldername);
        mkdirs(wfolder);

        /* すべて自動ダウンロード(ムービー + ペア音声)。選択メニューは出さない */
        printf("自動で %d 件のファイルをダウンロード(ムービー+ペア音声)...\n", n2);
        for (int i = 0; i < n2; i++)
            download_item(&items[i], wfolder);

        /* ダウンロード後に確認: mp4 へアンパックして音声を合成するか */
        char yn[16];
        printf("mp4 にアンパックして音声を合成しますか? (y/n): ");
        if (fgets(yn, sizeof yn, stdin) && (yn[0] == 'y' || yn[0] == 'Y'))
            unpack_cg_folder(wfolder);
    }
    return 0;
}

/* ================== 第1段階の選択: 共通の「対象を照会」 ================== */

/* 照会結果(id,name)を複数選択で列挙し、選んだ id を out に書き、個数を返す
 * 結果が1件だけなら自動選択。Space は不要 */
static int pick_rows(sqlite3_stmt *stmt, int *out, int max){
    static int row_ids[512];
    static dbdef tmp[513];
    int n = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && n < 512){
        row_ids[n] = sqlite3_column_int(stmt, 0);
        snprintf(tmp[n].name, sizeof tmp[n].name, "%d | %s", row_ids[n],
                 (const char*)sqlite3_column_text(stmt, 1));
        tmp[n].func = NULL;
        tmp[n].state = 0;
        n++;
    }
    sqlite3_finalize(stmt);
    if (n == 0){ printf("一致するレコードがありません\n"); return 0; }
    if (n == 1){ out[0] = row_ids[0]; return 1; }

    snprintf(tmp[n].name, sizeof tmp[n].name, "END");
    tmp[n].func = NULL;
    tmp[n].state = 0;
    int rc = pager_picks("検索結果(Spaceで選択, Enterで確定)", tmp, NULL, NULL, 1);
    if (rc <= 0) return 0;
    int c = 0;
    for (int i = 0; i < n && c < max; i++)
        if (tmp[i].state) out[c++] = row_ids[i];
    return c;
}

/* 楽曲を選択: 曲名(部分一致)または楽曲id を入力 */
static int choose_songs(sqlite3 *db, int *ids, int max){
    char buf[128];
    printf("曲名(部分一致)または楽曲idを入力: ");
    if (fgets(buf, sizeof buf, stdin) == NULL) return 0;
    buf[strcspn(buf, "\r\n")] = 0;
    if (!buf[0]) return 0;

    sqlite3_stmt *stmt = NULL;
    if (isdigit((unsigned char)buf[0])){
        int mid = atoi(buf);
        if (sqlite3_prepare_v2(db, "SELECT id,name FROM music_data WHERE id=?",
                               -1, &stmt, NULL) == SQLITE_OK){
            sqlite3_bind_int(stmt, 1, mid);
            return pick_rows(stmt, ids, max);
        }
        return 0;
    }
    char like[256];
    snprintf(like, sizeof like, "%%%s%%", buf);
    if (sqlite3_prepare_v2(db,
            "SELECT id,name FROM music_data WHERE name LIKE ? ORDER BY id",
            -1, &stmt, NULL) != SQLITE_OK){
        fprintf(stderr, "SQLエラー: %s\n", sqlite3_errmsg(db));
        return 0;
    }
    sqlite3_bind_text(stmt, 1, like, -1, SQLITE_TRANSIENT);
    return pick_rows(stmt, ids, max);
}

/* カードを選択: カード名/キャラ名(部分一致)または カードid/キャラid を入力 */
static int choose_cards(sqlite3 *db, int *ids, int max){
    char buf[128];
    printf("カード名/キャラ名(部分一致)またはidを入力: ");
    if (fgets(buf, sizeof buf, stdin) == NULL) return 0;
    buf[strcspn(buf, "\r\n")] = 0;
    if (!buf[0]) return 0;

    sqlite3_stmt *stmt = NULL;
    if (isdigit((unsigned char)buf[0])){
        int nid = atoi(buf);
        /* 先にカードidで照会。無ければキャラidで照会 */
        if (sqlite3_prepare_v2(db, "SELECT id,name FROM card_data WHERE id=?",
                               -1, &stmt, NULL) == SQLITE_OK){
            sqlite3_bind_int(stmt, 1, nid);
            int r = pick_rows(stmt, ids, max);
            if (r > 0) return r;
        }
        if (sqlite3_prepare_v2(db,
                "SELECT c.id,c.name FROM card_data c WHERE c.chara_id=? ORDER BY c.id",
                -1, &stmt, NULL) == SQLITE_OK){
            sqlite3_bind_int(stmt, 1, nid);
            return pick_rows(stmt, ids, max);
        }
        return 0;
    }
    /* カード名またはキャラ名の部分一致 */
    char like[256];
    snprintf(like, sizeof like, "%%%s%%", buf);
    if (sqlite3_prepare_v2(db,
            "SELECT c.id,c.name FROM card_data c WHERE c.name LIKE ? "
            "OR c.chara_id IN (SELECT chara_id FROM chara_data WHERE name LIKE ?) "
            "ORDER BY c.id",
            -1, &stmt, NULL) != SQLITE_OK){
        fprintf(stderr, "SQLエラー: %s\n", sqlite3_errmsg(db));
        return 0;
    }
    sqlite3_bind_text(stmt, 1, like, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, like, -1, SQLITE_TRANSIENT);
    return pick_rows(stmt, ids, max);
}

/* ================== 楽曲 ================== */

static int browse_song(sqlite3 *db, sqlite3 *rdb){
    int ids[32];
    int nids = choose_songs(db, ids, 32);
    if (nids <= 0) return 0;

    for (int s = 0; s < nids; s++){
        int id = ids[s];
        char sname[128] = "";
        sqlite3_stmt *nstmt = NULL;
        if (sqlite3_prepare_v2(db, "SELECT name FROM music_data WHERE id=?",
                               -1, &nstmt, NULL) == SQLITE_OK){
            sqlite3_bind_int(nstmt, 1, id);
            if (sqlite3_step(nstmt) == SQLITE_ROW)
                snprintf(sname, sizeof sname, "%s",
                         (const char*)sqlite3_column_text(nstmt, 0));
            sqlite3_finalize(nstmt);
        }
        printf("\n========== %d|%s ==========\n", id, sname);

        static BItem items[MAX_ITEMS];
        int n = 0;
        char res[256];

        /* 音声 */
        snprintf(res, sizeof res, "l/song_%d.acb", id);
        add_res(rdb, items, &n, res, L"acbファイル");
        /* ジャケット */
        sqlite3_stmt *jstmt = NULL;
        if (sqlite3_prepare_v2(db,
                "SELECT jacket_id FROM live_data WHERE music_data_id=? AND jacket_id > 0 LIMIT 1",
                -1, &jstmt, NULL) == SQLITE_OK){
            sqlite3_bind_int(jstmt, 1, id);
            if (sqlite3_step(jstmt) == SQLITE_ROW){
                snprintf(res, sizeof res, "jacket_%d.unity3d",
                         sqlite3_column_int(jstmt, 0));
                add_res(rdb, items, &n, res, L"ジャケット");
            }
            sqlite3_finalize(jstmt);
        }
        /* 譜面 + ステージ */
        sqlite3_stmt *lstmt = NULL;
        if (sqlite3_prepare_v2(db,
                "SELECT id, live_bg FROM live_data WHERE music_data_id=? ORDER BY id",
                -1, &lstmt, NULL) == SQLITE_OK){
            sqlite3_bind_int(lstmt, 1, id);
            while (sqlite3_step(lstmt) == SQLITE_ROW && n < MAX_ITEMS){
                int live_id = sqlite3_column_int(lstmt, 0);
                int live_bg = sqlite3_column_int(lstmt, 1);
                snprintf(res, sizeof res, "musicscores_m%d.bdb", live_id);
                add_res(rdb, items, &n, res, L"譜面");
                if (live_bg > 0){
                    snprintf(res, sizeof res, "3d_stage_%d.unity3d", live_bg);
                    add_res(rdb, items, &n, res, L"ステージ");
                    snprintf(res, sizeof res, "3d_stage_%d_hq.unity3d", live_bg);
                    add_res(rdb, items, &n, res, L"ステージ");
                }
            }
            sqlite3_finalize(lstmt);
        }
        /* モーション */
        char like[64];
        snprintf(like, sizeof like, "3d_cutt_an_chr_son%d%%", id);
        sqlite3_stmt *mstmt = NULL;
        if (sqlite3_prepare_v2(rdb,
                "SELECT name,hash FROM manifests WHERE name LIKE ? ORDER BY name",
                -1, &mstmt, NULL) == SQLITE_OK){
            sqlite3_bind_text(mstmt, 1, like, -1, SQLITE_TRANSIENT);
            while (sqlite3_step(mstmt) == SQLITE_ROW && n < MAX_ITEMS){
                snprintf(items[n].name, sizeof items[n].name, "%s",
                         (const char*)sqlite3_column_text(mstmt, 0));
                snprintf(items[n].hash, sizeof items[n].hash, "%s",
                         (const char*)sqlite3_column_text(mstmt, 1));
                snprintf(items[n].disp, sizeof items[n].disp, "%s", items[n].name);
                wcscpy(items[n].sub, L"モーション");
                n++;
            }
            sqlite3_finalize(mstmt);
        }

        if (n == 0){ printf("ダウンロードできるリソースがありません\n"); continue; }
        static dbdef tmp[MAX_ITEMS + 1];
        static int picked[MAX_ITEMS];
        make_menu(tmp, items, n);
        int rc = pager_picks("楽曲リソース(Spaceで選択, Enterでダウンロード)", tmp, db, rdb, 1);
        if (rc <= 0){ if (rc == -1) printf("キャンセルしました\n"); continue; }

        int c = collect(tmp, n, picked);
        char folder[512];
        snprintf(folder, sizeof folder, "%d%s", id, sname);
        wchar_t wroot[1024], wfolder[1024], wfoldername[512];
        get_dl_root(wroot, 1024);
        utf8_to_wide(folder, wfoldername, 512);
        wpath_join(wfolder, 1024, wroot, wfoldername);
        mkdirs(wfolder);
        for (int i = 0; i < c; i++)
            download_item(&items[picked[i]], wfolder);
        printf("合計 %d 件をダウンロード -> %ls\n", c, wfolder);
    }
    return 0;
}

/* ================== カード ================== */

static int browse_card(sqlite3 *db, sqlite3 *rdb){
    int ids[64];
    int nids = choose_cards(db, ids, 64);
    if (nids <= 0) return 0;

    for (int s = 0; s < nids; s++){
        int card_id = ids[s];
        sqlite3_stmt *stmt = NULL;
        if (sqlite3_prepare_v2(db,
                "SELECT id,name,chara_id,open_dress_id FROM card_data WHERE id=?",
                -1, &stmt, NULL) != SQLITE_OK){
            fprintf(stderr, "SQLエラー: %s\n", sqlite3_errmsg(db));
            continue;
        }
        sqlite3_bind_int(stmt, 1, card_id);
        if (sqlite3_step(stmt) != SQLITE_ROW){
            sqlite3_finalize(stmt);
            continue;
        }
        char cname[128];
        snprintf(cname, sizeof cname, "%s",
                 (const char*)sqlite3_column_text(stmt, 1));
        int chara_id = sqlite3_column_int(stmt, 2);
        int dress_id = sqlite3_column_int(stmt, 3);
        sqlite3_finalize(stmt);
        printf("\n========== %d|%s ==========\n", card_id, cname);

        static BItem items[MAX_ITEMS];
        int n = 0;
        char res[256];
        const char *sizes[6] = {"circle","sm","s","m","l","xl"};
        for (int i = 0; i < 6; i++){
            snprintf(res, sizeof res, "card_%d_%s.unity3d", card_id, sizes[i]);
            add_res(rdb, items, &n, res, L"カードイラスト");
        }
        snprintf(res, sizeof res, "card_bg_%d.unity3d", card_id);
        add_res(rdb, items, &n, res, L"背景");
        snprintf(res, sizeof res, "card_bg_%d_01.unity3d", card_id);
        add_res(rdb, items, &n, res, L"背景");
        snprintf(res, sizeof res, "card_bg_%d_s.unity3d", card_id);
        add_res(rdb, items, &n, res, L"背景");
        snprintf(res, sizeof res, "card_cartoon_%d.unity3d", card_id);
        add_res(rdb, items, &n, res, L"Live2D");
        snprintf(res, sizeof res, "idol_3d_%d_l.unity3d", card_id);
        add_res(rdb, items, &n, res, L"3Dフォト");
        snprintf(res, sizeof res, "idol_3d_%d_s.unity3d", card_id);
        add_res(rdb, items, &n, res, L"3Dフォト");
        snprintf(res, sizeof res, "v/card_%d.acb", card_id);
        add_res(rdb, items, &n, res, L"ボイス");
        snprintf(res, sizeof res, "card_spine_%d.unity3d", card_id);
        add_res(rdb, items, &n, res, L"Spine");
        add_res(rdb, items, &n, "spine_sprachen_petit_chara_common.unity3d", L"Spine");
        if (dress_id > 0 && n < MAX_ITEMS){
            snprintf(res, sizeof res, "3d_chara_body_%04d.unity3d", dress_id);
            add_res(rdb, items, &n, res, L"3Dモデル");
            snprintf(res, sizeof res, "3d_chara_head_%04d_%04d_hq.unity3d",
                     chara_id, dress_id);
            if (get_hash(rdb, res, items[n].hash, 64) != 0)
                snprintf(res, sizeof res, "3d_chara_head_%04d_%04d.unity3d",
                         chara_id, dress_id);
            add_res(rdb, items, &n, res, L"3Dモデル");
            snprintf(res, sizeof res, "3d_md_body%04d_hq.unity3d", dress_id);
            if (get_hash(rdb, res, items[n].hash, 64) != 0)
                snprintf(res, sizeof res, "3d_md_body%04d.unity3d", dress_id);
            add_res(rdb, items, &n, res, L"3Dモデル");
            const char *tx[3] = {"hq","multi","spec"};
            for (int i = 0; i < 3; i++){
                snprintf(res, sizeof res, "3d_tx_body%04d_%s.unity3d", dress_id, tx[i]);
                add_res(rdb, items, &n, res, L"3Dモデル");
            }
        }

        if (n == 0){ printf("ダウンロードできるリソースがありません\n"); continue; }
        static dbdef tmp[MAX_ITEMS + 1];
        static int picked[MAX_ITEMS];
        make_menu(tmp, items, n);
        int rc = pager_picks("カードリソース(Spaceで選択, Enterでダウンロード)", tmp, db, rdb, 1);
        if (rc <= 0){ if (rc == -1) printf("キャンセルしました\n"); continue; }

        int c = collect(tmp, n, picked);
        char folder[512];
        snprintf(folder, sizeof folder, "%d%s", card_id, cname);
        wchar_t wroot[1024], wfolder[1024], wfoldername[512];
        get_dl_root(wroot, 1024);
        utf8_to_wide(folder, wfoldername, 512);
        wpath_join(wfolder, 1024, wroot, wfoldername);
        mkdirs(wfolder);
        for (int i = 0; i < c; i++)
            download_item(&items[picked[i]], wfolder);
        printf("合計 %d 件をダウンロード -> %ls\n", c, wfolder);
    }
    return 0;
}

/* ================== ボイス ================== */

static int browse_voice(sqlite3 *db, sqlite3 *rdb){
    int ids[64];
    int nids = choose_cards(db, ids, 64);   /* カードの検索ロジックを再利用 */
    if (nids <= 0) return 0;

    for (int s = 0; s < nids; s++){
        int card_id = ids[s];
        sqlite3_stmt *stmt = NULL;
        if (sqlite3_prepare_v2(db, "SELECT id,name FROM card_data WHERE id=?",
                               -1, &stmt, NULL) == SQLITE_OK){
            sqlite3_bind_int(stmt, 1, card_id);
            if (sqlite3_step(stmt) != SQLITE_ROW){
                sqlite3_finalize(stmt);
                continue;
            }
            char cname[128];
            snprintf(cname, sizeof cname, "%s",
                     (const char*)sqlite3_column_text(stmt, 1));
            sqlite3_finalize(stmt);

            static BItem items[8];
            int n = 0;
            char res[256];
            snprintf(res, sizeof res, "v/card_%d.acb", card_id);
            add_res(rdb, items, &n, res, L"ボイス");
            if (n == 0) continue;

            char folder[512];
            snprintf(folder, sizeof folder, "%d%s", card_id, cname);
            wchar_t wroot[1024], wfolder[1024], wfoldername[512];
            get_dl_root(wroot, 1024);
            utf8_to_wide(folder, wfoldername, 512);
            wpath_join(wfolder, 1024, wroot, wfoldername);
            mkdirs(wfolder);
            download_item(&items[0], wfolder);
            printf("ボイスをダウンロードしました -> %ls\n", wfolder);
        }
    }
    return 0;
}

/* ================== モジュール入口 ================== */

int browse_main(void){
    sqlite3 *db = NULL, *rdb = NULL;
    const char *mp = find_manifest();
    if (GetFileAttributesA(DB_PATH) == INVALID_FILE_ATTRIBUTES){
        browse_fail("master.mdb がありません。CGSS_Script と同じディレクトリに置いてください");
        return -1;
    }
    if (!mp){
        browse_fail("manifest_*.db（リソースマニフェストDB）がありません。先に check_update.exe を実行して取得してください");
        return -1;
    }
    if (sqlite3_open(DB_PATH, &db) != SQLITE_OK){
        browse_fail("master.mdb を開けませんでした");
        return -1;
    }
    if (sqlite3_open(mp, &rdb) != SQLITE_OK){
        fprintf(stderr, "%s を開けませんでした（%s）\n", mp, sqlite3_errmsg(rdb));
        fprintf(stderr, "Enterでメニューに戻る\n");
        fflush(stderr);
        _getch();
        sqlite3_close(db);
        return -1;
    }

    dbdef menu[] = {
        {"1.自由検索(リソース名)", browse_all, 0},
        {"2.BGM", browse_bgm, 0},
        {"3.楽曲(名/id)", browse_song, 0},
        {"4.カード(名/キャラ名/id)", browse_card, 0},
        {"5.キャラボイス(名/id)", browse_voice, 0},
        {"6.譜面(曲名/id)", browse_chart, 0},
        {"7.ステージ(曲名/id)", browse_stage, 0},
        {"8.モーション(曲名/id)", browse_action, 0},
        {"9.3Dモデル(カード名/キャラ名/id)", browse_model, 0},
        {"10.Spine SDキャラ(カード名/キャラ名/id)", browse_spine, 0},
        {"11.ステッカー", browse_sticker, 0},
        {"12.CGムービー(キーワード)", browse_cg, 0},
        {"13.戻る", NULL, 0},
        {"END", NULL, 0}
    };
    while (1){
        int rc = pager_picks("リソース検索とダウンロード", menu, db, rdb, 0);
        if (rc == -1)
            continue;
        if (rc == 12)
            break;
    }
    sqlite3_close(rdb);
    sqlite3_close(db);
    return 0;
}
