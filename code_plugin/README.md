# Viewer 外部 DLL 插件开发指南

本文面向 Viewer 插件开发者和插件宿主维护者，描述当前仓库中 **Plugin SDK v2** 的实际行为。接口定义以 [`viewer_plugin_sdk.h`](../sdk/viewer_plugin_sdk/include/viewer_plugin/viewer_plugin_sdk.h) 为准；本文中的加载顺序、所有权和错误行为均按当前宿主实现整理。

## 1. 快速结论

- 插件是运行在 Viewer 进程内的 Qt 动态插件 DLL，不是独立进程，也不是 C 风格 `LoadLibrary/GetProcAddress` 接口。
- 插件类必须同时继承 `QObject` 和 `viewer::plugin::IViewerPlugin`，并声明 `Q_OBJECT`、`Q_PLUGIN_METADATA`、`Q_INTERFACES`。
- 当前 ABI 环境为 **Windows x64、MSVC v143、C++17、Qt 6.11.1**。插件必须使用与目标 Viewer 相同的 Qt、编译器、架构及 Debug/Release 配置。
- 插件只能依赖公开 SDK，不应包含 `code_viewer`、`code_ui`、QCustomPlot 或 ADS 的内部头文件。
- 每个插件需要一个独立目录，至少包含外部 `plugin.json` 和入口 DLL。
- Viewer 只在启动时扫描一次插件；当前没有运行时安装、卸载、重载或启停界面。部署或替换 DLL 后需要重启 Viewer。
- DLL 与宿主共享同一地址空间。插件崩溃、越界写、ABI 不一致或未停止的后台线程都可能使 Viewer 崩溃，因此只应安装可信插件。

## 目录

