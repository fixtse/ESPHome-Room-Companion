#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Generate Room Companion desktop enclosure STLs and optional CAD previews."""

import argparse
from functools import lru_cache
from dataclasses import dataclass
from math import radians, tan
from pathlib import Path

import cadquery as cq

# XY lies in the screen plane, +Y is up, +Z is toward the back.
# The dimensions below are in each vendor STEP's original coordinates.
@dataclass(frozen=True)
class Panel:
    name: str
    width: float
    height: float
    active: tuple[float, float]
    step_center_x: float
    step_glass_z: float
    usb: tuple[float, float, float]  # Receptacle mouth center and outward axis +X


PANELS = (
    Panel("ONX2432G028", 56, 78.6, (43.2, 57.6), -2.8, -7.295, (34.625, -0.15, 1.598)),
    Panel("ONX3248G035", 61, 92.44, (48.96, 73.44), 0.22, -7.213, (39, 0, 1.58)),
)
FACE = 1.8
TAPE = 0.25
TILT = 16
WALL = 2.4
LID = 2.8
EPS = 0.04


def block(x, y, z, w, h, d):
    return cq.Solid.makeBox(w, h, d, cq.Vector(x - w / 2, y - h / 2, z))


def rounded(x, y, z, w, h, d, r):
    return (
        cq.Workplane("XY", origin=(x, y, z))
        .sketch().rect(w, h).vertices().fillet(r).finalize()
        .extrude(d).val()
    )


def hole(x, y, z, diameter, depth):
    return cq.Solid.makeCylinder(diameter / 2, depth, cq.Vector(x, y, z))


def section(x, width, points):
    """Extrude a YZ profile in +X."""
    return cq.Workplane("YZ", origin=(x, 0, 0)).polyline(points).close().extrude(width).val()


