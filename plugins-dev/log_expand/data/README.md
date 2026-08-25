# Runtime data

`log_expand_mapping.json` maps `dat_decrypt` JSON paths to plugin variable
names. `capa`, `calib`, and `config` address `records[0]` in the three
documents published by `dat_decrypt`.

`log_expand_expressions.json` stores user-created expansion items. Both files
are editable runtime configuration and deployment only seeds them when the
target file does not already exist.
