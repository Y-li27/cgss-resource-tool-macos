# -*- coding: utf-8 -*-
# cgss_apply_textures.py
# 読み込み済みの CGSS FBX に、Blender 上で自動でテクスチャを貼り、すべてのマテリアルの BSDF 粗さを 1 にする。
# 使い方：FBX を読み込んだあと、Scripting パネルで本スクリプトを開き、Run Script を押す。
# 結果はダイアログに出る。テクスチャディレクトリが見つからない場合も、TEXDIR の変更をダイアログで知らせる。
import bpy
import os
import glob
import traceback

# テクスチャフォルダを手動指定（空欄なら自動検索）
TEXDIR = r''
# 検索したディレクトリ（診断用）
SEARCHED_PATHS = []


def show_popup(title, msg):
    if bpy.app.background:
        print('[%s] %s' % (title, msg))
        return
    lines = msg.split('\n')
    def draw(self, context):
        for line in lines:
            self.layout.label(text=line)
    bpy.context.window_manager.popup_menu(draw, title=title, icon='INFO')


def script_dir():
    # Text Editor で開いたスクリプトの所在ディレクトリを得る（パスを固定しないので、別の PC でも使える）
    for t in bpy.data.texts:
        if 'cgss_apply_textures' in t.name and t.filepath:
            d = os.path.dirname(bpy.path.abspath(t.filepath))
            if os.path.isdir(d):
                return d
    for t in bpy.data.texts:
        if t.filepath:
            d = os.path.dirname(bpy.path.abspath(t.filepath))
            if os.path.isdir(d):
                return d
    return None


def find_texdir():
    if TEXDIR and os.path.isdir(TEXDIR):
        return TEXDIR
    cands = []
    sd = script_dir()
    if sd:
        cands += [sd, os.path.join(sd, 'CGSS_DOWN'), os.path.join(sd, 'fbx書き出し比較')]
    if bpy.data.filepath:
        cands.append(os.path.dirname(bpy.data.filepath))
    cands.append(os.getcwd())
    SEARCHED_PATHS[:] = [c for c in cands if c]
    for c in cands:
        if not c or not os.path.isdir(c):
            continue
        try:
            has_png = any(f.lower().endswith('.png') for f in os.listdir(c))
        except OSError:
            has_png = False
        if has_png:
            return c
        pats = [os.path.join(c, 'CGSS_DOWN', '*', '3dモデル', 'テクスチャ'),
                os.path.join(c, 'fbx書き出し比較'),
                os.path.join(c, '**', 'テクスチャ')]
        for pat in pats:
            hits = glob.glob(pat, recursive=True) if '**' in pat else glob.glob(pat)
            for h in hits:
                if os.path.isdir(h):
                    try:
                        ok = any(f.lower().endswith('.png') for f in os.listdir(h))
                    except OSError:
                        ok = False
                    if ok:
                        return h
    return None


def pick(texdir, keys):
    cands = []
    for fn in sorted(os.listdir(texdir)):
        n = fn.lower()
        if n.endswith('.png') and all(k in n for k in keys):
            cands.append(fn)
    for fn in cands:
        if '_obj_' not in fn.lower():
            return os.path.join(texdir, fn)
    if cands:
        return os.path.join(texdir, cands[0])
    return None


def set_all_roughness():
    # 粗さを強制的に 1：先に Roughness 入力の接続をすべて外す（そうしないと default_value が効かない）
    n = 0
    for mat in bpy.data.materials:
        mat.use_nodes = True
        tree = mat.node_tree
        bsdf = tree.nodes.get('Principled BSDF')
        if not bsdf:
            continue
        if 'Roughness' in bsdf.inputs:
            inp = bsdf.inputs['Roughness']
            for link in list(inp.links):
                tree.links.remove(link)
            inp.default_value = 1.0
            n += 1
    return n


def clear_tex_nodes(mat):
    # マテリアル内の既存テクスチャノードを削除する。再実行時は貼り直しになり、スキップにはしない
    tree = mat.node_tree
    for node in list(tree.nodes):
        if node.type == 'TEX_IMAGE':
            tree.nodes.remove(node)


def pick_image(keys):
    # シーンにすでに読み込まれている画像を優先（GUI 書き出しの FBX はテクスチャ参照を持っており、読み込み時にロード済み）
    cands = [img for img in bpy.data.images
             if all(k in img.name.lower() for k in keys)]
    png = [img for img in cands if img.name.lower().endswith('.png')]
    cands = png or cands
    for img in cands:
        if '_obj_' not in img.name.lower():
            return img
    if cands:
        return cands[0]
    return None


