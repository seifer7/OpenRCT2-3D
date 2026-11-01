# Object models

GLB files for the 3D view. The game looks here, under the OpenRCT2 data directory (`--openrct2-data-path`, the same directory that contains `object/` and `g2.dat`), and then under the user directory.

The file name is the object identifier plus `.glb`:

```text
object-models/rct2.scenery_small.tcrp.glb
```

That is the Corsican Pine. `rct2.scenery_small.tcrp.meta.json` records how it was authored. The loader does not read the meta file. Scale, axes, and materials come from `src/openrct2-3d/src/ViewerScale.hpp` and from the GLB.

A model is glTF Y-up. One Blender unit is one tile. The origin is the tile centre, and Z = 0 in Blender is the tile base. Foliage should be alpha-tested (`MASK`, cutoff 0.5), nearest filtered, without mipmaps, and double-sided. Objects with no file here use the original four direction sprites on a crossed pair of quads.

On startup the game converts each `.glb` into `<cache>/object-models/<identifier>.o3d` (format version 2). The cache directory is the user cache (`DirBase::cache` on Linux, `~/.cache/OpenRCT2`), never the install data directory. The header stores the source size, modification time, and content hash, a checksum of the converted body, the converter version (`kGlbConverterVersion`), and the mesh size limits. A file is skipped when all of those still match. Drawing trusts size and modification time and only hashes the GLB when one of them changed. A bad GLB is logged and that object uses the sprite cross. Identifiers that are Windows device names (`CON`, `PRN`, `AUX`, `NUL`, `COM1`–`COM9`, `LPT1`–`LPT9`) or contain `:` are ignored.

Linux install copies this directory with `install(DIRECTORY "data/")`. A Visual Studio build copies it into `bin\data\object-models` from `openrct2.targets`, the same way as `data\shaders`.

How to add another object, and what this first milestone leaves out, is in [docs/agents/scenery-3d-milestone.md](../../docs/agents/scenery-3d-milestone.md).
