# CGSS Resource Tool（macOS 移植）

元リポジトリ https://github.com/Y-li27/cgss-resource-tool の macOS 向け移植です。

Windows API（WinHTTP / GDI+ / conio / CreateProcess）を POSIX + libcurl + stb_image に置き換えています。

## 移植範囲

動くもの:

- リソース検索とダウンロード（HTTPS / LZ4）
- Spine .skel → JSON（3.6 / 3.8.75）
- RGB + A8 テクスチャ合成
- Spine ブラウザプレビュー
- USM / CG 展開（ffmpeg があれば MP4 化）
- ACB 展開（acb2wavs または互換 CLI が PATH にあれば）
- unity3d 展開（隣に置いた AssetRipper.GUI.Free を headless で起動）
  - カードイラスト / 背景 / 3Dフォトは画像だけ残す
  - Spine は `.skel` `.atlas` `*_tex.png` `*_tex_A8.png` `SPC*.png` `SPSprachen_*` だけ残す
  - `.skel.bytes` は `.skel` に、`.atlas.bytes` / `.atlas.txt` は `.atlas` に改名する
  - 3D モデルは glb（無償版は FBX を出さない）とテクスチャだけ残す

移植していないもの:

- Windows 用の自動アップデート

## 必要環境

- macOS 12 以降
- CMake 3.16+
- Xcode Command Line Tools（clang）
- libcurl
- 任意: ffmpeg、acb2wavs
- 任意: [AssetRipper](https://github.com/AssetRipper/AssetRipper/releases) の macOS 版。`AssetRipper.GUI.Free` を実行ファイル隣の `AssetRipper/` に置く
- 実行時に `master.mdb` と `manifest_*.db` を置く

```bash
brew install cmake curl ffmpeg
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cd build
./CGSS_Script
```

ダウンロード用の別プログラムはありません。`./CGSS_Script` が本体です。

## ダウンロードのやり方

1. `master.mdb` と `manifest_*.db` を `CGSS_Script` と同じ場所（通常は `build/`）に置く。
2. `./CGSS_Script` を起動する。
3. メインメニューで **`1.リソース検索とダウンロード`** を選んで Enter。
4. 次のメニューで種類を選ぶ。カードなら **`4.カード(名/キャラ名/id)`**。カード id か名前を入れて Enter。
5. Space でリソースにチェックを付け、Enter でダウンロードする。

保存先は `CGSS_Script` と同じ階層の `CGSS_DOWN` です。カードなら `CGSS_DOWN/<カードid><カード名>/カードイラスト` や `Spine` になります。画面にも保存パスが出ます。`Ā` のようなフォルダは失敗時のゴミなので消して構いません。

アンパックはダウンロードではありません。ダウンロードが終わってから、メインメニューの **`2.アンパック`** → **`4.キャラリソース`** です。

