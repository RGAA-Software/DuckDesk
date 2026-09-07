# 退役代码归档

本目录按用户于 2026-09-07 确认的决定保留项目精简时退出活动实现的原代码。
`native_transport_simplification` 已保存首次 SDK 迁移前的源码快照，文件及哈希详见该批次的 `manifest.json`。

## 目录约定

每个归档批次保留原仓库相对路径，例如：

```text
backup/native_transport_simplification/
  manifest.json
  src/px_deps/px_client_sdk/connection/...
  src/px_android/...
```

## 归档要求

- 完整退役文件原样归档；混合文件在清理旧分支前保存完整原始内容，包括工作区未提交改动。
- 各批次清单记录原路径、基础提交、本地修改状态和归档原因；基础提交不能代替实际内容备份。
- 不覆盖已有归档；后续不同版本使用新批次。
- 本目录只供查阅，不参与源码自动发现、构建、测试、打包或运行加载。
- 不在这里维护第二套产品或可运行兼容层。具体操作遵循根目录 `AGENTS.md`。
