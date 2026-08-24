# log_expand Viewer plugin

`log_expand` is an independent Viewer plugin project for future data-item
expansion calculations. Its directory, Visual Studio project, intermediate
outputs, and final package layout follow `plugins-dev/dat_decrypt`.

```text
plugins-dev/log_expand/
  bin/Debug/            Debug intermediate files
  bin/Release/          Release intermediate files
  code/plugin/          Viewer plugin sources
  data/                 Future editable runtime data
  lib/                  Final plugin package
  project/LogExpand/    Independent Visual Studio solution and project
  plugin.json           Single canonical manifest
```

The solution contains exactly one x64 DLL project and is intentionally not part
of `CsvViewer.sln`. Both configurations write to the same package directory:

```text
lib/
  log_expand.dll
  log_expandd.dll
  plugin.json
```

After local packaging, the build automatically deploys the current DLL and JSON
package files to `lib/plugins/nathan.viewer.log_expand/` in the Viewer tree.
Override the `ViewerPluginDeployDir` MSBuild property when deploying to another
Viewer installation.

`plugin.json` declares `dat_decrypt` as a required runtime dependency. Viewer
therefore loads `dat_decrypt` first, and `log_expand` additionally verifies that
the dependency reached the `Started` state before it initializes. Communication
uses Viewer SDK services and `dat_decrypt` JSON documents rather than linking to
the private `dat_decrypt` import library.

The current implementation provides plugin lifecycle, dependency validation,
Viewer session tracking, and `dat_decrypt` JSON-input availability tracking.
Expansion formulas and derived-column publication are intentionally left for a
subsequent functional specification.
