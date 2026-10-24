# External mods and patches on macOS

The source game and DLC dump stay read-only. The Mac Mods page and native
settings manage loose-file overlays, patches and plugins; several changes need a
restart. The default writable root is `out/macos-data/`, overridden by
`BB_DATA_DIR`. See [the Mac guide](../packaging/macos/README.md) for settings.

## Loose-file mods

Place extracted mods below the data root's `mods/` directory:

```text
mods/
  My Mod/
    dvdroot_ps4/
      chr/...
      param/...
      menu/...
```

The loader also accepts an `app0/` or title-ID wrapper, one archive wrapper
folder, or bare game asset folders such as `chr/`, `parts/`, `param/` and `sfx/`.
ZIP/7z archives must be extracted first. Names match case-insensitively, including
mods assembled on a case-sensitive filesystem. Replacement `.dcx`/`.bnd` files
are taken whole; their contents are not merged.

The profile in `mods.json` controls order and disabled folders:

```json
{"order": ["First Mod", "Second Mod"], "disabled": ["First Mod"]}
```

Later enabled mods win when two replace the same file. New folders join the end
in alphabetical order. A sibling `CUSA00900-mods/dvdroot_ps4/` is loaded before
profile-selected mods. A selected mod root that directly contains `dvdroot_ps4/`
is also accepted. `BB_MODS_ENABLED=0` disables loose mods, including the sibling.

```sh
BB_GAME_DIR=/path/to/CUSA00900 BB_MODS_DIR=/path/to/mods bash run.sh
BB_GAME_DIR=/path/to/CUSA00900 BB_MODS_ENABLED=0 bash run.sh
```

`BB_MODS_CONFIG` overrides the profile path. A private symlink view in
`prepared/mod-game-*` combines the original files with replacements. The loader
expands only directories it must change, and cleans up its own view on normal
exit. A forced termination can leave a view behind; later runs create a new one.
The runtime exposes `/app0` and `/hostapp` read-only and keeps saves separate.

Symlinks inside a mod and replacements for `eboot.bin`, `sce_module` or `sce_sys`
are rejected. Windows junctions, hard-link fallbacks and DLL injectors are not
part of this path. `BB_MOD_TRACE=1` logs initial file reads.

## Executable patches

External patches live in the data root's `patches/` folder. The Mac runtime
accepts supported XML and TOML patches for the verified 1.09 layout; it checks
addresses, original bytes where required, and conflicts with installed timing
hooks before writing the generated image. Source executables stay unchanged.

`patches.json` stores the enabled and disabled selections:

```json
{"enabled": ["extra.xml/My Patch"], "disabled": ["extra.xml/Other Patch"]}
```

`BB_PATCHES_DIR` and `BB_PATCHES_CONFIG` override those paths. Unsupported mask
searches and invalid addresses are rejected. A matching SFO version alone does
not establish executable compatibility. The preparation report records patch
analysis in `macos-patch-analysis.json`.

## Plugins and player data

The selected randomizer, boss-rush, mutator and debug-menu plugins build as
macOS `.dylib` modules for the chosen runtime architecture. Configure them through
the Mac integration rather than copying Windows `.dll` or Linux `.so` files.
Plugin and parameter changes can persist in game saves. Existing saves and trophy
history remain in the selected user directory; use copies for experiments.
