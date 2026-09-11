# UDP 上游对齐开发检查点：参考程序与实验核心

本文件记录实际结果，不替代 [实施方案](native_udp_fec_upstream_implementation_plan.md) 的验收门槛。

> 2026-09-11 更新：下文保留的是初期参考核心检查点，不是当前产品接入状态。新核心已接入 Windows Render / SDK；
> 视频/音频上游差分与封装测试目前 11 个用例通过。真实 Windows 客户端已能连接 90、解码和操作游戏，
> 但快速转视角仍有间歇掉帧，尚未通过“稳定、不卡顿”验收。最新记录见
> [Windows 稳定性调试](windows_udp_stability_20260911.md)。Android 按用户最新要求暂停，最后再处理。

## 已落地

- `third_party/moonlight_media_reference/`：58 份原样参考文件逐项与固定上游提交的本地 checkout 核对 SHA-256；提交号、许可证和文件哈希保存在 `UPSTREAM.md`、`manifest.json`。
- `src/px_client_sdk/media_transport/`：独立 `pixels::media_transport`，仅标准 C++ / nanors；目前尚未接入产品收发路径。
- 视频分包实验实现：上游 RTP/NV 内层布局、最多 4 个 FEC 块、255 分片限制和超大帧关闭 FEC 的路径。
- 视频收包实验实现：每流值类型队列、多块暂存、乱序感知的预测丢失及恢复；没有 socket、回调或进程全局会话状态。
- 原版 `RtpVideoQueue.c` 真正编译进独立测试程序；测试不是我们自己的发送/接收互相证明。
- 音频基础纠错：使用上游专用 4＋2 校验矩阵，与原版 `RtpaInitializeQueue` 初始化出的矩阵实际解码比对。尚未实现完整音频包/播放队列接入。
- nanors C 符号在构建时加前缀，避免与仍在使用的旧 RS 库冲突；原文件没有改动。
- SDK CMake 入口备份见 `backup/native_udp_upstream_20260910_p0/`。

## 当前内层字节合同（实验接口）

未加 Pixels 外层，未启用视频加密。每数据报长度由 `datagram_size` 指定，测试使用 1400 字节。

| 偏移 | 字段 | 编码 |
|---|---|---|
| 0–11 | RTP 头 | 序号 BE16，90 kHz 时间戳 BE32；header 为 0x90 |
| 12–15 | RTP 扩展保留 | 初始为 0 |
| 16–19 | streamPacketIndex | LE32，按上游 `sequence << 8` 生成 |
| 20–23 | frameIndex | LE32 |
| 24 | NV flags | picture / SOF / EOF |
| 25–27 | extra / multiFecFlags / multiFecBlocks | 块号和末块号保留上游位布局 |
| 28–31 | fecInfo | LE32：块内序号、数据片数、实际 FEC 百分比 |
| 32 起 | 帧载荷 | 第一片含 8 字节上游短帧头，随后为编码字节 |

数据片先按上游构造保护内容，生成 parity 后再覆盖 RTP 与 FEC 路由字段；不能把“直接对编码裸字节做 RS”当作等价实现。
预测式恢复冷却保持基线的 300 秒媒体时间，测试无需真实等待 5 分钟。

尚未冻结 Pixels 外层的会话/屏幕元信息和最终产品包大小；不能将本表当作已部署的新产品协议。

## 已运行测试

命令：

```text
scripts\build_cpp_target.bat test_upstream_media_reference check_cpp_ownership
ctest --test-dir build_official -R ^upstream_media_reference$ --output-on-failure --timeout 60
scripts_build\build_cpp_android_common.bat pixels_media_transport
```

Windows 参考程序和所有权检查通过；Android ARM64 新媒体核心编译通过，非 APK 全量构建验收。

参考程序当前 6 个用例：

1. 原版 Moonlight 接收 500 B 至 1.2 MB 的分包数据。
2. 500 KB 多块帧，16 个种子，每块用满校验预算，核对原版与实验队列的恢复结果/预测事件。
3. 32 个种子、每个 8 帧，随机缺片、整帧缺失、乱序，比较交付载荷与预测/最终丢失事件。
4. 超过 4 块保护容量后的上游 FEC 关闭路径。
5. 音频全部 15 种丢两片组合，用原版音频矩阵恢复数据。
6. 保护元信息恢复和超出纠错预算的拒绝。

本机 CTest 通过，约 0.3 秒。90 已运行同一参考程序，6/6 通过，首次记录约 63 ms；远程目录为
`D:/software/esprit_169811/udp-fec-validation/20260910-222811/`，复制前后核对了可执行文件 SHA-256。
参考测试程序后续变更应重新部署并记录新的哈希，不复用旧结果。

最终增加测试基准全局状态的互斥保护后，已重新编译并在 90 再次运行，6/6 通过，约 63 ms：
`D:/software/esprit_169811/udp-fec-validation/20260910-223211/test_upstream_media_reference.exe`。
本机/90 SHA-256 一致：`79A5D1A8B82A79F22537A1E3D3FD33ACD7992084244A6E5401149E3DBCB5C27D`。
新增项目 C++ 文件完整格式检查通过；未对第三方源码执行格式化。

## 90 环境核对

- 实际连接 `39.71.45.66` 成功，WinRM TrustedHosts 操作后恢复原值。
- Render / Panel / Service 路径确认是 `D:/software/esprit_169811/render/`。
- `2dAdventure.exe` 和心脏医学应用均存在。
- 只在独立测试目录放置参考测试程序；没有替换运行中的 Render、修改配置、启动游戏或干预 WS。

## 未完成与下一步

P0 仍未整体勾选完成：需要冻结 Pixels 外层、确定最终音频时长/包格式、补上完整音频队列和发送端独立黄金向量、冻结性能对比条件。
视频实验核心用于验证移植可行性，不代表 P1 全面验收：还需扩大非法包、跨块丢失、回绕、冷却切换、多流和资源边界测试。
当前支持固定等长视频数据报；如最终方案需变长尾包，要单独对齐和测试。

测试基准使用串行 oracle 实例和模拟时钟；上游全局状态只在参考程序中存在，产品不可沿用。
输入超出上游 10-bit 索引能力时实验发送器直接拒绝，而非照搬其仅日志告警后继续发送；这是明确的安全差异。

接下来完成 P0/P1 门槛，再接入 Render/SDK、音频、实际发送与恢复信息，最后用本机真实 Client 连接 90 验收。
**尚未开始新内核的真实客户端画面验收，不存在“90 已运行参考测试，所以产品已验收”的结论。**
