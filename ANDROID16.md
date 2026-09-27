# Evolution X Android 16 for alioth

Use the `aosp-16` branches listed in [the local manifest](manifests/alioth-aosp-16.xml).
They retain the final alioth, SM8250, XiaomiParts, Dolby and MiuiCamera device work
while targeting Evolution X Android 16 (`bka`, release configuration `bp4a`).
`hardware/xiaomi` remains a standalone repository.

For a clean source checkout, place that XML in `.repo/local_manifests/` before
syncing. Download Git LFS objects for the vendor repositories, including the
ready-to-use camera APK. Do not mix the Android 17 framework or vendor branches
with these Android 16 branches.

## Build

```sh
source build/envsetup.sh
lunch lineage_alioth-bp4a-user
m evolution -j6
```

Use the **user** variant. The output ZIP is under `out/target/product/alioth/`.
This is a full ROM build; no incremental OTA generation or device flashing is
part of these commands. Keep release-signing keys outside public repositories.

## Compatibility boundaries

- Keep the gated Xiaomi Camera2/session tag and legacy Dolby/AC-4 integrations.
- Keep SM8250 DisplayConfig, gralloc, C2D, audio battery listener, USB and NFC fixes.
- Use Android 16 audio client libraries for Wi-Fi Display; no Android 17
  `libaudiobase` dependency or audio common V5 dependency.
- Use Android 16's existing ion policy rather than requiring the Android 17
  `device/lineage/sepolicy/libion` include.
- Android 16 already uses legacy virtual-display frame pacing and guarded
  buffer abandonment. No Android 17 SurfaceFlinger backport is necessary.
- Keep Android 16's native screen recorder and normal 60 FPS/AVC 5.1 settings.
  The independent maximum-FPS backport follows the active display mode and codec
  limits when selected, without requiring recording blur suppression.
- Enable the separate recording blur policy. The Keep blur effects switch restores
  user choice; this feature can be omitted independently of maximum-FPS recording.
- Android 16 already reports charger limits from charger nodes and does not
  contain the newer DMA-BUF iterator path. Those Android 17 fixes are unnecessary.

The accepted camera mode limits are unchanged: main-camera 4K is supported;
ultrawide 4K remains guarded and ultrawide 1080p60 is not a claim of true 60 FPS.
Android 17 runtime acceptance does not establish Android 16 runtime validation.
