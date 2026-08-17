# Render 端音视频录制插件设计方案（共享库 + 滚动录制 + 自动录制）

> 状态：待评审（未动工）
> 范围：PC 客户端（px_client）与 Render 端（px_render）共用一套录制核心；Android 客户端不在本次范围。
> 约定：所有文件均为计划内容，未修改任何代码。

---

## 1. 背景与目标

### 1.1 背景

- Render 端采集屏幕 + 系统声音，经编码器（AMF / NVENC / FFmpeg）编码为 H264/H265，经 Opus 编码器编码音频，再经 net 插件发往客户端。
- PC 客户端已有"录屏"功能（`px_client/plugins/media_record`）：把收到的**已编码流**直接 remux 成 MP4，不解码不重编码。
- Render 端 `px_render/plugins/media_recorder` 目前是一个**空壳插件**（只有元数据，无任何实现）。

### 1.2 目标

1. 在 Render 端实现音视频录制插件，**复用编码后的现成流**（视频 H264/H265、音频 Opus），remux 直存，零重编码。
2. 修复客户端现有录制实现的音视频同步问题，并让客户端与 Render 端**共用一套录制核心代码**。
3. 滚动式录制：单文件上限 1GB，超出自动开新文件；总文件数上限 24 个，超出删除最旧。
4. 文件命名统一 `record_` 前缀（不用 GammaRay 相关字眼）。
5. 支持"自动录制"配置：开关打开后，有人连接就录，无人连接就停。
6. 支持手动触发（Render 面板按钮）。

---

## 2. 现状分析

### 2.1 数据流

```
屏幕采集 ─→ 视频编码器(AMF/NVENC/FFmpeg) ─→ 编码帧 ──┬→ net 插件 → 客户端
(WASAPI/  ─→  Opus 编码器                ─→ 编码音频 ─┴→ net 插件 → 客户端
 MiniAudio)
```

关键分发点（render 侧）：

| 流 | 分发机制 | Stream 插件能否拿到 |
|---|---|---|
| 编码视频 | `PluginStreamEventRouter::ProcessEncodedVideoFrameEvent` → `PostStreamPluginTask` → 所有 Stream 插件 `OnEncodedVideoFrame(mon_name, type, data, frame_index, w, h, key)` | ✅ 已有，串行任务线程 |
| 原始音频 | `rd_app.cpp`（CaptureAudioFrame 监听）→ `PostStreamPluginTask` → `OnRawAudioData(data, samples, channels, bits)` | ✅ 已有（未编码 PCM） |
| **编码音频** | `OpusEncoderPlugin` 发 `PxPluginEncodedAudioFrameEvent` → `PluginEventRouter` → `ProcessEncodedAudioFrameEvent` → **只发给 net 插件** | ❌ **缺失，需新增** |

### 2.2 客户端现有录制实现（参考对象，含 bug）

`px_client/plugins/media_record/media_recorder.cpp`：

- 视频：`video_stream_->time_base = {1, 90000}`，pts = 录制起始后毫秒 × 90 —— **正确**。
- 音频：**未设置 time_base**（`avformat_new_stream` 默认 `{1, 1000000}`），pts = `960 × 帧号`。
  - MP4 muxer 音频轨 timescale 取 sample_rate（48000），pts 从 `1/1e6` 换算到 48000 后每包 ≈ **46 ticks ≈ 0.96ms**（实际一个 Opus 包是 20ms）→ **音频轨时间轴被压缩约 20 倍，与视频严重不同步**。
- 手工拼接 19 字节 OpusHead extradata（否则 MP4 无法播放）—— 正确，需保留。
- 无分段、无滚动清理、无自动录制。

### 2.3 Render 侧已有基础设施

