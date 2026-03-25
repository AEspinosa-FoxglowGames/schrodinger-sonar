# schrodinger-sonar

A puzzle game built from scratch in C++ and OpenGL — no engine, no framework —
built around quantum observation mechanics.

The core concept: the world only exists in a defined state when it is observed.
Pinging a sonar reveals the environment, but observation has consequences —
ghost walls materialise, hazards disappear, doors unlock. Every ping is a
decision, not just information. The mechanic is drawn from Schrödinger's
thought experiment: the act of looking changes what is real.

## Status

🚧 In active development — core systems complete, new puzzle mechanics and
levels in progress.

## Technical Details

| | |
|---|---|
| **Language** | C++ |
| **Graphics** | OpenGL (no engine, no framework) |
| **Windowing / Input** | GLFW |
| **Math** | GLM |
| **Build system** | CMake + vcpkg |

## How It Works

The map stores **two independent tile layers** — Unseen and Seen. Every cell
can behave differently depending on whether the player has pinged it.

| Glyph | Unseen (before ping) | Seen (after ping) |
|---|---|---|
| `#` `1` | Wall | Wall |
| `G` | Empty (passable) | **Ghost wall** (solid) |
| `H` | **Hazard** (damages player) | Empty (safe) |
| `D` | Door (solid) | Door (openable by triggers) |
| `T` | Pressure plate | Pressure plate |
| `B` | Button | Button |
| `E` | Exit | Exit |

This means a single ping can simultaneously reveal a path, spawn a wall behind
you, and defuse a hazard — all depending on what was in the undiscovered cells.

## What's Implemented

**Sonar & Observation**
- BFS-based sonar wave that propagates outward from the player on each ping
- Per-cell distance field computed per wave for accurate reveal timing
- Cells transition from Unseen to Seen as the wavefront passes through them
- Wall illumination — surfaces glow when struck by a wave, then fade over time
- Wave rings rendered with oscillation, echo delay, and additive blending

**Energy System**
- Pinging costs energy (0.32 per ping)
- Energy regenerates passively over time (0.25/sec)
- Encourages deliberate pinging over spam

**Puzzle Elements**
- Ghost walls — passable until observed, then permanently solid
- Hazards — dangerous when unseen, safe once revealed
- Crates — pushable in grid steps, used to hold down pressure plates
- Pressure plates — activated by player or crate presence
- Buttons — single-press edge triggers
- Doors — controlled by plates and buttons independently

**Player**
- Movement with circle-vs-grid collision
- Slide-along-wall collision (separate X/Y axis testing)
- Instant death on hazard contact, respawn at level spawn point
- Invulnerability window after respawn

**Level System**
- Levels defined in plain ASCII files — no binary format, no editor required
- Multiple levels per file, separated by blank lines
- Level validation at load time — flags unknown glyphs, missing exits, duplicate spawns
- Level Select screen with scrollable list

**Frontend**
- Title screen with menu navigation (Play, Level Select, Quit)
- Animated screen transitions
- In-game HUD (energy bar)
- Custom 5×7 bitmap font, rendered entirely via point geometry

**Rendering**
- All geometry rendered as point clouds via custom GLSL shaders
- Additive blending for glow effects
- Per-point alpha support for fade effects
- Death particle burst with expanding ring and shard spray

## Level Format

Levels are plain text files. Each level is a block of ASCII rows separated by
a blank line. Lines beginning with `;` are comments.

```
; Level 1
##########
#P.......#
#.HH.G...#
#....G...#
#.....T..#
#.C...D..#
#.....E..#
##########
```

| Glyph | Meaning |
|---|---|
| `#` `1` | Wall |
| `.` `0` _(space)_ | Empty |
| `P` `p` | Player spawn |
| `H` | Hazard (dangerous when unseen) |
| `G` | Ghost wall (solid when seen) |
| `D` | Door |
| `E` | Exit |
| `T` `t` | Pressure plate |
| `B` `b` | Button |
| `C` | Crate |

## Controls

| Key | Action |
|---|---|
| `W / A / S / D` | Move |
| `Space` | Ping sonar |
| `R` | Restart level |
| `Escape` | Quit |

## Building

Requires OpenGL and CMake. Clone the repo and open `CMakeLists.txt` in Visual
Studio, or build via CMake from the command line. Dependencies are managed via
vcpkg (`vcpkg.json` included).

```bash
git clone https://github.com/AEspinosa-FoxglowGames/schrodinger-sonar
cd schrodinger-sonar
cmake --preset default
cmake --build out/build/default
```

## About

