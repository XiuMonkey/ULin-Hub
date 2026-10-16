# ==================================================================
# CMakeLists.txt —— ULin Hub 的 CMake 构建入口（跨平台）
# ==================================================================
# 定位：这是「新增的、并存的」构建入口，不是 .vcxproj 的替代品。
#
#   * Visual Studio 侧一个文件都没动：ULin Hub.slnx / ULin Hub.vcxproj /
#     ULin Hub.Tests.vcxproj / *.filters 继续由 MSBuild + Qt VS Tools 构建，
#     日常编辑与调试照旧走 .slnx。
#   * 这条 CMake 线负责 Linux / macOS / CI，以及 Windows 上的命令行构建。
#     两条线读同一份源码，互不写对方的产物目录（CMake 产物在 <root>/build/）。
#
# 为什么本文件在 CMake/ 子目录：源码根目录不新增文件，保持 VS 工程目录干净。
# 代价是 source dir 指向本目录，命令里要带 -S（或在本目录内执行）：
#     cmake -S CMake -B build -G Ninja ...
#
# 常用命令（在本目录或仓库根目录均可，见 CMakePresets.json）：
#     cmake --preset windows-ninja
#     cmake --build --preset windows-release
#     ctest --preset windows-release          # 跑 unit_test
#   Linux / macOS 把 windows- 换成 linux- / macos-。
#
# Windows 上跑命令行构建需要 cl.exe 在 PATH 里：从「Developer PowerShell for VS」
# 启动，或先执行 vcvars64.bat。Ninja 也必须在 PATH 里（Qt 自带一份：
# <Qt>\Tools\Ninja，CMake 自带一份：<Qt>\Tools\CMake_64\bin）。
#
# 与 .vcxproj 的已知差异（有意为之，均为两边各自的事实来源所致）：
#   1) 源文件清单：.vcxproj 手工列举，这里用 GLOB + CONFIGURE_DEPENDS 自动收集，
#      避免新增 .cpp 时「只加了一边」。反过来说，新增文件时 .vcxproj 仍需手工补。
#   2) Qt 定位：.vcxproj 里硬编码 D:\Qt\...；这里走 find_package + CMAKE_PREFIX_PATH
#      （QTDIR 环境变量优先，约定与 misc/misc/tools/release-translations.ps1 一致）。
#   3) 翻译（misc/misc/tools/release-translations.ps1）未接入 CMake：.qm 已进 qrc，
#      这个 CMake 目标照样能拿到内置的中英语言包。跑 CMake 构建时若改了
#      translations/*.ts，需要手动跑一次那个脚本再重新构建（qrc 才会带上新包）。
# ==================================================================

cmake_minimum_required(VERSION 3.21)

project(ULinHub
	VERSION 1.0.0
	DESCRIPTION "ULin Hub - desktop client"
	# C：内嵌的 libvterm 是 C99（见 3rdparty/libvterm/README.md）。
	LANGUAGES C CXX)

# 源码根目录（本文件位于 <root>/CMake/ 下）
get_filename_component(ULINHUB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

# Qt 定位：QTDIR 优先（与 misc/misc/tools/release-translations.ps1 的约定一致），
# 也可以直接 -DCMAKE_PREFIX_PATH=<Qt 安装目录>。
if(DEFINED ENV{QTDIR} AND NOT "$ENV{QTDIR}" STREQUAL "")
	list(APPEND CMAKE_PREFIX_PATH "$ENV{QTDIR}")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# QT_FIND_PRIVATE_MODULES：让 find_package(Qt6) 连带加载私有模块包。
# 本项目需要它 —— 详见下面 Qt6::CorePrivate 那一段（ExtensionLoader.cpp
# 用了一个 Qt 私有头）。官方公开发行版都带私有模块包（<Qt>/lib/cmake/Qt6CorePrivate）。
set(QT_FIND_PRIVATE_MODULES ON)

# 至少 6.5：qt_standard_project_setup / qt_add_executable 都是 Qt 6.3+ 的接口。
# .vcxproj 上用的是 6.11.2_msvc2022_64。
#
# Qml/Quick：QML 壳（docs/ui-qml-migration-plan.md）跑在 QQmlApplicationEngine 上，必须显式链。
# QuickControls2/Layouts/Effects 等按 M2 起用到时再加（那些既有 C++ 库也有各自的 QML 模块，
# 发行版还要把 Qt 的 qml/ 目录部署到 exe 同目录 —— 见 misc/tools/run-qml-shell.ps1 的说明）。
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network WebSockets Qml Quick)

# 打开 AUTOMOC / AUTOUIC / AUTORCC 并把 C++ 标准等默认值设好。
qt_standard_project_setup()

