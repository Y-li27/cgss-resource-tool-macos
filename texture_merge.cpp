// texture_merge.cpp: CGSS Spine RGB + A8 merge (stb_image, portable)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_NO_THREAD_LOCALS
#include "stb_image.h"
#include "stb_image_write.h"
#include "texture_merge.h"

static void wide_to_utf8_local(const wchar_t *in, char *out, int n){
    if (!in || !out || n <= 0){ if (out && n > 0) out[0] = 0; return; }
    WideCharToMultiByte(CP_UTF8, 0, in, -1, out, n, NULL, NULL);
    for (char *p = out; *p; p++) if (*p == '\\') *p = '/';
}

static unsigned char *load_png_w(const wchar_t *path, int *w, int *h){
    char u8[1300];
    wide_to_utf8_local(path, u8, sizeof u8);
    int comp = 0;
    return stbi_load(u8, w, h, &comp, 4);
}

static int save_png_w(const wchar_t *path, int w, int h, const unsigned char *rgba){
    char u8[1300];
    wide_to_utf8_local(path, u8, sizeof u8);
    return stbi_write_png(u8, w, h, 4, rgba, w * 4) != 0;
}

static int merge_one(const wchar_t *dir, const wchar_t *a8name){
    wchar_t a8[1300], base[1300], merged[1300], atlas[1300], v38atlas[1300];
    swprintf(a8, 1300, L"%ls\\%ls", dir, a8name);

    wchar_t basename[512];
    wcscpy(basename, a8name);
    wchar_t *p = wcsstr(basename, L"_A8");
    if (!p) return 0;
    wcscpy(p, L".png");
    swprintf(base, 1300, L"%ls\\%ls", dir, basename);
    if (GetFileAttributesW(base) == INVALID_FILE_ATTRIBUTES) return 0;

    int bw = 0, bh = 0, aw = 0, ah = 0;
    unsigned char *bmpBase = load_png_w(base, &bw, &bh);
    unsigned char *bmpA8 = load_png_w(a8, &aw, &ah);
    if (!bmpBase || !bmpA8){
        stbi_image_free(bmpBase);
        stbi_image_free(bmpA8);
        return 0;
    }
    if (bw != aw || bh != ah){
        printf("  %ls と %ls のサイズが一致しないため、合成をスキップ\n", basename, a8name);
        stbi_image_free(bmpBase);
        stbi_image_free(bmpA8);
        return 0;
    }

    int result = 0;
    unsigned char *out = (unsigned char*)malloc((size_t)bw * bh * 4);
    if (out){
        size_t np = (size_t)bw * bh;
        for (size_t i = 0; i < np; i++){
            out[i*4+0] = bmpBase[i*4+0];
            out[i*4+1] = bmpBase[i*4+1];
            out[i*4+2] = bmpBase[i*4+2];
            out[i*4+3] = bmpA8[i*4+3];
        }
        swprintf(merged, 1300, L"%ls\\%ls", dir, basename);
        wchar_t *dot = wcsrchr(merged, L'.');
        if (dot) wcscpy(dot, L"_merged.png");
        if (save_png_w(merged, bw, bh, out)){
            wchar_t atlas_name[512];
            wcscpy(atlas_name, basename);
            wchar_t *d2 = wcsrchr(atlas_name, L'.');
            if (d2) wcscpy(d2, L".atlas");
            swprintf(atlas, 1300, L"%ls\\%ls", dir, atlas_name);
            if (GetFileAttributesW(atlas) == INVALID_FILE_ATTRIBUTES){
                wchar_t atlas_name2[512];
                wcscpy(atlas_name2, basename);
                wchar_t *d2b = wcsrchr(atlas_name2, L'.');
                if (d2b) wcscpy(d2b, L".atlas.asset");
                swprintf(atlas, 1300, L"%ls\\%ls", dir, atlas_name2);
            }
            swprintf(v38atlas, 1300, L"%ls", atlas);
            wchar_t *d3 = wcsrchr(v38atlas, L'.');
            if (d3) wcscpy(d3, L"_v38.atlas");
            FILE *fin = _wfopen(atlas, L"rb");
            FILE *fout = _wfopen(v38atlas, L"wb");
            if (fin && fout){
                wchar_t merged_name[512];
                wcscpy(merged_name, basename);
                wchar_t *mn = wcsrchr(merged_name, L'.');
                if (mn) wcscpy(mn, L"_merged.png");
                char merged_u8[512];
                wide_to_utf8_local(merged_name, merged_u8, sizeof merged_u8);
                int page_replaced = 0;
                char line[4096];
                while (fgets(line, sizeof line, fin)){
                    if (!page_replaced && strstr(line, ".png")){
                        fputs(merged_u8, fout);
                        if (strchr(line, '\n')) fputc('\n', fout);
                        page_replaced = 1;
                    } else {
                        fputs(line, fout);
                    }
                }
            }
            if (fin) fclose(fin);
            if (fout) fclose(fout);
            printf("  透明度を合成: %ls + %ls -> %ls\n", basename, a8name, merged);
            result = 1;
        }
        free(out);
    }
    stbi_image_free(bmpBase);
    stbi_image_free(bmpA8);
    return result;
}

