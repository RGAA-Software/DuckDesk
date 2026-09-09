# WebView 中文输入实机验收（2026-09-10）

## 范围与结果

本机 Console + Windows Client / 桌面 Chromium，远端节点 `10.0.0.90`。
本轮完成 WebView 核心中文输入验收；用户确认 90 不能运行 game，因此没有启动游戏进行测试。
实现基线为 `8048a83e4`，本轮修复如下。未增加传输连接，未使用剪贴板中转中文。

| 项目 | Qt Windows | 桌面 Web（Chromium） |
|---|---|---|
| 本机系统拼音 `nihao` 选词为 `你好`，点击发送，远端实际出现中文 | 通过 | 通过 |
| 中文、英文、emoji 整段提交，检查远端画面 | 通过 | 通过 |
| 重新聚焦远端输入框、Ctrl+A、面板发送替换选区 | 通过 | 通过 |
| 在当前光标处追加中文 | 通过 | 未单独验收 |
| 关闭面板后恢复普通英文按键 | 通过，远端追加 `Z` | 未单独验收 |
| Qt 预编辑不与提示文字重叠，组合期间不能发送 | 通过 | 不适用 |

这里的真实拼音测试通过 Windows 键盘事件调用本机输入法，不是给输入框直接赋值或模拟 DOM composition。
同时另有直接设置面板文字的 Unicode 传输测试；两类证据分开计算。
`submitted` 回执本身不是结果证明，上表成功提交还检查了远端视频画面中的实际文字。

## 修复

1. CEF 提交原来使用默认 `CefRange{}`，其值是有效的 `[0,0]`，实机出现提交到文本开头。
   改用 `CefRange::InvalidRange()`；重新部署后，Qt 的选区替换及追加、Web 的选区替换均通过。
   不把 CEF 接口接受等同于任意网页已正确处理文本。
2. Qt 空编辑框的 placeholder 与 IME 预编辑文字叠加。组合开始时暂存并隐藏 placeholder，
   组合结束/取消时恢复；增加取消组合回归测试，并用系统拼音复测显示效果。

Qt 继续复用现有认证 WS/WSS；Web 实测只有既有 media/ft/input/ping DataChannel，
可靠 `media_data_channel` 为 ordered=true、maxRetransmits=null、maxPacketLifeTime=null。
没有为中文输入新增连接或 DataChannel。

## 测试记录与边界

- Qt 修复前实例 `inst-109-37683881` 暴露上述两处问题；修复后 `inst-113-7f2307f6` 验证
  `选区替换成功追加你好Z`，含真实 IME 和普通英文恢复。最长批次总计约 461 秒，小于 10 分钟。
- Web `inst-129-2e9e6ad9` 真实系统 IME 提交 `你好` 后远端可见。
  该批后续选区阶段因测试脚本点击位置不准确超时，不记为整批通过。
- 修正脚本点击坐标后，`inst-132-fc58db02` 中文/emoji 与选区替换均成功，浏览器 exit=0。
  最终远端输入框显示 `选区替换成功`。
- 早期错误点击曾得到 target_changed，草稿保留且未自动重发；这不等同完整断线/失权实机矩阵。
- 百度首页有动态 DOM 重建，初次提交后的输入框需重新确认焦点。本轮选区测试明确重新聚焦后执行；
  不宣称任意动态页面、富文本编辑器、多行文本或密码字段均已验收。
- 两次 Qt 关闭后 35 秒内未观察到实例自然 Stopped；随后通过本次实例 Stop API 清理成功，重复 Stop 成功。
  因此本轮不把“自然超时退出”记为通过，也没有为此改变产品宽限期或注销远端用户。
- 所有测试实例通过自身实例 API 收尾；最终节点应用模式 Render 数量为 0，`px_service` 仍为 Running。
  未注销账号、重启机器、终止无关游戏或更改应用配置。
- 数据库应用仍为 `rtc-accept-webview-90`，entry_url=`https://www.baidu.com/`，listen_port=32010，fps=30。

本地临时证据（不入库，可能按缓存策略清理）：`.cache/webview-selection-and-append.png`、
`.cache/webview-ime-preedit-fixed.png`、`.cache/webview-qt-ime-fixed-result.png`、`.cache/web-text-replaced.png`。
临时启动脚本不输出连接票据或凭证；测试不要求用户点击连接。

## 构建、回归与发布

使用 `scripts_build/build_cpp_tests.bat px_client px_render test_client_application_text_input check_cpp_ownership`
增量构建通过，未运行 release-only 全量构建。Qt 输入面板现有 17 项测试通过。
收尾重跑以下 6 组 CTest 全部通过，共 7.50 秒，每项超时 45 秒：
`client_application_text_input`、`application_text_protocol`、`application_text_service`、
`rtc_payload_authorization`、`logical_session_registry`、`ws_ipc_client_lifecycle`。
所有权检查、修改文件格式检查及 `git diff --check` 通过。
此前基线的 Web 49 项单测结果仍保留在进度文档，本轮没有修改 Web 产品源码。

| 运行文件 | 最新 SHA-256 |
|---|---|
| `px_client.exe` | `ED9DC507EA8B2D0B58B500E2D3932F1D10221B64BD3B053A0C5BFAF80C1BD54C` |
| `px_render.exe` | `3661D2C80B7F2B333B7F97C306A87CC01D0ED02486494A943B130BAF8C8172FA` |

Client/Render 构建树与 `build_official/dist` 副本一致；90 安装目录 Render 与该哈希一致。
基线 Web 的 5 个资源及配套 Hook/RTC 运行文件也已同步并校验，资源哈希见进度文档。
远端部署保留替换前文件备份，没有删除用户数据；本机验收从 `build_official/dist` 启动。

## 使用方法

点击远端 WebView 的输入框，打开客户端“输入文字”面板，用本机中文输入法完成选词，
然后点击“发送文字”。需要替换内容时先在远端选中目标文本，再打开面板提交。
候选框显示在本机面板；关闭面板后恢复普通远端操作。

未验收范围：game、移动浏览器、其他桌面浏览器、多行/富文本页面、真实断网与撤权全过程。
这些不影响本轮已确认的 Qt/桌面 Chromium → WebView 核心输入流程，但不得据此宣称全平台或全网页兼容。
