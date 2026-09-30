#ifndef _CGSS_TEXTURE_MERGE_H
#define _CGSS_TEXTURE_MERGE_H
#include <windows.h>

/* ディレクトリ内の全 xxx_A8.png を走査し、xxx.png の RGB と A8 の alpha を xxx_merged.png に合成する。
 * それを参照する xxx_v38.atlas も生成（Spine 3.8.75 エディタ用。エディタは二枚テクスチャ非対応）。
 * 合成に成功したテクスチャ数を返す。 */
#ifdef __cplusplus
extern "C" {
#endif
int merge_a8_textures_in_dir(const wchar_t *dir);
/* src_png から (x,y,w,h) を切り出して dst_png に保存（RGBA 維持）。rotate=1 で時計回り 90 度 */
int crop_png_region(const wchar_t *src_png, int x, int y, int w, int h,
                    int rotate, const wchar_t *dst_png);
/* Spine atlas（テキスト）を解析し、各領域を out_dir\prefix_1.png / _2.png ... に切り出す。
 * 成功した領域数を返す（ステッカーモーションは通常 2 フレーム） */
int crop_atlas_regions(const wchar_t *atlas_path, const wchar_t *png_path,
                       const wchar_t *out_dir, const wchar_t *prefix);
#ifdef __cplusplus
}
#endif

#endif