- 插件体系：`PxStreamPlugin`（Stream 型插件）已接收 `OnEncodedVideoFrame` / `OnRawAudioData`；`VisitStreamPlugins` 遍历所有 Stream 插件（含禁用插件，插件内部自查 `IsPluginEnabled()`）。
- 插件 ID：`kMediaRecorderPluginId` 已在 `plugin_ids.h`（`media_recorder` DLL 已在 `px_build_premium_all` 目标中）。
- 配置下发：插件 DLL 内 `RdSettings::Instance()` 是 header-static 独立副本，**配置必须由 exe 侧通过 `PxPluginParam` cluster 显式下发**（现有 `app_mode` 即此模式）。
- 连接感知：`PluginNetEventRouter` 已把 `OnNewClientConnected` / `OnClientDisconnected` 广播给所有插件；另有 `MsgConnectedClientCount`（1s 周期）。
- 关键帧请求：`PxPluginInterface::InsertIdr()` → `kPluginInsertIdrEvent` → 所有编码器补 IDR；`PxPluginInsertIdrEvent` 支持 `mon_name_` 定向（当前只有广播路径）。
- FFmpeg：根 CMake `find_package(FFMPEG REQUIRED)`，`FFMPEG_ROOT = ${VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}`，插件直接可用（`enc_ffmpeg` 即先例）。
- 目录：`FolderUtil::GetProgramDataPath()` = `C:\Users\Public\Pixels`（render 数据在 `...\px_data`）。
- 客户端插件参数：`ClientPluginParam.cluster_` 已含 `device_id`、`screen_recording_path` 等（`ct_plugin_manager.cpp`）。

---

## 3. 总体架构

**核心思路：抽一个与平台无关的共享静态库 `px_media_record_new`，客户端与 Render 端各自保留一个薄适配层。**

```
┌──────────────────────── px_media_record_new（核心，一份代码）───────────────────────┐
│ 依赖：仅 C++ std + FFmpeg（不依赖 Qt / protobuf / 插件接口 / 平台）                  │
│                                                                                       │
│  RecordWriter（每路录制一个实例，即"一个显示器 = 一个实例"）                          │
│   • 统一墙钟 pts：video = 起始后毫秒×90 @ time_base 1/90000                          │
│                  audio = 起始后毫秒×48 @ time_base 1/48000（显式设置）                │
│   • OpusHead extradata 手拼；SPS/PPS/VPS 参数集缓存（新段 extradata 复用）            │
│   • 1GB 分段：写满 → trailer → 回调"请插 IDR" → 等关键帧 → 开新段                    │
│     （等关键帧期间音频入队缓冲，开段后回填，避免丢声音）                              │
│   • 命名 record_ 前缀 + 滚动清理（保留最新 N 个，只删自己的文件）                     │
└──────────────┬──────────────────────────────────────┬────────────────────────────────┘
       客户端适配层（薄壳）                      Render 适配层（薄壳）
  px_client/plugins/media_record              px_render/plugins/media_recorder
  • proto VideoFrame/AudioFrame → core        • OnEncodedVideoFrame / OnEncodedAudioFrame
  • 保留：录像路径(面板设置/视频文件夹)          → core
  • 保留：结束通知、多屏 index 分流            • 保留：OnCommand、auto 配置、连接事件
                                              • 保留：面板 Enable/Disable + Record 按钮
```

- 共享库放在 `src/px_deps/px_media_record_new/`（与 `px_opus_codec_new`、`px_encoder_new` 同级）。
- 两端插件 CMakeLists 各 `target_link_libraries(... px_media_record_new)`；静态库编进各自 DLL，ffmpeg DLL 依赖与现状一致。
- Android 构建不引用该模块 → 零影响。

---

## 4. 详细设计

### 4.1 共享库 API（草案）

```cpp
// record_writer.h（px_deps/px_media_record_new）
namespace px {

enum class RecordVideoCodec { kH264, kH265 };

struct RecordWriterConfig {
    std::string dir;            // 录像目录（自动创建）
    std::string device_id;      // 命名用，空则 fallback "default"
    std::string monitor_name;   // 命名用，空则 "mon0"；单屏可传空
    std::string file_prefix = "record_";   // 命名与清理均按此前缀
    int64_t max_segment_bytes = 1024LL * 1024 * 1024;  // 单文件上限 1GB
    int max_file_count = 24;    // 总文件数上限（≈24GB）
    // 滚动到新段前，core 回调请求适配层插入关键帧（客户端无需：服务端录制期间
    // 本就在每秒插 IDR；render 端：调用自身 InsertIdr()）
    std::function<void()> on_request_keyframe = nullptr;
};

class RecordWriter {
public:
    static std::shared_ptr<RecordWriter> Make(const RecordWriterConfig& cfg);

    // 编码视频帧（Annex-B，含起始码；关键帧时包内含 SPS/PPS 或仅 IDR）
    void OnEncodedVideo(const uint8_t* data, size_t size,
                        RecordVideoCodec codec, int width, int height, bool key);
    // 编码音频帧（Opus 单包，默认 48kHz 立体声 20ms/960 样本）
    void OnEncodedAudio(const uint8_t* data, size_t size);

    // 结束录制：写 trailer、关文件、执行滚动清理
    void Stop();

    // 是否正在写文件（供 UI 状态显示）
    bool IsRecording() const;
};
}
```

