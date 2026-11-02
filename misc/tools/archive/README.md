# misc/tools/archive

这里放**一次性/停产**的开发辅助脚本，保留是为了追溯，不再是日常命令。

## server-trim

服务端资产裁剪时期的一组脚本（2026-10）：从 profile 挂载行反推 import 闭包、分诊死包、批量删 `.d.ts/.map` 等。
现在服务端资产已经按发布包固化，这些脚本只在需要重新审计时参考：

- `audit-server-deps.py`
- `audit-server-prune.py`
- `prune-server-assets.py`
- `prune-client-ui.py`
- `junk-sweep.py`
- `prune-junk.bat`

## deploy-qml-runtime.ps1

旧的“启动 QML 壳/重放风格包”脚本，已被 `StyleResolver` + 运行时 `ui/` 风格包机制取代。

## selfcheck/cpp-selfcheck

旧的 C++ 自检工程，路径仍指向 `source/src`、`source/include` 拆分前布局；保留内容供迁移参考。
当前可运行的 VT 自检在 `misc/tools/vt-selfcheck/`。
