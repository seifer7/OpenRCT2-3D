# openrct2-3d

All of the 3D viewer lives in this directory. The original OpenRCT2 sources stay as they are, apart from the small hooks that open this view.

bgfx is already in this folder, as pinned submodules:

- `third_party/bgfx.cmake` builds the library with CMake
- `third_party/bx`, `third_party/bimg`, and `third_party/bgfx` are the renderer sources

The pins match `bgfx.cmake` `cmake/dependencies.cmake`. Clone this repository with `git submodule update --init --recursive` so those directories are present.

`openrct2-3d.vcxproj` is the Visual Studio project. It is part of `openrct2.sln` and produces `openrct2-3d.lib`, which `openrct2-win` links. It compiles this folder's sources together with the amalgamated bx and bgfx sources and `bimg`'s `image.cpp`. Only bgfx's OpenGL renderer is enabled there, matching the context this viewer creates on Windows. If those submodule directories are empty, the project fetches bx, bimg, bgfx, and bgfx.cmake before it compiles. CMake remains the build used on Linux, macOS, and the mingw job.

This folder will also hold:

- the read-only scene extractor
- meshes, materials, and the camera
- the pass that draws the original UI over the 3D park

The game still simulates in `gameStateTick()` and still draws with the original 2D renderer unless the View menu option is on. See [docs/3d-viewer.md](../../docs/3d-viewer.md).
