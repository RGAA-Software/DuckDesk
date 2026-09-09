# 90：B 工作区临时配置文件只读诊断（2026-09-09）

> 后续用户已明确要求卸载 FSLogix；15:13 卸载执行成功，但安装器要求重启，未执行重启。
> 详见文末卸载记录。此前“恢复 FSLogix 服务”方案不再作为当前执行方向。
> 用户随后自行重启；15:42 已完成 B 的普通本地 profile 恢复及短时重连验证，详见文末。
> 15:51 A 也完成本地恢复，A/B 文件隔离复测通过；此前 A 待迁移状态已被文末记录取代。

本次仅诊断，不是修复完成报告。基线 `d7e47ab71`；时间为 90 本地时间（UTC+8）。
未注销会话、启动/停止系统服务、挂载磁盘、改注册表、删改用户目录或重置账号。

## 结论

B 的原配置由机器上已有的 **FSLogix Profile Container** 承载，并非普通本地目录。
10:51 的 FSLogix 日志证明原 VHDX 挂载及配置加载成功；11:23:32 FSLogix 服务崩溃；
11:38 B 登录时 Windows 改用 TEMP。当前服务仍停止、原容器未挂载。
这是本次 TEMP 的直接故障链证据，但 FSLogix 崩溃的内部根因尚未定位。

**不能把空的 `C:\Users\grdp_75b2ed95a7304d3` 和缺失的 NTUSER.DAT 解读为原数据已被删除。**
原 VHDX 文件仍存在；尚未挂载检查内部数据，不能承诺其完整性或恢复成功。
也不能根据故障模块 MSVCP140.dll 就断言运行库损坏，或将此问题与此前 DWM 崩溃视为同一个根因。

## 身份与证据

- B：`app-105-797a8ee1` / `node-106-7d3e91ec`，账号 `grdp_75b2ed95a7304d3`。
- SID：`S-1-5-21-651462275-30253513-3142702281-1092`。
- 当前 B 为断开的 Session 2（11:38 登录）；Administrator 为 console Session 1（11:23 登录）。
- `HKLM\SOFTWARE\FSLogix\Profiles`：`Enabled=1`，`VHDLocations=\\WIN-RASS8RC6V3H\ShareUsers`。
  本机 SMB 共享 ShareUsers 对应 `D:\ShareUsers`。

| 时间 | 实机证据 |
|---|---|
| 00:58:07 | B 原 VHDX 的创建时间；00:58:09 Windows 日志标记 Regular profile。 |
| 10:51:22 | FSLogix Profile 日志记录 VHDX attached/mounted、原用户目录重定向、LoadProfile successful，Session 8。 |
| 11:23:15 | FSLogix Operational 事件 8：frxsvc 加载成功。 |
| 11:23:32 | Application 事件 1000：frxsvc.exe 3.26.126.19110，MSVCP140.dll 14.51.36247.0，异常 0xC0000005，偏移 0x44FCB。System 7034 同时记录意外停止。 |
| 11:38:47 | User Profiles Service 1515 / 1511：备份原配置映射、使用临时配置；1511 明确警告注销会丢失临时配置更改。 |
| 11:38:48 | User Profile Service/Operational 事件 5、67：加载 TEMP 的 ntuser.dat / UsrClass.dat，类型 Temporary。 |
| 本次检查 | frxsvc：Stopped / Auto / ExitCode 1067；原 VHDX 的 Get-DiskImage Attached=False。 |

原容器精确位置：

```text
D:\ShareUsers\S-1-5-21-651462275-30253513-3142702281-1092_grdp_75b2ed95a7304d3\Profile_grdp_75b2ed95a7304d3.vhdx
```

文件大小 272,629,760 字节（260 MiB），最后修改时间 11:08:02。
此时间仅为文件元数据，不作为容器完整性或全部写入已落盘的证明。

当前注册表与目录：

- ProfileList 的 SID 项指向 `C:\Users\TEMP.WIN-RASS8RC6V3H.000`，State=18948。
- 同 SID 的 `.bak` 项指向原目录，State=32768；两者 RefCount 均未设置。
- HKU 下 B 的 Volatile Environment / USERPROFILE 确认当前 TEMP。
- 原目录使用 `-Force -ErrorAction Stop` 枚举为 0 项；精确读取 NTUSER.DAT 返回不存在，而非拒绝访问。
- TEMP 内 NTUSER.DAT 存在，786,432 字节；本轮只统计 Desktop/Documents/Downloads 的直接子项，未读取个人内容。
- `C:\Users\local_grdp_75b2ed95a7304d3` 属于同 SID。FSLogix 日志明确其为 Local temp directory，
  包含排除/重定向数据，不是可直接替换原 profile 的完整备份。

