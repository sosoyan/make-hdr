# MakeHDR 

![plot](./icons/net.sf.openfx.make_hdr.png)

MakeHDR is an OpenFX plug-in for merging multiple LDR images into a single HDRI.

## Feature notes
* Merge up to 16 inputs with 8, 10 or 12 bit depth processing
* User friendly logarithmic Tone Mapping controls within the tool
* Advanced controls such as Sampling rate and Smoothness

Available at cross platform on Linux, MacOS and Windows
Works consistent in compositing applications like Nuke, Fusion, Natron. 

## How to Build
```
git clone https://github.com/Sosoyan/make-hdr.git && cd make-hdr
git submodule update --init
mkdir build && cd build
cmake .. && make install
```

## How to Install
Copy make_hdr.ofx.bundle directory to your system's default OFX_PLUGIN_PATH or where the environment variable is set to.
- Linux: /usr/OFX/Plugins
- MacOS: /Library/OFX/Plugins
- Windows: C:\Program Files\Common Files\OFX\Plugins

## How to Use
1. Create MakeHDR node within your DCC app.
2. Connect your source images shot with multiple shutter speed up to 16 inputs.
3. Fill the details of exposure times in seconds for appropriate inputs, i.e. for 1/250 shutter speed enter 0.004.

[![Create ACES HDRIs in NukeX using MakeHDR and CaraVR](https://img.youtube.com/vi/yTeBWqiZiTs/0.jpg)](https://www.youtube.com/watch?v=yTeBWqiZiTsE)

## Node Parameters Reference
exposure_times
- 1-16: Shutter speeds of corresponding source input in seconds

tone_mapping
- exposure: Exposure aka f-stop offset
- gamma: Gamma correction
- highlights: Logarithmic highlights compensation

advanced
- show samples: Show sample pixels for debugging purposes.
- input depth: 8, 10 or 12 bit input image processing.
- sampling: Sampling count squared, 8 means 64 total samples.
- smoothness: Normalized smoothing of the response curve.
- log level: Log verbosity level of the node
