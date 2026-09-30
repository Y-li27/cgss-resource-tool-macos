// download.c: データのダウンロードと解析（メニュー2：カード／楽曲／キャラ一括）
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "sqlite3.h"
#include "download.h"
#include "net.h"
#include "util.h"
#include "sticker.h"
#include "paper.h"
#define DB_PATH "master.mdb"

typedef struct {
    char name[256];
    char hash[64];
    wchar_t sub[64];
} ResItem;

static int get_hash(sqlite3 *rdb, const char *name, char *hash_out, int n){
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(rdb, "SELECT hash FROM manifests WHERE name=?", -1, &stmt, NULL) != SQLITE_OK)
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


static void add_item(sqlite3 *rdb, ResItem *items, int *n, const char *name, const wchar_t *sub){
    if (*n >= 64) return;
    if (get_hash(rdb, name, items[*n].hash, 64) != 0){
        printf("マニフェストに %s なし\n", name);
        return;
    }
    snprintf(items[*n].name, sizeof items[*n].name, "%s", name);
    wcscpy(items[*n].sub, sub);
    (*n)++;
}


static void download_items(ResItem *items, int n, const wchar_t *wfolder){
    for (int i = 0; i < n; i++){
        wchar_t wsub[1024];
        wpath_join(wsub, 1024, wfolder, items[i].sub);
        mkdirs(wsub);
        dl_one(items[i].name, items[i].hash, wsub);
    }
    printf("リソース合計 %d 件\n", n);
}
/* ================== メニュー2：カードリソースのダウンロード ================== */

/* カード id で照会。成功なら 0 を返し cname/chara_id/dress_id を埋める */

