# Object models

GLB files for the 3D view. The game looks here, under the OpenRCT2 data directory (`--openrct2-data-path`, the same directory that contains `object/` and `g2.dat`), and then under the user directory.

The file name is the object identifier plus `.glb`:

```text
object-models/rct2.scenery_small.tcrp.glb
```

That is the Corsican Pine. `rct2.scenery_small.tcrp.meta.json` records how it was authored. The loader does not read the meta file. Scale, axes, and materials come from `src/openrct2-3d/src/ViewerScale.hpp` and from the GLB.

A model is glTF Y-up. One Blender unit is one tile. The origin is the tile centre, and Z = 0 in Blender is the tile base. Foliage should be alpha-tested (`MASK`, cutoff 0.5), nearest filtered, without mipmaps, and double-sided. Objects with no file here use the original four direction sprites on a crossed pair of quads.

On startup the game converts each `.glb` into `<cache>/object-models/<identifier>.o3d`. The cache directory is the user cache (`DirBase::cache` on Linux, `~/.cache/OpenRCT2`), never the install data directory. A file is skipped when its size, modification time, content hash, and cache format version still match. A bad GLB is logged and that object uses the sprite cross.

Linux install copies this directory with `install(DIRECTORY "data/")`. A Visual Studio build copies it into `bin\data\object-models` from `openrct2.targets`, the same way as `data\shaders`.

How to add another object, and what this first milestone leaves out, is in [docs/agents/scenery-3d-milestone.md](../../docs/agents/scenery-3d-milestone.md).