内部要点：

- **初始化时机**：收到首个"配置帧"（H264 的 SPS / H265 的 VPS，判断 `data[4]` 起始码后 NAL 类型，与客户端现有逻辑一致）时创建 `AVFormatContext`、视频流（只填 `codecpar`，不开编码器）、音频流，`avio_open` + `write_header`。首个配置帧作为文件的第一个包（pts=0）。
- **视频 pts**：`(now - start) ms × 90`，dts = pts（低延迟编码无 B 帧；如编码器配置出 B 帧再另行处理）。
- **音频 pts**：`(now - start) ms × 48`（48kHz 时间基下 1ms = 48 ticks，20ms 包 = 960 ticks，样本精确）。
- **写包**：`av_interleaved_write_frame`，包数据直接引用（零拷贝：上游 `shared_ptr<Data>` 队列持有）。
- **OpusHead**：19 字节手拼 header（Magic/版本/声道/预跳过/采样率/增益/mapping family），写入 `audio_codecpar->extradata`，来源与客户端版一致（带注释说明）。
- **SPS/PPS/VPS 缓存**：任何时候收到配置帧都更新缓存；开新文件时若缓存存在，把参数集写入视频流 `codecpar->extradata`（mp4 muxer 据此生成 avcC/hvc1 box，不依赖"首包必须带 SPS/PPS"）。
- **HEVC tag**：MP4 中默认 hev1，兼容性更好的 hvc1 通过设置 `codec_tag` 实现（实现时验证，VLC/ffprobe 均支持则保留默认）。

### 4.2 A/V 同步设计（修复客户端 bug 的核心）

- 音视频**统一用同一个墙钟**（录制开始时刻起算），两轨时间基显式设置：
  - 视频：`time_base {1, 90000}`，pts = 毫秒×90；
  - 音频：`time_base {1, 48000}`，pts = 毫秒×48。
- 两轨从同一时刻起算 → 天然对齐；任何一轨丢帧/抖动只影响自身时间轴，不会牵连另一轨。
- 客户端适配层接入共享库后，旧实现（960×帧号 + 默认 time_base）整体废弃，**bug 自动修复**。
- 验证：`ffprobe` 对比两轨 duration（应基本相等）；播放器试听。

### 4.3 滚动录制（1GB 分段 + 24 文件上限）

**分段流程**（`RecordWriter` 内部）：

1. 累计写入字节 ≥ `max_segment_bytes`（按写入的包字节累计，近似即可）→
2. 写 trailer、关文件（**不是** Stop，会话继续）→
3. 回调 `on_request_keyframe`（render 端 → 插件 `InsertIdr()` 广播；客户端 → 无需动作，服务端录制期间每秒已插 IDR）→
4. 进入"等关键帧"状态：丢弃非关键视频帧；**音频帧入队缓冲**（上限约 5 秒，超出丢最旧）→
5. 收到关键帧（key=true）→ 用 SPS/PPS 缓存初始化新文件（pts 从新文件打开时刻重新起算）→
6. 回填缓冲的音频（pts = 到达墙钟时间 − 新文件起点），继续正常写入。

> 这样每个分片都是"首帧关键帧 + 可播放"的独立 MP4；分段瞬间只损失 ≤1 个 GOP 的 P 帧（正常 0.1~2 秒），音频不丢。

**滚动清理**：

- 时机：每次开新段后、每次 Stop 后、插件启动时（兜底清残留）。
- 规则：扫描录像目录，匹配 `record_` 前缀 + `.mp4`，按文件修改时间排序，**保留最新 `max_file_count` 个，删除更旧的**；只删自己的文件。
- 说明：24 个 × 1GB ≈ 24GB 总量上限（录到一半停止的段不满 1GB，总量自然更小，符合"最大 24GB"语义）；**24 个配额默认所有屏幕全局共享**（约束总容量）。

