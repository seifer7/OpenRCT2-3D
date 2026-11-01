# Copyright (c) 2026 OpenRCT2 developers
# Licensed under the GNU General Public License version 3.

"""Mesh archetypes. Each one turns a params dict and an atlas tile map into a mesh."""

from __future__ import annotations

def build_mesh(archetype, params, tiles):
    if archetype == "conifer":
        from . import conifer

        return conifer.build(params, tiles)
    raise ValueError(
        f"unknown archetype {archetype!r}. Implemented: conifer "
        "(the Corsican Pine template). Add a module beside conifer.py for a new shape."
    )