static int query_card(sqlite3 *db, int card_id, char *cname, int n, int *chara_id, int *dress_id){
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db,
            "SELECT id,name,chara_id,open_dress_id FROM card_data WHERE id=?",
            -1, &stmt, NULL) != SQLITE_OK){
        fprintf(stderr, "SQLエラー: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    sqlite3_bind_int(stmt, 1, card_id);
    if (sqlite3_step(stmt) != SQLITE_ROW){
        fprintf(stderr, "該当するカードがありません\n");
        sqlite3_finalize(stmt);
        return -1;
    }
    snprintf(cname, n, "%s", (const char*)sqlite3_column_text(stmt, 1));
    *chara_id = sqlite3_column_int(stmt, 2);
    *dress_id = sqlite3_column_int(stmt, 3);
    sqlite3_finalize(stmt);
    return 0;
}


static void print_card_res_menu(void){
    printf("選択できるリソース（Space/カンマ区切りの数字、a=すべて、0=ダウンロード開始）：\n");
    printf("1.カードイラスト(6サイズ)\t2.背景(通常/縦版/小横版)\t3.カードイラストSpinaアニメ(beta)\n");
    printf("4.3Dフォト(L/S)\t5.ボイス\t6.Spine SDキャラ(beta)\t7.3Dモデル\t8.セリフテキスト\n");
}

/* カードごとに選択したリソースを組み立ててダウンロード（カードイラスト/背景/カードイラストSpinaアニメ/3Dフォト/ボイス/spine/3dモデル/セリフ） */

static void dl_card_resources(sqlite3 *db, sqlite3 *rdb, int card_id, const char *cname,
                              int chara_id, int dress_id, const int *sel, int nsel){
    /* キャラディレクトリ: CGSS_DOWN\{id}{name} */
    char folder[512];
    snprintf(folder, sizeof folder, "%d%s", card_id, cname);
    wchar_t wroot[1024], wfolder[1024], wfoldername[512];
    get_dl_root(wroot, 1024);
    utf8_to_wide(folder, wfoldername, 512);
    wpath_join(wfolder, 1024, wroot, wfoldername);
    mkdirs(wfolder);

    ResItem items[64];
    int n = 0;
    char res[256];
    if (selected(sel, nsel, 1)){
        const char *sizes[6] = {"circle","sm","s","m","l","xl"};
        for (int i = 0; i < 6; i++){
            snprintf(res, sizeof res, "card_%d_%s.unity3d", card_id, sizes[i]);
            add_item(rdb, items, &n, res, L"カードイラスト");
        }
    }
    if (selected(sel, nsel, 2)){
        snprintf(res, sizeof res, "card_bg_%d.unity3d", card_id);
        add_item(rdb, items, &n, res, L"背景");
        snprintf(res, sizeof res, "card_bg_%d_01.unity3d", card_id);
        add_item(rdb, items, &n, res, L"背景");
        snprintf(res, sizeof res, "card_bg_%d_s.unity3d", card_id);
        add_item(rdb, items, &n, res, L"背景");
    }
    if (selected(sel, nsel, 3)){
        snprintf(res, sizeof res, "card_cartoon_%d.unity3d", card_id);
        add_item(rdb, items, &n, res, L"カードイラストSpinaアニメ");
    }
    if (selected(sel, nsel, 4)){
        snprintf(res, sizeof res, "idol_3d_%d_l.unity3d", card_id);
        add_item(rdb, items, &n, res, L"3Dフォト");
        snprintf(res, sizeof res, "idol_3d_%d_s.unity3d", card_id);
        add_item(rdb, items, &n, res, L"3Dフォト");
    }
    if (selected(sel, nsel, 5)){
        snprintf(res, sizeof res, "v/card_%d.acb", card_id);
        add_item(rdb, items, &n, res, L"ボイス");
    }
    if (selected(sel, nsel, 6)){
        snprintf(res, sizeof res, "card_spine_%d.unity3d", card_id);
        add_item(rdb, items, &n, res, L"spine");
        /* 共有SDキャラのスケルトン（SPSprachen）。カードイラストのSDキャラと一緒にダウンロードし、アンパック時に自動で JSON へ変換 */
        add_item(rdb, items, &n, "spine_sprachen_petit_chara_common.unity3d", L"spine");
    }
    if (selected(sel, nsel, 7)){
        if (dress_id > 0){
            snprintf(res, sizeof res, "3d_chara_body_%04d.unity3d", dress_id);
            add_item(rdb, items, &n, res, L"3dモデル");
            /* 頭部モデルは _hq を優先（M_Head/M_Cheek メッシュと頭部テクスチャを含む）。マニフェストに無ければ通常版 */
            snprintf(res, sizeof res, "3d_chara_head_%04d_%04d_hq.unity3d", chara_id, dress_id);
            if (get_hash(rdb, res, items[n].hash, 64) != 0)
                snprintf(res, sizeof res, "3d_chara_head_%04d_%04d.unity3d", chara_id, dress_id);
            add_item(rdb, items, &n, res, L"3dモデル");
            snprintf(res, sizeof res, "3d_md_body%04d_hq.unity3d", dress_id);
            if (get_hash(rdb, res, items[n].hash, 64) != 0)
                snprintf(res, sizeof res, "3d_md_body%04d.unity3d", dress_id);
            add_item(rdb, items, &n, res, L"3dモデル");
            const char *tx[3] = {"hq","multi","spec"};
            for (int i = 0; i < 3; i++){
                snprintf(res, sizeof res, "3d_tx_body%04d_%s.unity3d", dress_id, tx[i]);
                add_item(rdb, items, &n, res, L"3dモデル");
            }
        } else {
            fprintf(stderr, "このカードに専用衣装がないため、3Dモデルをスキップ\n");
        }
    }
    if (selected(sel, nsel, 8)){
        sqlite3_stmt *cstmt = NULL;
        if (sqlite3_prepare_v2(db,
                "SELECT use_type, `index`, discription FROM card_comments WHERE id=? ORDER BY use_type, `index`",
                -1, &cstmt, NULL) == SQLITE_OK){
            sqlite3_bind_int(cstmt, 1, card_id);
            wchar_t wtextdir[1024], wtxt[1200], wtxtname[128];
            char txtname[128];
            snprintf(txtname, sizeof txtname, "card_%d_セリフ.txt", card_id);
            utf8_to_wide(txtname, wtxtname, 128);
            wpath_join(wtextdir, 1024, wfolder, L"セリフ");
            mkdirs(wtextdir);
            wpath_join(wtxt, 1200, wtextdir, wtxtname);
            FILE *tf = _wfopen(wtxt, L"wb");
            if (tf){
                int nlines = 0;
                while (sqlite3_step(cstmt) == SQLITE_ROW){
                    fprintf(tf, "[%d-%d] %s\n",
                            sqlite3_column_int(cstmt, 0),
                            sqlite3_column_int(cstmt, 1),
                            (const char*)sqlite3_column_text(cstmt, 2));
                    nlines++;
                }
                fclose(tf);
                printf("セリフを %d 件書き出し -> セリフ\\card_%d_セリフ.txt\n", nlines, card_id);
            }
            else{
                char errbuf[1200];
                wide_to_utf8(wtxt,errbuf,1200);
                fprintf(stderr,"%sの作成に失敗\n",errbuf);
            }
            sqlite3_finalize(cstmt);
        }
        else{
            fprintf(stderr,"データベースの検索に失敗。改ざんされていないか確認してください\n");
        }
    }
    download_items(items, n, wfolder);
}

/* カード id でダウンロード */

static int dl_card(sqlite3 *db, sqlite3 *rdb){
    char buf[64];
    printf("カードidを入力してください\n");
    if (fgets(buf, sizeof buf, stdin) == NULL) return -1;
    int card_id = atoi(buf);
    if (card_id <= 0){ fprintf(stderr, "入力エラー\n"); return -1; }

    char cname[128];
    int chara_id = 0, dress_id = 0;
    if (query_card(db, card_id, cname, sizeof cname, &chara_id, &dress_id) != 0) return -1;
    printf("%d|%s\n", card_id, cname);

    print_card_res_menu();
    if (fgets(buf, sizeof buf, stdin) == NULL) return -1;
    int sel[64], nsel = parse_multi(buf, sel, 8);
    if (nsel < 0){ nsel = 8; for (int i = 0; i < 8; i++) sel[i] = i + 1; }
    if (nsel == 0) return -1;
    dl_card_resources(db, rdb, card_id, cname, chara_id, dress_id, sel, nsel);
    return 0;
}

/* キャラ chara_id で一括ダウンロード：全カードを一覧。複数選択または a ですべて。リソース種別は一度だけ聞く */

static int dl_chara(sqlite3 *db, sqlite3 *rdb){
    char buf[128];
    printf("キャラidを入力してください（chara_id。カードidでも自動判定）：\n");
    if (fgets(buf, sizeof buf, stdin) == NULL) return -1;
    int chara_id = atoi(buf);
    if (chara_id <= 0){ fprintf(stderr, "入力エラー\n"); return -1; }

    /* 自動判定：カードidでも可。まずその数がキャラidか見る（そのキャラにカードがあるか）。
       無ければカードidとして再照会し、本当のキャラidを得る */
    sqlite3_stmt *chk = NULL;
    int cnt = 0;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM card_data WHERE chara_id=?", -1, &chk, NULL) == SQLITE_OK){
        sqlite3_bind_int(chk, 1, chara_id);
        if (sqlite3_step(chk) == SQLITE_ROW) cnt = sqlite3_column_int(chk, 0);
        sqlite3_finalize(chk);
    }
    if (cnt == 0){
        chk = NULL;
        if (sqlite3_prepare_v2(db, "SELECT chara_id,name FROM card_data WHERE id=?", -1, &chk, NULL) == SQLITE_OK){
            sqlite3_bind_int(chk, 1, chara_id);
            if (sqlite3_step(chk) == SQLITE_ROW){
                int real = sqlite3_column_int(chk, 0);
                printf("%d はカードidと検出 -> キャラid %d（%s）\n", chara_id, real,
                       (const char*)sqlite3_column_text(chk, 1));
                chara_id = real;
            }
            sqlite3_finalize(chk);
        }
    }

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db,
            "SELECT id,name,open_dress_id FROM card_data WHERE chara_id=? ORDER BY id",
            -1, &stmt, NULL) != SQLITE_OK){
        fprintf(stderr, "SQLエラー: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    sqlite3_bind_int(stmt, 1, chara_id);
    int ids[128], ncards = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && ncards < 128){
        int cid = sqlite3_column_int(stmt, 0);
        ids[ncards++] = cid;
        printf("[%d] %d | %s | dress=%d\n", ncards, cid,
               (const char*)sqlite3_column_text(stmt, 1),
               sqlite3_column_int(stmt, 2));
    }
    sqlite3_finalize(stmt);
    if (ncards == 0){ fprintf(stderr, "このキャラにカードがありません\n"); return -1; }

    printf("ダウンロードするカードを選択（Space/カンマ区切りの数字、a=すべて、0=戻る）：");
    if (fgets(buf, sizeof buf, stdin) == NULL) return -1;
    int card_sel[128], ncard_sel = parse_multi(buf, card_sel, ncards);
    if (ncard_sel < 0){
        ncard_sel = ncards;
        for (int i = 0; i < ncards; i++) card_sel[i] = i + 1;
    }
    if (ncard_sel == 0) return -1;

    print_card_res_menu();
    if (fgets(buf, sizeof buf, stdin) == NULL) return -1;
    int sel[64], nsel = parse_multi(buf, sel, 8);
    if (nsel < 0){ nsel = 8; for (int i = 0; i < 8; i++) sel[i] = i + 1; }
    if (nsel == 0) return -1;

    for (int s = 0; s < ncard_sel; s++){
        int card_id = ids[card_sel[s] - 1];
        char cname[128];
        int ch = 0, dress = 0;
        if (query_card(db, card_id, cname, sizeof cname, &ch, &dress) != 0) continue;
        printf("\nダウンロード %d|%s\n", card_id, cname);
        dl_card_resources(db, rdb, card_id, cname, ch, dress, sel, nsel);
    }
    printf("キャラ一括ダウンロード完了\n");
    return 0;
}

