# 多路调度容量调整（2026-10-12）

承接80路正式授权，用户授权调整节点和应用部署容量。本批通过现有Console管理API更新，未改代码、许可合同或应用启动参数。

| 配置 | 原值 | 当前值 | 核对结果 |
|---|---:|---:|---|
| 90 Cloud Node实例上限 | 4 | 64 | revision2，fresh/ready，generation464、epoch72 |
| XR Cloud Node实例上限 | 1 | 64 | revision2，fresh/ready，generation23、epoch72 |
| 90 WebView部署 | 5 | 20 | revision2，节点重新上报ready，调度可用 |
| 90医学GameHook部署 | 12 | 20 | revision4，节点重新上报ready，调度可用 |
| 90 2dadventure GameHook部署 | 1 | 20 | revision3，节点重新上报ready，调度可用 |
| XR测试WebView部署 | 1 | 20 | revision9，原应用/部署禁用状态保持 |
| 90 RDP工作区部署 | 1 | 1 | 单工作区规则保持，revision1，ready |

节点配置目前支持1–64，选择64可容纳至少三类各20个应用实例的总数。
正式许可证总80路保持；节点/部署容量是调度准入上限，不证明机器能同时编码这些流或达到相应帧率。

三个已启用GameHook/WebView的实际只读调度预览均为eligible=true、node_slots=64、deployment_slots=20，拒绝原因为空。
XR测试应用仍disabled，预览明确拒绝；没有为了增加容量启用已清理测试应用。
配置PATCH逐项携带原revision，保存后确认新revision和配置，再独立GET/预览核对；Console ready204。
disabled/draining、GPU绑定、应用配置、本地TOML、运行二进制、既有授权与Windows工作区未改。
未重启服务，未拉起20个实例或执行媒体/混传测试。

**RDP每类20路尚未完成。** 当前部署代表一个既有Windows工作区，`DeploymentConfiguration::validate`要求RDP capacity=1，
该工作区按既有用户决定只允许一个活动前端。20路RDP需要20个独立工作区的部署/管理能力，不能通过把同工作区容量改成20实现。
本批保留此规则，RDP调度预览为deployment_slots=1，不把80路授权当成已经提供20个RDP工作区。

原节点和部署配置已保存在本机`.env/multistream-license-20261012/capacity-before.json`，
无凭据配置基线、逐项操作和节点上报/预览见[证据索引](20261012_multistream_capacity.json)。
首次读取应用名称错误使用顶层name而非spec.name，在写入任何配置前退出；修正读取后执行，未将此脚本错误计为部署失败。

下一步用已具备容量的GameHook/WebView分批核对多个Endpoint实际Relay分配与正常负载，不重复原接入验收。
卡帧调查保持阶段性暂缓；Android、NIC及已停止的饱和混传不纳入本批。
