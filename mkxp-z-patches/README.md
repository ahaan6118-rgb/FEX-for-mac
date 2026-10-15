# mkxp-z Patches for ANGLE Metal on macOS (ARM64)

These patches modify [mkxp-z](https://github.com/mkxp-z/mkxp-z) (GPL-2.0+)
to run natively on Apple Silicon Macs using ANGLE's Metal backend.

## Changes

- **CWD restore for ANGLE Metal**: ANGLE's EGL context creation fails when
  the working directory has been changed to a game folder. This patch saves
  the original CWD before config loading and restores it around GL
  initialization in the RGSS thread.
- **ibtool-free build**: Replaces the XIB compilation step (requires full
  Xcode) with a prebuilt NIB, allowing builds with just Command Line Tools.
- **System iconv**: Uses macOS system iconv instead of the vendored version,
  fixing crashes with non-ASCII filenames (e.g., "Pokémon").

## Applying

```bash
git clone https://github.com/mkxp-z/mkxp-z.git
cd mkxp-z
git checkout dev
git apply ../mkxp-z-patches/mkxp-z-angle-metal-macos.patch
```

## Building

```bash
meson setup build --buildtype=release \
  -Dgfx_backend=gl \
  -Duse_miniffi=true \
  -Dopenssl=disabled \
  -Dextra_shaders=disabled \
  -Diconv_system=enabled

ninja -C build
```

## License

These patches are licensed under GPL-2.0+, same as mkxp-z.
