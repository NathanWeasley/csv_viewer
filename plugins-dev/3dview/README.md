# 3dview Viewer plugin

`3dview` is an independent Viewer plugin project. It follows the same source,
project, intermediate-output, and package-output layout as `dat_decrypt` and is
intentionally not included in `CsvViewer.sln`.

The Visual Studio solution contains one production project and one independent
console test project:

```text
plugins-dev/3dview/
  bin/                 Debug/Release intermediate files
  code/                 config, trajectory, scene, rendering, model and UI code
  data/3dview.json      canonical editable runtime configuration
  lib/                 final plugin package
  project/3DView/      independent Visual Studio solution and projects
  test/                 TEST sources and ignored test output
  plugin.json          single canonical manifest
```

Both configurations write to the same package directory:

```text
lib/
  3dview.dll
  3dviewd.dll
  plugin.json
  data/3dview.json
```

Release Viewer loads `3dview.dll`; Debug Viewer loads `3dviewd.dll` through the
manifest's `debugEntry`. The project targets Windows x64, MSVC v143, Qt 6.11.1,
and Viewer Plugin SDK v2. `ViewerPluginSdkDir` can be overridden as an MSBuild
property when the plugin is developed outside the Viewer source tree.

After local packaging, the build automatically deploys the current DLL,
`plugin.json`, and runtime data to `lib/plugins/nathan.viewer.3dview/` in the
Viewer tree. Override the `ViewerPluginDeployDir` MSBuild property for another
Viewer location. An existing deployed `data/3dview.json` is never overwritten;
the canonical file is copied only when the destination configuration is absent.

The 3D view is a non-modal, plugin-owned top-level window. Closing it only hides
the existing window; selecting **显示 3D 视图** reuses it. It is deliberately not
inserted into Viewer/QADS docking because reparenting a `QOpenGLWidget` can
invalidate its native surface and OpenGL context.

Window geometry, splitter/header layout, selected joint/TCP tracks, index-based
playback speed/frame, trajectory display mode/local sample range, and trajectory
styles are stored in `3dview.ini` beside the plugin DLL. The plugin does not
write these values into Viewer's `user/config.ini`.

## Viewer data synchronization

The plugin subscribes to Viewer's successful-data-load and data-about-to-unload
events. If the 3D window already exists, a successful load immediately acquires
the new immutable Viewer snapshot and rebuilds its tracks and controls. Clearing
Viewer data releases that snapshot, stops playback, clears trajectories/TCP and
controls, returns linked models and the progress index to zero, and resets the
camera. Data events do not create the 3D window; opening it later acquires the
current Viewer snapshot on demand.

## Configuration

`joint` entries use Viewer column names and explicitly declare the joint type:

```json
{ "name": "actual_joint_1", "type": "revolute" }
{ "name": "actual_joint_3", "type": "prismatic" }
```

`revolute` rotates around the corresponding link/STL local Z axis;
`prismatic` translates along that local Z axis. Parent transforms cascade to
their children. All frames are right-handed and Z-up. TCP and model origins use
`[x, y, z, rx, ry, rz]`; orientation is composed as `Rz * Ry * Rx`.

Playback and the progress slider operate only on row/sample indexes. The plugin
does not read timestamps or derive playback positions from time. Joint-track
selection only drives linked STL transforms, while TCP selection only drives
the TCP coordinate axes. Trajectories can be hidden, shown in full, or limited
to a symmetric configurable sample range around the current index.

STL configuration is optional. `directory` is relative to `3dview.json` unless
it is absolute. A base mesh remains fixed, and each `links` item corresponds to
the joint variable at the same index:

```json
"model": {
  "directory": "models",
  "base": "base.stl",
  "links": [
    { "stl": "link1.stl", "origin": [0, 0, 100, 0, 0, 0] },
    { "stl": "link2.stl", "origin": [200, 0, 0, 0, 0, 0] }
  ]
}
```

If only `model.directory` is provided, all STL files in that directory are
loaded at the world origin without joint binding. Both binary and ASCII STL are
accepted.

Build `3DView.sln` to build the plugin and TEST project. TEST outputs remain
under `test/bin`; they are never copied to `lib`.
