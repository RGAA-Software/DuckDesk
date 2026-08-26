# Asio Notify、RTC 与虚拟显示器最终验收报告（2026-08-26）

## 1. 结论

本轮实现、构建和局域网真实设备验收完成。当前发布门禁按最新约定为连续
10 轮，不再要求 100 轮。90 机器完成 10/10 连续虚拟显示器全链路验收，
共 50 个阶段全部通过，最终没有遗留 GammaRay 所有的虚拟显示器、活动测试
会话或有效测试票据。

由于当前没有公网和两条独立外网，真实跨公网 TURN relay 仍属于环境未覆盖项，
不计为本轮产品失败。局域网内的标准 RTC、TURN UDP、TURN TCP 回退、登录与
游客鉴权、Web 与 Windows 客户端并发等门禁已经完成。

## 2. 本轮收敛的问题

1. Render 与 Service 建立本地连接后，监听器注册晚于连接启动，可能丢失首次
   `MsgRenderConnected2Service`。已改为监听器就绪后再启动连接。
2. Service 的虚拟显示器 Query 曾参与请求 ID 缓存，进程 PID 复用时可能返回旧
   状态。Query 现在始终读取实时状态，只有变更操作保持幂等缓存。
3. USBMMIDD worker 可能已经完成驱动副作用但随后丢失响应。新增和删除两条路径
   现在都会核验真实拓扑；确认只发生一个符合请求的变化后提交所有权与 generation。
4. 实际显示器已经全部消失时，Query 会清除过期的 owned slot，避免后续操作被
   永久判定为所有权冲突。
5. Web 不再用失败 protobuf 的默认零值覆盖已确认状态。
6. Render 只接受成功且 generation 不倒退的虚拟显示状态。启动和显示拓扑通知后
   都会向 Service 查询权威状态并重新发送配置，操作响应丢失时在线会话可以自愈。
7. Console Relay 断开清理改为按连接实例比较；旧 WebSocket 的退出任务不能删除
   已经替换上线的新 Render 连接或清理新连接的房间。
8. RTC 切换显示器后，如果关键帧刚好落在新消费游标之前，编码适配层会在等待
   首个 IDR 期间按 800ms 节流持续请求关键帧，避免 RTC connected 但画面不再解码。

## 3. 自动化测试结果

### 3.1 C++ 与所有权门禁

- `scripts/run_tc_tests.bat`：377 passed，0 failed，4 skipped。
- 4 个 skip 为设计性长时间稳定性或硬件音频环境用例，不是失败。
- `check_cpp_ownership`：通过；没有新增裸指针声明、手工所有权或 `[this]` 异步捕获。
- `git diff --check`：通过。
- 在此前 Asio Notify 收敛阶段，完整 C++ 套件已经连续执行 10 轮并全部通过；原始
  日志位于 `build_official/test_results/asio_notify_10round_20260826/`。

### 3.2 Rust

- `px_service`：57 passed，0 failed。
- `px_console_server`：149 passed，0 failed，1 ignored。忽略项是要求显式本地 MongoDB
  的 L1 ticket 集成门禁；真实登录、签票、兑换与续签已由局域网 E2E 覆盖。
- 新增竞态测试覆盖：旧 Relay 连接退出不删除替代连接、当前连接正常注销、驱动
  新增/删除先发生但 worker 报错后的拓扑提交、过期所有权自动清理、Query 不走变更
  幂等缓存。

### 3.3 Web

- WebClient 单元测试：11 passed。
- 语音相关 Web 测试：19 assertions passed。
- WebClient production build：通过。
- WebClient 源 build、`build_official/dist/web_client` 和 90 部署目录均为 5 个文件，
  0 hash mismatch，0 extra。

## 4. 90 机器虚拟显示器 10 轮真实 E2E

证据目录：

`build_official/test_results/asio_notify_20260826/virtual_display_10round_final_v4/`

每轮均使用新的浏览器 profile、临时账号、一次性 ticket 和 RTC 房间，并依次验证：

1. 单物理屏基线有持续解码画面。
2. 添加一个虚拟屏后 owned、generation 和显示器枚举一致，RTC 重建成功。
3. 切换到虚拟屏，采集目标正确且解码帧持续增长。
4. 切回物理屏，采集目标正确且解码帧持续增长。
5. 删除虚拟屏后恢复单物理屏，RTC 重建成功且画面继续解码。

