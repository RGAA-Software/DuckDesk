# FreeRDP：固定版本的最小 MF 输出状态补丁

2026-09-09 经用户确认维护。原始参考 `D:/dolit/rdp` 和干净上游 checkout 均只读。

- 上游：FreeRDP 3.31.0，`aa8650b300aa4cabd85d9c72b431301509b9043f`。
- 补丁：`0001-mf-output-state.patch`。
- SHA-256：`A74AAD69B61E4E6E4758EFC6F1E38E9D07C2663E5613923E3F0097AD0EAE640B`。
- 修改范围：`libfreerdp/codec/h264_mf.c`、`h264.c`；不修改协议、NLA、代理或用户会话策略。
- 原 FreeRDP Apache-2.0 许可继续适用；SDK/发行产物携带 `licenses/FreeRDP-LICENSE`。

## 原因及边界

官方 3.31.0 的 MF 解码器在 `MF_E_TRANSFORM_STREAM_CHANGE` 后只重配输出类型、缓冲区，
没有再次取出待处理输出，却返回“已产出帧”；`NEED_MORE_INPUT` 同样返回成功帧。
首批 AVC444 数据因而进入 0×0 表面的矩形校验。使用官方 Windows 客户端直接连接 90
也能复现，不是 GammaRay WebSocket 分包或 Qt 显示层独有的问题。

补丁在格式改变后重试 **ProcessOutput**，不重复 **ProcessInput**；最多四次格式重配。
暂未产出画面返回 pending，公共转换路径不读取空表面。补齐 AddBuffer 错误处理、
锁定/引用释放及实际输出长度检查。不引入旧 demo 的双解码器修改。
MF 仍是上游标注为 experimental 的解码器；修复首帧不意味着已验证所有 GPU、AVC444 素材或 60 FPS。

微软定义参见 [MFT 流格式变化处理](https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/medfound/handling-stream-changes.md)。

## 重建及一致性门禁

2026-09-10：已改为仓库源码子模块依赖，普通开发不再需要外部绝对路径。
先运行无参 `scripts_build\build_cpp_rdp_sdk.bat`，再运行 `scripts_build\build_cpp_rdp_policy.bat`。
新机器准备、依赖固定与升级步骤见 [源码依赖说明](../../third_party/freerdp/README.md)。
下方显式路径命令和旧构建目录仍作为历史验证记录；新的默认构建目录按源码/补丁/依赖摘要生成。

```bat
scripts_build\build_cpp_rdp_sdk.bat <干净的固定版本源码> <独立构建目录> <SDK目录>
scripts_build\build_cpp_rdp_policy.bat <SDK目录> <SDK目录>
scripts_build\build_cpp_client.bat
scripts_build\build_cpp_render.bat
```

`prepare_rdp_sdk.ps1` 将补丁应用到 `.cache/rdp_source_<补丁摘要前12位>`，每次构建验证
基线、完整 diff 和意外文件；不自动重置有改动的源码。修改补丁后使用新的独立构建目录，
避免复用绑定旧源码目录的 CMake cache。本次构建目录为 `.cache/rdp_proxy_patched_3_31`。
`gammaray-rdp-sdk.json` 记录基线、补丁摘要、解码器和运行库 SHA-256；Client CMake 与
90 部署入口均检查它。节点部署还要求代理构建树与 SDK 中 FreeRDP/WinPR DLL 完全一致。

升级先复现并回归连接、动态图像、resize、音频、剪贴板、重复连接和错误路径；
若上游已修复，重新审查后删除补丁依赖，不能在新版本上盲目继续应用。
当前实测及尚未验收项见 [实施进度](../../docs/rdp_implementation_progress_20260908.md)。

## 已执行的 decoder A/B 门禁

聚焦入口为 `scripts_build/build_cpp_tests.bat test_client_rdp_decoder`，随后运行
`ctest --test-dir build_official -R "^client_rdp_decoder$" --output-on-failure`。
A/B 必须使用同一测试 exe、相同帧数据和独立运行目录；只交换整套同版本 FreeRDP/WinPR 运行库，
不能替换正在使用的 dist DLL，也不能混合版本。记录两套 DLL 的 SHA-256。

本次未打补丁的官方 3.31.0 两项均复现 `0x0/-1015`；补丁版两项通过，覆盖首帧、八次重置、
仅 SPS/PPS 无图像后再提交 IDR，以及八次创建/销毁。已部署补丁版 `freerdp3.dll` SHA-256：
`771BF6F7D8E8BA77EB1954B56F1C5ED13A91A3AC3A2CFEFAAE5BDBA6DC3B337E`。
这不是 60 FPS 结论：目前真实 1920×1080 动态样本的稳定统计窗口为 24–31 FPS。