### 4.4 文件命名与目录

```
rec_{monitor}_{YYYYMMDD}_{HH.MM.SS}.mp4
例：rec_DISPLAY1_20260817_12.43.28.mp4
```

- 前缀固定 `rec_`，不使用任何 GammaRay 字眼。
- `monitor`：显示器名，去掉 `\\.\` 设备路径前缀并做文件名安全化（`\\.\DISPLAY1` → `DISPLAY1`）；单屏固定 `mon0` 或省略（实现时定，默认保留 `mon0` 保持格式统一）。
- 时间戳：墙上时钟，`YYYYMMDD_HH.MM.SS`（人类可读；pts 仍用单调时钟，两者互不干扰）。
- **目录**：
  - Render 端默认：`C:\Users\Public\Pixels\px_render_records\`（`FolderUtil::GetProgramDataPath()` = Public\Pixels，与 `px_data`、frame_debugger 输出同约定）；`settings.toml` 新增 `[record] dir`，非空则覆盖默认（自动创建目录）。
  - 客户端：`screen_recording_path_`（面板可设置）优先，未设置时默认 `C:\Users\Public\Pixels\px_client_records`。

### 4.5 自动录制配置

- `settings.toml` 新增：

  ```toml
  [record]
  auto_enabled = false   # 有人连接自动录，无人连接自动停
  dir = ""               # 录像目录，空 = 默认 C:\Users\Public\Pixels\px_render_records
  ```

- `RdSettings` 增加 `record_auto_` / `record_dir_` 成员 + TOML 解析。
- **下发**：`PluginManager::LoadAllPlugins` 构建 `PxPluginParam` 时追加 `{"record_auto_enabled", bool}`、`{"record_dir", std::string}`（沿用 `app_mode` 的下发模式，避免 DLL 内 RdSettings 副本问题）。
- **插件行为**（`MediaRecorderPlugin` override `OnNewClientConnected` / `OnClientDisconnected`，事件驱动无轮询）：
  - auto 开启：连接数 0→1 → `StartRecord()`；最后 1→0 → `StopRecord()`。
  - Stop 以"工作队列尾部任务"执行，保证已入队的帧全部写完再写 trailer。
  - 与手动按钮互斥：auto 开启时忽略面板手动命令（或置灰）。
- 无需改采集门控：capture→encode 管线本身只在有连接客户端时跑（`HasConnectedPeer()`），自动录制天然"连接才有数据、断开即停"。

### 4.6 手动触发（Render 面板）

- `px_message_new/px_render_panel_message.proto`：`RpPanelCommand` 增加 `kStartMediaRecordServerSide = 2`、`kStopMediaRecordServerSide = 3`。
- `px_render/network/ws_panel_client.cpp`：新命令路由到目标插件 `OnCommand("record:start" / "record:stop")`。
- `px_panel/src/render_panel/ui/st_plugin_item_widget.cpp`：对 Media Recorder 插件（按 `kMediaRecorderPluginId` 判断）追加 "Start Record / Stop Record" 按钮（沿用 Enable/Disable 按钮模式）。

### 4.7 Render 端接口新增：编码音频分发

1. `px_render/plugin_interface/px_plugin_interface.h`：新增虚函数
   ```cpp
   virtual void OnEncodedAudioFrame(const std::shared_ptr<Data>& data, int samples,
                                    int channels, int bits, int frame_size) {}
   ```
   （默认空实现，与 `OnEncodedVideoFrame` 对称；`PxStreamPlugin` 同步加 override。）
2. `px_render/plugins/plugin_net_event_router.cpp` `ProcessEncodedAudioFrameEvent`：在现有"发 net 插件"之外，追加 `PostStreamPluginTask` 把编码音频分发给所有 Stream 插件。

### 4.8 线程模型与队列保护

- 上游回调线程：视频/音频回调发生在共享的 Stream 插件任务线程上（串行）。**插件回调内只做拷贝入队（或持 shared_ptr），不碰 FFmpeg**；实际写盘在插件自己的工作线程（`PostWorkTask`）。
- 队列上限：写盘队列超过阈值（如 512 帧）时丢弃新帧并记日志，避免磁盘慢拖垮管线（render 已有"慢分发告警"机制，插件必须遵守）。
- 停止顺序：`Stop()` 以队列尾部任务执行 → 写完全部帧 → trailer → 清理。
- 多屏：每屏一个 `RecordWriter`（render 按 `mon_name` 建 map；客户端维持现有按 index 的 vector）。

---

## 5. 改动清单

### 5.1 新增

| 文件 | 内容 |
|---|---|
| `src/px_deps/px_media_record_new/CMakeLists.txt` | 静态库，链接 `${FFMPEG_LIBRARIES}` |
| `src/px_deps/px_media_record_new/record_writer.h/.cpp` | 核心录制器（4.1~4.3 全部逻辑） |
| `src/px_render/plugins/media_recorder/media_recorder.cpp` | 录制器实现（原空壳插件补充） |

### 5.2 修改

| 文件 | 改动 |
|---|---|
| `src/px_render/plugin_interface/px_plugin_interface.h` | + `OnEncodedAudioFrame` 虚函数 |
| `src/px_render/plugin_interface/px_stream_plugin.h/.cpp` | + override |
| `src/px_render/plugins/plugin_net_event_router.cpp` | 编码音频追加分发到 Stream 插件 |
| `src/px_render/plugins/media_recorder/media_recorder_plugin.h/.cpp` | 实现 `OnEncodedVideoFrame`/`OnEncodedAudioFrame`/`OnCommand`/`OnNewClientConnected`/`OnClientDisconnected`/`OnCreate`；Start/Stop；auto 逻辑 |
| `src/px_render/plugins/media_recorder/CMakeLists.txt` | 链接 `px_media_record_new` |
| `src/px_render/plugins/plugin_manager.cpp` | param cluster 增加 `record_auto_enabled` / `record_dir` |
| `src/px_render/settings/rd_settings.h/.cpp` | + `record_auto_` / `record_dir_` + TOML 解析 |
| `src/px_render/network/ws_panel_client.cpp` | 新面板命令 → `OnCommand` |
| `src/px_deps/px_message_new/px_render_panel_message.proto` | `RpPanelCommand` + 2 个枚举值 |
| `src/px_panel/src/render_panel/ui/st_plugin_item_widget.cpp` | + Record 按钮 |
| `src/px_client/plugins/media_record/media_recorder.h/.cpp` | 改造为适配层：proto 帧 → `RecordWriter`，删除 ffmpeg 内部逻辑 |
| `src/px_client/plugins/media_record/CMakeLists.txt` | 链接 `px_media_record_new`（去掉直接 ffmpeg 链接） |
| `src/px_client/plugins/media_record/media_record_plugin.cpp` | 适配层接线（目录、通知、多屏分流） |
| `src/px_render/plugins/plugins/CMakeLists.txt`（如需要） | 确保 `media_recorder` 已 add_subdirectory（已存在） |

### 5.3 删除 / 不改

- 客户端旧 muxer 逻辑（随适配层改造移除）。
- Android：不引用新模块，零改动。
- `plugin_ids.h`：`kMediaRecorderPluginId` 已存在，不改。

---

## 6. 构建与验证

1. **构建**：根工程 x64 Release（`px_build_premium_all`），确认生成 `media_recorder.dll`、`media_record_client.dll`。
2. **Render 手动录制**：面板 Enable Media Recorder → Start Record → 客户端连上跑 1~2 分钟（含音频）→ Stop。
3. **同步验证**：`ffprobe` 检查 MP4 两轨 duration 基本相等；播放器试听 A/V 对齐。
4. **滚动验证**：临时调小 `max_segment_bytes`（如 20MB）验证分段 + 关键帧首帧 + 音频连续；验证超过 `max_file_count` 后最旧文件被删。
5. **命名/清理验证**：确认 `record_{device_id}_{mon}_{ts}.mp4`；目录内放入无关文件确认不被误删。
6. **编码器矩阵**：FFmpeg 软编 / AMF / NVENC × H264 / H265 各录一段；多屏各录一段。
7. **自动录制**：`[record] auto_enabled = true` → 连接 → 自动开始；断开 → 自动收尾（文件完整可播放）；重连 → 新文件。
8. **客户端回归**：客户端录制功能照旧可用，文件可播放、音画同步；顺带验证客户端 1GB 分段/滚动（如启用）。

---

## 7. 风险与对策

| 风险 | 对策 |
|---|---|
| MP4 muxer 需要 avcC/hvc1 box，首包不带 SPS/PPS 时文件不可播 | 参数集缓存 + 写 extradata（4.1）；分段必然等关键帧开新文件 |
| Opus 进 MP4 无 extradata 报 "invalid size 0 in stsd" | 19 字节 OpusHead 手拼（客户端已验证思路） |
| 编码器不重复输出 SPS/PPS | 关键帧到达即开新段（key 标志），参数集用缓存，不依赖带 SPS/PPS |
| 写盘慢拖垮共享 Stream 任务线程 | 回调只入队，写盘在工作线程；队列上限丢弃 + 日志 |
| 滚动时新文件首帧非关键帧 | "等关键帧"状态 + 音频缓冲回填（4.3） |
| 磁盘写满 | v1 不处理（写失败记日志并停止该段录制）；后续可加剩余空间检查 |
| 多屏文件配额语义不清 | 默认全局共享 24 个；可按 mon 分组（待确认项） |
| 无客户端时想录（无人值守） | 明确不做：采集管线被 `HasConnectedPeer()` 门控，自动录制仅"连接期间"有效 |

---

## 8. 默认决定与待确认项

**默认决定（如无异议按此执行）：**

1. 音频走**编码后 Opus**（新增 `OnEncodedAudioFrame` 分发），零重编码。
2. 客户端与 Render 端**共用 `px_media_record_new`**，客户端同步获得 1GB/24 滚动规则（两端行为一致）。
3. 命名：`record_{device_id}_{mon}_{时间戳}.mp4`；device_id 用**本机（录制方）**的 device_id。
4. 24 个文件配额**全局共享**（约束总容量）。
5. Render 默认目录 `C:\Users\Public\Pixels\px_render_records\`，`settings.toml` 可覆盖；客户端目录维持现状。
6. `[record] auto_enabled = false`（默认关）。
7. 自动录制开启时，面板手动按钮忽略。

**待确认：**

- 客户端是否确认启用同样的 1GB 分段 + 滚动清理（默认：启用）。
- 是否需要按访客 device_id 命名（默认：录制方 device_id）。
- 是否每个显示器独立 24 个配额（默认：全局共享）。
- 面板目录选择 UI（v1 不做，后续可加）。

---

## 9. 测试准备与验证方案

### 9.1 现有测试基建（已确认可用）

| 基建 | 位置/说明 |
|---|---|
| 桌面构建 | `build_official/`（Ninja，RelWithDebInfo，triplet `x64-windows-static-release`，VCPKG `C:/source/vcpkg`） |
| 可运行部署 | `build_official/dist/`：`px_render.exe` / `px_client.exe` / `px_panel.exe` / `settings.toml` / `deps/rd_plugins/*.dll`（`media_recorder.dll` 已存在，当前是空壳） |
| 单测体系 | `TESTS_ENABLED=ON`；GTest；模式 = 模块下 `tests/` 子目录 + `add_tc_test()`；构建 `build_official_tests.bat`、运行 `run_tc_tests.bat`（现有 19 个 `test_*`） |
| 媒体工具 | `C:\source\vcpkg\installed\x64-windows-static-release\tools\ffmpeg\ffmpeg.exe` / `ffprobe.exe` |
| 其他插件 | `mock_video_stream.dll`（可模拟流，多屏测试不用真接第二显示器） |

### 9.2 测试基建新增（随实现一起提交）

1. **单测目标 `test_record_writer`**：`src/px_deps/px_media_record_new/tests/`，GTest，照 `px_common_new/tests` 模式（`TESTS_ENABLED` 门控）；加入 `build_official_tests.bat` 构建列表与 `run_tc_tests.bat` 运行列表。
2. **素材生成脚本 `scripts/gen_record_test_assets.bat`**：调 vcpkg ffmpeg 生成确定性素材——H264（`testsrc` 10s 640x360@30，`libx264` g=30，Annex-B）与 Opus（`sine` 10s 48k 立体声）。供单测与人工验证共用。
3. **验证脚本 `scripts/verify_record_file.bat`**：封装 ffprobe 输出（流数量、编码、分辨率、两轨 duration、关键帧/包统计），一次调用即可判定文件是否合格。
4. **测试口（建议采纳为正式配置）**：`settings.toml` 的 `[record]` 增加可选 `max_segment_bytes` / `max_file_count` 覆盖项（默认 1GB / 24）。否则滚动测试只能改代码常量重新编译。
5. **测试输出目录**：端到端测试时通过 `[record] dir` 指到 `build_official/test_records/`（不进仓库、不进默认目录，互不污染）。

### 9.3 单测用例（test_record_writer，确定性）

| # | 用例 | 断言 |
|---|---|---|
| 1 | 基础录制：喂 10s H264（首帧为配置帧）+ Opus 包（每 20ms 一个，时间与视频同墙钟）→ Stop | 文件存在；ffprobe：2 轨；视频/音频 duration ≈10s 且互差 <100ms（同步回归测试） |
| 2 | 首帧关键帧 | 文件第一个视频包 key=true；从头解码可播（新文件不依赖任何外部状态） |
| 3 | 分段：`max_segment_bytes` 调小（如 2MB），生成 8MB 数据 | 产出 ≥4 个文件；每段首帧关键帧；各段音频 duration 之和 ≈ 视频 duration 之和（音频连续） |
| 4 | 滚动清理：`max_file_count=3`，连续录 4 段 | 目录只剩 3 个文件，最旧的被删；目录内放一个无关文件确认不被误删 |
| 5 | 命名 | `record_{device_id}_{mon}_{ts}.mp4`；device_id 含非法字符被安全化；为空 fallback `default` |
| 6 | 目录自动创建 | 不存在的 `dir` 自动建目录并成功写入 |
| 7 | 提前 Stop（录 2s 即停） | 文件完整可读（trailer 正常、ffprobe 通过） |
| 8 | 无关键帧到达（模拟编码器不回 IDR） | 等关键帧状态不崩溃；音频缓冲有上限（约 5s），超限丢最旧并记日志 |

> 测试向量：视频用 avcodec 内存内 libx264 编码生成（或读素材文件），音频用 `px_opus_codec_new` 编码正弦 PCM——完全自包含、可重复、无外部依赖。

### 9.4 端到端用例（本机真机，build_official/dist）

环境：本机跑 render（`px_render.exe`，端口 20371，采集 DDA/GDI，音频 WASAPI 全局）+ 本机跑 client（`px_client.exe` 连 127.0.0.1）。

| # | 场景 | 步骤 | 判据 |
|---|---|---|---|
| E1 | 手动模式 | 面板 → 插件页 → Enable Media Recorder → Start Record → 客户端连上播放音频 1~2 分钟 → Stop | 文件生成；ffprobe 两轨 duration 基本相等；播放器音画同步、可拖拽 |
| E2 | 自动模式 | `[record] auto_enabled = true` → 启动 render → 客户端连接 | 连接后自动开始录（文件出现）；断开后自动收尾（trailer 完整、ffprobe 可读）；重连开新文件 |
| E3 | 滚动 | `[record] max_segment_bytes` 调小 + 长时间录 | 分段正确、段首关键帧、超上限删最旧（9.3 的端到端复现） |
| E4 | 编码器矩阵 | settings 切 FFmpeg 软编 / AMF / NVENC × H264 / H265 各录一段 | 全部可播、同步正常 |
| E5 | 多屏 | 真机双屏，或启用 `mock_video_stream` 模拟第二路 | 每个 mon 一个独立文件、命名带 mon、互不干扰 |
| E6 | 客户端回归 | 客户端录制功能照旧使用 | 文件可播、音画同步（验证旧 time_base bug 已修复） |

### 9.5 回归

- `run_tc_tests.bat` 全量跑现有 19 个测试，确认共享库未破坏其他模块。
- 桌面全量构建 `px_build_premium_all` 确认 render/client 全部插件编译链接正常。

### 9.6 执行顺序

1. 先单测（test_record_writer 全绿）→ 2. 端到端 E1~E6 → 3. 全量回归。
4. 单测与端到端都通过后，再走提交流程（按既定规则，不经确认不提交）。