def make_case(panel, portrait, audio):
    width, height = (panel.width, panel.height) if portrait else (panel.height, panel.width)
    case_width = width + 16
    bottom = -height / 2 - 7
    top = height / 2 + 7
    middle = (top + bottom) / 2
    case_height = top - bottom
    rear = 40 if audio else 22
    rear_outer = rear + LID
    screw_x = case_width / 2 - 4.3
    screws = [(x, y) for x in (-screw_x, screw_x) for y in (bottom + 6, top - 6)]
    usb_x, usb_y, usb_z = panel.usb
    usb_x -= panel.step_center_x
    usb_z += FACE + TAPE - panel.step_glass_z
    # The 3.5-inch assembly narrows the first few millimetres behind the
    # receptacle. The broad overmold can start just past this neck.
    if panel.name == "ONX3248G035":
        gauge = block(usb_x + 3.5, usb_y, usb_z - 2.75, 4, 12.5, 5.5).fuse(
            block(usb_x + 19, usb_y, usb_z - 5, 27, 18, 10)
        )
    else:
        gauge = block(usb_x + 17, usb_y, usb_z - 5, 31, 18, 10)
    if portrait:
        port_x = usb_y
        usb_relief = block(port_x, bottom - 0.5, usb_z - 6, 21, 8, 12)
        gauge = gauge.rotate((0, 0, 0), (0, 0, 1), -90)
    else:
        port_y = usb_y
        usb_relief = block(case_width / 2 + 0.5, port_y, usb_z - 6, 8, 21, 12)

    outer = rounded(0, middle, 0, case_width, case_height, rear, 5)
    interior = rounded(
        0, middle, FACE + 2.2, case_width - 2 * WALL, case_height - 2 * WALL, rear, 3
    )
    # Pocket and lip capture the glass from the rear with removable thin tape.
    seat = rounded(0, 0, FACE, width + 0.8, height + 0.8, 3, 2.9)
    window = rounded(0, 0, -EPS, width - 2.6, height - 2.6, FACE + 2 * EPS, 1.5)
    body = outer.cut(interior, seat, window)
    # Grow the bosses from the front pocket so they print with support from below.
    bosses = [hole(x, y, FACE, 6.8, rear - FACE) for x, y in screws]
    body = body.fuse(*bosses).cut(
        seat, usb_relief,
        *(hole(x, y, rear - 9, 2.6, 9 + EPS) for x, y in screws),
    )

    cover = rounded(0, middle, rear, case_width, case_height, LID, 5)
    # A shallow locating rim aligns the cover while leaving the screw bosses clear.
    rim_outer = rounded(
        0, middle, rear - 1.2, case_width - 2 * WALL - 0.65,
        case_height - 2 * WALL - 0.65, 1.2 + EPS, 2.8,
    )
    rim_inner = rounded(
        0, middle, rear - 1.2 - EPS, case_width - 2 * WALL - 3.15,
        case_height - 2 * WALL - 3.15, 1.2 + 3 * EPS, 1.6,
    )
    rim = rim_outer.cut(rim_inner, *(hole(x, y, rear - 2, 7.6, 3) for x, y in screws))
    cover = cover.fuse(rim).cut(*(hole(x, y, rear - EPS, 3.4, LID + 2 * EPS) for x, y in screws))
    vent_rows = [top - 8 - i * 3.6 for i in range(3)]
    if not audio:
        vent_rows += [bottom + 8 + i * 3.6 for i in range(3)]
    vents = [rounded(0, y, rear - EPS, 31, 1.9, LID + 2 * EPS, 0.9) for y in vent_rows]
    cover = cover.cut(*vents)

    if audio:
        # The mic lies inside the top wall, behind the display and facing up.
        mic_z = 27
        mic_ports = [
            cq.Solid.makeCylinder(
                0.8, WALL + 2 * EPS,
                cq.Vector(mic_x + x, top - WALL - EPS, mic_z + z),
                cq.Vector(0, 1, 0),
            )
            for mic_x in (-20, 20) for x in (-2.8, 0, 2.8) for z in (-2.8, 0, 2.8)
        ]
        body = body.cut(*mic_ports)
        mic_space = block(0, top - WALL - 3.25, mic_z - 7, 48, 6, 14)
        if body.intersect(mic_space).Volume() > 0.05:
            raise ValueError("Top microphone space intersects the bezel")
        # The Nextion BOX Speaker's tabs are on a 36.6 mm pitch. Its own
        # acoustic enclosure faces the grille with a thin foam gasket.
        speaker_y = bottom + 25
        grill = [
            hole(x, speaker_y + y, rear - EPS, 2.2, LID + 2 * EPS)
            for x in range(-10, 11, 4) for y in range(-10, 11, 4)
            if x * x + y * y < 135
        ]
        speaker_tabs = [(-18.3, speaker_y), (18.3, speaker_y)]
        cover = cover.fuse(*(hole(x, y, rear - 6.5, 5.4, 6.5 + EPS) for x, y in speaker_tabs))
        cover = cover.cut(
            *grill, *(hole(x, y, rear - 6.5 - EPS, 1.7, 5.9) for x, y in speaker_tabs),
        )

    # The stand screws share the lower cover bosses. The front of each foot
    # reaches below the screen footprint and the rear crossbar leaves the
    # USB-C approach open between the feet.
    slope = tan(radians(TILT))
    clearance = 33 if portrait else 3
    tail_z = rear_outer + (40 if portrait else 30)
    toe_z = 0.6
    def floor(z):
        return bottom - clearance + (z - rear_outer) * slope

    if min(vertex.Y - floor(vertex.Z) for vertex in gauge.Vertices()) < 3:
        raise ValueError("USB-C overmold is too close to the desktop")

    corners = [
        (floor(toe_z), toe_z), (floor(tail_z), tail_z), (floor(tail_z) + 5, tail_z),
        (bottom + 14, rear_outer + 5.5), (bottom + 14, rear_outer),
        (bottom - 0.8, rear_outer), (bottom - 0.8, rear_outer - 2),
        (floor(toe_z) + 3.5, toe_z),
    ]
    foot_width = 10.8
    cross_z = tail_z - 5
    brace = section(-screw_x, 2 * screw_x, [
        (floor(cross_z), cross_z), (floor(tail_z), tail_z),
        (floor(tail_z) + 4, tail_z), (floor(cross_z) + 4, cross_z),
    ])
    stand = brace.fuse(*(section(x - foot_width / 2, foot_width, corners) for x in (-screw_x, screw_x)))
    for x in (-screw_x, screw_x):
        stand = stand.cut(
            hole(x, bottom + 6, rear_outer - EPS, 3.5, 5.5 + 2 * EPS),
            hole(x, bottom + 6, rear_outer + 5.5 - EPS, 6.5, tail_z - rear_outer),
        )
    return (body.clean(), cover.clean(), stand.clean()), gauge