/* ================== メニュー2：楽曲リソースのダウンロード ================== */


static int dl_song(sqlite3 *db, sqlite3 *rdb){
    char buf[64];
    printf("楽曲idを入力してください\n");
    if (fgets(buf, sizeof buf, stdin) == NULL) return -1;
    int music_id = atoi(buf);
    if (music_id <= 0){ fprintf(stderr, "入力エラー\n"); return -1; }

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db,
            "SELECT id,name FROM music_data WHERE id=?",
            -1, &stmt, NULL) != SQLITE_OK){
        fprintf(stderr, "SQLエラー: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    sqlite3_bind_int(stmt, 1, music_id);
    if (sqlite3_step(stmt) != SQLITE_ROW){
        fprintf(stderr, "該当する楽曲がありません\n");
        sqlite3_finalize(stmt);
        return -1;
    }
    int id = sqlite3_column_int(stmt, 0);
    char sname[128];
    snprintf(sname, sizeof sname, "%s", (const char*)sqlite3_column_text(stmt, 1));
    printf("%d|%s\n", id, sname);
    sqlite3_finalize(stmt);

    char folder[512];
    snprintf(folder, sizeof folder, "%d%s", id, sname);
    wchar_t wroot[1024], wfolder[1024], wfoldername[512];
    get_dl_root(wroot, 1024);
    utf8_to_wide(folder, wfoldername, 512);
    wpath_join(wfolder, 1024, wroot, wfoldername);
    mkdirs(wfolder);

    printf("選択できるリソース（Space/カンマ区切りの数字、a=すべて、0=ダウンロード開始）：\n");
    printf("1.音声(acb)\t2.ジャケット(jacket)\t3.モーション\n");
    printf("4.譜面\t5.ステージ\t6.ディレクターパック(カメラ/表情/フォーメーション)\t7.すべて\n");
    if (fgets(buf, sizeof buf, stdin) == NULL) return -1;
    int sel[64], nsel = parse_multi(buf, sel, 7);
    if (nsel < 0){ nsel = 7; for (int i = 0; i < 7; i++) sel[i] = i + 1; }
    if (nsel == 0) return -1;

    ResItem items[64];
    int n = 0;
    char res[256];
    if (selected(sel, nsel, 1)){
        snprintf(res, sizeof res, "l/song_%d.acb", id);
        add_item(rdb, items, &n, res, L"acbファイル");
    }
    if (selected(sel, nsel, 2)){
        /* ジャケット = jacket_{jacket_id}。jacket_id は live_data から検索 */
        sqlite3_stmt *jstmt = NULL;
        if (sqlite3_prepare_v2(db,
                "SELECT jacket_id FROM live_data WHERE music_data_id=? AND jacket_id > 0 LIMIT 1",
                -1, &jstmt, NULL) == SQLITE_OK){
            sqlite3_bind_int(jstmt, 1, id);
            if (sqlite3_step(jstmt) == SQLITE_ROW){
                snprintf(res, sizeof res, "jacket_%d.unity3d", sqlite3_column_int(jstmt, 0));
                add_item(rdb, items, &n, res, L"ジャケット");
            }
            sqlite3_finalize(jstmt);
        }
    }
    if (selected(sel, nsel, 3)){
        char like[64];
        snprintf(like, sizeof like, "3d_cutt_an_chr_son%d%%", id);
        sqlite3_stmt *mstmt = NULL;
        if (sqlite3_prepare_v2(rdb,
                "SELECT name,hash FROM manifests WHERE name LIKE ? ORDER BY name",
                -1, &mstmt, NULL) == SQLITE_OK){
            sqlite3_bind_text(mstmt, 1, like, -1, SQLITE_TRANSIENT);
            while (sqlite3_step(mstmt) == SQLITE_ROW && n < 64){
                snprintf(items[n].name, sizeof items[n].name, "%s",
                         (const char*)sqlite3_column_text(mstmt, 0));
                snprintf(items[n].hash, sizeof items[n].hash, "%s",
                         (const char*)sqlite3_column_text(mstmt, 1));
                wcscpy(items[n].sub, L"モーション");
                n++;
            }
            sqlite3_finalize(mstmt);
        }
    }
    if (selected(sel, nsel, 4) || selected(sel, nsel, 5)){
        sqlite3_stmt *lstmt = NULL;
        if (sqlite3_prepare_v2(db,
                "SELECT id, live_bg FROM live_data WHERE music_data_id=? ORDER BY id",
                -1, &lstmt, NULL) == SQLITE_OK){
            sqlite3_bind_int(lstmt, 1, id);
            while (sqlite3_step(lstmt) == SQLITE_ROW && n < 64){
                int live_id = sqlite3_column_int(lstmt, 0);
                int live_bg = sqlite3_column_int(lstmt, 1);
                if (selected(sel, nsel, 4)){
                    snprintf(res, sizeof res, "musicscores_m%d.bdb", live_id);
                    add_item(rdb, items, &n, res, L"譜面");
                }
                if (selected(sel, nsel, 5) && live_bg > 0){
                    snprintf(res, sizeof res, "3d_stage_%d.unity3d", live_bg);
                    add_item(rdb, items, &n, res, L"ステージ");
                    snprintf(res, sizeof res, "3d_stage_%d_hq.unity3d", live_bg);
                    add_item(rdb, items, &n, res, L"ステージ");
                }
            }
            sqlite3_finalize(lstmt);
        }
    }
    if (selected(sel, nsel, 6)){
        char kw[128] = "";
        printf("ディレクターパックのキーワードを入力（例: koicover、Enterで全件一覧）：");
        if (fgets(kw, sizeof kw, stdin)) kw[strcspn(kw, "\r\n")] = 0;
        char like[256];
        if (kw[0]) snprintf(like, sizeof like, "3d_cutt_%s%%", kw);
        else snprintf(like, sizeof like, "3d_cutt_%%");
        sqlite3_stmt *mstmt = NULL;
        if (sqlite3_prepare_v2(rdb,
                "SELECT name,hash FROM manifests WHERE name LIKE ? AND name NOT LIKE '3d_cutt_an_chr%' ORDER BY name",
                -1, &mstmt, NULL) == SQLITE_OK){
            sqlite3_bind_text(mstmt, 1, like, -1, SQLITE_TRANSIENT);
            ResItem tmp[64];
            int tn = 0;
            while (sqlite3_step(mstmt) == SQLITE_ROW && tn < 64){
                snprintf(tmp[tn].name, sizeof tmp[tn].name, "%s", (const char*)sqlite3_column_text(mstmt, 0));
                snprintf(tmp[tn].hash, sizeof tmp[tn].hash, "%s", (const char*)sqlite3_column_text(mstmt, 1));
                wcscpy(tmp[tn].sub, L"ディレクターパック");
                tn++;
            }
            sqlite3_finalize(mstmt);
            if (tn == 0){
                printf("一致するディレクターパックがありません\n");
            } else if (tn > 40){
                printf("一致 %d 件は多すぎます。もっと具体的なキーワードを入力してください\n", tn);
            } else {
                printf("一致 %d 件：\n", tn);
                for (int i = 0; i < tn; i++) printf("[%d] %s\n", i + 1, tmp[i].name);
                printf("選択（Space区切りの数字、a=すべて、0=スキップ）：");
                fgets(buf, sizeof buf, stdin);
                int sel2[64], n2 = parse_multi(buf, sel2, tn);
                if (n2 < 0){ n2 = tn; for (int i = 0; i < tn; i++) sel2[i] = i + 1; }
                for (int i = 0; i < n2 && n < 64; i++){
                    items[n] = tmp[sel2[i] - 1];
                    n++;
                }
            }
        }
    }
    download_items(items, n, wfolder);
    return 0;
}

/* ================== メニュー2：ステッカーモーション(310件) ==================
 * spine_motion_sticker_XXXXX.unity3d をすべて CGSS_DOWN\ステッカー\ へダウンロード。
 * 元ファイルunity3d / spineファイル / ステッカーPNG の3サブディレクトリ：
 *   - 元ファイルunity3d: LZ4 展開後の unity3d パッケージ
 *   - spineファイル\SPMotionSticker_XXXXX: skel + atlas + png（あわせて自動で json 化）
 *   - ステッカーPNG: 各ステッカーを atlas から _1.png / _2.png の2フレームに切り出す */
static int dl_sticker(sqlite3 *db, sqlite3 *rdb){
    (void)db;   /* ステッカーは rdb のみ。引数は dbdef の func シグネチャに合わせる */
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(rdb,
            "SELECT name,hash FROM manifests WHERE name LIKE 'spine_motion_sticker_%.unity3d' ORDER BY name",
            -1, &stmt, NULL) != SQLITE_OK){
        fprintf(stderr, "ステッカーマニフェストの照会に失敗: %s\n", sqlite3_errmsg(rdb));
        return -1;
    }
    ResItem *items = (ResItem*)malloc(sizeof(ResItem) * 512);
    if (!items){ sqlite3_finalize(stmt); return -1; }
    int n = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && n < 512){
        snprintf(items[n].name, sizeof items[n].name, "%s",
                 (const char*)sqlite3_column_text(stmt, 0));
        snprintf(items[n].hash, sizeof items[n].hash, "%s",
                 (const char*)sqlite3_column_text(stmt, 1));
        n++;
    }
    sqlite3_finalize(stmt);
    if (n == 0){
        printf("マニフェストに spine_motion_sticker リソースがありません\n");
        free(items);
        return -1;
    }
    printf("マニフェストにステッカーモーションが %d 件。ダウンロード/アンパックを開始...\n", n);

    wchar_t base[1024], wroot[1024], wraw[1300], wspine[1300], wpng[1300];
    get_dl_root(base, 1024);
    wpath_join(wroot, 1024, base, L"ステッカー");
    mkdirs(wroot);
    wpath_join(wraw, 1300, wroot, L"元ファイルunity3d");
    mkdirs(wraw);
    wpath_join(wspine, 1300, wroot, L"spineファイル");
    mkdirs(wspine);
    wpath_join(wpng, 1300, wroot, L"ステッカーPNG");
    mkdirs(wpng);

    int ndl = 0, nunpack = 0;
    for (int i = 0; i < n; i++){
        wchar_t wrawfile[1300], wname[512];
        utf8_to_wide(items[i].name, wname, 512);
        wpath_join(wrawfile, 1300, wraw, wname);
        if (GetFileAttributesW(wrawfile) == INVALID_FILE_ATTRIBUTES){
            if (dl_one(items[i].name, items[i].hash, wraw) == 0)
                ndl++;
        } else {
            printf("[%d/%d] %s は既存です\n", i + 1, n, items[i].name);
            ndl++;
        }
        if (sticker_unpack_file(items[i].name, wraw, wspine, wpng, i))
            nunpack++;
    }
    printf("ステッカーのダウンロード完了：元ファイル %d 件、アンパック %d 件\n", ndl, nunpack);
    free(items);
    return 0;
}