Built by [Austin Espinosa](https://foxglowgames.com) — indie developer and
Information Science student at ICU Tokyo.

🌐 [foxglowgames.com](https://foxglowgames.com)

---

# シュレーディンガー・ソナー（日本語）

量子観測メカニクスをテーマに、C++とOpenGLをゼロから実装したパズルゲーム。エンジン・フレームワーク不使用。

核となるコンセプト：「世界は、観測された瞬間にのみ確定した状態として存在する。」ソナーを発信すると周囲が明らかになる。しかし観測には代償が伴う — ゴーストウォールが出現し、ハザードが消え、ドアが開く。ソナーの発信は単なる情報収集ではなく、判断そのものだ。このメカニクスはシュレーディンガーの思考実験に着想を得ている：観測という行為が、現実を変える。

## 開発状況

🚧 開発中 — コアシステム実装済み、新たなパズルメカニクスとレベルを追加中。

## 技術詳細

| | |
|---|---|
| **言語** | C++ |
| **グラフィックス** | OpenGL（エンジン・フレームワーク不使用） |
| **ウィンドウ / 入力** | GLFW |
| **数学ライブラリ** | GLM |
| **ビルドシステム** | CMake + vcpkg |

## 仕組み

マップは**2つの独立したタイルレイヤー** — 未観測（Unseen）と観測済み（Seen）を保持しています。すべてのセルは、プレイヤーがそのエリアをピングしたかどうかによって異なる挙動をします。

| グリフ | 未観測（ピング前） | 観測済み（ピング後） |
|---|---|---|
| `#` `1` | 壁 | 壁 |
| `G` | 空き（通過可） | **ゴーストウォール**（固体） |
| `H` | **ハザード**（ダメージ） | 空き（安全） |
| `D` | ドア（固体） | ドア（トリガーで開閉可） |
| `T` | 感圧板 | 感圧板 |
| `B` | ボタン | ボタン |
| `E` | 出口 | 出口 |

一度のピングで、通路が現れると同時に背後に壁が出現し、ハザードが消える — すべては未発見セルの内容次第です。

## 実装済み機能

**ソナー・観測**
- BFSベースのソナー波：ピングのたびにプレイヤーから外側へ伝播
- 各ウェーブごとにセル単位の距離フィールドを計算、正確なリビールタイミングを実現
- 波面が通過するとセルが未観測から観測済みへ遷移
- 壁照明 — ソナー波が当たった面が発光し、時間とともにフェードアウト
- 振動・エコー遅延・加算ブレンディングを用いたウェーブリング描画

**エネルギーシステム**
- ピングにエネルギーを消費（1回あたり0.32）
- 時間経過で自動回復（0.25/秒）
- 無計画なピングを抑制し、判断を促す設計

**パズル要素**
- ゴーストウォール — 観測前は通過可、観測後は永続的に固体化
- ハザード — 未観測では危険、観測後は安全
- クレート — グリッド単位で押せる、感圧板の重しとして使用
- 感圧板 — プレイヤーまたはクレートの乗載で起動
- ボタン — エッジトリガー式の単押しスイッチ
- ドア — 感圧板・ボタンにより独立して制御

**プレイヤー**
- 円形コリジョンによるグリッドマップとの衝突判定
- X軸・Y軸個別判定によるスライド壁コリジョン
- ハザード接触で即死、レベルのスポーン地点に即リスポーン
- リスポーン後の無敵時間

**レベルシステム**
- レベルはプレーンテキストのASCIIファイルで定義 — バイナリ形式不使用、専用エディタ不要
- 1ファイルに複数レベルを収録、空行で区切り
- ロード時にバリデーション — 未知グリフ、出口なし、スポーン重複を検出
- スクロール可能なリストを持つレベルセレクト画面

**フロントエンド**
- タイトル画面とメニューナビゲーション（プレイ・レベルセレクト・終了）
- アニメーション付き画面遷移
- インゲームHUD（エネルギーバー）
- ポイントジオメトリのみで描画する独自5×7ビットマップフォント

**レンダリング**
- 全ジオメトリをカスタムGLSLシェーダーによるポイントクラウドで描画
- グロー効果への加算ブレンディング
- フェード効果のためのポイントごとのアルファ対応
- 死亡時パーティクル演出（拡張リングと破片の散布）

## レベルフォーマット

レベルはプレーンテキストファイルで定義します。各レベルはASCII行のブロックで、空行で区切ります。`;` で始まる行はコメントです。

```
; Level 1
##########
#P.......#
#.HH.G...#
#....G...#
#.....T..#
#.C...D..#
#.....E..#
##########
```

| グリフ | 意味 |
|---|---|
| `#` `1` | 壁 |
| `.` `0` _（スペース）_ | 空き |
| `P` `p` | プレイヤースポーン |
| `H` | ハザード（未観測時に危険） |
| `G` | ゴーストウォール（観測後に固体化） |
| `D` | ドア |
| `E` | 出口 |
| `T` `t` | 感圧板 |
| `B` `b` | ボタン |
| `C` | クレート |

## 操作方法

| キー | アクション |
|---|---|
| `W / A / S / D` | 移動 |
| `Space` | ソナー発信 |
| `R` | レベルリスタート |
| `Escape` | 終了 |

## ビルド方法

OpenGLとCMakeが必要です。リポジトリをクローンし、Visual Studioで `CMakeLists.txt` を開くか、コマンドラインからCMakeでビルドしてください。依存関係はvcpkgで管理されています（`vcpkg.json` 同梱）。

```bash
git clone https://github.com/AEspinosa-FoxglowGames/schrodinger-sonar
cd schrodinger-sonar
cmake --preset default
cmake --build out/build/default
```

## 開発者について

[Austin Espinosa](https://foxglowgames.com) 制作 — インディーゲーム開発者・ICU東京在籍の情報科学専攻学生。

🌐 [foxglowgames.com](https://foxglowgames.com)
