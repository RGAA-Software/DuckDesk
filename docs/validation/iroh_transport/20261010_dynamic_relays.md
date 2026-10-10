# Relay 候选动态更新与维护排空

2026-10-10；本批已经通过完整 Setup 安装到 90，开发工作区仍为 `D:\112233`。

## 实际结果

- Console 将 90 Relay 设为维护后，Client 在 5.062 秒收到单 BJ 候选；Render 在 15.252 秒内上报 BJ home 地址。原 EndpointId、游戏实例及资源会话保持，观察窗口内没有重新准入，持续收到视频。这个时间是配置传播时间，不是画面停顿时间。
- 维护期间保留健康的旧 Relay 连接。原视频仍可使用 90 的既有转发连接；home 地址变为 BJ 不等于旧 QUIC 已迁移。此处实现的是不打断已有连接，并将后续分配转向可用 Relay。
- 主动结束该测试实例后，新启动的游戏只有 BJ 候选，通过 BJ 连接并解码。取消维护后，仍运行的 Client 自动恢复两个候选。两个 Relay 服务全程未停止。
- 本轮接收统计大多约 60 FPS；维护传播阶段有两次大于 100ms 的帧交付间隔，最大 126.4ms。没有据此宣称零抖动或显示呈现 FPS 达标。新 BJ 连接两个接收统计窗口最大间隔 32.1/34.5ms，仅作为该窗口的功能证据。
- 首次测试立即关闭并重开同一实例的控制会话，遇到 `409 connection_retiring`，整体结果保留为失败。随后测试明确停止自有旧实例、再创建新实例，完成新分配验证；没有改为绕过占用保护，也未声称立即重开问题已修复。

实机报告：`20261010_dynamic_relay_real_game_retry.json`；首次失败：`20261010_dynamic_relay_real_game.json`；Client 记录：`20261010_dynamic_relay_client.log`。

## 实现边界

- Console 的已认证节点 Report 响应携带最新候选；Service 缓存后通过 Render 心跳下发。运行中更新不重启桌面 Render。Render 将当前候选和 home 地址继续上报。
- Client 每五秒通过原资源会话凭据刷新端点信息；重连也复用此路径。不新增短期 token、票据或第二套身份系统。
- 项目 Rust 封装通过公开 `insert_relay/remove_relay` 更新私有候选。更新和健康重试串行，先加入替代节点再撤下旧节点，防止健康重试重新插入已撤下的候选。被 FFI 超时取消的等待者不会中断已接受的整组更新。
- 无初始候选时仍保留空的私有 Relay transport，支持之后添加；不启用公共默认 Relay。空数组明确表示撤下候选。初次启动的 relay-only 配置仍要求非空候选。
- 只热更新 Relay URL/QAD 候选。CA 信任根、监听绑定和 direct/relay-only 策略仍属于端点创建配置，变更后需要重建相应运行时。
- 参考固定 iroh 1.3.0 revision `0072d7d84b233f9e7185eb676f049beaf557ac03` 的公开 API 与 `test_endpoint_online_add_relay`。本批未追加上游补丁；现有隔离队列补丁及 84 文件哈希复核通过。

## 验证

- 两台及十台本地 TLS/QAD Relay：从无候选开始动态添加；已有可靠流/数据报连接保持时替换候选；新 peer 使用替代 Relay；重复更新不改变端点身份。两轮各两项测试通过，未跳过。十实例是单机功能检查，不是十台物理机器或容量/均衡结果。
- SDK 持续视频下停止选中 Relay，另一台恢复地址和重新准入，通过；最终代码恢复耗时 8.688 秒，故障 Relay 保持离线。报告 `20261010_dynamic_relay_business_recovery.json`。
- Rust 五项测试通过，含健康任务退出、空闲保活、无效候选不生效及等待者取消；C++ transport/frontend CTest 通过。
- 本机 Client、Cloud Node 内 Client、Render 和 Service 开发产物已经发布至各自 `build_official/<product>/dist` 并核对 SHA-256。完整身份保存在 `20261010_dynamic_relay_installation.json`。

## 当前安装及接续

- Cloud Node 3.3.97 候选 `iroh-dynamic-relays-20261010`：314 文件核验，Setup `1D6E823B90A292028F61D047C13E290B2E41866B40F1EC21E5FC3809F9679D33`，manifest `65287E3C596D97C8333304586061F75DCF75F6E9EF788201A2428F2FECC0D91E`，Render `B1B613903E98329D10222A4EF97DC2F248F0A93DB41704AED40303C4331BF1B4`。
- Server 1.0.55：35 文件核验，Setup `620806E4CE183075AD7A6758D5C5F841AC2381875807593D2AE48E2412BC9891`，manifest `4C0BDB5E3E4E40D14E1220CCF24190BBC8469C6F5521BB03AEE4780C5D396B13`。仅 Console 两个 EXE 相对上一包有变化。
- 测试实例、资源会话、Client、本地探针已清理，90 三服务 Running，仅桌面 Render 54096；90/BJ 均 fresh、维护标志已取消，generation 81/7，Console epoch 66。三份持久配置 SHA 未变，详见 `20261010_dynamic_relay_cleanup.json`。
- 不重复安装旧 fast-recovery 包或重做 BJ 登记。本批完成动态候选与维护路径；正式多 Relay 容量/均衡及长期负载仍有待办，随后按主计划推进 M3 Windows/RDP 附属功能。Android 最后。
- D 盘空间整理只移动三份自有旧候选到 C 盘并核验文件，记录 `20261010_dynamic_relays_space_archive*.json`；未删除第三方引用或现行安装产物。
