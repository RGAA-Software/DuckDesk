# 最新网络改动的 Windows 完整安装包验收

2026-10-13，用户授权制作完整正式优化安装包，并在本机和90进行少量Setup覆盖验证。源码基线为`1882ac3016576e3359506da235bfa2eee0715559`，包含设置持久化、实际Relay名称及Console带宽展示；本批仅修改打包编排并保存交付记录。四个完整候选已生成，90 Server→Cloud Node覆盖成功，本机Client首次安装及随后同版本覆盖成功；全部实际载荷SHA核对通过。

候选标识为`network-publication-20261013-1882ac301`。复用正式优化Release树，显式18jobs；没有升版本、覆盖既有发行安装器、发布外部更新源或把正式载荷复制到development dist。现有六个开发Client/Panel文件的SHA保持不变，用户仍可从原开发dist运行。完整路径和SHA见[交付身份](20261013_windows_publication_delivery.json)。

## 安装包

| 产品 | 版本 | Setup字节数 | 已验证载荷 | 本批实际安装 |
| --- | --- | --- | --- | --- |
| Server | 1.0.56 | 13,747,711 | 35 | 90覆盖 |
| Cloud Node | 3.3.97 | 278,574,523 | 314 | 90覆盖 |
| Client | 3.3.81 | 47,979,985 | 41 | 本机首次安装、同版本覆盖 |
| Remote | 3.3.79 | 92,201,153 | 76 | 完整候选，未独立安装 |

下载或打开本机安装包：

- [Server Setup](D:/GoCloud/GammaRayPremium/build_official/private_server/candidates/network-publication-20261013-1882ac301/PixelsServer_1.0.56_Setup.exe)
- [Cloud Node Setup](D:/GoCloud/GammaRayPremium/build_official/cloud_node/official/candidates/network-publication-20261013-1882ac301/installer/3.3.97/PixelsCloudNode_official_3.3.97_Setup.exe)
- [Client Setup](D:/GoCloud/GammaRayPremium/build_official/client/official/candidates/network-publication-20261013-1882ac301/installer/3.3.81/PixelsClient_official_3.3.81_Setup.exe)
- [Remote Setup](D:/GoCloud/GammaRayPremium/build_official/remote/official/candidates/network-publication-20261013-1882ac301/installer/3.3.79/PixelsRemote_official_3.3.79_Setup.exe)

| 安装包 | SHA-256 |
| --- | --- |
| Server | `77356b06912f51620fd739ec4ca21abc85c30ca5fa8c90cf60d1b382f8274c7e` |
| Cloud Node | `969e0b8a1015066cb1510f5615ab90ac3501a222820cc8ab2a18d0edfe77e90d` |
| Client | `eb65dafafe6bc9fec5a3b5dd4ee3edb573129d2942f61f9ca080cef8ecd2d8ff` |
| Remote | `32f4e0c8631e7782db08c7f1b17b882e89fd8011af28a504534c3af22bd56d63` |

这是优化Release完整安装候选，Windows按既定策略不做Authenticode签名或时间戳。没有提交GitHub发行版或TUF更新源，也没有创建额外授权材料。Remote独立安装需要另一台未安装其他桌面产品的机器；现有Client/Cloud Node/Remote互斥规则保留，没有为扩大验收而卸载已有产品。

## 完整覆盖与配置保持

90安装文件、脚本和临时页面均在`D:\112233\windows-publication-20261013`，没有向Desktop复制开发文件。Setup和载荷在安装前后核验，不用手工替换程序代替完整安装。本机和90四次Setup均exit0：90 Server、90 Cloud Node、本机Client首次安装、本机Client同版本覆盖。

- 90 Server最终Console SHA`94C31068A9D5C214F3E758460FDA9C8283E218667F9CBA8B0508BA00A2CDE41F`，Server清单SHA`11edb394…`。19个原配置文件，包括私有环境、CA/TLS、workspace密钥、PG连接、iroh配置、许可证及Backup计划，前后SHA完全相同。Console数据库和Backup目标schema都为42，Backup配置格式版本为2，两者单独记录。
- 90 Node最终Client SHA`9186FC68…`、Panel`74840D79…`；Render`FEE6ED1F…`和Service`4CE0F74B…`沿用原正式优化身份。314项载荷及构建树/发行dist对照通过；两份节点TOML原值保持。Console、Relay、Backup、Service均Running，实际Panel PID57984位于Administrator session2，默认Render PID56256。
- 原Windows管理员会话2和RDP会话3–17仍存在，账号名、session ID及原登录时间保留，没有logoff或删除profile。许可80路、RDP容量10保持；原11应用spec和11部署的类型/容量/GPU/禁用配置逐项相等。复用的owned夹具有正常编辑revision递增，不声称其revision不变。
- 本机原来没有本次产品的正式Client安装身份，因此先首次安装，再用同一新Setup完整覆盖。两次安装均核对41项载荷；实际Client SHA`E849DAD0…`、Panel`0BBF191A…`。本机没有进行另一旧正式版本到新版本的独立跨版本升级，不把同版本覆盖称为该项通过。