int merge_a8_textures_in_dir(const wchar_t *dir){
    wchar_t pat[1300];
    swprintf(pat, 1300, L"%ls\\*_A8.png", dir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int n = 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (merge_one(dir, fd.cFileName)) n++;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return n;
}

int crop_png_region(const wchar_t *src_png, int x, int y, int w, int h,
                    int rotate, const wchar_t *dst_png){
    if (!src_png || !dst_png || w <= 0 || h <= 0) return 0;
    int sw = 0, sh = 0;
    unsigned char *src = load_png_w(src_png, &sw, &sh);
    if (!src) return 0;
    if (x < 0 || y < 0 || (x + w) > sw || (y + h) > sh){
        printf("  トリミング範囲がテクスチャ外です %ls (%d,%d %dx%d / %dx%d)\n",
               src_png, x, y, w, h, sw, sh);
        stbi_image_free(src);
        return 0;
    }
    int dw = rotate ? h : w;
    int dh = rotate ? w : h;
    unsigned char *dst = (unsigned char*)malloc((size_t)dw * dh * 4);
    if (!dst){ stbi_image_free(src); return 0; }
    for (int yy = 0; yy < h; yy++){
        for (int xx = 0; xx < w; xx++){
            const unsigned char *sp = src + ((size_t)(y + yy) * sw + (x + xx)) * 4;
            int dx, dy;
            if (rotate){
                dx = h - 1 - yy;
                dy = xx;
            } else {
                dx = xx;
                dy = yy;
            }
            unsigned char *dp = dst + ((size_t)dy * dw + dx) * 4;
            dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = sp[3];
        }
    }
    int ok = save_png_w(dst_png, dw, dh, dst);
    free(dst);
    stbi_image_free(src);
    return ok;
}

static void trim_line(char *s){
    size_t l = strlen(s);
    while (l > 0 && (s[l-1] == '\r' || s[l-1] == '\n' || s[l-1] == ' ' || s[l-1] == '\t'))
        s[--l] = 0;
    char *p = s;
    while (*p == ' ' || *p == '\t') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
}

int crop_atlas_regions(const wchar_t *atlas_path, const wchar_t *png_path,
                       const wchar_t *out_dir, const wchar_t *prefix){
    FILE *f = _wfopen(atlas_path, L"rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 4 * 1024 * 1024){ fclose(f); return 0; }
    char *buf = (char*)malloc((size_t)sz + 1);
    if (!buf){ fclose(f); return 0; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz){
        free(buf); fclose(f); return 0;
    }
    buf[sz] = 0;
    fclose(f);

    int xs[32] = {0}, ys[32] = {0}, ws[32] = {0}, hs[32] = {0}, rt[32] = {0}, valid[32] = {0};
    int cur = -1;
    char line[512];
    char *p = buf;
    while (*p && cur < 32){
        size_t i = 0;
        while (p[i] && p[i] != '\n' && i < sizeof line - 1){ line[i] = p[i]; i++; }
        line[i] = 0;
        p += i;
        if (*p == '\n') p++;
        trim_line(line);
        if (!line[0]) continue;
        if (line[0] >= '0' && line[0] <= '9'){
            char *end = NULL;
            long v = strtol(line, &end, 10);
            if (end && *end == 0 && v >= 1 && v <= 32){
                cur = (int)v - 1;
                valid[cur] = 1;
            } else {
                cur = -1;
            }
            continue;
        }
        if (cur < 0 || !valid[cur]) continue;
        if (strncmp(line, "rotate:", 7) == 0){
            rt[cur] = (strstr(line, "true") != NULL);
        } else if (strncmp(line, "xy:", 3) == 0){
            sscanf(line + 3, "%d,%d", &xs[cur], &ys[cur]);
        } else if (strncmp(line, "size:", 5) == 0){
            sscanf(line + 5, "%d,%d", &ws[cur], &hs[cur]);
        }
    }
    free(buf);

    int n = 0;
    for (int i = 0; i < 32; i++){
        if (!valid[i] || ws[i] <= 0 || hs[i] <= 0) continue;
        wchar_t dst[1300];
        swprintf(dst, 1300, L"%ls\\%ls_%d.png", out_dir, prefix, i + 1);
        if (crop_png_region(png_path, xs[i], ys[i], ws[i], hs[i], rt[i], dst))
            n++;
    }
    return n;
}
