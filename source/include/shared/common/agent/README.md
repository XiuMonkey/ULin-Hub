# common/agent

Agent 侧的配置与纯逻辑，保持“无 QML、无宿主 Widget”边界：

- `AgentPresetService` — 从 DSH 拉取 Agent 预设、解析默认预设、写回默认选择。
- `AgentEventParser` — DSH 事件解析（assistant/tool/chunk 等字段提取），从 network 层收回。
- `ModelSelectionService` — Agent 模型目录/推理档位/凭据引用的解析与写回。
- `ToolsFilter` — Agent 工具可见性配置（`ulin-kit` 的配置读写 + QML 行模型）。
- `AgentSessionClient` — follow/page、snapshot 归一化、job/list 名册与断线重开。
- `AgentManager` — Master 后端注册表、active/selected 状态、切换生命周期与本地选择持久化（`AgentSetting.json`）。
- `dsh/` — DSH 通讯层（`DshApiClient`：mux/HTTP RPC、接管态、流与重连）。
- `ulincore/` — ULin 自有核心逻辑（预留，放不依赖 DSH 的纯逻辑）。

`SettingsController` 只做设置页的视图状态与装配，不再持有 Agent 配置细节；
后续新增 Agent 后端时，在 `AgentManager` 中注册（`activate` / `setSelectedId`），
后端切换在 `SettingsController` 的 `masterBackendChangeRequested` 上接管。
