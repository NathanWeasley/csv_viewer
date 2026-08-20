# 3dview Viewer plugin

`3dview` is an independent Viewer plugin project. It follows the same source,
project, intermediate-output, and package-output layout as `dat_decrypt` and is
intentionally not included in `CsvViewer.sln`.

The Visual Studio solution contains exactly one production project:

```text
plugins-dev/3dview/
  bin/                 Debug/Release intermediate files
  code/plugin/         plugin and QWidget implementation
  data/                plugin-owned development data
  lib/                 final plugin package
  project/3DView/      independent Visual Studio solution/project
  plugin.json          single canonical manifest
```

Both configurations write to the same package directory:

```text
lib/
  3dview.dll
  3dviewd.dll
  plugin.json
```

Release Viewer loads `3dview.dll`; Debug Viewer loads `3dviewd.dll` through the
manifest's `debugEntry`. The project targets Windows x64, MSVC v143, Qt 6.11.1,
and Viewer Plugin SDK v2. `ViewerPluginSdkDir` can be overridden as an MSBuild
property when the plugin is developed outside the Viewer source tree.
