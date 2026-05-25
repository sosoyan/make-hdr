# MakeHDR

![MakeHDR icon](./icons/net.sf.openfx.make_hdr.png)

MakeHDR is an OpenFX plug-in for merging multiple LDR images into a single HDRI.

## Feature Notes

* Merge up to 16 inputs with 8, 10 or 12 bit depth processing
* User-friendly logarithmic tone-mapping controls
* Advanced controls such as sampling rate and smoothness

Available cross‑platform on Linux, macOS, and Windows; works consistently in compositing applications such as Nuke, Fusion, and Natron.

## Installation

Download the [latest release](https://github.com/sosoyan/make-hdr/releases) and copy the `MakeHDR.ofx.bundle` directory to your custom `OFX_PLUGIN_PATH` or your system's default OpenFX plugin folder:

* Linux: `/usr/OFX/Plugins`
* macOS: `/Library/OFX/Plugins`
* Windows: `C:\Program Files\Common Files\OFX\Plugins`

## How to Use

1. Create a `MakeHDR` node within your DCC app.
2. Connect your source images, shot with multiple shutter speeds.
3. Fill in the exposure times in seconds for the appropriate inputs (e.g., for a 1/250 shutter speed, enter 0.004).

[![Create ACES HDRIs in NukeX using MakeHDR and CaraVR](https://img.youtube.com/vi/yTeBWqiZiTs/mqdefault.jpg)](https://www.youtube.com/watch?v=yTeBWqiZiTs)

## Build from Source

If you prefer to compile the plugin yourself, ensure you have CMake ≥ 3.5 and a C++14-compatible compiler ready on your system. Then run:

```sh
git clone --recursive https://github.com/sosoyan/make-hdr.git
cd make-hdr && mkdir build && cd build
cmake .. && sudo make install
```

## Parameter Reference

### Exposure Times

| Parameter | Description |
|----------:|:------------|
| `calibrate response` | Estimates the non-linear response curve from the source images. When disabled, assumes the source images are already linear. |
| `1–16` | Defines the exposure time in seconds for each corresponding connected image source. |

### Tone Mapping

| Parameter | Description |
|----------:|:------------|
| `target middle gray` | Automatically adjusts global exposure so the average scene luminance matches the `middle gray` value. |
| `middle gray` | Defines a reference gray from the scene (e.g., sampled from a colour checker) to scale the merged image's luminance. |
| `exposure` | Applies a global exposure adjustment in stops to the merged HDR image before tone mapping. |
| `gamma` | Applies a gamma exponent per-pixel after HDR merging, where 1.0 keeps linear output. |
| `highlights` | Blends between fully compressed (0) and uncompressed linear (1) output. Lower values roll off bright areas more aggressively. |

### Advanced

| Parameter | Description |
|----------:|:------------|
| `show samples` | Displays the sample positions on the output image as bright green pixels. |
| `samples` | Defines the number of calibration samples. For `Debevec`, it specifies the exact number of points. For `Robertson`, the value is multiplied by 100 to compensate for sparse bin coverage. |
| `solver` | [Debevec](https://www.pauldebevec.com/Research/HDR/) solves a linear system (fast, sparse samples). [Robertson](https://doi.org/10.1109/ICIP.1999.817091) uses an iterative expectation-maximisation approach. |
| `smoothness / iterations` | For `Debevec`, higher values force a smoother response curve. For `Robertson`, it sets the maximum number of expectation-maximisation iterations. |
| `input depth` | Determines the internal histogram resolution (`256`, `1024`, or `4096` bins). Match this to your camera's original bit depth. |
| `log level` | Controls plugin logging verbosity. `off` silences all logging, while `debug` prints detailed per-frame diagnostics. |