# ------------------------------------------------------------------
# 源文件收集
# ------------------------------------------------------------------
# 本项目没有手写 #include "moc_*.cpp"（已确认），所以 AUTOMOC 的
# mocs_compilation.cpp 机制可以直接用。
#
# include/ 下的头文件必须一并交给 target：AUTOMOC 只扫描「出现在 target 源文件
# 列表里」的头文件，而本项目头文件与 .cpp 不同目录，不满足「同名同目录」的推断规则。
file(GLOB_RECURSE ULINHUB_SOURCES CONFIGURE_DEPENDS
	"${ULINHUB_ROOT}/src/*.cpp"
	"${ULINHUB_ROOT}/src/*.h"
	"${ULINHUB_ROOT}/include/*.h")

# 内嵌的 VT 解析器（3rdparty/libvterm）：9 个 C99 编译单元，零外部依赖。
# 与 .vcxproj 的 9 条 <ClCompile> 一一对应 —— 那边是手工列举，新增文件时两边都要动。
file(GLOB_RECURSE ULINHUB_VENDOR_SOURCES CONFIGURE_DEPENDS
	"${ULINHUB_ROOT}/3rdparty/libvterm/src/*.c")
list(APPEND ULINHUB_SOURCES ${ULINHUB_VENDOR_SOURCES})

# 原样引入的第三方 C 代码不随我们改，构建侧替它放行 CRT 弃用警告：
# parser.c:53 的 strncpy 在 MSVC 下报 C4996。VCXPROJ 侧还叠了 /sdl 会把它抬成错误，
# CMake 的 MSVC 默认参数里没有 /sdl，所以这边只需要这一个宏。
if(MSVC)
	set_source_files_properties(${ULINHUB_VENDOR_SOURCES} PROPERTIES
		COMPILE_DEFINITIONS "_CRT_SECURE_NO_WARNINGS")
endif()

# 原样引入的第三方 C 代码不随我们改，工程侧替它放行 CRT 弃用警告：
# MSVC 下工程开了 SDLCheck(/sdl)，会把 parser.c:53 那处 strncpy 的 C4996 抬成错误。
# 与 .vcxproj 里那 9 条的 <PreprocessorDefinitions> 保持一致。
if(MSVC)
	set_source_files_properties(${ULINHUB_VENDOR_SOURCES} PROPERTIES
		COMPILE_DEFINITIONS "_CRT_SECURE_NO_WARNINGS")
endif()

list(LENGTH ULINHUB_SOURCES ULINHUB_SOURCE_COUNT)
message(STATUS "ULin Hub: 已收集 ${ULINHUB_SOURCE_COUNT} 个源文件（src/ + include/ + 3rdparty/libvterm）")

# 同名源文件：src/ui/Main.cpp 与 src/core/main.cpp 基名相同。
# Ninja / Makefile 生成器按相对路径区分对象文件，没有问题；Visual Studio 生成器
# 把对象文件平铺到同一目录会撞名 —— .vcxproj 里专门给 src/ui/Main.cpp 写了
# <ObjectFileName>$(IntDir)ui-Main.obj</ObjectFileName> 就是为了这个，这里保持一致。
if(CMAKE_GENERATOR MATCHES "^Visual Studio")
	set_source_files_properties("${ULINHUB_ROOT}/src/ui/Main.cpp" PROPERTIES
		COMPILE_OPTIONS "/Fo${CMAKE_CURRENT_BINARY_DIR}/ui-Main.obj")
endif()

# ------------------------------------------------------------------
# 主程序（对应 ULin Hub.vcxproj）
# ------------------------------------------------------------------
qt_add_executable(ulinhub WIN32
	${ULINHUB_SOURCES}
	"${ULINHUB_ROOT}/../assets/resources/ulinhub.qrc"
	# QML 壳的资源（source/qml/）：单独一份 qrc，与 .vcxproj 的 <QtRcc Include="qml\qml.qrc" /> 对应
	"${ULINHUB_ROOT}/qml/qml.qrc")

set_target_properties(ulinhub PROPERTIES
	# 产物名与 .vcxproj 一致：ULin Hub.exe（target 名不带空格，避免生成器差异）
	OUTPUT_NAME "ULin Hub"
	# ⚠️ 必须显式打开：qt_standard_project_setup() 只开 AUTOMOC/AUTOUIC，
	#    而把 .qrc 直接写进源列表、又没有 AUTORCC 时，它只是一份"没有编译规则的文件"——
	#    构建静默通过，exe 里却一个资源都没有（图标/语言包/样式模板，以及 QML 壳的 qml/*.qml）。
	#    .vcxproj 侧由 Qt VS Tools 的 QtRcc 负责，不受影响。
	AUTORCC ON)

