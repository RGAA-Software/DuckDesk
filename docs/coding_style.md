# 项目编码风格

2026-09-17 用户决定。适用于项目维护的新增代码和本次修改涉及的逻辑块；不批量重排无关历史代码、生成文件、`backup/` 或只读第三方源码。

## 可读命名硬规则

所有项目维护的代码都必须让人能从标识符直接看出职责，生产代码、测试、fixture、闭包捕获、回调参数、脚本和配置辅助代码没有豁免。
禁止 `a`、`b`、`c`、`x`、`y`、`tmp`、`data`、`obj`、`item`、`thing`、`foo`、`bar` 等脱离上下文无法说明含义的名字，
也禁止用 `f` 代替 fixture、`s` 代替 store、`r` 代替 request/result、`b` 代替 barrier 这类仅凭类型猜测的缩写。
应使用 `fixture`、`activity_store`、`open_request`、`start_barrier`、`candidate_index`、`payload_bytes` 等能表达领域角色的名称。
只有确属该领域公认术语且在当前公式中无歧义时才允许短名，例如几何公式中的 `x`/`y` 坐标；“作用域很小”“循环惯例”不构成例外。
修改既有代码时同步修复所触及范围的不可读命名。生成代码、只读第三方源码和 `backup/` 归档不做机械重命名。
本规则不是建议项：`scripts/check_readable_names.ps1` 是当前 DB0–DB5、Console/Auth/Desk PostgreSQL 代码和 Windows Service
改造范围的 Rust 全量自动门禁，并由 `scripts/server_validation/postgres.ps1` 在创建测试数据库前执行。
`scripts/check_cpp_readable_names.ps1` 检查相对合并基线的全部 C/C++ 变更以及暂存区、工作区变更；CI 可以通过
`CPP_NAMING_BASE_REVISION` 或 `-BaseRevision` 明确评审基线。它拒绝新增的单字母和脱离上下文命名，并单独列出 `x`/`y`
供人工确认确属几何坐标。新纳入维护范围的源码必须同步扩展门禁路径；不得用排除文件、例外清单或缩短扫描范围来绕过命名问题。
此规范替代旧的 C++ LLVM 规则；不替代项目所有权、初始化、异步安全、架构分层、国际化和主题要求。

## C++：Google Style

以 [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html) 为基础，根 `.clang-format` 使用 `BasedOnStyle: Google`。
按用户明确覆盖，普通缩进与续行缩进均为 4 空格，不使用 Tab；项目代码行宽为 150 列，其余采用 Google 的命名与头文件组织规则。
无法合理拆分的 URL、外部字面量等按指南例外处理。
文件后缀不因此批量改名；既有外部 ABI、Qt 边界及 WebRTC 例外不改变。

[项目 C++ 安全规范](cpp_smart_pointer_standard.md) 更严格的智能指针/RAII、禁止异步捕获裸 `this`、确定性初始化等仍是硬门禁。
指南允许的裸指针用法不自动获得项目许可。只格式化改动范围，再运行 ownership 检查和相应用例。

## Rust：官方风格

遵循 [Rust 官方 Style Guide](https://doc.rust-lang.org/style-guide/)，以当前锁定工具链的 `cargo fmt` / `rustfmt` 默认风格为准：
4 空格、100 列；类型/trait 用 UpperCamelCase，函数/变量/模块用 snake_case，常量用 SCREAMING_SNAKE_CASE。
不自定义紧凑的一行函数/事务，也不把多个职责压在一行。字符串内容不为满足行宽而改变。
保持 Cargo 声明的 edition，不以风格调整名义顺便升级语言版本。

按实际改动 crate 运行 `cargo fmt --manifest-path <workspace>/Cargo.toml -p <crate> -- --check`，
并运行对应 `cargo clippy --all-targets -- -D warnings` 和行为测试；格式化通过不代替事务/并发/生命周期验收。

## TypeScript：Microsoft Style

采用微软 TypeScript 团队的 [Coding Guidelines](https://github.com/microsoft/TypeScript/wiki/Coding-guidelines) 中适用于应用的通用规则，
不是声称整个 TypeScript 社区必须遵循该团队的内部规范，也不引入编译器私有 helper/API。

- 类型、接口、类和枚举使用 PascalCase；函数、变量、参数和成员使用 camelCase，不给接口机械添加 `I` 前缀。
- 4 空格缩进、双引号、分号；优先箭头函数，单参数在语法允许时不加无意义括号；每次变量声明一个变量。
- 开始大括号同行，条件/循环使用大括号，`else` 换行。对象类型完整，避免 `any` 隐藏业务边界；异步错误和资源生命周期显式处理。
- 本项目工具行宽为 100（项目选择，不冒称微软强制列宽）；Vue 的 TypeScript 脚本使用同一规则，模板遵守框架语法。

Prettier 只做基础排版，不能单独证明符合微软全部约定；尤其 `else` 换行由后续 ESLint `brace-style: stroustrup` 修正。
对改动文件执行 Prettier → ESLint fix → ESLint / 类型检查 / 测试；不要再用 Prettier 覆盖最终 `else` 排版。
命名、模块边界和安全性由类型检查与代码评审共同把关，不把格式化工具当作架构检查器。

## 验收边界

新风格从本决定起生效；历史文件随实际功能修改逐步收敛。不得为了让样式检查变绿关闭授权/生命周期测试，
也不得更改第三方所有权模型。格式化涉及 SQL 字符串时保持内容不变，并重新校验 SQLx metadata；完整回归期间冻结源码。
