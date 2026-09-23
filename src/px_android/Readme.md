# Pixels Android Client

`src/px_android` 是正式 Android 客户端。Pixels Official 的 applicationId 为 `yun.pixels.client`，Pixels Customer 为
`yun.pixels.client.customer`；OEM 使用其不可变 release profile 中的独立 applicationId、应用名、图标和签名谱系。只发布 `arm64-v8a`，最低系统
版本为 Android 12 / API 31；不保留旧 Android 应用、数据、入口、协议默认值或包名兼容。

产品能力和设计边界见：

- [Pixels Android 客户端产品规划](../../docs/android_pixels_product_plan.md)
- [Pixels Android UI/UX 设计规范](../../docs/android_pixels_ui_design.md)
- [Android 云应用模块实施记录](../../docs/android_cloud_apps_implementation_plan_20260914.md)
- [产品编译、产物与使用说明](../../docs/product_build_and_usage.md)

## 产品构建

必须从仓库根目录使用统一产品入口：

```bat
scripts_build\build_android_product.bat official fast-release
scripts_build\build_android_product.bat official fast-release install
scripts_build\build_android_product.bat customer fast-release
scripts_build\build_android_product.bat customer fast-release install
scripts_build\build_android_product.bat release
set PIXELS_OEM_RELEASE_PROFILE=D:\secure\north-star\oem-release-profile.json
scripts_build\build_android_product.bat oem fast-release
scripts_build\build_android_product.bat oem fast-release install
scripts_build\build_android_product.bat oem release
```

`fast-release` 每次调用只删除所选发行类型的旧沙箱、独立提升 Android 版本，并以 Release 运行语义、O1 native 优化、关闭 R8/资源压缩的方式
构建完整可签名 APK；`fast-release install` 使用 `adb install -r` 覆盖安装，不卸载应用或清除用户数据。它只用于开发短测，不是发布候选。
正式 `release` 在任何清理和升版前同时预检两种发行，随后只提升 Android 版本一次，并用同一版本构建隔离的 Official/Customer
完整制品；旧的单发行 Release 入口不再支持。只有两份 release manifest 均通过才生成 `build_official/android/release-matrix.json`。
构建前必须配置用于应用升级的 `PIXELS_UPDATE_ROOT_FILE`。Official 和 Customer 都必须注入
`PIXELS_OFFICIAL_CONSOLE_URL`：Official 固定使用该地址且不提供编辑；Customer 只将它作为禁止地址，要求用户填写自己的私有部署地址。
Console 连接使用标准 HTTPS，不再携带或校验部署身份证书、nonce、信任库或本地身份水印。

OEM 不属于 Pixels 双发行矩阵。fast Release/正式 Release 分别清理并写入 `build_official/android/oem/<oem_id>/`；二者都必须使用与 profile 固定值相同的
Android 签名证书。applicationId、应用名、launcher/round icon、
`oem_id/release_namespace` 和 profile SHA-256 均由同一 profile 注入，Official/Customer 反向拒绝这些 OEM 输入。
运行界面使用编译注入的应用名；账号、关于、隐私、通知、诊断、剪贴板、远控和录像提示不硬编码 Pixels。OEM Splash、launcher/round icon 和通知
小图标使用 profile 品牌资源。升级签名域、HTTP 协议头及开源法律声明保留 Pixels 技术/权利人标识，不能随 OEM 显示品牌改写。

开发期 fast Release APK：

```text
build_official/android/<official|customer>/dist/Pixels-<distribution>-<version>-fast-release-arm64-v8a.apk
build_official/android/oem/<oem_id>/dist/OEM-<oem_id>-<version>-fast-release-arm64-v8a.apk
```

Release 目录：

```text
build_official/android/<official|customer>/dist/<version>/
build_official/android/oem/<oem_id>/dist/<version>/
```

Release 同时生成并校验签名 APK、AAB、R8 mapping、native symbols、FFmpeg n6.1 对应源码、从本次 native 构建对象自动生成的 LGPL relink kit、第三方 notices 和带 SHA-256 的 `release-manifest.json`。FFmpeg 源码由当前 `VCPKG_ROOT`（未设置时为 `C:\source\vcpkg`）的已安装 SPDX 清单与下载缓存锁定，不再要求手工准备旧的源码/relink ZIP。

正式签名通过被 Git 忽略的 `keystore.properties` 或完整的 `PIXELS_*` 签名环境变量提供。不得直接调用 Gradle 的 Release 打包任务；它们会拒绝绕过统一入口，以防产生缺少合规材料或发布清单的半成品。

## 开发验证

聚焦 native C++ 修改可使用 `scripts_build\build_cpp_android_*.bat`，这类命令不代表完整产品交付。最终交付必须重新执行上述完整产品构建。

Android 使用独立“云应用”一级 Tab、`client_type=android` 和 Console 返回的权威端点；不使用旧设备内应用页、旧身份、旧固定端口或任何运行时回退。
