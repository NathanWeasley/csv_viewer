# Viewer plugin host

`code_plugin` contains the Viewer-side plugin infrastructure and is independent
of the `UI` class implementation:

- `PluginManager` discovers manifests, resolves dependencies, loads Qt plugin
  DLLs, and shuts them down in reverse order.
- `PluginHost` implements the public SDK services for load sessions, ZIP entry
  access, immutable data snapshots, zero-copy derived-column commits, events,
  cross-plugin service discovery, logging, actions, and docks.

The application shell only supplies the main window, plugin menu, outer ADS
dock manager, and a callback that refreshes the data tree.

