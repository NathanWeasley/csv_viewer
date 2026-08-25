# Viewer Plugin SDK

插件使用 Qt 6.11、MSVC v143、x64，并与 Viewer 使用相同的 Debug/Release 配置。

插件 DLL 实现 `viewer::plugin::IViewerPlugin`，通过 `Q_PLUGIN_METADATA` 和
`Q_INTERFACES` 导出。插件只能包含本目录的公开头文件，不应包含
`code_viewer`、`code_ui`、QCustomPlot 或 ADS 的内部头文件。

每个插件放在独立目录中：

```
plugins/<plugin-id>/
  plugin.json
  <plugin dll>
```

最小 `plugin.json`：

```json
{
  "sdkVersion": 2,
  "id": "com.company.example",
  "name": "Example",
  "version": "1.0.0",
  "entry": "example.dll",
  "debugEntry": "exampled.dll",
  "dependencies": []
}
```

`entry` 是 Release Viewer 使用的入口；Debug Viewer 在存在 `debugEntry` 时优先
使用它，否则回退到 `entry`。SDK v2 还提供由 Viewer 托管的会话级 JSON 文档
服务，以及由插件声明、宿主创建的层级菜单。

`viewer_expression_data_sdk.h` 定义 Viewer 核心提供的可选
`IExpressionDataService`。插件可从 `viewer.core/expression-data` 查询 v1 服务，
用 Viewer 数据快照和插件标量计算表达式，并通过 provider-owned batch 原子替换
自己发布的扩充数据列。该扩展不改变 `IViewerPlugin` 的 SDK v2 ABI。

依赖项支持 `mode` 为 `required` 或 `runtime`。`runtime` 依赖缺失时插件仍会
加载，插件可以在用户触发功能时通过 `IPluginRegistry` 显示具体错误。