证据源：Application、System、Microsoft-Windows-User Profile Service/Operational、
Microsoft-FSLogix-Apps/Operational，以及 `C:\ProgramData\FSLogix\Logs\Profile\Profile-20260909.log`。
文档只保留相关结论，不提交原始日志、凭证或用户数据。

## 下一步恢复方案（待单独授权）

1. 先保全 B 的原 VHDX、当前 TEMP、local_ 目录及该 SID 的注册表映射。备份到新建受限目录，
   不覆盖任何已有备份；核验复制结果。TEMP 仍在使用，不能把活动文件复制宣称为完整一致备份；
   若无法保全必要内容，停止，不注销。原容器的离线检查优先对备份副本只读进行。
2. 评估并处理 frxsvc 崩溃。它是**机器级服务**，即使只验证 B 也不能承诺服务操作仅影响 B。
   不擅自升级/替换系统 DLL、禁用 FSLogix、修改全局清理策略或重启机器/RDS。
3. 仅在另行获准后注销精确核对 SID 的 B 会话，再重新授权连接 B，验证原容器挂载、Regular profile、
   原 SID/文件保留及正常断开重连。注销会结束 B 的程序及未保存状态；其他会话不注销。
4. 不直接删除 TEMP、重命名 `.bak`、重建账号或向空目录复制 Default hive。
   这些操作不能代替恢复既有 FSLogix 容器，并可能破坏恢复依据。
5. 恢复后再继续产品交互与持久性验收，每批含收尾不超过 10 分钟。
   产品应另列 profile 健康检查，不能仅以出帧/Ready 作为持久工作区验收通过。

