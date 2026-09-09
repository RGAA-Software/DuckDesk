# Game 最终文字提交后端

状态：2026-09-10 本机实现、增量编译及自动化回归完成，远端 90 离线，实机验收暂停。
构建、发布哈希及测试记录见 `application_text_input_progress_20260909.md`。本文不是游戏兼容性通过声明。

## 路径及授权

`GameTextBackend` 将统一文字服务适配为 Render → 指定 PID 的 Hook IPC → 该进程聚焦窗口的 `WM_CHAR`。
不使用系统剪贴板或全局 `SendInput`，不按标题、同名进程、全桌面窗口扫描寻找输入目标。

- `AppManagerWinImpl::AcquireTextTarget` 使用既有 `AcquireHookTarget`，同时要求本次私有 Job 成员及规范化完整路径匹配。
  保留进程句柄贯穿请求，根窗口取自本实例已选定的采集窗口，并重新验证窗口 PID。
- IPC 实际客户端身份必须由 `FindLoopbackTcpClientPid` 的 Windows TCP owner 表验证，不能信任 `?pid=` 自报值。
  WS 集成必须按已认证 PID 定向发送和关联回复；不得广播新文本命令。
- 前端只使用不透明的目标代次。内部代次组合进程 PID、创建时间、根窗口和 Hook 内代次，防止进程编号复用。
- Hook 用 `GetGUIThreadInfo` 取得根窗口所属线程的真实聚焦窗口，并要求属于本进程且为根窗口或其子窗口。
  不使用被伪装的 `GetFocus`、`GetForegroundWindow` 作为授权依据。未知焦点失败关闭。
- Hook 为目标窗口创建 RAII property 注册；窗口销毁时 Windows 清除 property，复用 HWND 不会继承旧代次。
  提交前及每个 UTF-16 单元投递前再次检查 PID、真实焦点、property 代次。

## 请求及结果

`px_capture/capture_text_input.h` 定义显式小端、有 magic/version 的 Query、Submit、Release 命令和 Reply。
严格验证消息总长度、类型及版本；正文最多 16 KiB UTF-8，拒绝非法 UTF-8、NUL、DEL 及不允许的 C0 控制字符。
保留 tab、CR、LF、空格和 Unicode 代理对，不额外发送 Enter，不进行 trim 或规范化。

Render 最多保留 32 个进行中的 IPC 请求，每个请求 3 秒回执期限。停止时取消定时器，释放固定进程句柄，
向未完成调用报告不确定结果。错误 PID、已终止进程的回复不被接收。

提交请求的 lease/输入代次授权谓词一直传到最后一个可信的 IPC WebSocket 写入队列，不在入队时缓存布尔结果。
每个进行中的请求拥有 `GameTextWritePermit`；排队写入只持弱引用。停止、超时、回复及所有者销毁都会使旧许可失效。
实际写入前再次检查授权，授权回调中发生取消也会失败关闭。Send、授权和完成回调均不在后端互斥锁中执行。

`LogicalSessionRegistry` 同时验证票据授予该物理绑定的 input 权限及逻辑会话当前的 input 权限。
权限更新先修改权威 registry，再通知 WS/RTC；撤权、恢复和控制租约重新取得都会生成新的输入能力代次。
Qt/Web 目标中的租约标识使用该代次，旧排队提交在恢复权限后也不会重新变得有效。
输入能力代次独立于文件传输使用的控制租约，单独撤销输入不会使在途文件操作的租约失效。

`submitted` 仅表示窗口消息投递接受，不能证明游戏引擎消费或页面业务提交。Windows 消息队列达到配额、
目标在投递中变化或部分投递后失败，都必须保留“不确定”的语义，禁止自动重发全文。
16 KiB 是输入验证上限，不是 Windows 消息队列容量保证。

Release 在同一 IPC 接收执行队列中释放该连接记录的全部按键和鼠标按钮，再重置 Hook 输入状态，然后返回确认。
失效窗口不会被替代窗口接管；不能确认释放时返回失败，由上层继续保持编辑屏障。

## 自动提示的实现差异

本版没有照搬 streamer 的 `ImmAssociateContext/Ex` 全局函数 Hook。
采用成对 `ImmGetContext/ImmReleaseContext` RAII 查询，在 Query 时检测聚焦窗口的关联状态变化；
普通游戏窗口初次关联状态仍为 Unknown，只有观察到关联变化才更新提示，原生 EDIT/RichEdit 可提供 Editable 提示。
上层需定期 Query 才能观察变化；非常短的关联变化可能被轮询遗漏，永久手动入口不受影响。

该信号仅用于提示，不是控制权限或窗口授权，也不声明能够识别 UE/Unity 内部每一个文本控件。
参考仓库仍为只读 `D:/dolit/streamer`，参考版本和源位置见统一设计文档第 7.3 节。

## 验证目标

新增 `test_game_text_input`：自有隐藏 Win32 EDIT 窗口验证中文、emoji、选区替换、tab/CR/LF 消息保持、无额外 Enter，
以及错误 PID/根窗口、失焦、窗口销毁、property 丢失、旧代次、非法编码和重复重置。
同时验证 IPC 编解码截断/版本/长度，以及真实本机 TCP 连接的内核 PID 与端口匹配。

这些是最小 Win32 消息行为和负向测试，不能替代用户目标游戏、真实本机输入法选词、Qt/Web 面板的端到端验收。
不创建 Windows 用户，不注销远端会话，不接管或关闭用户自行启动的游戏。
