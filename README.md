# ULin Hub

**English** | [简体中文](README.zh-CN.md)

ULin Hub is the continuation product based on the DSH Hub prototype. This baseline carries over the prototype's current working-tree source, resources, local server runtime, extension examples, tooling, and tests. The prototype remains separate and unchanged.

The first baseline intentionally keeps the existing Qt Widgets UI and build behavior. QML/QQmlApplicationEngine migration will be done as a separate UI-only phase.

## Project layout

- `source/` — Qt/C++ application and CMake / Visual Studio projects
- `assets/` — application resources, translations, themes, and bundled server files
- `assets/storage/server/` — Node runtime, server dependencies, harness configuration, and launch-root data (`node`, `node.exe` and `node_modules` are not tracked)
- `source/include/linux-64/`, `source/src/linux-64/` — Linux platform layer (window bridge, server manager, extension stubs, Office conversion)
- `extensions/` — client extension sources and examples (CodexBackend, PluginTranspiler)
- `misc/legacy-tests/` — C++ unit tests
- `misc/tools/` — build, packaging and diagnostic scripts

## Build

The CMake entry point is `source/CMake/CMakeLists.txt`. Configure from the repository root with a Qt 6.5+ installation. On Windows, `source/ULin Hub.slnx` is the Visual Studio solution.

### Linux

```bash
bash source/CMake/build-linux.sh release
# equivalent to:
cmake -S source/CMake -B build/cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DULINHUB_BUILD_TESTS=OFF
cmake --build build/cmake -j"$(nproc)"
```

- Requirements: Qt 6.5+ (base / declarative / webengine / webchannel / websockets), cmake, ninja, g++
- Output: `x64/Linux-x64/release/`. A release build runs `stage-runtime.sh`, which syncs `assets/storage/server` and `assets/resources/node` into the output directory.
- Run: `cd x64/Linux-x64/release && ./run.sh`
- The build cache lives in `build/cmake` and artifacts in `x64/`; neither is tracked.
- Server state is pinned to `~/.local/share/ulinhub/harness`: VMware/HGFS shared folders do not support Unix permissions, while the `credentials-local` plugin requires `.credentials.yaml` to be `0600`.

## Naming glossary (unified 2026-10-18; new code must follow it)

Each concept has exactly one name, used end to end: QML type, C++ class, token, extension registration name, injection point, connection id and translation id. The rightmost column lists deprecated spellings — a grep hit means a missed rename (unless a historical document refers to a deleted widget shell).

| Concept | Canonical | Derived | Deprecated |
|---|---|---|---|
| Left rail | `SessionBar` | `sessionBarWidth` (size), `sessionBarBg` (color), `sessionBar_*` (translation ids) | `sidebar`, `Sidebar.qml`, `sidebarBg`, `sidebar_*` |
| Right rail (whole card) | `PanelBar` | `panelBarWidth`, `panelBarMinWidth`, and `panelGap` in `panelBarGap` contexts | `right sidebar`, `rightInset`, `rightColumn` |
| Dock inside the right rail (tabs + launcher + content) | `PanelBarDock` | model `PanelBarDockModel`, view `PanelBarDockView.qml`, QML singleton `PanelBarDock` | `PanelDock`, `Dock`, `panelDock` |
| A "block" inside the dock | `Block` (code) / "tab" (UI copy) | kind: `terminal` / `browser` / `files` / `pluginUi` / `pluginData` | `tab` (when referring to a block) |
| Extension registration name | `sessionBar`, `panelBarDock` | `kSessionBar`, `kPanelBarDock` | `sidebar`, `panelDock`, `kSidebar`, `kPanelDock` |
| Extension injection point | `sessionBar.extDock`, `panelBarDock.extDock` | — | `sidebar.extDock`, `panelBar.extDock` |
| Connection id | `qml.sessionBar.*` | — | `qml.sidebar.*` |
| Translation id | `sessionBar_*`, `panelBar_*`, `topbar_panelBar_tip` | — | `sidebar_*`, `right_sidebar_*` |
| Backend (the DSH process launched by the client) | User-facing copy always says "server" (zh: 服务端) | Code identifiers keep `Server*` / `DshApiClient` | "服务器" (server), "引擎" (engine, when meaning DSH), "后端" (backend) |

Three hard rules:

1. **A rename must cover the host and every extension in one pass.** Registration names are plain strings with no compile-time check; if the host changes and an extension does not, the extension silently stops working (no error, just missing features). All extension sources currently live under `..\` (Plugin Transpiler / DSH Plugin UI Supporter / Codex Backend / Local Terminal) — rename, rebuild and redeploy them together.
2. **Never rename the ABI.** Virtual methods in `VirtualClass/VirtualCommon.h` may only be appended at the end (no renames, no signature changes), and `com.ulinhub.*` IIDs stay as-is. That family only unifies user-facing wording, not virtual interface names.
3. **Re-run `lrelease` after changing translation ids** (`assets/translations/*.ts` → `*.qm`), otherwise the UI displays raw ids.