def export(parts, gauge, panel, portrait, audio, output, previews):
    orientation = "portrait" if portrait else "landscape"
    trim = "audio" if audio else "plain"
    stem = f"{panel.name.lower()}-{orientation}-{trim}"
    body, cover, stand = parts
    # Bezel front, cover exterior, and stand's desk face go on the bed.
    printable = {
        "bezel": body,
        "back": cover.rotate((0, 0, 0), (1, 0, 0), 180),
        "stand": stand.rotate((0, 0, 0), (1, 0, 0), 90 + TILT),
    }
    for name, part in printable.items():
        if len(part.Solids()) != 1 or not part.isValid():
            raise ValueError(f"{stem}-{name} must be one valid solid")
        part = part.translate((0, 0, -part.BoundingBox().zmin))
        cq.exporters.export(part, str(output / "stl" / f"{stem}-{name}.stl"), tolerance=0.06)
    if previews:
        from render import render_case
        render_case(panel, portrait, audio, parts, gauge, output / "renders" / f"{stem}.png")


@lru_cache(maxsize=2)
def read_board(path):
    return cq.importers.importStep(str(path)).val()


def verify_fit(panel, portrait, parts, gauge, step_directory):
    board = read_board(step_directory / f"{panel.name}.stp")
    board = board.translate((-panel.step_center_x, 0, FACE + TAPE - panel.step_glass_z))
    if portrait:
        board = board.rotate((0, 0, 0), (0, 0, 1), -90)
    for part in parts:
        if part.distance(board) < 0.02:
            raise ValueError(f"{type(part).__name__} touches the reference board")
        if part.intersect(gauge).Volume() > 0.05:
            raise ValueError("Case obstructs the nominal USB-C plug")
    if gauge.distance(board) < 0.02:
        raise ValueError("The nominal USB-C overmold crosses the reference board")
    for i, part in enumerate(parts):
        for other in parts[i + 1:]:
            if part.intersect(other).Volume() > 0.05:
                raise ValueError("Case parts interfere when assembled")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument("--model", choices=[panel.name for panel in PANELS])
    parser.add_argument("--orientation", choices=["portrait", "landscape"])
    parser.add_argument("--trim", choices=["audio", "plain"])
    parser.add_argument("--previews", action="store_true")
    parser.add_argument("--check-step", type=Path, metavar="DIRECTORY",
                        help="check fit using <model>.stp from OpenNextion")
    args = parser.parse_args()
    (args.output / "stl").mkdir(parents=True, exist_ok=True)
    if args.previews:
        (args.output / "renders").mkdir(parents=True, exist_ok=True)
    for panel in PANELS:
        if args.model and args.model != panel.name:
            continue
        for orientation in ("portrait", "landscape"):
            for trim in ("plain", "audio"):
                if args.orientation and args.orientation != orientation:
                    continue
                if args.trim and args.trim != trim:
                    continue
                print(panel.name, orientation, trim, flush=True)
                portrait = orientation == "portrait"
                audio = trim == "audio"
                parts, gauge = make_case(panel, portrait, audio)
                if args.check_step:
                    verify_fit(panel, portrait, parts, gauge, args.check_step)
                export(parts, gauge, panel, portrait, audio, args.output, args.previews)


if __name__ == "__main__":
    main()