target_include_directories(ulinhub PRIVATE
	"${ULINHUB_ROOT}/include"
	# libvterm 只公开这一层头（src/ 内部头是相对同目录包含的）
	"${ULINHUB_ROOT}/3rdparty/libvterm/include"
	# resources/ 只为下面 rc.exe 解析 "ulin-hub.ico" 的相对路径用：
	# CMake 会把 include 目录翻成 /I 传给 rc.exe（<INCLUDES> 占位符）。
	"${ULINHUB_ROOT}/../assets/resources")

# 与 .vcxproj 的 <PreprocessorDefinitions> 保持一致
target_compile_definitions(ulinhub PRIVATE QT_NO_DEPRECATED_WARNINGS)

target_link_libraries(ulinhub PRIVATE
	Qt6::Core
	Qt6::Gui
	Qt6::Widgets
	Qt6::Network
	Qt6::WebSockets
	Qt6::Qml
	Qt6::Quick)

# Qt 私有头：src/ExtensionSystem/ExtensionLoader.cpp 用了
# <QtCore/private/qzipreader_p.h>（解压 .ext 包），全项目仅此一处。
# .vcxproj 是靠硬编码那条私有头目录（D:\Qt\...\include\QtCore\6.11.2）实现的；
# CMake 侧的对应做法是链 Qt6::CorePrivate —— 它的 INTERFACE_INCLUDE_DIRECTORIES
# 就是 <Qt>/include/QtCore/<版本>，即私有头的根目录。
# 注意：私有头不保证跨 Qt 小版本稳定，能不用最好不用；真要彻底跨平台，
# 长期方案是换掉这个私有依赖（换 QuaZip，或自己解析 zip）。
if(NOT TARGET Qt6::CorePrivate)
	message(FATAL_ERROR
		"找不到 Qt6::CorePrivate：ExtensionLoader.cpp 需要 Qt 私有头 qzipreader_p.h。\n"
		"上方已打开 QT_FIND_PRIVATE_MODULES；若仍失败，说明这份 Qt 是裁剪安装"
		"（缺 Qt6CorePrivate 包或私有头），请补装后再 configure。")
endif()

target_link_libraries(ulinhub PRIVATE Qt6::CorePrivate)

 # 图标资源脚本：非 Windows 平台没有这个文件（.ico/.rc 是 Windows 专用）
if(WIN32)
	target_sources(ulinhub PRIVATE "${ULINHUB_ROOT}/../assets/resources/ulin-hub.rc")

# 让 exe 从 dependence\ 子目录解析 Qt / MSVC 运行库（私有程序集）：Windows 的加载器
# 默认**不搜索 exe 的子目录**，靠这段 manifest 声明才会去找。DLL 清单在 source/dependence.manifest，
# 加/换 DLL 时清单与依赖声明的 version 都要改。
if (MSVC)
    target_link_options(ulinhub PRIVATE "/MANIFESTINPUT:${ULINHUB_ROOT}/dependence.deps.manifest")
    # 构建后把运行库收进 dependence\（私有程序集要求清单与 dll 同在）。
    # CMake 的构建目录里本来没有 Qt dll，所以让脚本从 QTDIR\bin 取（见 misc/tools/rebuild.sh）。
    add_custom_command(TARGET ulinhub POST_BUILD
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${ULINHUB_ROOT}/../misc/tools/make-dependence.ps1" -Target "$<TARGET_FILE_DIR:ulinhub>"
        COMMENT "把运行库收进 dependence`（私有程序集）")
endif()
endif()

# WIN32_EXECUTABLE 由 qt_add_executable 的 WIN32 关键字设置；Qt6::Core 会据此
# 自动链上 Qt6::EntryPointPrivate（见 Qt6CoreTargets.cmake 里的 generator expression），
# 所以 Win32 子系统下 int main() 能正常作为入口，不需要自己写 WinMain。

# ------------------------------------------------------------------
# 单元测试（对应 ULin Hub.Tests.vcxproj）
# ------------------------------------------------------------------
# unit_test 编译了 Thunk（Windows x64 调用约定机器码）与 ComCaller（COM），
# 非 Windows 平台目前编不过，所以默认只在 Windows 上打开；如需强制打开：
#     cmake -DULINHUB_BUILD_TESTS=ON ...
if(WIN32)
	option(ULINHUB_BUILD_TESTS "构建 unit_test（Qt Test）" ON)
