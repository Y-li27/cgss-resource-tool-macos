# -*- coding: utf-8 -*-
# cgss_anim_to_shapekeys.py
# CGSS の head FBX にあるボーン表情モーション（AnimationStack）をシェイプキー（Shape Keys / Blend Shapes）にベイクする。
# 使い方：Blender の Scripting パネルで本スクリプトを開き、Run Script を押す。
#  - シーンに head FBX（モーション付き）があればそのまま処理する。
#  - なければ自動検索して読み込む（FBX_PATH / 自動検索パスを参照）。
# 結果はダイアログに出るので、システムコンソールを見る必要はない。
import bpy
import os
import glob
import traceback

# ===== 設定 =====
# モーション付き FBX を手動指定（空欄なら自動検索）
FBX_PATH = r''
# 名前にこの文字列を含むモーションだけ処理（空欄＝すべて）。デフォルト an_chr ＝キャラの face モーション
ACTION_FILTER = 'an_chr'
# ================


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
        if 'cgss_anim_to_shapekeys' in t.name and t.filepath:
            d = os.path.dirname(bpy.path.abspath(t.filepath))
            if os.path.isdir(d):
                return d
    for t in bpy.data.texts:
        if t.filepath:
            d = os.path.dirname(bpy.path.abspath(t.filepath))
            if os.path.isdir(d):
                return d
    return None


def search_dirs():
    dirs = []
    sd = script_dir()
    if sd:
        dirs += [sd, os.path.join(sd, 'CGSS_DOWN'), os.path.join(sd, 'fbx書き出し比較')]
    if bpy.data.filepath:
        dirs.append(os.path.dirname(bpy.data.filepath))
    dirs.append(os.getcwd())
    return dirs


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


def find_fbx():
    if FBX_PATH and os.path.isfile(FBX_PATH):
        return FBX_PATH
    for d in search_dirs():
        if not os.path.isdir(d):
            continue
        hits = [p for p in glob.glob(os.path.join(d, '**', '*.fbx'), recursive=True)
                if 'chr' in os.path.basename(p).lower()]
        if hits:
            hits.sort(key=os.path.getmtime, reverse=True)
            return hits[0]
    return None


def ensure_imported(fbx_path):
    # シーンにスケルトンとスキニングメッシュがあればそのまま使う
    for obj in bpy.data.objects:
        if obj.type == 'MESH':
            for mod in obj.modifiers:
                if mod.type == 'ARMATURE' and mod.object:
                    return True
    if not fbx_path:
        return False
    # 先にシーンを空にしてから読み込む。古いオブジェクトの干渉を避ける
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    bpy.ops.import_scene.fbx(filepath=fbx_path)
    return True


def main():
    fbx_path = find_fbx()
    ok = ensure_imported(fbx_path)
    if not ok:
        show_popup('モデル未読み込み', 'シーンにスケルトン付きのスキニングメッシュがなく、FBX も見つかりません。\n'
                   '先に Blender でモーション付きの head FBX を読み込むか、\n'
                   'スクリプト先頭の FBX_PATH を変更してください。')
        return

    scene = bpy.context.scene
    depsgraph = bpy.context.evaluated_depsgraph_get()

    meshes, arm = [], None
    for obj in scene.objects:
        if obj.type != 'MESH':
            continue
        for mod in obj.modifiers:
            if mod.type == 'ARMATURE' and mod.object:
                meshes.append(obj)
                if arm is None:
                    arm = mod.object
                break

    if not meshes or arm is None:
        show_popup('メッシュが見つかりません', '読み込んだ FBX にアーマチュアモディファイア付きのメッシュがありません。')
        return

    actions = [a for a in bpy.data.actions if not ACTION_FILTER or ACTION_FILTER in a.name]
    if not actions:
        show_popup('一致するモーションがありません', 'モーションフィルタ "%s" に一致するモーションがありません（モーションは全部で %d 個）。\n'
                   '\n'
                   'よくある原因：この FBX は CLI でアンパックしたもので、スケルトンとメッシュだけで\n'
                   'アニメーションデータがありません。AssetStudio GUI で\n'
                   'Animator と AnimationClips を全選択し、モーション付き FBX を書き出してください：\n'
                   '  Animator を右クリック -> Export selected objects (merge)\n'
                   '  + Selected AnimationClips\n'
                   'その後、このスクリプトを再実行してください。' % (ACTION_FILTER, len(bpy.data.actions)))
        return

    arm.animation_data_create()
    arm.animation_data.action = None
    scene.frame_set(scene.frame_start)
    depsgraph.update()

    base = {}
    for obj in meshes:
        ev = obj.evaluated_get(depsgraph)
        base[obj.name] = [ev.data.vertices[i].co.copy() for i in range(len(obj.data.vertices))]
        if not obj.data.shape_keys:
            obj.shape_key_add(name='Basis')

    def sample_verts():
        res = {}
        for obj in meshes:
            ev = obj.evaluated_get(depsgraph)
            res[obj.name] = [ev.data.vertices[i].co.copy() for i in range(len(obj.data.vertices))]
        return res

    def action_frames(action):
        times = set()
        for fc in action.fcurves:
            for kp in fc.keyframe_points:
                times.add(int(round(kp.co[0])))
        fr = action.frame_range
        if times:
            times.add(int(round(fr[0])))
            times.add(int(round(fr[1])))
            return sorted(times)
        f0, f1 = int(round(fr[0])), int(round(fr[1]))
        return [f0] if f1 <= f0 else list(range(f0, f1 + 1))

    count = 0
    for action in actions:
        arm.animation_data.action = action
        frames = action_frames(action)
        main = meshes[0].name
        best_f, best_dev, best_verts = frames[0], -1.0, None
        for f in frames:
            scene.frame_set(f)
            depsgraph.update()
            vs = sample_verts()
            dev = sum((vs[main][i] - base[main][i]).length for i in range(len(base[main])))
            if dev > best_dev:
                best_dev, best_f, best_verts = dev, f, vs
        if best_verts is None:
            continue
        for obj in meshes:
            name = action.name
            # Blender が FBX を読み込むと、モーション名が "オブジェクト|モーション|Base Layer" の三段になることがある。モーション部分を取る
            if '|' in name:
                parts = name.split('|')
                name = parts[1] if len(parts) >= 2 else parts[-1]
            if name in obj.data.shape_keys.key_blocks:
                name += '_%d' % best_f
            sk = obj.shape_key_add(name=name, from_mix=False)
            for i, v in enumerate(best_verts[obj.name]):
                sk.data[i].co = v
        count += 1

    arm.animation_data.action = None
    rough_n = set_all_roughness()
    msg = '完了：%d 個のモーションをシェイプキーに変換\nメッシュ：%s\nモーション数：%d\n粗さ=1 にしたマテリアル：%d' % (
        count, ', '.join(o.name for o in meshes), len(actions), rough_n)
    print(msg)
    show_popup('変換完了', msg)


try:
    main()
except Exception:
    err = traceback.format_exc()
    print(err)
    show_popup('スクリプトエラー', err[-500:])
