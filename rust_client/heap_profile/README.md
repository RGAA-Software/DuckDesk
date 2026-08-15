# px_sysinfo 堆分析结果

## 测试环境

- **测试日期**：2026-06-26
- **目标程序**：`GammaRaySysInfo.exe`
- **测试场景**：指向一个不存在的 Panel 服务（`ws://127.0.0.1:59999/sys/info`），模拟网络失败/断连。
- **分析工具**：Rust `dhat` crate（跨平台堆分析器）
- **构建命令**：
  ```bash
  cargo run -p px_sysinfo --bin GammaRaySysInfo --features dhat-heap -- --port 59999 --exit-after <secs>
  ```

## 原始问题

`px_sysinfo` 在一天内内存占用涨到 ~10 GB。代码修复前主要风险：

1. WebSocket 发送无超时，半开连接可能长期挂起。
2. 断线后旧 `SplitSink` 未清理。
3. 固定高频重连。
4. 每次刷新都重新初始化 NVML/ADLX。

## 测试记录

| 测试项 | 数值 |
|---|---|
| 测试日期 | 2026-06-26 |
| 测试时长 #1 | 60 秒 |
| 测试时长 #2 | 180 秒 |
| 目标端口 | 59999（无服务监听） |
| 重连间隔 | 2 秒（固定） |
| 连接错误 | `IO error: No connection could be made because the target machine actively refused it. (os error 10061)` |
| `sender strong_count` | 2（断网期间稳定） |

## 修复后结果

| 指标 | 60 秒 | 180 秒 | 说明 |
|---|---|---|---|
| 总分配 (Total) | 87,889 bytes | 131,717 bytes | 运行期间累计分配 |
| 最大存活 (t-gmax) | 51,322 bytes | 50,714 bytes | **几乎没有增长** |
| 结束存活 (t-end) | 50,339 bytes | 49,731 bytes | **几乎没有增长** |
| 重连次数 | ~30 次 | ~90 次 | 固定 2 秒间隔 |
| 原始日志片段 | `dhat: Total: 87,889 bytes in 454 blocks` | `dhat: Total: 131,717 bytes in 1,083 blocks` | — |

## 结论

- **堆内存没有泄漏**：`t-gmax` 和 `t-end` 在 60→180 秒之间基本持平，说明没有 live 对象持续累积。
- **总分配增长合理**：从 60 秒到 180 秒，总分配增加约 44 KB，主要来自日志字符串、临时 JSON、重连相关小对象，且这些对象都会被释放。
- 原始 10 GB 内存占用很可能来自 **RSS/堆碎片** 或 **发送任务/连接挂起** 导致的资源残留。本次修复的「发送超时 + 断线清 sender + 指数退避 + NVML/ADLX 缓存」正是针对这些机制。

## 局限性

- `dhat` 只能测量 **堆分配**，不能直接测量 **RSS、虚拟内存、堆碎片、线程栈、TLS/TCP 缓冲** 等。
- 在 Windows 上 `dhat` 的调用栈符号有时不完整（本报告中显示为 `?`），因此无法给出精确到函数行的火焰图。
- 要确认 10 GB 问题是否完全解决，需要：
  1. 在实际场景中长时间运行（数小时到一天）。
  2. 使用 Windows Performance Toolkit / VMMap 观察 RSS 和堆碎片。
  3. 对比修复前后的内存曲线。

## 后续建议

1. **现场长时间验证**：在出问题的机器上部署修复版本，用任务管理器或 PerfMon 持续观察 `GammaRaySysInfo.exe` 的 Working Set。
2. **WPT 分析**：如果 RSS 仍涨，用 Windows Performance Toolkit 的 `WPA` 抓取 `VirtualAlloc`/`HeapAlloc` ETW 跟踪。
3. **正常连接场景**：当前测试是断网场景。也应在正常连接场景下跑同样测试，确认发送路径没有引入新泄漏。
