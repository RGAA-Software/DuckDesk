# Pixels Android Client

`src/px_android` 是正式 Pixels Android 客户端，产品名为 `Pixels`。Official 的 applicationId 为 `yun.pixels.client`，Customer 为
`yun.pixels.client.customer`。只发布 `arm64-v8a`，最低系统版本为 Android 12 / API 31；不保留旧 Android 应用、数据、入口、协议默认值或包名兼容。

产品能力和设计边界见：

- [Pixels Android 客户端产品规划](../../docs/android_pixels_product_plan.md)
- [Pixels Android UI/UX 设计规范](../../docs/android_pixels_ui_design.md)
- [Android 云应用模块实施记录](../../docs/android_cloud_apps_implementation_plan_20260914.md)
- [产品编译、产物与使用说明](../../docs/product_build_and_usage.md)

## 产品构建

必须从仓库根目录使用统一产品入口：

```bat
scripts_build\build_android_product.bat official debug
scripts_build\build_android_product.bat official debug install
scripts_build\build_android_product.bat official release
scripts_build\build_android_product.bat customer debug
scripts_build\build_android_product.bat customer release
```

每次调用只删除所选发行类型的旧沙箱、独立提升 Android 版本，并构建完整目标。`debug install` 使用 `adb install -r` 覆盖安装，不卸载应用或清除用户数据。
构建前必须配置 `PIXELS_DEPLOYMENT_TRUST_STORE_FILE` 以及三个最低安全水位。Official 还必须配置
`PIXELS_EXPECTED_DEPLOYMENT_ID`、`PIXELS_OFFICIAL_CONSOLE_URL`，应用内不提供地址编辑；Customer 禁止携带这两个 Official 参数，要求用户填写
私有部署地址，并只接受签名类别为 `private` 的部署。

Debug APK：

```text
build_official/android/<official|customer>/dist/Pixels-<distribution>-<version>-debug-arm64-v8a.apk
```

Release 目录：

```text
build_official/android/<official|customer>/dist/<version>/
```

Release 同时生成并校验签名 APK、AAB、R8 mapping、native symbols、FFmpeg n6.1 对应源码、从本次 native 构建对象自动生成的 LGPL relink kit、第三方 notices 和带 SHA-256 的 `release-manifest.json`。FFmpeg 源码由当前 `VCPKG_ROOT`（未设置时为 `C:\source\vcpkg`）的已安装 SPDX 清单与下载缓存锁定，不再要求手工准备旧的源码/relink ZIP。

正式签名通过被 Git 忽略的 `keystore.properties` 或完整的 `PIXELS_*` 签名环境变量提供。不得直接调用 Gradle 的 Release 打包任务；它们会拒绝绕过统一入口，以防产生缺少合规材料或发布清单的半成品。

## 开发验证

聚焦 native C++ 修改可使用 `scripts_build\build_cpp_android_*.bat`，这类命令不代表完整产品交付。最终交付必须重新执行上述完整产品构建。

Android 使用独立“云应用”一级 Tab、`client_type=android` 和 Console 返回的权威端点；不使用旧设备内应用页、旧身份、旧固定端口或任何运行时回退。
