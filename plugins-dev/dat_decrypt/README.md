# dat_decrypt Viewer plugin

`dat_decrypt` is built independently from Viewer. Its Visual Studio solution contains one
production project, `LibDATConverter.vcxproj`; the project compiles the converter and the
Viewer plugin wrapper into the same DLL and is intentionally not part of `CsvViewer.sln`.

Build requirements match Viewer: Windows x64, MSVC v143, Qt 6.11.1, and the public Viewer
Plugin SDK v2. `ViewerPluginSdkDir` can be overridden from an MSBuild property or a local
property sheet when the plugin is developed outside the Viewer source tree.

Both configurations write to the same package directory:

```text
lib/
  dat_decrypt.dll
  dat_decryptd.dll
  plugin.json
```

Release Viewer loads `dat_decrypt.dll`; Debug Viewer loads `dat_decryptd.dll` through the
manifest's optional `debugEntry`. After local packaging, the build automatically deploys
the current DLL and JSON package files to
`lib/plugins/nathan.viewer.dat_decrypt/` in the Viewer tree. Override the
`ViewerPluginDeployDir` MSBuild property when deploying to a different Viewer location.