- [系统结构](#2-系统结构)
- [二进制兼容要求](#3-二进制兼容要求)
- [插件目录和发现规则](#4-插件目录和发现规则)
- [`plugin.json`](#5-pluginjson)
- [创建最小插件](#6-创建最小插件)
- [生命周期与资源所有权](#7-生命周期与资源所有权)
- [Host API 详解](#8-host-api-详解)
- [线程模型](#9-线程模型)
- [加载顺序和失败行为](#10-加载顺序和失败行为)
- [发布与验收清单](#11-发布与验收清单)
- [当前 SDK v2 的边界](#12-当前-sdk-v2-的边界)

## 2. 系统结构

```text
CsvViewer.exe
├─ PluginManager
│  ├─ 递归发现 plugin.json
│  ├─ 解析依赖并生成加载顺序
│  ├─ 通过 QPluginLoader 加载 DLL
│  └─ 逆序关闭并卸载插件
└─ PluginHost（IViewerHost）
   ├─ IDataService       数据会话、只读快照、派生列
   ├─ IArchiveService    当前 ZIP 的目录和条目读取
   ├─ IEventService      数据加载/卸载/新增列事件
   ├─ IPluginRegistry    插件状态和跨插件服务
   ├─ IUiService         “插件”菜单、Dock、消息框
   └─ ILogService        Viewer 统一日志
```

`PluginManager` 只负责发现和生命周期；插件功能只能通过 `IViewerHost` 暴露的公开服务访问 Viewer。宿主不会把 `UI`、`Viewer`、ADS dock manager 等内部对象交给插件。

主要源码入口：

- [`PluginManager.cpp`](PluginManager.cpp)：发现、manifest 校验、依赖排序、加载和卸载。
- [`PluginHost.cpp`](PluginHost.cpp)：全部 SDK 服务的当前实现。
- [`UI.cpp`](../code_ui/UI.cpp)：插件目录配置与宿主启动/关闭时机。
- [`ExamplePlugin`](../sdk/viewer_plugin_sdk/examples/ExamplePlugin)：可编译的最小示例。

## 3. 二进制兼容要求

公开接口跨 DLL 传递了 Qt 类型、`std::shared_ptr`、`std::function` 和带虚函数的 C++ 对象，因此它是 **C++ ABI**，不是稳定的纯 C ABI。开发和发布时应满足下表。

| 项目 | 当前要求 |
| --- | --- |
| 操作系统/架构 | Windows x64 |
| 编译器工具集 | MSVC v143 |
| C++ 标准 | C++17 |
| Qt | 当前工程配置为 Qt 6.11.1 `msvc2022_64`，插件应使用同一套 Qt 二进制 |
| 构建类型 | Release 插件配 Release Viewer；Debug 插件配 Debug Viewer |
| MSVC 运行库 | 与目标 Viewer/Qt 匹配，通常 Release 为 `/MD`、Debug 为 `/MDd` |
| Qt 模块 | SDK 基础依赖为 `Core`、`Widgets` |
| SDK 版本 | manifest 中必须是整数 `1` |
| 插件接口 IID | `com.weekendbuild.csvviewer.IViewerPlugin/2.0` |

即使代码可以编译，也不要混用 x86/x64、MinGW/MSVC、不同 MSVC ABI、不同 Qt 构建或 Debug/Release。常见结果是 `QPluginLoader` 报 DLL 无法加载，严重时会在对象释放或回调时崩溃。

## 4. 插件目录和发现规则

### 4.1 默认目录

Viewer 总会递归扫描：

```text
<CsvViewer.exe 所在目录>/plugins/
```

推荐的包结构为：

```text
plugins/
└─ com.company.example/
   ├─ plugin.json
   ├─ ExamplePlugin.dll
   └─ 插件自己的运行时依赖.dll
```

扫描是递归的，因此 `plugin.json` 不必位于扫描根目录的第一层。每个插件仍应使用独立目录，避免依赖 DLL、配置和资源互相污染。

### 4.2 附加搜索目录

可以在 `<exe>/user/config.ini` 的 `[General]` 节增加 `pluginDirectories`：

```ini
[General]
pluginDirectories=../shared-viewer-plugins,D:/ViewerPlugins
```

- 绝对路径直接使用。
- 相对路径相对于 **CsvViewer.exe 所在目录** 解析，而不是当前工作目录。
- 默认 `<exe>/plugins` 始终会扫描；配置项只负责追加目录。
- 同一个 `id` 被多次发现时，先发现的 manifest 生效，后者被忽略并记错误日志。不要依赖递归遍历顺序来覆盖插件。

## 5. `plugin.json`

最小 manifest：

```json
{
  "sdkVersion": 2,
  "id": "com.company.example",
  "name": "Example Plugin",
  "version": "1.0.0",
  "entry": "ExamplePlugin.dll",
  "debugEntry": "ExamplePlugind.dll",
  "dependencies": []
}
```

字段含义：

| 字段 | 必需 | 当前行为 |
| --- | --- | --- |
| `sdkVersion` | 是 | 必须严格等于宿主的 `kViewerPluginSdkVersion`，当前为 `2`。不支持版本范围。 |
| `id` | 是 | trim 后非空；在全部扫描目录中必须唯一，并且必须与 DLL 中 `IViewerPlugin::id()` 完全一致。推荐反向域名格式。 |
| `name` | 建议 | 展示名称。当前加载器不校验它是否与 `IViewerPlugin::name()` 相同。 |
| `version` | 建议 | 插件自身版本字符串。当前不参与依赖解析，也不与 DLL 返回值交叉校验。 |
| `entry` | 是 | 入口 DLL 路径，通常写相对于 manifest 所在目录的文件名。 |
| `debugEntry` | 否 | Debug Viewer 优先使用的入口 DLL；省略时同样使用 `entry`。 |
| `dependencies` | 否 | 依赖对象数组；省略等同空数组。 |

依赖项格式：

```json
{
  "dependencies": [
    { "id": "com.company.data-provider", "mode": "required" },
    { "id": "com.company.optional-renderer", "mode": "runtime" }
  ]
}
```

两种受支持的 `mode`：

- `required`：依赖缺失，或依赖没有成功进入 `Started`，当前插件不会初始化并标记为 `Failed`。
- `runtime`：依赖缺失时只写警告，当前插件仍会初始化。插件应在用户真正触发相关功能时调用 `pluginState()`/`queryService()`，并给出清晰错误提示。

所有已安装的依赖，无论是 `required` 还是 `runtime`，都会先于依赖方加载；关闭时顺序相反。循环依赖会使循环及受影响的插件加载失败。`mode` 省略时当前实现默认为 `runtime`；不要使用其他拼写来依赖宿主的宽松解析行为。

> `Q_PLUGIN_METADATA(... FILE "plugin.json")` 会把构建时的 JSON 编入 Qt 插件元数据，但 Viewer 的发现器仍会独立读取磁盘上的外部 `plugin.json`。因此发布包必须保留外部文件，并确保两份信息来自同一个源文件。

## 6. 创建最小插件

### 6.1 插件实现

```cpp
#include "viewer_plugin/viewer_plugin_sdk.h"

#include <QLabel>

class ExamplePlugin final
    : public QObject
    , public viewer::plugin::IViewerPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID VIEWER_PLUGIN_INTERFACE_IID FILE "plugin.json")
    Q_INTERFACES(viewer::plugin::IViewerPlugin)

public:
    QString id() const override
    {
        return QStringLiteral("com.company.example");
    }

    QString name() const override
    {
        return QStringLiteral("Example Plugin");
    }

    QString version() const override
    {
        return QStringLiteral("1.0.0");
    }

    bool initialize(viewer::plugin::IViewerHost* host) override
    {
        if (!host || host->sdkVersion() != viewer::plugin::kViewerPluginSdkVersion)
            return false;

        m_host = host;
        m_action = host->ui()->addPluginAction(
            id(), QStringLiteral("打开 Example"), [this]() { openDock(); });
        return m_action != 0;
    }

    void shutdown() override
    {
        // 先停止并 join 本插件创建的线程、timer 和异步任务。
        // 菜单、Dock、事件订阅、注册服务随后由宿主统一移除。
        m_host = nullptr;
        m_action = 0;
        m_dock = 0;
    }

private:
    void openDock()
    {
        if (!m_dock)
        {
            auto* label = new QLabel(QStringLiteral("Viewer Plugin SDK is ready."));
            label->setAlignment(Qt::AlignCenter);
            m_dock = m_host->ui()->createDock(
                id(), QStringLiteral("main"), name(), label,
                viewer::plugin::DockArea::Right);
        }
        if (!m_host->ui()->showDock(m_dock))
            m_host->ui()->showError(name(), QStringLiteral("无法打开插件面板。"));
    }

    viewer::plugin::IViewerHost* m_host = nullptr; // 不拥有
    viewer::plugin::PluginActionHandle m_action = 0;
    viewer::plugin::PluginDockHandle m_dock = 0;
};

#include "ExamplePlugin.moc"
```

三个 Qt 宏缺一不可：

- `Q_OBJECT`：启用 Qt 元对象。
- `Q_PLUGIN_METADATA`：导出 Qt 插件元数据和正确 IID。
- `Q_INTERFACES`：让 `qobject_cast<IViewerPlugin*>` 成功。

插件实例由 `QPluginLoader` 创建和销毁，插件代码不得手动 `delete this`。`m_host` 是借用指针，只能从 `initialize()` 开始到 `shutdown()` 返回之前使用。

### 6.2 CMake

仓库内示例可直接参考 [`CMakeLists.txt`](../sdk/viewer_plugin_sdk/examples/ExamplePlugin/CMakeLists.txt)。独立插件工程可以使用：

```cmake
cmake_minimum_required(VERSION 3.16)
project(ExamplePlugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

set(VIEWER_PLUGIN_SDK_DIR "C:/path/to/csv_viewer/sdk/viewer_plugin_sdk")
add_subdirectory("${VIEWER_PLUGIN_SDK_DIR}" ViewerPluginSDK-build)

add_library(ExamplePlugin MODULE
    ExamplePlugin.cpp
    plugin.json
)
target_link_libraries(ExamplePlugin PRIVATE ViewerPluginSDK)

# Windows 下得到 ExamplePlugin.dll，而不是 libExamplePlugin.dll。
set_target_properties(ExamplePlugin PROPERTIES PREFIX "")

add_custom_command(TARGET ExamplePlugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/plugin.json"
        "$<TARGET_FILE_DIR:ExamplePlugin>/plugin.json"
)
```

用与 Viewer 相同的 Qt 安装配置和构建：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/msvc2022_64
cmake --build build --config Release
```

将 `Release/ExamplePlugin.dll`、`plugin.json` 和插件自身依赖一起放入目标 Viewer 的 `plugins/com.company.example/`。

如果使用 Visual Studio `.vcxproj`：

- 项目类型选择 DLL，平台为 x64，工具集为 v143。
- 启用 Qt VS Tools 的 `Core`、`Widgets` 模块并保证包含该类的文件经过 `moc`。
- 可导入 [`ViewerPluginSDK.props`](../sdk/viewer_plugin_sdk/ViewerPluginSDK.props) 来添加 SDK include 路径和 C++17 设置。
- 仍需手动保证 Qt 版本和 Debug/Release 与 Viewer 一致。

## 7. 生命周期与资源所有权

启动流程：

```text
发现 manifest
  → 校验 sdkVersion/id/entry
  → 依赖拓扑排序
  → QPluginLoader::instance()
  → qobject_cast<IViewerPlugin*>()
  → 校验 manifest.id == plugin.id()
  → 状态 Loaded
  → initialize(host)
  → true: Started / false 或异常: Failed 并卸载
```

Viewer 退出时按成功加载顺序的逆序执行：

```text
plugin.shutdown()
  → 宿主删除该插件的服务、订阅、菜单和 Dock
  → QPluginLoader::unload()
```

开发者必须遵守以下规则：

1. `initialize()` 只做可回滚的注册和轻量初始化，成功返回 `true`，失败返回 `false`。
2. `initialize()` 返回 `false` 或抛异常时，宿主 **不会调用 `shutdown()`**。宿主会移除已登记的宿主资源，但插件必须在返回失败前停止自己启动的线程、释放非宿主管理资源。
3. `shutdown()` 中先阻止新任务进入，再取消任务、停止 timer、join 所有后台线程，最后释放跨插件服务引用并清空 host 指针。
4. 不要让 `std::function`、Qt signal 连接、线程回调或静态对象在 DLL 卸载后继续指向插件代码。
5. 宿主在 `shutdown()` 返回后自动移除用插件 ID 登记的 action、dock、subscription 和 service。插件可以提前取消订阅/注销服务，但不要重复释放宿主拥有的 action 或 dock。
6. `createDock()` 成功后，content 被放入宿主管理的 dock；让宿主随 dock 清理它。若调用返回 `0`，content 尚未被可靠接管，插件应自行释放。
7. 插件自己的普通 `QObject`、模型、worker 等仍由插件负责；推荐建立清晰的 QObject parent 或 RAII 所有权链。
8. 不要让异常越过任何 SDK 接口边界。宿主只在 `initialize()`、`shutdown()`、action 和事件回调等部分入口做兜底捕获，并非所有虚函数调用都有保护。
9. 所有 `ownerPluginId` 和 `providerPluginId` 参数必须传本插件准确的 `id()`。宿主依靠这个字符串在卸载前清理资源，当前不会在每个 API 上验证调用者身份；传错 ID 可能留下指向已卸载 DLL 的回调或对象。

## 8. Host API 详解

### 8.1 `IViewerHost`

`initialize()` 获得的 `IViewerHost*` 是所有能力的入口：

```cpp
host->sdkVersion(); // 当前为 2
host->data();       // IDataService
host->archive();    // IArchiveService
host->events();     // IEventService
host->plugins();    // IPluginRegistry
host->ui();         // IUiService
host->log();        // ILogService
```

这些子接口和 host 生命周期相同，插件不拥有它们，也不得删除。

### 8.2 数据会话和只读快照 `IDataService`

`currentSession()` 返回当前数据集：

| 字段 | 含义 |
| --- | --- |
| `sessionId` | 单调变化的会话标识；`0` 表示当前没有有效数据集。 |
| `sourceType` | `None`、`Csv`、`Zip` 或 `BinaryLog`。 |
| `sourcePath` | 当前数据源路径。多 CSV 场景当前记录第一个成功加载的文件。 |
| `sourceFileName` | `sourcePath` 的文件名部分。 |

`acquireSnapshot()` 在没有有效数据或没有列时返回空指针。成功时返回不可变快照：

- `columnNames()` 是当前列名列表。
- `rowCount()` 是数据行数。
- `column(name)` 按区分大小写的完整列名查找，返回连续的 `double` 只读视图。
- `ColumnView::data` 不能修改，也不能在所属 `DataSnapshotPtr` 销毁后继续使用。
- 快照通过共享所有权保持列内存有效；即使 Viewer 随后加载了别的数据，已取得的旧快照仍可读取。
- 快照的 `sessionId()` 表示它来自哪个数据集。异步工作完成时必须再通过提交结果检查数据集是否仍有效。

示例：根据已有列创建派生列。

```cpp
using namespace viewer::plugin;

bool addScaledColumn(IViewerHost* host, const QString& sourceName)
{
    const DataSnapshotPtr snapshot = host->data()->acquireSnapshot();
    if (!snapshot)
        return false;

    const ColumnView source = snapshot->column(sourceName);
    if (!source.isValid())
        return false;

    const QString outputName = sourceName + QStringLiteral("_x2");
    DerivedColumnCreateResult created = host->data()->createDerivedColumn(
        snapshot->sessionId(), outputName, snapshot->rowCount());
    if (!created.success())
    {
        host->ui()->showError(QStringLiteral("派生列"), created.error);
        return false;
    }

    double* output = created.writer->data();
    for (qsizetype i = 0; i < source.size; ++i)
        output[i] = source.data[i] * 2.0;

    const DataCommitResult committed = created.writer->commit();
    if (!committed.success())
    {
        host->ui()->showError(QStringLiteral("派生列"), committed.error);
        return false;
    }
    return true;
}
```

派生列 writer 的约束：

- `createDerivedColumn()` 会预先校验有效 session、非空且不重复的列名、正数且与数据集一致的行数，并一次性分配输出数组。
- 一个 writer 只能 `commit()` 一次。第一次调用后，即使提交失败，writer 也已失效；`data()` 将返回 `nullptr`。
- 提交成功时数值 buffer 通过 move 交给 Viewer，不再复制整列。
- 计算期间若切换/清空数据集，提交返回 `StaleSession`，不会污染新数据集。
- 不要让多个线程同时写同一个 writer 的 buffer。适合的模式是“一个后台任务拥有一个 snapshot 和一个 writer”。
- 大列计算应放到后台线程；不要在菜单回调或数据加载事件里长时间占用 UI 线程。

`DataCommitStatus`：

| 状态 | 含义/处理建议 |
| --- | --- |
| `Success` | 已添加列，并触发 `ColumnAdded`。 |
| `InvalidWriter` | writer 已提交过；创建新 writer。 |
| `StaleSession` | 用户已切换或清空数据；丢弃结果，必要时基于新快照重算。 |
| `InvalidName` | 列名为空。 |
| `DuplicateName` | 创建后、提交前出现同名列，或名称本来就重复。换名后重建 writer。 |
| `RowCountMismatch` | 输出长度与当前数据集不一致。 |
| `HostShuttingDown` | Viewer 正在退出；直接结束任务。 |
| `InternalError` | 宿主添加失败；记录 `error` 便于排查。 |

### 8.3 ZIP 读取 `IArchiveService`

该服务只对当前 `SourceType::Zip` 会话有效：

```cpp
QString error;
const auto entries = host->archive()->listCurrentZipEntries(sessionId, &error);
for (const auto& entry : entries)
{
    if (!entry.readable)
        continue;
    const auto read = host->archive()->readCurrentZipEntry(sessionId, entry.path);
    if (read.success)
    {
        // read.data 是 QSharedPointer<const QByteArray>
    }
}
```

- 两个 API 都会校验 `sessionId` 仍是当前 ZIP 会话。
- `ArchiveEntryInfo::path` 是 ZIP 内路径；读取时优先原样使用枚举结果。传入路径会把 `\` 规范为 `/` 并移除开头的 `/`，最终匹配仍区分大小写。
- `readable` 为 false 的目录或不可读条目不能读取。
- `readCurrentZipEntry()` 会把整个解压后条目读入内存。先检查 `uncompressedSize`，大文件应放后台线程并控制内存占用。
- 返回数据的生命周期由 `QSharedPointer` 管理；失败时查看 `error`，不要解引用空的 `data`。

### 8.4 数据事件 `IEventService`

```cpp
m_loadedSubscription = host->events()->subscribeDataLoaded(
    id(), [this](const viewer::plugin::LoadSessionInfo& session)
    {
        onDataLoaded(session);
    });

m_unloadSubscription = host->events()->subscribeDataAboutToUnload(
    id(), [this](quint64 sessionId)
    {
        cancelWorkFor(sessionId);
    });

m_columnSubscription = host->events()->subscribeColumnAdded(
    id(), [this](quint64 sessionId, const QString& name)
    {
        onColumnAdded(sessionId, name);
    });
```

事件语义：

| 事件 | 触发时机 |
| --- | --- |
| `DataLoaded` | 有效数据集已经安装，并获得新 `sessionId` 后。 |
| `DataAboutToUnload` | 当前数据仍存在、即将清空之前。适合取消属于该 session 的任务。 |
| `ColumnAdded` | 派生列提交成功、Viewer 数据树已请求刷新后。 |

回调当前在 Viewer/UI 线程执行。回调应快速返回，把重计算投递给 worker。宿主会捕获越过事件回调边界的异常并记录，但插件仍应在自身边界内处理错误。

`unsubscribe(id)` 可以安全地接受任一事件类型的订阅 ID。宿主卸载插件时也会按 `ownerPluginId` 自动移除全部订阅；插件如果有后台任务，仍建议在 `shutdown()` 开始时主动取消订阅，避免产生新工作。

### 8.5 插件状态和跨插件服务 `IPluginRegistry`

`PluginState` 含义：

| 状态 | 含义 |
| --- | --- |
| `NotFound` | 没有发现该 ID。 |
| `Discovered` | manifest 已发现但尚未启动；正常关闭后也会回到该状态。 |
| `Loaded` | DLL 和接口已加载，正在执行 `initialize()`。 |
| `Started` | `initialize()` 成功。 |
| `Failed` | manifest、依赖、DLL、接口或初始化失败。 |

`isPluginLoaded()` 对 `Loaded` 和 `Started` 都返回 true。若功能要求依赖已经可用，应明确检查 `pluginState(id) == PluginState::Started`，而不是只调用 `isPluginLoaded()`。

跨插件服务由 provider 注册一个 `QObject*`：

```cpp
const bool ok = host->plugins()->registerService(
    id(), QStringLiteral("diagnosis"), 2, serviceObject);

int actualVersion = 0;
QObject* object = host->plugins()->queryService(
    QStringLiteral("com.company.provider"),
    QStringLiteral("diagnosis"),
    1,
    &actualVersion);
```

- `providerPluginId`、`serviceId` 非空，版本必须大于 0，对象不能为空。
- 同一 `(providerPluginId, serviceId)` 只能注册一次。
- provider 在自身 `initialize()` 中处于 `Loaded`，可以注册服务。
- registry 只保存 `QPointer`，不拥有 service。provider 必须保证 QObject 存活到注销或 `shutdown()`，最简单的做法是让它由插件对象拥有。
- `queryService()` 只有在实际版本不低于 `minimumVersion` 时返回对象；`actualVersion` 只应在查询成功后使用。
- consumer 不应向下转型到 provider 的具体实现类。把共享服务接口放在双方共同依赖的公开头文件中，使用 `Q_DECLARE_INTERFACE`/`Q_INTERFACES` 和 `qobject_cast`，并按版本演进接口。
- 返回的是非拥有裸指针，不要跨 provider 卸载长期缓存。`required` 依赖可确保 provider 先加载、consumer 先关闭，是消费启动期服务的首选。

### 8.6 UI `IUiService`

`addPluginAction()` 把入口加入主窗口的“插件”菜单，返回 `0` 表示失败。action 的回调由宿主捕获异常并记日志。

`addPluginMenu()` 一次注册插件自己的层级菜单。插件用稳定的 `id`/`parentId`
描述 `Menu`、`Action`、`CheckableAction` 和 `Separator`；宿主拥有实际
`QMenu`/`QAction`，并在插件卸载前统一删除。菜单状态通过
`setPluginMenuItemEnabled()`、`setPluginMenuItemChecked()` 和
`setPluginMenuItemVisible()` 更新。

`createDock()` 创建 ADS dock：

- `ownerPluginId` 和 `dockId` 必须非空，content 不能为 null。
- `dockId` 应在插件内稳定且唯一；宿主对象名为 `plugin.<ownerPluginId>.<dockId>`。
- 支持 `Left`、`Right`、`Top`、`Bottom`、`Center` 五个首选区域。
- `showDock()` 显示并聚焦；`closeDock()` 只是隐藏，不销毁。
- action 和 dock handle 只在本次插件加载期间有效，不能持久化到配置文件或下次启动复用。

`showError()` 和 `showInformation()` 显示模态消息框。从 worker 调用时宿主会异步投递到 UI 线程，因此调用返回不代表用户已经关闭消息框。不要在退出流程中依赖消息框完成同步。

### 8.7 JSON 文档 `IJsonDocumentService`

SDK v2 的 JSON 服务按 `sessionId + providerPluginId + documentId` 保存不可变
`QJsonDocument` 快照。`publishBatch()` 原子替换 provider 在当前会话中的全部结果，
并拒绝过期 session；`listDocuments()` 和 `acquireDocument()` 可供 Viewer 功能或
其他插件消费。数据会在会话卸载或 provider 插件卸载时由宿主清理。

### 8.8 核心表达式与扩充列 `IExpressionDataService`

Viewer 注册保留服务 `viewer.core/expression-data` v1。consumer 通过
`queryService()` 和 `qobject_cast<IExpressionDataService*>` 获取接口；不需要、也
不允许链接 Viewer 的私有表达式实现。

`evaluate()` 对不可变 `DataSnapshotPtr` 计算与 Viewer 数据表达式一致的 exprtk
表达式，并额外接受插件提供的标量。输出缓冲区由调用方独占；传入 null 可只做
语法和符号校验。`createDerivedColumnBatch()` 为当前 session 创建一组宿主管理的
等长输出缓冲区，`commit()` 会原子替换同一 provider 先前发布的整批列。不同
provider、原始列和 Viewer 表达式列之间不允许同名覆盖。

批量提交后 DataManager 接管列的生命周期。快照仍通过共享所有权保留旧列，宿主
同步重建数据树并重新绑定已有曲线，因此插件不能在提交后继续写 writer 缓冲区。

### 8.9 日志 `ILogService`

```cpp
host->log()->write(
    id(),
    viewer::plugin::LogLevel::Info,
    QStringLiteral("analysis completed"));
```

级别为 `Debug`、`Info`、`Warning`、`Error`。最终格式包含：

```text
plugin[com.company.example] info: analysis completed
```

日志写入 `<exe>/log/csv_viewer.log`，达到大小限制后会继续写入 `csv_viewer_001.log` 等分片。插件日志属于 Viewer 的 `operation` 日志类别；排错时确认该类别已启用，然后搜索 `plugin[插件ID]`。

## 9. 线程模型

插件初始化、关闭、action 回调和 Viewer 数据事件当前都发生在 UI 线程。原则上只在 UI 线程注册/注销事件、服务、菜单和 Dock，并在 UI 线程操作 QWidget。

当前宿主对下列调用提供了跨线程处理：

| 调用 | 当前跨线程行为 |
| --- | --- |
| `currentSession()`、`acquireSnapshot()` | 同步切换到宿主线程读取，调用线程会阻塞。 |
| `createDerivedColumn()`、`createDerivedColumnBatch()`、writer `commit()` | 同步切换到宿主线程校验/提交，调用线程会阻塞。 |
| ZIP 枚举/读取 | 会安全取得当前 session，实际文件 I/O 在调用线程完成。 |
| `showError()`、`showInformation()` | 异步投递到宿主线程。 |
| `write()` | 进入 Viewer 的线程安全日志队列。 |

`IEventService`、`IPluginRegistry`、`addPluginAction()`、`createDock()`、`showDock()`、`closeDock()` 内部没有通用的跨线程封送或锁，不要从 worker 直接调用。

推荐的异步流程：

1. 在 UI 线程捕获一个 `DataSnapshotPtr` 和当前 `sessionId`。
2. worker 只读 snapshot，并独占自己的派生列 writer/output buffer。
3. 计算期间响应插件自己的取消标志。
4. worker 调用 `commit()`；宿主在 UI 线程最终检查 session。`StaleSession` 是正常取消结果，不应当作崩溃错误。
5. `shutdown()` 设置取消标志并 join worker，保证 DLL 卸载前没有代码仍在运行。

不要在 UI 线程等待一个正在调用同步 host API 的 worker，否则可能形成“UI 等 worker、worker 等 UI”的死锁。

## 10. 加载顺序和失败行为

发现后，宿主按依赖做深度优先排序。只有成功 `Started` 的插件才进入关闭列表，且按逆序关闭。这保证 required provider 通常比 consumer 活得更久。

以下错误会写入 Viewer 日志：

| 日志关键字 | 常见原因 | 检查项 |
| --- | --- | --- |
| `Invalid plugin manifest` | JSON 语法错误、根不是对象、缺少 `id`/`entry`、SDK 版本不支持 | 用 JSON 校验器检查外部文件；确认 `sdkVersion` 是数字 2。 |
| `Duplicate plugin id` | 扫描目录中有两个相同 ID | 清理旧包或修改 ID；不要依赖目录顺序覆盖。 |
| `Required plugin dependency is missing` | required 依赖未安装 | 部署依赖插件及 manifest。 |
| `Required plugin dependency did not start` | 依赖被发现但加载/初始化失败 | 先查看 provider 更早的错误。 |
| `Runtime plugin dependency is not installed` | 可选依赖不存在 | 若功能确实可选可忽略；触发功能时给用户提示。 |
| `Cyclic plugin dependency detected` | manifest 依赖成环 | 重新划分服务层或把真正可选的交互改成运行时查询。 |
| `Plugin entry DLL does not exist` | `entry` 拼写或部署位置错误 | 路径相对于该 manifest 目录。 |
| `Plugin DLL failed to load` | 架构、Qt、Debug/Release 不匹配，或依赖 DLL 缺失 | 用同一工具链重编译；检查依赖 DLL 和 `QPluginLoader::errorString()`。 |
| `DLL does not implement the Viewer plugin interface` | 缺宏、IID 错误、SDK 头不一致 | 核对三个 Qt 宏和 `VIEWER_PLUGIN_INTERFACE_IID`。 |
| `Manifest id and plugin interface id do not match` | JSON 与 `id()` 不同 | 统一反向域名 ID，注意大小写和空格。 |
| `Plugin DLL could not be unloaded` | 仍有对象、线程或对 DLL 的引用存活 | 检查 shutdown、QObject 所有权和跨插件缓存。 |

`initialize()` 返回 false 时宿主只记录状态失败，不知道插件自身原因。插件应在返回前通过 `ILogService` 写出具体原因。

## 11. 发布与验收清单

构建前：

- [ ] 只包含 SDK 和插件自己的公开依赖头文件。
- [ ] x64、MSVC v143、C++17、Qt 6.11.1、Debug/Release 与目标 Viewer 一致。
- [ ] manifest `id` 与 `IViewerPlugin::id()` 完全一致。
- [ ] `name`、`version` 在 manifest 和 DLL 中保持一致，尽管当前宿主只强校验 ID。
- [ ] 所有 required/runtime 依赖已正确声明，没有循环。

部署后：

- [ ] 独立插件目录中存在外部 `plugin.json`、入口 DLL 和第三方运行时 DLL。
- [ ] 启动日志出现 `Plugin started: <name> <version>`。
- [ ] “插件”菜单 action 可用，Dock 可反复显示/隐藏。
- [ ] 无数据、CSV、二进制日志、ZIP 各场景都能优雅处理。
- [ ] 在长计算中切换或清空数据，插件能处理 `StaleSession` 并取消旧任务。
- [ ] 重复创建派生列时能处理重名。
- [ ] 缺失 runtime 依赖时 Viewer 仍可启动，用户触发功能时得到明确提示。
- [ ] 关闭 Viewer 时线程均已结束，日志没有 DLL 无法卸载或回调异常。

## 12. 当前 SDK v2 的边界

以下能力当前没有公开接口，插件不应通过包含内部头文件绕过：

- 直接操作曲线、QCustomPlot、游标、书签、表达式或 Viewer 内部 DataManager。
- 直接取得主窗口、ADS dock manager 或任意 UI 内部对象。
- 动态重载、单插件启停、插件设置页或插件管理 UI。
- manifest 版本范围、依赖版本约束、平台条件或能力声明。
- 插件签名、权限隔离、进程沙箱或崩溃隔离。

若确有新能力需求，应先在 [`viewer_plugin_sdk.h`](../sdk/viewer_plugin_sdk/include/viewer_plugin/viewer_plugin_sdk.h) 中设计可版本化的公开接口，同时升级 SDK 版本/IID，并在 `PluginHost` 中实现；不要让第三方插件链接 Viewer 内部库来形成隐式 ABI。