/* その他メニュー。まだ調べ切っていないものを置く */
int dl_other(sqlite3 *db,sqlite3 *rdb){
    (void)db; (void)rdb;
    printf("その他の機能はまだ未実装\n");
    return 0;
}

/* ================== メニュー2の入口 ================== */


int dl_main(void){
    sqlite3 *db = NULL, *rdb = NULL;
    const char *mp = find_manifest();
    if (GetFileAttributesA(DB_PATH) == INVALID_FILE_ATTRIBUTES){
        fprintf(stderr, "master.mdb がありません。プログラムと同じディレクトリに置いてください\n");
        return -1;
    }
    if (!mp){
        fprintf(stderr, "manifest_*.db（リソースマニフェストDB）がありません。先に check_update.exe を実行して取得してください\n");
        return -1;
    }
    if (sqlite3_open(DB_PATH, &db) != SQLITE_OK){
        fprintf(stderr, "master.mdb のオープンに失敗（%s）\n", sqlite3_errmsg(db));
        return -1;
    }
    if (sqlite3_open(mp, &rdb) != SQLITE_OK){
        fprintf(stderr, "%s のオープンに失敗（%s）\n", mp, sqlite3_errmsg(rdb));
        sqlite3_close(db);
        return -1;
    }
    dbdef menu[] = {
        {"1.カードリソース",dl_card,0},
        {"2.楽曲リソース",dl_song,0},
        {"3.一括ダウンロード",dl_chara,0},
        {"4.ステッカー(310件)",dl_sticker,0},
        {"5.その他",dl_other,0},
        {"6.戻る",NULL,0},
        {"END",NULL,0}
    };
    while (1){
        int rc = pager_picks("ダウンロードメニュー",menu,db,rdb,0);
        if(rc == -1)
            continue;
        else if(rc == 5)
            break;        
    }
    sqlite3_close(rdb);
    sqlite3_close(db);
    return 0;
}
/* ================== メニュー6：ACB音楽の抽出とHCAデコード ================== */

typedef struct {
    wchar_t folder[512];
    wchar_t acbdir[512];
    wchar_t acb[512];
    char acb_name[256];
    char folder_name[256];
} AcbItem;


/* .acb の dir を走査し chara_folder に記録 */
