# GameHook 帧身份与发送暂停定位（2026-10-10）

## 复用旧证据定位

保留固定 256 KiB 文件回执额度；未重新试验已撤回的自适应文件窗口。
从 90 原始 Render 日志取回上一轮 22:59 / 23:00:3 的记录，保存为
`20261010_sender_pause_previous_render.log`；同机时间关联结果为
`20261010_hook_frame_identity_analysis.json`。

- Relay 四次封包提交间隔 133/184/266/167 ms，对应准入额度等待
  116710/166921/249207/150764 us，准入恢复至提交分别仅 4/6/5/5 ms。
  同期 enc diag 的捕获输入约 60 FPS，任务最大 9 ms。此四次主要是传输额度背压，
  不能归因于 GPU 编码或游戏渲染；也不据此解释全部接收停顿。
- direct 的首帧是回放缓存：23:00:33.315 回放 index=152，.344 输出 frame=152；
  .477 真实帧再次输出 frame=152，.523 收到参考恢复请求。
  Client 实际收到两个 152，提交间隔 133133 us、接收间隔 169487 us、组帧调用 27 us。
  两主机墙钟有偏差，仅分别使用本机间隔及帧身份，不跨主机直接相减。
- `ReplayLatestGameHookFrame` 只递增自己的缓存回放计数，后续 `DeliverCapturedVideoFrame`
  又恢复源捕获编号；相同帧号在编码链里代表不同输出。客户端
  `RequiresVideoReferenceReset` 明确拒绝非关键帧编号小于等于已解码编号。
  重复帧还使累计回执的帧身份歧义。它是确定缺陷，但不是整个 133 ms 启动间隔的充分解释。

## 修复与参考

GameHook 在串行编码工作线程入口为每个 monitor 统一分配单调递增身份，覆盖缓存回放、
真实帧、跳过的捕获和源编号重置。编码/NVENC inputTimeStamp/数据报描述符/参考恢复继续使用同一身份。
真实捕获计数不改写到 Hook 生产者；桌面/WebView/RDP 不切换编号策略。
序列达到 uint64 上限拒绝分配，不回绕到旧参考。

参考只读 Sunshine `D:/source/Sunshine`，HEAD
`3cba9baebac882b336be3ebe129ee612cb189853`：`src/video.cpp` 的编码入口使用
`encode(frame_nr++, ...)`，NVENC 输出检查对应 frame_index；没有复制其指针所有权模式。
旧工作区源码（含既有未提交修改）存于 `backup/iroh_hook_frame_identity_20261010/`，带哈希/基线清单。

## 聚焦验证

- `encoder_frame_sequence` 三项用例通过，直接调用真实 Client 参考判定：旧重复编号要求重置，
  统一编号保留前驱参考；同时覆盖缓存/真实帧交错、捕获重启、独立 monitor、跳号、溢出拒绝。
- `encoded_video_delivery`、`iroh_transport`、`iroh_frontend` CTest 通过；外部环境依赖的
  内部跳过项不算实机通过。日志 `.cache/hook-frame-identity-tests.log`。
- 新文件 clang-format 检查通过，聚焦 Release 构建通过；Render build/dist SHA 一致。
  完整候选 `iroh-hook-frame-identity-20261010`，版本 3.3.97，314 文件清单通过。
  Setup/manifest/Render 身份见 `20261010_hook_frame_identity_build.json`。

## 实机交付与后测

90 通过完整新鲜 Setup 安装本候选，314 文件校验通过，Render SHA
`785DDEBB55324033DE8B6227411347F1E78EB8036E2D3F32A1E10A74DB767222`。
开发 Render 已同步 dist，SHA
`C1EA2243DC209F9E6F90D1F46A1D09DC8EA44CDC89E9555E0D9B7DA2B895CF5B`。
Client 沿用上一轮已核验版本，没有用手工复制替代安装。

| 场景 | 64 MiB 不可压缩往返 | 5 秒窗口交付 FPS | >100 ms 次数 | 最大间隔 |
| --- | --- | --- | --- | --- |
| 实际 direct 39.71.45.66:4613 | 27.047 秒、SHA 一致 | 51.2–58.8 | 4 | 198.9 ms |
| 实际 90 Relay | 42.061 秒、SHA 一致 | 55.5–60.4 | 7 | 170.4 ms |

两轮只读 GET 重试均为零。具体原始结果/Client 与 Render 日志见
`20261010_hook_frame_identity_{direct,relay}*`。

direct 首帧现在是 152 → 153，消除了原来的重复身份；缓存至真实帧提交仍间隔
130377 us，因此明确不声称修复全部首屏等待。Relay 所有采样窗口 IDR/RFI 请求为零；
direct 后续仍有 RFI、QUIC tx_lost 增长（如 5 秒窗口计数 69 → 291 → 602 → 800），
有捕获额度阻塞。该轮没有双端包头定位，不冒称确定物理丢包位置，也不把网络轮次差异当作修复收益。
数据报不是全部无损、媒体性能未整体通过。

清理后分页检查 232 个资源会话无活动连接；无测试 Client、远端测试文件；
90 三服务 Running，仅桌面 Render 12556，Render 哈希复核一致；90/BJ fresh/ready/未维护。
本机从 23:10 到清理未记录新 NDIS 10400。未改网卡/驱动/持久网络配置；未启动新的 Pktmon 捕获。
清理与环境证据为 `20261010_hook_frame_identity_{after,cleanup,environment}.json`。

下一步保留本次帧身份修复和固定文件额度，针对 direct 混传丢包/参考恢复与捕获背压继续定位，
将连接首屏等待与稳定期间隔分别统计。复用既有 Relay 重传、容量/恢复证据；
真实 NAT/长期稳定性、Windows/Linux 收束和 Android 最后仍未完成。
