# GameHook 双向文本剪贴板

用户最新决定覆盖此前“不需要游戏剪贴板”的范围调整。

游戏 Render 原先把文本转给仅连接桌面 Render 的 user-proxy，因而丢弃消息。现在 GameHook 使用独立的 ApplicationClipboard，复用既有 Windows 剪贴板平台，通过现有授权连接收发文本。写入成功后更新观察值以抑制回声；读取失败保留待发送变化。文本上限 1 MiB，日志仅记录方向、字节数和结果，不记录正文。普通桌面、WebView、RDP 路径不变。

验证：三个剪贴板单元用例及 Render 生命周期测试通过。开发 Render 已同步 dist，SHA-256 一致，详见 `20261010_game_clipboard_build.json` 和 `20261010_game_clipboard_tests.log`。

首次完整候选 Setup 在 90 返回 1603。64 位 PowerShell 检查显示 Parsec 驱动正常，32 位 PowerShell 单独运行即返回 -327680，提示找不到 SOFTWARE\Microsoft\PowerShell 注册表项。安装器原先从 32 位进程调用默认 PowerShell，把检查失败误判为驱动失败。修复安装/卸载检查，显式调用 System32 的 64 位 PowerShell并禁用文件系统重定向。首个候选已按哈希清单归档，节点服务已恢复。

修正后的完整候选为 `game-clipboard-vdd-20261010`，运行文件清单不变，仅 Setup 检查修正。90 安装退出码 0，314 个运行文件均通过校验，px_service 为 Running。

实机游戏实例 `d6dc6ec3-9455-4422-9d91-749551d66521` 完成中英文文本往返，耗时 4.722 秒。GameHook Render 日志显示客户端 37 bytes 写入 applied、远端 38 bytes 回传；测试辅助程序确认两端匹配并恢复原剪贴板。测试 Client/实例关闭，远端仅保留桌面 Render，Console/Relay 持续运行。结果见 `20261010_game_clipboard_live.json`，远端证据见 `20261010_game_clipboard_remote.log`。本批范围是文本，不新增文件或图片剪贴板。
