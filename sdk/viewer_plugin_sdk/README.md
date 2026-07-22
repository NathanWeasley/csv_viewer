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
  "sdkVersion": 1,
  "id": "com.company.example",
  "name": "Example",
  "version": "1.0.0",
  "entry": "example.dll",
  "dependencies": []
}
```

依赖项支持 `mode` 为 `required` 或 `runtime`。`runtime` 依赖缺失时插件仍会
加载，插件可以在用户触发功能时通过 `IPluginRegistry` 显示具体错误。