else()
	option(ULINHUB_BUILD_TESTS "构建 unit_test（Qt Test）" OFF)
	message(STATUS "ULin Hub: unit_test 依赖 Windows-only 的 Thunk/ComCaller，非 Windows 平台默认关闭")
endif()

if(ULINHUB_BUILD_TESTS)
	enable_testing()
	find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

	# 清单与 ULin Hub.Tests.vcxproj 一致：只编被测到的那部分源码，不是整个 app
	set(ULINHUB_TEST_SOURCES
		"${ULINHUB_ROOT}/../tests/UnitTestMain.cpp"
		"${ULINHUB_ROOT}/../tests/TestDshEventParser.cpp"
		"${ULINHUB_ROOT}/../tests/TestCodeHighlighter.cpp"
		"${ULINHUB_ROOT}/../tests/TestHistoryManager.cpp"
		"${ULINHUB_ROOT}/../tests/TestMarkdownPreprocess.cpp"
		"${ULINHUB_ROOT}/../tests/TestThunk.cpp"
		"${ULINHUB_ROOT}/../tests/TestSessionCatalog.cpp"
		"${ULINHUB_ROOT}/../tests/TestPluginMarketModel.cpp"
		"${ULINHUB_ROOT}/../tests/TestModelSelection.cpp"
		"${ULINHUB_ROOT}/../tests/TestDshApiClient.cpp"
		"${ULINHUB_ROOT}/../tests/TestSessionPrefetcher.cpp"
		"${ULINHUB_ROOT}/../tests/TestSettingsStore.cpp"
		"${ULINHUB_ROOT}/../tests/TestClientSettings.cpp"
		"${ULINHUB_ROOT}/../tests/TestLogger.cpp"
		"${ULINHUB_ROOT}/../tests/TestTranslationManager.cpp"
		"${ULINHUB_ROOT}/../tests/TestServerManager.cpp"
		"${ULINHUB_ROOT}/../tests/TestClientExtension.cpp"
		"${ULINHUB_ROOT}/src/ExtensionSystem/Thunk.cpp"
		"${ULINHUB_ROOT}/src/ExtensionSystem/DllCaller.cpp"
		"${ULINHUB_ROOT}/src/ExtensionSystem/ComCaller.cpp"
		"${ULINHUB_ROOT}/src/ExtensionSystem/ClientExtension.cpp"
		"${ULINHUB_ROOT}/src/network/DshEventParser.cpp"
		"${ULINHUB_ROOT}/src/common/util/CodeHighlighter.cpp"
		"${ULINHUB_ROOT}/src/common/util/MarkdownPreprocess.cpp"
		"${ULINHUB_ROOT}/src/common/session/SessionCatalog.cpp"
		"${ULINHUB_ROOT}/src/common/PluginMarket/PluginMarketModel.cpp"
		"${ULINHUB_ROOT}/src/network/DshApiClient.cpp"
		"${ULINHUB_ROOT}/src/network/SessionPrefetcher.cpp"
		"${ULINHUB_ROOT}/src/common/settings/SettingsStore.cpp"
		"${ULINHUB_ROOT}/src/common/session/AgentPresetService.cpp"
		"${ULINHUB_ROOT}/src/common/settings/ClientSettings.cpp"
		"${ULINHUB_ROOT}/src/common/util/Logger.cpp"
		"${ULINHUB_ROOT}/src/common/appearance/TranslationManager.cpp"
		"${ULINHUB_ROOT}/src/ui/TranslationUi.cpp"
		"${ULINHUB_ROOT}/src/core/ServerManager.cpp"
		# ServerManager.cpp 与 DshApiClient.cpp 都经 dshRegister 引用 ConnectionManager::instance()，
		# 所以测试目标也必须编它，否则 LNK2019（理由同上一条）。
		"${ULINHUB_ROOT}/src/common/util/ConnectionManager.cpp"
		# DshApiClient.cpp 按 kApiSink 从公共注册表取"后端接管的接收端"（客户端扩展），
		# 所以测试目标也要编 CommonRegistry.cpp，否则同样是 LNK2019。
		"${ULINHUB_ROOT}/src/common/util/CommonRegistry.cpp"
		"${ULINHUB_ROOT}/src/ExtensionSystem/ExtensionRegistry.cpp")

	# 测试类头文件带 Q_OBJECT，交给 AUTOMOC
	file(GLOB ULINHUB_TEST_HEADERS CONFIGURE_DEPENDS "${ULINHUB_ROOT}/../tests/*.h")

	# include/ 下的头也要交给 AUTOMOC，但**只列测试目标真正编译了 .cpp 的那些**。
	#
	# 为什么需要：AUTOMOC 只扫描「出现在 target 源文件列表里」的头文件，而这些被测
	# 源码的 Q_OBJECT 都在 include/ 里、.cpp 又在 src/ 下，不满足「同名同目录」的
	# 自动推断 —— 漏掉它们 moc 不会生成，链接期报一串 metaObject / qt_metacast /
	# staticMetaObject 无法解析。.vcxproj 侧由 Qt VS Tools 处理，所以那边一直是好的。
	#
	# 注意别图省事写成 include/*.h：那会把 ULinHub / ChatInputWidget 等**没有编进
	# 测试目标**的类的 moc 也生成出来，反而引入新的未解析符号（moc 会引用它们的
	# 构造函数与槽）。清单必须与上面 ULINHUB_TEST_SOURCES 里那几个带 Q_OBJECT 的
	# 类一一对应。
	set(ULINHUB_TEST_PUBLIC_HEADERS
		"${ULINHUB_ROOT}/include/network/DshApiClient.h"
		"${ULINHUB_ROOT}/include/network/SessionPrefetcher.h"
		"${ULINHUB_ROOT}/include/common/appearance/TranslationManager.h"
		"${ULINHUB_ROOT}/include/core/ServerManager.h"
		"${ULINHUB_ROOT}/include/common/util/ConnectionManager.h"
		"${ULINHUB_ROOT}/include/common/util/CommonRegistry.h")

	qt_add_executable(ulinhub_tests
		${ULINHUB_TEST_SOURCES}
		${ULINHUB_TEST_HEADERS}
		${ULINHUB_TEST_PUBLIC_HEADERS})

	set_target_properties(ulinhub_tests PROPERTIES
		OUTPUT_NAME "ULin Hub.Tests")

	target_include_directories(ulinhub_tests PRIVATE "${ULINHUB_ROOT}/include")