微软资料用于解释容器、重定向和临时 profile 行为，不能替代本机故障证据：
[配置 Profile Container](https://learn.microsoft.com/en-us/fslogix/how-to-configure-profile-containers)、
[排查临时/本地配置文件](https://learn.microsoft.com/en-us/fslogix/troubleshooting-old-temp-local-profiles)。
其中带删除本地配置或机器级修改的示例不是本次执行指令。

## 用户授权卸载（2026-09-09 15:13）

用户明确要求“把 fslogix 删除掉”。本次范围为 90 的 FSLogix 产品卸载，不包括删除用户、
容器、TEMP 数据、注销会话或重启机器；也不代表已经完成容器到本地 profile 的迁移。

- 使用现有微软签名有效的 `FSLogixAppsSetup.exe` 3.26.126.19110，
  参数 `/uninstall /quiet /norestart /log <受限维护目录>\uninstall.log`。
  参数依据 [微软安装/卸载说明](https://learn.microsoft.com/en-us/fslogix/how-to-install-fslogix)。
- 卸载前在 `C:\ProgramData\GammaRay\maintenance\fslogix-uninstall-20260909-151324` 保存安装程序、
  FSLogix 注册表和 A/B 的 ProfileList 映射；目录 ACL 仅 SYSTEM 与 Administrators 完全控制。
  安装程序备份与源 SHA-256 一致。该目录不是用户数据的完整备份。
- 15:13:53 卸载器返回 0，Apps 与 Cloud Caching 两包成功；主日志明确
  `Apply complete, result: 0x0, restart: Required`、`restarting: No`。
  **退出码 0 不等于无需重启；目前是卸载成功、等待维护窗口重启。**
- 验证安装项数量 0、`frx*` 服务数量 0、Win32_SystemDriver 中 `frx*` 注册项数量 0，
  `C:\Program Files\FSLogix\Apps` 已不存在。未据此宣称所有已加载内核组件均已完成卸载。
- 官方卸载器保留 `HKLM\SOFTWARE\FSLogix` 配置；没有再手动清空，保留恢复和迁移依据。
- A/B 原 VHDX 及 `.metadata` 前后逐文件 SHA-256 相同：
  A `44276BB374287B5A4FA9B741F961CB81C25BFBB3AD9047C9EC0078B41C350D43`；
  B `24B00892F87202B94D4E6E54F6376590DEE5E510F6F2B77118F70D1C907B950A`；
  两个 metadata 均 `7872911F302A525968DFA798604365BE71C5C684E020BEF4E32CA29A15E08075`。
- Administrator Session 1、B Session 2 和用户手动游戏 PID 6376 原样保留。
  B 的 TEMP Loaded=True 仍未改变；卸载不等于 profile 修复。

后续：先保全 B 活动 TEMP 数据，再安排获准的重启及原容器到普通本地 profile 的迁移。
保留 A/B 账号 SID 和原容器；不再依赖 FSLogix 自动挂载，也不擅自建立空 profile 覆盖原数据。
由于 TEMP 的更改会在注销时丢失，不能先重启再尝试抢救 TEMP。
软件可通过保留的安装程序重新安装；本次未删除用户数据，未进行产品编译或测试。

## 用户重启后：B 恢复为普通本地配置（2026-09-09 15:42）

用户告知已经重启并要求继续。重启后仅 Administrator console Session 1（15:31 登录）；
FSLogix 服务和驱动查询均为空。A/B 原 VHDX 保留，两个原本地目录均为空。
B 原 TEMP 目录已不存在，当前 SID 的 TEMP 映射也已消失，仅原 SID `.bak` 留存。
**本次无法恢复重启前 TEMP 中的新更改；没有将原容器恢复描述成 TEMP 数据恢复。**

本轮只修改 B。A 的容器和空本地目录未迁移，暂不启动 A，避免生成另一份临时配置。

### 保全和迁移

- 新受限维护目录：`C:\ProgramData\GammaRay\maintenance\b-profile-local-20260909-1533`，
  SYSTEM / Administrators 完全控制，保存原容器副本 `Profile_B.vhdx` 和重启后的 `.bak` 注册表导出。
- 原容器和副本 SHA-256 均为 `24B00892F87202B94D4E6E54F6376590DEE5E510F6F2B77118F70D1C907B950A`。
  只挂载副本，明确 ReadOnly、NoDriveLetter，磁盘 IsReadOnly=True；NTFS 报告 Healthy。
- 容器中存在完整 Profile 目录和 NTUSER.DAT；迁移前 hive 哈希为
  `1F99985A102FF8A037923CDCE291F51D3932D672E08BC2B178AED1E2609DE76D`。
- 首次直接用卷 GUID 路径调用 robocopy 遇到错误 53 / 返回 16，目标仍为 0 项，映射未改；
  随后改用维护目录内临时卷挂载点，未向原 VHDX 写入。
- 确认 B 原 SID 一致、profile 未加载、目标目录仍为空，再复制到
  `C:\Users\grdp_75b2ed95a7304d3`。robocopy `/E /COPYALL /DCOPY:DAT /XJ /SL /B /R:0 /W:0` 返回 1，
  **1650 个普通文件逐一 SHA-256 相同**；18 个 reparse points 未纳入普通文件哈希验证，
  目录 junction 按 `/XJ` 排除，不跟随至其他路径。未使用 `/MIR`、删除或覆盖原用户数据。
- 原有 `local_grdp_75b2ed95a7304d3` 保留；未声称已完成全部应用凭据/缓存重定向兼容验收。
- 复制出的 NTUSER.DAT 成功临时加载/卸载验证，再把 B 的 `.bak` 改回原 SID 项，
  ProfileImagePath 保持原目录、State=0 / RefCount=0。其他 SID 映射未改。
  原根 ACL 仍仅 SYSTEM、Administrators、B 的原 SID 完全控制。
- finally 移除临时挂载路径、卸载副本；收尾验证原容器与副本均未挂载，哈希仍与迁移前相同。
  维护目录保留以供回退；不是重新安装 FSLogix 的操作。

复制及只读挂载参数参考：
[Mount-DiskImage](https://learn.microsoft.com/en-us/powershell/module/storage/mount-diskimage)、
[robocopy](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/robocopy)。

### 产品链路验证与边界

- 参数启动 `build_official/dist/px_client.exe`，使用既有 B 应用，不登录 Administrator。
- `GraceReconnect` / `inst-145-235f504b`，31 秒：FirstFrameCallback=True、NormalExit=True、
  CodecErrorCount=0、GraceReconnected=True、GraceExited=True、Passed=True。
  日志中的 `ERRCONNECT_CONNECT_CANCELLED` 对应主动结束连接；本轮不以此声称出现非预期断网。
- 15:42:18 Windows Operational 事件 67 明确标记 **Regular**，路径为原 B 目录；
  NTUSER.DAT / UsrClass.dat 从本地目录加载。WMI Loaded=True / Status=0，HKU USERPROFILE 与原目录一致。
  重连仍为 Session 2、同 SID；Client 与 Render 结束后会话保留。
- Console 只读查询确认该实例 `stopped`、error 为空、exit_code=1196556289（既有 no_clients 退出码）。
  未修改应用或节点的数据库启动配置。
- 收尾仅有 Session 1 桌面 Render；没有本次测试 App Render / FreeRDP 代理残留。
  短窗口未发现新增 profile 1511/1515 或 DWM/Render 1000；不代表旧 DWM 根因已定位。
- 未新增账号、重置密码、注销用户、重启机器，未编译或变更产品运行文件。

当前可确认 B 的普通本地 profile 和短时生命周期恢复；A 迁移、全部应用状态、图形目视、
外设/交互及跨重启持久性不在这轮通过范围，不能据此标记全量验收完成。

## A 本地配置恢复及双账号隔离复测（2026-09-09 15:51）

用户要求继续。A 账号 `grdp_ed5edf1d99a6468`、SID 尾号 1091、profile 未加载，
原本地目录仍为空；B 保持 Session 2，未注销或修改。

- 受限备份目录 `C:\ProgramData\GammaRay\maintenance\a-profile-local-20260909-1547`，
  保存 `Profile_A.vhdx` 和 `profile-before.reg`。原件与第一份备份 SHA-256 始终为
  `44276BB374287B5A4FA9B741F961CB81C25BFBB3AD9047C9EC0078B41C350D43`。
- A 的 Mount-DiskImage / Get-DiskImage 返回 0x80070005；VHDMP 事件 13 为 0xC0000022。
  两份 VHDX header 的序号 18/19，LogGuid 均非零；B 对照为零。
  这提示待重放日志，不能只凭此把所有 Access Denied 归结为 NTFS ACL 或证明内部损坏。
- 另建 `Profile_A_work.vhdx`，未修改原件或第一份备份。Mount-DiskImage 的读写尝试仍失败，
  最后 DiskPart 对**精确工作副本**执行 select vdisk / attach vdisk / detach vdisk 成功。
  未执行 clean、format、chkdsk、压缩、合并或针对原件的修复。
  这轮正常读写挂载后工作副本哈希变为
  `F6798F200FF64CC7CA76DF6A8AEF928DCC249133A52C2BAD75532B2C4683A367`，
  随后只读挂载提取成功；未对内部日志重放过程进行独立逐条验证。
- 使用 B 相同的受控复制流程，目标是原 A 本地目录，复制返回 1；
  **1591 个普通文件逐一哈希一致**，18 个 reparse points 不计入普通文件校验，junction 不跟随。
  复制出的 NTUSER.DAT 成功加载/卸载；A 已有原 SID 键，无需 `.bak` 重命名，
  仅将原映射的 State/RefCount 设为 0。根 ACL 保留 SYSTEM、Administrators、A 原 SID。
- 全部临时挂载已解除，工作副本卸载后哈希不变；原件、第一份备份、工作副本均保留供恢复追溯。
  原 local_ 目录也保留，不声称每个旧应用的凭据/缓存重定向都已验收。

实际验证：

- A `GraceReconnect` / `inst-147-5a4de07b`，28 秒，出帧、正常退出、宽限重连、
  Render 自然退出均通过，CodecErrorCount=0，最终 stopped。
- Windows 15:51:10 明确记录 A 为 Regular profile；A Session 3、B Session 2 的
  WMI 均 Loaded=True / Status=0。两个账号保留原 SID、原本地路径，不依赖 VHDX 挂载。
- 补做约 8 秒双向 ACL 实测：独立标准用户令牌下，自有测试文件可读，
  邻居文件不可读/不可写，`C:\Program Files\PixelsRender\rdp\proxy.key` 不可读，双方断言全部通过。
  只创建唯一命名的测试文件，finally 精确删除，收尾确认 0 个本轮测试文件残留。
  测试使用内存凭证，不提交密码、ticket 或用户数据。
- A/B 节点数据库仍对应 device `001190520`，端口分别 32014 / 32016，没有修改启动参数。
  收尾仅 Session 1 的桌面 Render PID 10748，无测试 App Render 或 FreeRDP 代理残留；
  Administrator Session 1、B Session 2、A Session 3 均保留。
- 短观察窗口未见新增 profile 1511/1515、DWM/Render 1000；不据此声称历史 DWM 问题根治。

每批实际测试远低于 10 分钟。此轮未编译、发布、注销或重启。
A/B 本地 profile 恢复和文件隔离已验证；全部应用兼容、目视画质、外设与跨重启持久性仍单列。

VHDX 日志行为参考 [微软 Log Replay 规范](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-vhdx/0d588e33-23a6-4c71-b27f-87d97ac3e914)；
规范要求非空日志在普通 I/O 前重放，不代表本次已经定位 Windows 挂载入口差异的全部原因。
