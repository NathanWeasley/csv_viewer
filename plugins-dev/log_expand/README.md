# log_expand Viewer plugin

`log_expand` is an independent Viewer plugin for calculating expanded data
items from Viewer columns and scalar parameters published by `dat_decrypt`.
Its directory, Visual Studio project, intermediate outputs, and final package
layout follow `plugins-dev/dat_decrypt`.

```text
plugins-dev/log_expand/
  bin/Debug/            Debug intermediate files
  bin/Release/          Release intermediate files
  code/plugin/          Viewer plugin sources
  data/                 Default mapping and expression configuration
  lib/                  Final plugin package
  project/LogExpand/    Independent Visual Studio solution and projects
  test/                 Plugin tests and their output
  plugin.json           Single canonical manifest
```

The solution contains one x64 production DLL project plus the optional `TEST`
console project, and is intentionally not part of `CsvViewer.sln`. Both plugin
configurations write to the same package directory:

```text
lib/
  log_expand.dll
  log_expandd.dll
  plugin.json
  data/
    log_expand_mapping.json
    log_expand_expressions.json
```

After local packaging, the build deploys the current DLL and manifest to
`lib/plugins/nathan.viewer.log_expand/` in the Viewer tree.
Override the `ViewerPluginDeployDir` MSBuild property when deploying to another
Viewer installation.

`plugin.json` declares `dat_decrypt` as a required runtime dependency. Viewer
therefore loads `dat_decrypt` first, and `log_expand` additionally verifies that
the dependency reached the `Started` state before it initializes. Communication
uses Viewer SDK services and `dat_decrypt` JSON documents rather than linking to
the private `dat_decrypt` import library.

`data/log_expand_mapping.json` maps `capa`, `calib`, and `config` paths to
short plugin variable names. Only successfully mapped values appear in the
plugin's mapping viewer. Numeric and Boolean values may be used as scalar
symbols in `data/log_expand_expressions.json`; strings remain view-only.

After a hiklog session and all three `dat_decrypt` documents are ready, enabled
expressions are evaluated against the current Viewer data snapshot and
published atomically as plugin-owned columns. Missing source paths or expression
symbols skip the affected output and are reported by the diagnostics dialog.
Expressions are evaluated in JSON order: a later expression may use an earlier
successfully calculated output as an input, while self-references and forward
references are rejected. Calculation progress is reported to Viewer's load
progress bar.

Expanded items are managed by Viewer after publication. Their names use the
same theme-aware accent color as markers, and prefix groups containing expanded
items use a distinct accent color and folder icon in the data tree.

The two runtime JSON files are copied to a Viewer plugin directory only when the
corresponding destination file does not exist, so user edits survive rebuilds.
Use the plugin menu to view mapped variables, edit expressions, recalculate, or
inspect warnings and status messages.
