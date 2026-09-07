# AutoRemesher for Houdini

A Houdini SOP that runs [AutoRemesher](https://github.com/huxingyi/autoremesher), Jeremy Hu's open-source quad remesher, on Houdini geometry. Triangles in, quads out.

This repo is the remesher core (upstream 1.2, commit `3cb2012`) plus one HDK plugin.

**Changes to the core:** two values upstream hard-codes, the maximum quad aspect ratio and the adaptivity clamp range, are exposed as setters. Without them the Anisotropy and Gradient Adaptivity controls saturate at 1.0.

## Install

Download the zip from [Releases](https://github.com/JJ-9000/autoremesher-houdini/releases) and extract it into `Documents/houdini22.0/packages`. It adds one package file and one folder:

```
packages/autoremesher-houdini.json
packages/autoremesher-houdini/dso/SOP_AutoRemesher.dll
```

Restart Houdini. The node appears as **AutoRemesher** in the SOP tab menu. The DLL is built for Houdini 22.0.407 on Windows.

## Build

Houdini 22.0 with the HDK, Visual Studio 2022, CMake 3.20. From an x64 Native Tools prompt:

```cmd
cmake -B build -G "Visual Studio 17 2022" -A x64 -DHoudini_DIR="C:\Program Files\Side Effects Software\Houdini 22.0.407\toolkit\cmake"
cmake --build build --config Release
```

The core links against Houdini's own oneTBB and C++ runtime. The build writes `SOP_AutoRemesher.dll` into `Documents/houdini22.0/dso`, which Houdini also scans. Keep one copy or the other.

## Parameters

| Parameter | Default | Effect |
| --- | --- | --- |
| Target Quads | 5000 | Size hint. Results usually land below it. |
| Edge Scaling | 1.0 | Uniform quad size multiplier. |
| Gradient Adaptivity | 1.0 | How strongly quad size follows curvature, 0 to 1. |
| Anisotropy | 1.0 | Elongates quads along principal curvature. Above 1 needs Max Quad Aspect raised. |
| Sharp Edge Angle | 90° | Dihedral angle above which an edge is held as a feature. |
| Smooth Normal Angle | 0° | Crease threshold for smoothed normals during resampling. 0 disables. |
| Max Quad Aspect | 2.3 | Ceiling on aspect ratio. |
| Adaptivity Min / Max Ratio | 0.3 / 3.0 | Clamp on the per-face size multiplier. |
| Print Solver Stats | off | Writes the solver's phase report to stderr, which opens the Houdini console on Windows. |

## Limits

- Non-manifold input is rejected with an error. The core would crash the process on an edge shared by three faces. A VDB from Polygons and Convert VDB round trip repairs most scans.
- Progress shows in Houdini's status bar. A solve runs to completion once started; Escape is reported as a warning afterwards.
- Output can include a few non-quads. A 25k-triangle rubber toy at target 3000 gives 1169 quads and 2 pentagons.
- No attributes are carried through.

## License

MIT. Bundled: Eigen (MPL2), isotropicremesher (Apache 2.0), meshoptimizer (MIT). Each carries its licence in `thirdparty/`.