# 让 exe 从 dependence\ 子目录解析 Qt / MSVC 运行库（私有程序集）：Windows 的加载器
# 默认**不搜索 exe 的子目录**，靠这段 manifest 声明才会去找。DLL 清单在 source/dependence.manifest，
# 加/换 DLL 时清单与依赖声明的 version 都要改。
if (MSVC)
    target_link_options(ulinhub_tests PRIVATE "/MANIFESTINPUT:${ULINHUB_ROOT}/dependence.deps.manifest")
    # 构建后把运行库收进 dependence\（私有程序集要求清单与 dll 同在）。
    # CMake 的构建目录里本来没有 Qt dll，所以让脚本从 QTDIR\bin 取（见 misc/tools/rebuild.sh）。
    add_custom_command(TARGET ulinhub_tests POST_BUILD
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${ULINHUB_ROOT}/../misc/tools/make-dependence.ps1" -Target "$<TARGET_FILE_DIR:ulinhub_tests>"
        COMMENT "把运行库收进 dependence`（私有程序集）")
endif()
	target_compile_definitions(ulinhub_tests PRIVATE QT_NO_DEPRECATED_WARNINGS)

	# 与 .vcxproj 的 QtModules（core;gui;testlib）一致，控制台子系统
	target_link_libraries(ulinhub_tests PRIVATE
		Qt6::Core
		Qt6::Gui
		Qt6::Widgets
		Qt6::Test
		Qt6::Network
		Qt6::WebSockets)

	# WORKING_DIRECTORY 必须指到仓库根：用例里读的是「相对当前目录」的路径
	# （TestThunk 读 unit_test/test_signature.json 与 resources/server/launch-root/...），
	# 而 ctest 默认的工作目录是 build/ 下的二进制目录，那里没有这些文件 ——
	# 表现是 ctest 报失败、但 exe 单独跑却全过（QTest 用 -o 写文件可看到真实结果）。
	add_test(NAME ulinhub_tests COMMAND ulinhub_tests
		WORKING_DIRECTORY "${ULINHUB_ROOT}")
endif()

# ------------------------------------------------------------------
# 后续步骤（本文件暂未涉及，见 README「跨平台」相关讨论）
# ------------------------------------------------------------------
# 运行时资源还没有纳入构建：程序启动时会在可执行文件同目录找
# resources/server/node[.exe]、resources/harness-node-entry.mjs 等（见
# ServerManager.cpp / PluginMarketInstaller.cpp）。这些是「按平台分发」的事，
# 不适合塞进构建脚本：Linux/macOS 需要各自平台编译出来的 node 运行时，
# 以及在该平台重新 install 的 node_modules。等做跨平台部署时再补 install()/
# POST_BUILD 规则，顺带解决 windeployqt / macdeployqt / linuxdeployqt 的差异。
