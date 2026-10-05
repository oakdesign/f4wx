# F4Wx

```
  ______ _  ___          __     
 |  ____| || \ \        / /     
 | |__  | || |\ \  /\  / /_  __ 
 |  __| |__   _\ \/  \/ /\ \/ / 
 | |       | |  \  /\  /  >  <  
 |_|       |_|   \/  \/  /_/\_\ 
```

**F4Wx** is a real weather tool for BMS 4.36+ that lets you download and convert real weather (GRIB2) into the simulator in a user-friendly way.

This [oakdesign fork](https://github.com/oakdesign/f4wx) adds a visual editor for creating custom weather maps and editing existing maps or downloaded weather. The first editor release is **2.3.0-beta.1**, intended for testing with Falcon BMS 4.38. Report editor bugs in this fork's [issue tracker](https://github.com/oakdesign/f4wx/issues).

For questions, bug reports, and feature suggestions, see the release thread on the BMS Community MODs/WIP forum.

---

## Credits and copyright

F4Wx is Copyright 2016–2026 **[syn111](https://github.com/syn111)** (Callsign: Ahmed).

This repository includes third-party code; see [NOTICE](NOTICE) for a list of components and their licenses.

---

## Requirements

- Windows 7 or Windows Server 2008 R2 or later
- [Visual C++ Redistributable (x64)](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist) — the standard VC runtime (2015–2022 / latest supported)

---

## Installation (end users)

1. Extract all contents to a folder of your choice.
2. Run **F4Wx.exe**.

Generated `.fmap` files: single files go in your campaign directory (load manually in BMS); sequences go in the **WeatherMapsUpdates** directory inside the campaign folder and are loaded by BMS at the appropriate time.

See [f4wx/README.txt](src/f4wx/README.txt) for the full user guide, FAQ, and bug reporting.

---

## Creating and editing weather

Select the theater, then choose **New Weather**, **Open fmap**, or **Edit Current**.
The editor works on one independent map; editing a downloaded frame leaves the real-weather forecast available in the main window.

- **New** fills a map using the selected Sunny, Fair, Poor, or Inclement preset. Presets set weather type, clouds, visibility, and fog altitude while preserving temperature, pressure, and winds when painted.
- Choose a paint field and value, then drag on the theater. Use radius **0** to change exactly one cell. Numeric fields support strength and soft edges; categories, density, and flags use replacement.
- Choose **Rectangle** to fill a region, **Fill whole map** to apply the current field everywhere, or **Eyedropper** / right click to inspect a cell and pick its value.
- Use the wheel to zoom and middle drag to pan. **Undo** and **Redo** operate on complete strokes, fills, and map-wide changes.
- Wind painting affects the selected altitude only. Stratus heights, contrail heights, and map wind are separate map-wide settings. Map wind stays manual unless **Derive map wind from 12000 ft** is used.
- **Save As** exports a version-8 `.fmap`. Load it manually in BMS from the campaign directory. The editor prompts before discarding changed maps.

Imported maps must use version 8 and match the selected theater's grid dimensions. An fmap contains no theater identity, so select the correct theater yourself. The editor validates weather values using the ranges displayed in its controls. Sequence editing is not included.

## Building from source

1. Clone the repository (including submodules):
   ```bash
   git clone --recurse-submodules https://github.com/oakdesign/f4wx
   ```
   If you already cloned without submodules, run:
   ```bash
   git submodule update --init --recursive
   ```
2. Open **f4wx.sln** in Visual Studio 2017 or later.
3. Select the **f4wx** project and build (e.g. Release | x64).

The solution also includes:

- **g2c** – NCEPLIBS-g2c static library for GRIB2 decode (built automatically as a dependency of f4wx).

Dependencies (g2c) are Git submodules in `dependencies/`. GRIB2 support is provided by [NCEPLIBS-g2c](https://github.com/NOAA-EMC/NCEPLIBS-g2c) (built as a static lib with PNG/Jasper disabled for a single portable .exe).

Run `./tests/run_weather_tests.ps1` from PowerShell to compile and run the weather editor data tests (debug and release), Win32 control tests, and a canvas render check. Pass `-VisualStudioPath` to choose a compiler installation explicitly. Test artifacts are written to `obj/editor-tests`.

After building Release, run `./tests/release_smoke_test.ps1` to check the beta's startup, New Weather action, and normal shutdown. Use `-ExecutablePath` and `-ExpectedVersion` for another build.

---

## License

This project is licensed under the **Apache License 2.0**.  
See the [LICENSE](LICENSE) file for the full text.

You may use and reuse this code, including in proprietary projects, under the terms of that license. Attribution must be preserved as described in the license and in [NOTICE](NOTICE).

---

## Third-party code

This repository contains or links to third-party software. Copyright and license information for each component are listed in [NOTICE](NOTICE).
