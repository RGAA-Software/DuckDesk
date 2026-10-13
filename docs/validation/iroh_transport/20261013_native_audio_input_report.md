# Native 应用声音与输入闭环

2026-10-13，接续小规模视频维护验收。本轮始终最多一个本机或一个Company Client；不涉及麦克风、语音通话、Android、录制功能验收或暂停的卡帧专项。启动阶段波动仍按用户决定单独记录，不作为稳态强制条件。

## 找到并修复的缺陷

旧 Client 的 DesktopShell 只初始化 SDL VIDEO/EVENTS，`ClientAudioOutput`却直接打开 SDL 音频播放流。没有任何 `SDL_INIT_AUDIO` 初始化，SDL底层明确拒绝未初始化音频驱动的设备打开；播放接口失败又被调用方忽略，原日志没有显示原因。

现在 Native 会话在启动SDK之前，在UI线程显式调用音频输出的`Start()`，检查初始化结果；模块以RAII持有自己的一份SDL音频子系统引用，销毁时释放，先关闭播放流。静音时关闭流、保留子系统，恢复播放可重新打开；不释放其他模块的引用。初始化、设备打开、首个PCM提交和失败原因均记入日志，相同设备错误不会每帧重复刷屏。RDP原生音频流程保持原边界。

## 实际验证

在90运行loopback页面，远端点击三个区域，分别产生523.25Hz、997Hz和停止。页面click计数1→2→3和对应Tone状态均由接收画面确认；控制事件经现有可靠流，声音经iroh数据报/Opus进入Client并由WASAPI输出。无需外网网页、代理或额外授权机制。

| 路径 | 输出实测峰值 | 相对停止后的目标频率幅度 |
|---|---|---|
| 强制Relay，实际90 | 522.991Hz、997.098Hz | 47.16倍、69.07倍 |
| 自动选择，实际UDP直连39.71.45.66:4613 | 523.326Hz、997.098Hz | 473.47倍、41.53倍 |

接收端测量的是Windows默认播放设备的loopback FFT，每阶段约3秒、48kHz双声道。背景声音仍存在，不能用总RMS判断测试音；两种目标频率跟随远端输入切换，停止后幅度明显下降。只保留频率/RMS汇总，没有采集麦克风或保留声音录音。直连是实际公网UDP端点，不当新增真实NAT打洞证明。

旧C39 Client试播相同页面时没有对应音调增强、没有属于该Client的Windows音频会话。修复后B7 Client日志确认`initialize driver=wasapi`、`open sample_rate=48000 channels=2`、`first_write bytes=3840`，Windows音频会话active且未静音。详见[WebView分析](20261013_audio_input_analysis.json)。

Company只增量更新Client exe，旧FE8→B7，依赖资源不重传。更新前确认无Client占用，保留原exe/两份manifest，核对压缩包/展开文件/安装文件SHA，更新两份清单中的该exe摘要并验证全部41项。新GUI Client按脚本启动6秒后立即执行PID验证的真实鼠标点击，日志记录down/up sent=true，随后打开48kHz双声道设备并提交3840字节PCM；30秒保持阶段结束后正常清理，脚本exit0。Company未做FFT或主观听音，不把设备提交日志等同于该机物理扬声器听音验收。见[交付](20261013_audio_input_company_delivery.json)。

GameHook另取独立owned测试部署，4000kbps，单本机Client。有效408实际走90 Relay、持续出图，音频播放流于11:11:52打开并收到PCM，Windows音频会话active；源端Opus约46kbps、音频数据报约89kbps。游戏没有注入人工测试音，不声称游戏声音频谱与WebView音调同等验收。见[游戏分析](20261013_audio_game_analysis.json)。

## 回归、交付与清理

4项新音频测试通过：重复Start/Stop及恢复播放、保留外部SDL引用、销毁后的排队弱引用回调、初始化失败后重试且无引用泄漏。实际使用SDL dummy驱动，不依赖本机扬声器。C++所有权、可读命名、Google格式和diff检查通过。三产品均为Release增量编译、显式18jobs；所有改变的运行文件已发布到对应dist并由发布脚本核对哈希，没有运行全量release构建或生成Setup。