首次安装前，既有同应用设置数据库7个文件的SHA已保存；首次安装后完全相同。真实Client载入原统计关闭状态，开启统计并保存、关闭进程后再次保存7文件快照；完整覆盖后7文件再次相等。新Client进程自动恢复统计开启，画面和真实Relay名称/URL/RTT继续显示。设置保存在既有Public数据目录，包内替换不会重建该数据库。

## 安装后的有界连接

复用上一轮自有WebView夹具，使用90本地动画页与2000kbps视频配置，始终只同时运行一个本机Client；没有依赖外网站点或使用Company。两个新实例分别连接、关闭、停止：Client PID14184和88132。两次均实际选中Single Server Relay（`https://39.71.45.66:4605/`），可靠接入和画面正常；0接入拒绝、0连接失败。新进程恢复已保存的统计开关，真实画面显示约59FPS、RTT约4ms，这只是功能样本。

日志在进程正常关闭、日志缓冲完成后压缩保存：首路3,763,906字节压至257,422字节，覆盖后3,747,466字节压至253,087字节。实际呈现窗口分别32个/160.289秒/9574帧、24个/120.172秒/7191帧，详细SHA和首窗口在交付JSON中。

首路首窗口最大166.562ms，后续窗口最大193.360ms；覆盖后首窗口365.571ms，后续最大33.884ms。首路与正式编译重叠，不能仅据此认定延迟原因；本批没有定位该193ms间隔，也不声称零短顿或旧卡帧根因已消除。启动窗口单列，沿用用户接受启动暂不稳定的决定。这轮验收安装与功能保持，不扩展成短顿、网卡或饱和混传专项。

正式优化Client树新增/重新构建的6个聚焦CTest全部通过：hello生命周期、声音输出、RDP生命周期、统计展示、设置数据库/浮球位置、启动描述。其他已完成的游戏/RDP/声音、Linux数据库升级、双Relay维护和同应用两窗口设置证据继续复用，不重跑独立附属业务。

最终两资源closed、两实例stopped、guest注销204；夹具恢复原禁用配置，无新增应用目录项。本机和90 owned Client0、90临时48778监听0。只按本批页面PID/创建时间/脚本身份停止临时页面，保留默认Render和正常Panel。两Relay fresh/ready/未排空，默认Windows Relay连接1、BJ0；授权、应用和部署配置保持。

## 编排修改与限制

`build_windows_product_matrix.ps1`新增`Jobs`和隔离`CandidateId`：候选使用当前版本，普通发行仍独立升版；已有候选拒绝覆盖，候选必须Incremental，防止清空原发行安装器和历史候选。18jobs显式传到RDP SDK和Publication传输构建，策略DLL及嵌套Cargo/vcpkg同步设置。RDP SDK和策略DLL此前固定6/4任务，本批已修正。PowerShell解析、发行品牌/清单预检及diff检查通过，使用实际三产品完整构建验证新入口，没有添加模拟内部实现的测试。

初次产品预检缺少已有批准TUF root的环境变量，在编译前拒绝；设置`.env/approved-update-root.json`后通过，没有新建root或请求额外授权。初次夹具模块搜索路径失败发生于API修改前；修正后才启动测试。原快照把Backup格式版本2误称schema，保留旧样本后用v2脚本正确读取目标schema42。一次输出读取用了错误task ID，随后只读取原操作实际ID，没有重放安装或清理。

本批交付完成。Remote仅为完整包候选，独立Linux Server主机仍未指定；BJ继续作为Relay，XR与Company未部署或测试。复杂NAT、规模与长稳仍保留既有证据边界，Android完全排除，暂停专项没有转成发行前置条件。
