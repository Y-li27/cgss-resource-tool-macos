#ifndef _CGSS_CG_H
#define _CGSS_CG_H
#include <windows.h>

/* メインメニュー: USM/CG アンパック（カスタムのファイル/ディレクトリ + ダウンロード済みCG） */
int unpack_usm(void);

/* ディレクトリ内の全 usm を再帰アンパックし、対応 acb も展開（browse.c がダウンロード後に呼ぶ） */
int unpack_cg_folder(const wchar_t *dir);

#endif
