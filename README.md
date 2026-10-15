# PICO Window Resize

An LSPosed module that lets you resize floating Android app windows on PICO 4. Hold the controller trigger just outside a window edge or corner, then drag. App content reflows, the opposite edge stays anchored, and horizontal resizing keeps the window facing its original viewing center.

Window dimensions and scale can be remembered per app and restored when reopened. The module includes a toggle for remembering sizes and a button to forget them.

Below the settings, compatibility diagnostics show the installed XRShell version, native binary SHA-256, loaded ELF build ID, and live results for exported targets, panel layout, accessor ABI, relocations, geometry and hook installation. Unknown binaries can still pass the live checks; stale reports and pending restarts are marked explicitly.

Developed on **PICO 4 / Phoenix, Android 10, arm64**. PICO OS **5.13.3, 5.13.7 and 5.13.8** have identical relevant shell components, verified offline. Version 0.6.1 finds native hooks through exported symbols and relocation tables, derives panel offsets from validated instructions, and checks accessor ABI contracts instead of requiring one firmware build ID. Changed implementations can still require support; this does not guarantee compatibility with other headsets or future updates. Native discovery and hook installation have been verified on a PICO 4 running 5.13.7. Some apps enforce their own layout or orientation.

## Install

1. Download the APK from this repository's **Releases** and install it on the headset.
2. In LSPosed, enable the module for:
   - XRShell (`com.picoxr.xrshell`)
   - SystemExt (`com.picovr.systemext`)
3. Reboot the headset.
4. Open PICO Border Resize and enable **outside-border resizing**.

Requires a rooted headset with LSPosed. Drag an edge to change one dimension, or a corner to change both. Turning the module off restores normal input bounds; open windows keep their current size.

## Build

Install Python 3, a JDK with `javac` and `keytool` (JDK 17 or newer), and these Android SDK packages:

- `platforms;android-29`
- `build-tools;35.0.0`
- `ndk;27.1.12297006`

Set `ANDROID_SDK_ROOT` to your Android SDK directory, then run:

```sh
python3 fetch-deps.py
python3 build.py
```

Alternatively, pass the SDK directory with `python3 build.py --sdk /path/to/android-sdk`.

The signed APK is written to `dist/`. The builder creates a local development signing key in `build/research-debug.p12`; keep it for future updates signed with the same key. To reuse an existing development key, add `--keystore /path/to/research-debug.p12`. Build output, dependencies and signing keys are excluded from Git. The Xposed API is used only for compilation and is not bundled in the APK.

Run the host tests with:

```sh
python3 tests/run.py
```

The native tests also require a host C++ compiler (`c++`).