| Client运行产物 | SHA-256 |
|---|---|
| client build-tree/dist，Company安装 | B7DEB80B2B8CA270AA80559BB81FFB12165565041452B8A259F5F368853C0272 |
| remote build-tree/dist | 43978D5ACC1427C25D01BE71F5022FE4C775BFC6E11167F3DAF0705C09A4531B |
| cloud_node build-tree/dist | DBACE47DAE8BA6E29C7236AB5E4B133C75DAF067992E2F7559582D80DF13A69E |

本轮没有更新90/XR节点服务或Render，也没有把remote/cloud_node的Client称为已安装到远端。Company更新是已授权开发增量，保留包身份，不能当新Setup安装验收。

WebView阶段6 owned实例/5资源，Game阶段407、408共2实例/2资源，全部stopped/closed，guest均注销，2测试应用/2部署禁用；本机/Company Client均0。动画页首个20分钟任务正常结束，第二个任务确认cancelled；48777监听0，90仅默认Render PID13524，Relay恢复Windows1/BJ0。原应用spec、RDP10容量与80路授权保持。启动信封已删除，受限private记录保留；未清扫非owned程序或Windows账号/会话。

源端日志先gzip后下载：Web源1025567bytes/SHA65863019…、Company342542bytes/SHAA21940AA…、Game源617270bytes/SHAA9A4EC22…，下载SHA逐项相等；本机日志也压缩保存。原始日志在各自`.cache`和D:\112233开发目录，提交的JSON只保留必要摘要及异常。

用户随后要求清理无用测试编译产物：已删除早于本轮的94个测试exe/pdb/ilk、3个Rust incremental目录和本轮临时旧Client副本，总98条、12682376770bytes（11.811GiB）。此前已上传确认的31.86MB临时压缩包亦已删除，不计入该总数。产品dist、当前音频测试exe、源码、backup、日志证据和.env保留；其余磁盘空间变化不归功于本批。详见[清理清单](20261013_unused_test_build_cleanup.json)。

## 无效样本和边界

- 最初PostMessage鼠标模拟只更新位置，SDL过滤了按键；改用PID验证的原生鼠标事件后，远端状态顺序正确，随后恢复光标和前台窗口。没有修改产品输入代码来迎合夹具。
- Company首次exe上传因目标目录不存在终态failed、未传入字节；确认原操作失败后创建目录，以只含该exe的ZIP重传并校验。下一次406验证与更新结果均成功。
- WebView404启动时本地测试页20分钟期限已经到达，源日志page load failed -102。该实例已stopped、没有资源；后续错误启动脚本读取了不存在的404信封，返回失败且未创建Client。已恢复本地页、给准备步骤增加exit_code守卫，405/406有效样本分别保存。405的外部点击落在结束弹窗，被local UI捕获，不作音频通过证据；406把点击编入脚本，实际通过。
- Game407没有完成peer接入，Client hello enqueue失败、资源随后关闭；未将其算音频通过，也没有断言网络根因。408准备完成立即启动、实际connected及声音输出确认。首次日志与终态保留。
- 一次本地临时文件创建报磁盘不足，空脚本未执行；删除已确认上传的本批ZIP后继续。清理汇总的Measure-Object未统计字典键，改为按已记录的每条bytes求和，不重跑删除或覆盖原清单。

本批收尾的是声音播放与输入的传输消费路径，不证明长期零卡顿、最大并发或NAT覆盖；上一批稳态短间隔记录保留，Android和暂停专项不启动。

复现页：[native_audio_input_page.html](../../../scripts/server_validation/native_audio_input_page.html)。播放设备探针：[probe_native_audio_output.py](../../../scripts/server_validation/probe_native_audio_output.py)，在Windows仓库环境使用numpy及缓存目录中的`pyaudiowpatch==0.2.12.9`，标签输出只存统计。测试入口：`scripts_build/build_cpp_client_audio_tests.bat client 18`。