| 轮次 | generation | 阶段 | 本轮最小解码增量 | 最终 owned | 最终屏幕数 | 最终 RTC |
|---:|---:|---:|---:|---:|---:|---|
| 1 | 80 → 82 | 5/5 | 8 | 0 | 1 | connected |
| 2 | 82 → 84 | 5/5 | 11 | 0 | 1 | connected |
| 3 | 84 → 86 | 5/5 | 21 | 0 | 1 | connected |
| 4 | 86 → 88 | 5/5 | 25 | 0 | 1 | connected |
| 5 | 88 → 90 | 5/5 | 25 | 0 | 1 | connected |
| 6 | 90 → 92 | 5/5 | 25 | 0 | 1 | connected |
| 7 | 92 → 94 | 5/5 | 25 | 0 | 1 | connected |
| 8 | 94 → 96 | 5/5 | 26 | 0 | 1 | connected |
| 9 | 96 → 98 | 5/5 | 24 | 0 | 1 | connected |
| 10 | 98 → 100 | 5/5 | 25 | 0 | 1 | connected |

最终 90 Service 持久状态：`desired_count=0`、`owned_slots=[]`、
`topology_generation=100`、`removal_safe=true`、`last_error=null`。Service 为 Running，
退出码为 0；近 2 小时 Windows Application 日志中没有 `px_service`、`px_render`、
`px_panel` 或 `net_rtc_local` 相关错误事件。测试结束后有效 ticket 和 session 均为 0。

## 5. RTC 和鉴权覆盖

- Web → 90 标准 RTC 连续 10/10。
- Windows 原生客户端：登录/游客 × Direct/标准 RTC 均通过。
- Windows 原生客户端交替连接 10/10。
- Web 与 Windows 同时连接通过。
- TURN UDP 和阻断 UDP 后 TURN TCP 回退通过。
- 游客会话使用设备密码，不要求 ticket；登录会话使用 Console 短期 ticket。
- Direct 探测成功但实际连接失败后的标准 RTC 回退、配置更新、ICE restart、候选和
  RTT 统计展示已进入自动化或局域网验收范围。

尚缺的环境项：两台不同公网网络机器上的真实 TURN relay，以及真实公网 UDP 阻断
后的 TCP 回退复测。具备公网环境后直接按
`docs/lan_release_acceptance_test_plan.md` 中对应用例补证据。

## 6. 构建与交付哈希

以下构建树、`build_official/dist` 和 90 部署文件已经核对 SHA-256：

| 产物 | SHA-256 | 本机 dist | 90 |
|---|---|---|---|
| `px_service.exe` | `E3489612445BD1FEF0EA2A34D7BB03A3B6095F2897CB424238FFAD390C355436` | match | match |
| `px_service_manager.exe` | `53133CA2F04C02E6B219AF49F21F1818A1E4C4D36C5CB884E22A295863E48AD5` | match | match |
| `px_osinfo.exe` | `908F44E9A65AEBF16B24900F579B2F0A0B6C0FFBFB0E2DA963A3A92CAD3856A6` | match | match |
| `px_function.exe` | `24559AE3D02CEC1CCE0224F9CAA186E4253C0F6F611E0F518733646AF6C34757` | match | match |
| `px_render.exe` | `4C463AE7C3A040D5A0BA08A6EF5EFC0EF4A1357F0C65EA3D4A335218B5EC2BA4` | match | match |
| `net_ws.dll` | `4A31EF4764D873757F0A309928818A8815E12870C139A6C882A990E130496FE2` | match | match |
| `net_rtc_local.dll` | `7769C6633240C91FEED7C8B4B9F7809DB35846B306E56456832201334AA9BCD3` | match | match |
| `px_console.exe` | `AEF8062F1220914942AB38209D59A8C3374BFC78B16686B952278432B7C01C8E` | output match | 本机服务端 |

最终源码变更后重新执行了权威整包命令 `cmake --build build_official -j 6`，
`px_build_premium_all` 与最终 `collect_dist.py` 全部成功。Rust release 目录、CMake
构建树和 `build_official/dist` 的对应文件哈希一致；以上变更文件部署到 R90 后再次
逐项匹配。

Console HTTPS `30500` 和 Relay `30502` 正常监听，HTTPS 根页面返回 HTTP 200。
Windows 客户端变更产物已经同步到 `build_official/dist`，用户从该目录启动即可验证。

最终官方产物部署后又执行一次完整虚拟显示器/RTC 冒烟，证据在
`build_official/test_results/asio_notify_20260826/final_official_build_smoke/`：五阶段
解码增量分别为 26、8、32、29、13；generation 从 100 到 102，结束时 owned 为 0、
仅保留一个物理屏且 RTC 为 connected。

Android official APK 构建也已完成：

`src/px_android/app/apk/release/gammaray_official_3.0.0.apk`

SHA-256：`6B4DB0EFB80F0EB9216BD16BB321B054607043EBEC84385B8D0D2C7C732809FB`。

## 7. 发布判断

在当前局域网和硬件范围内，本轮 Asio Notify 并发迁移、RTC 客户端链路和虚拟显示器
闭环满足发布验收要求。公网 TURN 项待环境具备后补测，不阻止本轮局域网交付结论，
但在完成公网证据前不能声称“跨公网生产验收完成”。
