# 已内联（vendored）的第三方子仓库清单

> 记录时间：2026-08。`src/px_deps/` 下的 18 个库原本是 git submodule，现已全部改为**普通目录**直接纳入本仓库管理（去子模块化）。本文件记录每个库内联时的 commit，便于日后对照快照版本。

## 更新方法

子仓库已不再以 git submodule 形式存在。要更新某个库：

1. 单独 clone 该库到临时目录（原远端地址已不再随本仓库记录，需自行获取）。
2. `git checkout <目标 commit>`。
3. 把内容拷贝覆盖到本仓库 `src/px_deps/<name>/`。
4. 在本仓库 `git add` + `git commit`，并在下方更新对应 commit 记录。

## 清单

以下为 vendored 依赖快照，原远端仓库已不再作为子模块引用，仅保留目录名与快照 commit 记录。

| 目录 | 内联 commit |
|------|-------------|
| `px_3rdparty` | `b761f411a3165f88a7d58a780717080e93bf0d17` |
| `px_account_sdk` | `d7efb34d77db6da3d40a77dae8cfa96f658a7902` |
| `px_capture_new` | `688e9da2785557700e644aa7b473e3dc4bbc9964` |
| `px_client_sdk_new` | `b819cb0d7a8089e86080f65353d644507d84c042` |
| `px_client_web` | `67b2153a5e531599038b638a1a549a56e2b496ce` |
| `px_common_new` | `5b268f08248f8b22ecf9ce65a231a5b5f35c08a4` |
| `px_controller` | `8ebf72c95115b99870707039b91f1db66af9a91c` |
| `px_encoder_new` | `5f9595b60c872b222f4fcf50643f9bb804127b07` |
| `px_manager_client` | `ecb764b942d48f984bd1f777bb9a806fbf98854e` |
| `px_message_new` | `4560c3512aa886d67811626cda1007a610735f07` |
| `px_opus_codec_new` | `1b979d4df95eeb75c5490fa7742a80844292118c` |
| `px_profile_client` | `29352878cc4173a046b18bbe0d28f7a5555b9b77` |
| `px_qt_widget` | `325a8fe1c37439df78676c9f8258571976095df8` |
| `px_relay_client` | `6cffd28cc03932c9841cdad97265e404e2576e16` |
| `px_server_protocol` | `a463ba51975e24b08fcaafe06c53281d6d93ae0c` |
| `px_console_client` | `6adc5019224744c664c09e6dd4a3aa7f9327385c` |
| `px_steam_manager_new` | `9d2147b32bdbcc78f7bfb5b70445b6e2ff7e82b0` |
| `px_webrtc_client` | `5a87b638bb83d2fc2b9d2c72f1196731e4964241` |

## 注意事项

- `px_3rdparty` 内含预编译库（webrtc/x64/webrtc.lib、asio2/3rd/openssl/prebuilt/*、libplacebo/lib/*.dll 等），体积约 452 MB。更新时注意 GitHub 单文件 100MB 上限（当前 webrtc.lib 约 84MB）。
- `px_3rdparty/asio2/3rd/asio` 的 standalone Asio 单独跟随官方快照；目标版本为 `asio-1-38-2`，commit
  `8806a6803cde7054c3049d3666d3ec36786568c5`，来源为 `https://github.com/chriskohlhoff/asio.git`。
- 各子库原本自带的 `force_pull.bat`（子模块同步脚本）已删除，不再适用。