def main():
    # 粗さ=1 は常に実行する。テクスチャディレクトリが見つかったかどうかには依存しない
    rough_n = set_all_roughness()

    texdir = find_texdir()
    lines = []
    lines.append('粗さ=1 を %d 個のマテリアルに設定' % rough_n)
    if texdir:
        lines.append('テクスチャディレクトリ：%s' % texdir)
    else:
        lines.append('テクスチャディレクトリ：見つかりません')
    mats = list(bpy.data.materials)
    lines.append('シーンのマテリアル数：%d' % len(mats))
    if not texdir:
        lines.append('')
        lines.append('検索したディレクトリ：')
        for p in SEARCHED_PATHS:
            lines.append('  ' + p)
        lines.append('テクスチャフォルダをこれらのディレクトリのいずれかに置くか、')
        lines.append('スクリプト先頭の TEXDIR を変更して手動指定してください。')
        msg = '\n'.join(lines)
        print(msg)
        show_popup('テクスチャディレクトリがありません', msg)
        return

    mat_count = 0
    slot_count = 0
    for mat in bpy.data.materials:
        match_info = []
        mat.use_nodes = True
        clear_tex_nodes(mat)
        bsdf = mat.node_tree.nodes.get('Principled BSDF')
        if not bsdf:
            lines.append('  [%s] Principled BSDF がないためスキップ' % mat.name)
            continue
        # スペキュラ入力名は Blender のバージョンで変わる：4.x は Specular IOR Level、旧版は Specular
        spec_key = None
        for cand in ('Specular IOR Level', 'Specular'):
            if cand in bsdf.inputs:
                spec_key = cand
                break
        m = mat.name.lower()
        # チーク：テクスチャにアルファチャンネルがある。Base Color と Alpha の両方につなぎ、透過ブレンドを有効にする
        if 'cheek' in m:
            img = pick_image(['cheek'])
            if not img and texdir:
                png = pick(texdir, ['cheek'])
                if png:
                    img = bpy.data.images.load(png, check_existing=True)
            if img:
                tex = mat.node_tree.nodes.new('ShaderNodeTexImage')
                tex.image = img
                mat.node_tree.links.new(tex.outputs['Color'], bsdf.inputs['Base Color'])
                if 'Alpha' in bsdf.inputs:
                    mat.node_tree.links.new(tex.outputs['Alpha'], bsdf.inputs['Alpha'])
                mat.blend_method = 'BLEND'
                mat.show_transparent_back = True
                mat_count += 1
                slot_count += 2
                match_info.append('cheek -> %s(+Alpha)' % img.name)
            else:
                match_info.append('cheek のテクスチャが見つかりません')
            lines.append('  [%s] %s' % (mat.name, '; '.join(match_info) or 'なし'))
            continue
        slots = []
        if m.startswith('m_body') or 'mt_body' in m:
            img = pick_image(['body', '_hq']) or pick_image(['body'])
            if not img and texdir:
                png = pick(texdir, ['body', '_hq']) or pick(texdir, ['body'])
                if png:
                    img = bpy.data.images.load(png, check_existing=True)
            if img:
                slots.append(('Base Color', img))
                match_info.append('body -> %s' % img.name)
            img = pick_image(['body', '_spec'])
            if not img and texdir:
                png = pick(texdir, ['body', '_spec'])
                if png:
                    img = bpy.data.images.load(png, check_existing=True)
            if img:
                slots.append(('Specular', img))
                match_info.append('spec -> %s' % img.name)
        elif m.startswith('m_head') or m.startswith('m_cheek') or 'mt_chr' in m:
            img = pick_image(['chr', '_hq']) or pick_image(['chr'])
            if not img and texdir:
                png = pick(texdir, ['chr', '_hq']) or pick(texdir, ['chr'])
                if png:
                    img = bpy.data.images.load(png, check_existing=True)
            if img:
                slots.append(('Base Color', img))
                match_info.append('head -> %s' % img.name)
            img = pick_image(['chr', '_spec'])
            if not img and texdir:
                png = pick(texdir, ['chr', '_spec'])
                if png:
                    img = bpy.data.images.load(png, check_existing=True)
            if img:
                slots.append(('Specular', img))
                match_info.append('spec -> %s' % img.name)
        if not slots:
            lines.append('  [%s] マテリアル名が規則に一致しないためスキップ' % mat.name)
            continue
        for slot, img in slots:
            tex = mat.node_tree.nodes.new('ShaderNodeTexImage')
            tex.image = img
            if slot == 'Base Color':
                mat.node_tree.links.new(tex.outputs['Color'], bsdf.inputs['Base Color'])
            elif slot == 'Specular' and spec_key:
                mat.node_tree.links.new(tex.outputs['Color'], bsdf.inputs[spec_key])
            slot_count += 1
        mat_count += 1
        lines.append('  [%s] %s' % (mat.name, '; '.join(match_info) or 'なし'))

    msg = ('テクスチャ完了：%d 個のマテリアル、%d 枚のテクスチャ\n粗さ=1 にしたマテリアル：%d 個\n\n詳細：\n%s' % (
        mat_count, slot_count, rough_n, '\n'.join(lines)))
    print(msg)
    show_popup('テクスチャ完了', msg)


try:
    main()
except Exception:
    err = traceback.format_exc()
    print(err)
    show_popup('スクリプトエラー', err[-500:])
