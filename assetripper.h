#ifndef CGSS_ASSETRIPPER_H
#define CGSS_ASSETRIPPER_H
#include <windows.h>

/* 0: Spine 再生に使う skel / atlas / tex.png / A8 / SPC / SPSprachen だけ
   1: カード画像・背景などの画像だけ
   2: 3D は glb/fbx とテクスチャ画像だけ */
enum {
    RIP_KEEP_SPINE = 0,
    RIP_KEEP_IMAGE = 1,
    RIP_KEEP_MODEL = 2
};

int assetripper_export(const wchar_t *unity_path, const wchar_t *out_dir);
int assetripper_collect(const wchar_t *export_root, const wchar_t *dest, int mode);
void assetripper_stop(void);
#endif
