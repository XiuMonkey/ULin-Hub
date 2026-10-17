# ULin Hub

ULin Hub is the continuation product based on the DSH Hub prototype. This baseline carries over the prototype's current working-tree source, resources, local server runtime, extension examples, tooling, and tests. The prototype remains separate and unchanged.

The first baseline intentionally keeps the existing Qt Widgets UI and build behavior. QML/QQmlApplicationEngine migration will be done as a separate UI-only phase.

## Project layout

- `source/` — Qt/C++ application and CMake/Visual Studio projects
- `assets/` — application resources, translations, themes, and bundled server files
- `assets/resources/server/` — Node runtime, server dependencies, harness configuration, and launch-root data
- `extensions/` — client extension sources and examples (CodexBackend, PluginTranspiler)
- `misc/legacy-tests/` — C++ unit tests
- `misc/tools/` — build, packaging, and diagnostic scripts

## Build

The CMake entry point is `source/CMake/CMakeLists.txt`. Configure from the repository root with a Qt 6.5+ installation. On Windows, `source/ULin Hub.slnx` is the Visual Studio solution.
