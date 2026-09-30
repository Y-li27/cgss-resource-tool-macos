// unpack.c: アンパックメニュー + 共通アンパック処理
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "util.h"
#include "unpack.h"
#include "acb.h"
#include "paper.h"

void wipe_dir(const wchar_t *dir){
    wchar_t pat[1300];
    swprintf(pat, 1300, L"%ls\\*", dir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        wchar_t full[1300];
        swprintf(full, 1300, L"%ls\\%ls", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY){
            wipe_dir(full);
            RemoveDirectoryW(full);
        } else {
            DeleteFileW(full);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

/* exe と同じディレクトリの AssetStudio\AssetStudio.CLI.exe を探す。無ければ空（呼び出し側が表示） */

void find_assetstudio(wchar_t *out, int n){
    wchar_t exedir[1024];
    GetModuleFileNameW(NULL, exedir, 1024);
    wchar_t *p = wcsrchr(exedir, L'\\');
    if (p) *p = 0;
    wchar_t local[1200];
    /* macOS 移植では AssetStudio.CLI による unity3d 展開は対象外。
       3D/カード画像などは AssetRipper を別途使用する。 */
    swprintf(local, 1200, L"%ls\\AssetStudio\\AssetStudio.CLI.exe", exedir);
    if (GetFileAttributesW(local) != INVALID_FILE_ATTRIBUTES){
        wcscpy(out, local);
    } else {
        out[0] = 0;
    }
}

/* ヒント: 表情アニメはすべてボーンアニメ。ボディのテクスチャ参照欠落は Blender スクリプトで修復 */

void print_gui_guide(const wchar_t *model_dir){
    char gui_u8[1200], dir_u8[1200], tex_u8[1200], sk_u8[1200];
    wchar_t gui[1200];
    /* GUI のパス（exe と同じディレクトリの AssetStudio\AssetStudio.GUI.exe） */
    GetModuleFileNameW(NULL, gui, 1200);
    wchar_t *gp = wcsrchr(gui, L'\\');
    if (gp) *gp = 0;
    wcscat(gui, L"\\AssetStudio\\AssetStudio.GUI.exe");
    if (GetFileAttributesW(gui) == INVALID_FILE_ATTRIBUTES)
        gui[0] = 0;
    wide_to_utf8(gui, gui_u8, sizeof gui_u8);
    wide_to_utf8(model_dir, dir_u8, sizeof dir_u8);
    {
        wchar_t script[1200];
        GetModuleFileNameW(NULL, script, 1200);
        wchar_t *sp = wcsrchr(script, L'\\');
        if (sp) *sp = 0;
        wcscat(script, L"\\cgss_apply_textures.py");
        wide_to_utf8(script, tex_u8, sizeof tex_u8);
        GetModuleFileNameW(NULL, script, 1200);
        sp = wcsrchr(script, L'\\');
        if (sp) *sp = 0;
        wcscat(script, L"\\cgss_anim_to_shapekeys.py");
        wide_to_utf8(script, sk_u8, sizeof sk_u8);
    }
    printf("\n==============================================\n");
    printf("ヒント:\n");
    printf("  1. CLI 書き出しの FBX はスケルトン+メッシュのみで、表情モーションがありません。\n");
    printf("     モーション付き FBX は AssetStudio GUI で全選択して書き出してください:\n");
    if (gui_u8[0])
        printf("     1) 開く: %s\n", gui_u8);
    else
        printf("     1) AssetStudio GUI を開く（未検出。AssetStudio フォルダをプログラムと同じディレクトリに置いてください）\n");
    printf("     2) File -> Load folder で選択: %s\n", dir_u8);
    printf("     3) アセット一覧で Animator(md_chr*_hq) とすべての AnimationClip(an_*_face*) を選択\n");
    printf("        （Ctrl で複数選択）\n");
    printf("     4) Animator を右クリック -> Export selected objects (merge) + Selected AnimationClips\n");
    printf("        FBX は同じディレクトリに出力\n");
    printf("  2. 表情モーション（GUI 書き出し）はボーンアニメで、モデル自体にシェイプキーはありません。\n");
    printf("     シェイプキーが必要なら Blender で cgss_anim_to_shapekeys.py を実行し、表情をシェイプキーにベイクしてください。\n");
    printf("  3. ボディ（md_body）のテクスチャ参照が欠けています。テクスチャは別の tx_body パックにあり、\n");
    printf("     AssetStudio はパックをまたいで解決できないため、FBX に参照すらありません。\n");
    printf("     Blender で cgss_apply_textures.py を実行すれば、マテリアル名で自動的にテクスチャを貼れます。\n");
    printf("Blender スクリプト:\n");
    printf("  %s\n", tex_u8);
    printf("  %s\n", sk_u8);
    printf("==============================================\n\n");
}

/* アンパック済みか: 同じディレクトリに <パック名>.done がある、または対応 .fbx がある（旧書き出し互換） */

int is_done(const wchar_t *dir, const wchar_t *pkg){
    wchar_t buf[1300];
    swprintf(buf, 1300, L"%ls\\%ls.done", dir, pkg);
    if (GetFileAttributesW(buf) != INVALID_FILE_ATTRIBUTES) return 1;

    wchar_t base[512];
    wcscpy(base, pkg);
    wchar_t *dot = wcsrchr(base, L'.');
    if (dot) *dot = 0;
    swprintf(buf, 1300, L"%ls\\%ls.fbx", dir, base);
    if (GetFileAttributesW(buf) != INVALID_FILE_ATTRIBUTES) return 1;
    /* 3d_md_body3760_hq.unity3d の書き出し fbx は md_body3760_hq.fbx */
    if (wcsncmp(base, L"3d_", 3) == 0){
        swprintf(buf, 1300, L"%ls\\%ls.fbx", dir, base + 3);
        if (GetFileAttributesW(buf) != INVALID_FILE_ATTRIBUTES) return 1;
    }
    return 0;
}


int copy_dir(const wchar_t *outdir, const wchar_t *sub, const wchar_t *dest, const wchar_t *ext){
    wchar_t pat[1300];
    swprintf(pat, 1300, L"%ls\\%ls\\%ls", outdir, sub, ext);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int n = 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        wchar_t src[1300], dst[1300];
        swprintf(src, 1300, L"%ls\\%ls\\%ls", outdir, sub, fd.cFileName);
        swprintf(dst, 1300, L"%ls\\%ls", dest, fd.cFileName);
        if (CopyFileW(src, dst, FALSE)){
            char cname[300];
            wide_to_utf8(fd.cFileName, cname, sizeof cname);
            printf("  -> %s\n", cname);
            n++;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return n;
}


int unpack_main(void){
    def menu[]={
        {"1.モーション解析",NULL,0},
        {"2.表情/カメラ",NULL,0},
        {"3.モデルをFBXにアンパック(beta)",unpack_fbx_main,0},
        {"4.キャラリソース(カードイラスト/背景/カードイラストSpinaアニメ(beta)/3Dフォト/spine(beta))",unpack_resources_main,0},
        {"5.ACBファイルアンパック",acb_main,0},
        {"6.戻る",NULL,0},
        {"END",NULL,0}
    };
    while (1){
        int rc = pager_pick("アンパック",menu,0);
        if(rc == -1)
            continue;
        else if(rc == 5)
            break;
    }
}

