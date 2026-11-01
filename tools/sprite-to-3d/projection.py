# Copyright (c) 2026 OpenRCT2 developers
# Licensed under the GNU General Public License version 3.

"""RCT2 four-direction projection used by comparison renders.

The game projects a rotated world point with
screen = (y' - x', (x' + y') / 2 - z') in sim pixels
(Viewport.cpp Translate3DTo2DWithZ; the game truncates the half-sum).

A 30 degree orthographic camera, with the model scaled on Z by sqrt(2/3)
for the render only, lands on the same pixels. Exported files stay at
sim/32 with no squash. See conventions.Z_SQUASH.
"""

from __future__ import annotations

import math

import numpy as np

from conventions import ORIGIN_PIXEL, RENDER_SIZE, SIM_PER_UNIT, Z_SQUASH

# Camera azimuths for views 0..3. The horizontal aim is (d0, d1); the
# vertical component is Z_SQUASH, which is exactly 30 degrees elevation
# because tan(30) = 1/sqrt(3) and the horizontal length of (1, 1) is sqrt(2).
VIEW_DIRS = ((1.0, 1.0), (-1.0, 1.0), (-1.0, -1.0), (1.0, -1.0))

# Pixels per Blender unit along the camera's horizontal axis.
# A tile diamond is 64 px across and sqrt(2) units along that diagonal.
PIXELS_PER_UNIT = SIM_PER_UNIT * math.sqrt(2.0)


def rotate_xy(x, y, view):
    """CoordsXY::rotate for a view rotation in 0..3."""
    if view == 0:
        return x, y
    if view == 1:
        return y, -x
    if view == 2:
        return -x, -y
    if view == 3:
        return -y, x
    raise ValueError(f"view must be 0..3, got {view}")


def rct_screen(sim_xyz, view=0):
    """Continuous RCT screen position in pixels, relative to the model origin.

    sim_xyz is in sim units, Z up, origin at the tile centre and the element base.
    """
    x, y, z = sim_xyz
    x, y = rotate_xy(x, y, view)
    return (y - x, (x + y) / 2.0 - z)


def rct_pixel(sim_xyz, view=0):
    """Pixel in the comparison image (y down). Origin sits on ORIGIN_PIXEL."""
    sx, sy = rct_screen(sim_xyz, view)
    return (ORIGIN_PIXEL[0] + sx, ORIGIN_PIXEL[1] + sy)


def _camera_basis(view):
    d0, d1 = VIEW_DIRS[view]
    direction = np.array([d0, d1, Z_SQUASH], dtype=np.float64)
    direction /= np.linalg.norm(direction)
    # Camera sits on this ray looking at the origin. +Z of the camera points
    # back along the ray (Blender cameras look down local -Z). World up is +Z.
    z_axis = direction
    world_up = np.array([0.0, 0.0, 1.0])
    x_axis = np.cross(world_up, z_axis)
    x_axis /= np.linalg.norm(x_axis)
    y_axis = np.cross(z_axis, x_axis)
    eye = direction * 20.0
    return eye, x_axis, y_axis


def camera_pixel(blender_xyz, view=0):
    """Project one Blender-space point (Z up, units) through the squashed camera.

    Z is multiplied by Z_SQUASH here. The point itself is not modified.
    """
    eye, x_axis, y_axis = _camera_basis(view)
    point = np.array(
        [blender_xyz[0], blender_xyz[1], blender_xyz[2] * Z_SQUASH], dtype=np.float64
    )
    rel = point - eye
    cx = float(np.dot(rel, x_axis))
    cy = float(np.dot(rel, y_axis))
    width, height = RENDER_SIZE
    u = width / 2.0 + cx * PIXELS_PER_UNIT
    # Positive Blender shift_y of 64/192 moves the origin from the frame
    # centre (64, 96) down to pixel (64, 160).
    v = height / 2.0 - cy * PIXELS_PER_UNIT + 64.0
    return (u, v)


def camera_depth(blender_xyz, view=0):
    """Distance from the camera along its view axis, for the same squashed camera.

    Smaller is nearer. The camera is orthographic, so this is affine in the
    point and can be interpolated linearly across a projected triangle.
    """
    eye, _x_axis, _y_axis = _camera_basis(view)
    d0, d1 = VIEW_DIRS[view]
    forward = -np.array([d0, d1, Z_SQUASH], dtype=np.float64)
    forward /= np.linalg.norm(forward)
    point = np.array(
        [blender_xyz[0], blender_xyz[1], blender_xyz[2] * Z_SQUASH], dtype=np.float64
    )
    return float(np.dot(point - eye, forward))


def pixels_match_rct(samples=None):
    """Return the max pixel error between the squashed camera and the RCT formula."""
    if samples is None:
        samples = [
            (0, 0, 0),
            (32, 0, 0),
            (0, 32, 0),
            (0, 0, 32),
            (16, 16, 8),
            (-10, 20, 40),
            (7, -12, 100),
        ]
    worst = 0.0
    for view in range(4):
        for sim in samples:
            blender = (sim[0] / SIM_PER_UNIT, sim[1] / SIM_PER_UNIT, sim[2] / SIM_PER_UNIT)
            cam = camera_pixel(blender, view)
            rct = rct_pixel(sim, view)
            worst = max(worst, math.hypot(cam[0] - rct[0], cam[1] - rct[1]))
    return worst
