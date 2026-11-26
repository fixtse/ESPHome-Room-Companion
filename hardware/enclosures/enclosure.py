#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Generate Room Companion desktop enclosure STLs and optional CAD previews."""

import argparse
from dataclasses import dataclass
from functools import lru_cache
from math import atan, degrees, radians, tan
from pathlib import Path

import cadquery as cq
from OCP.BRepExtrema import BRepExtrema_DistShapeShape

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
    buttons: tuple[tuple[float, float, float], ...]  # Button centers at the outward face
    power: tuple[float, float, float]  # Center of slider travel
    power_position: float  # Paddle offset from mid-travel in the vendor STEP


PANELS = (
    Panel(
        "ONX2432G028", 56, 78.6, (43.2, 57.6), -2.8, -7.295, (34.625, -0.15, 1.598),
        ((9.25, -24.75, 0.968), (15.1, -24.75, 0.968)), (22.65, -24.938, 0.718), 0.75,
    ),
    Panel(
        "ONX3248G035", 61, 92.44, (48.96, 73.44), 0.22, -7.213, (39, 0, 1.58),
        ((21.15, 27, 0.95), (29.15, 27, 0.95)), (24, -27.3, 0.7), -0.75,
    ),
)
FACE = 1.8
TAPE = 0.25
TILT = 16
WALL = 2.4
LID = 3.4
TAPER = 0.34
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


def make_controls(panel, portrait, case_width, top, floor):
    """Captive plungers and a fork that slides over the original power tab."""
    apertures, assembled, endpoints = [], [], []
    printable = {}
    for index, position in enumerate((*panel.buttons, panel.power)):
        slider = index == len(panel.buttons)
        x, y, z = position
        x -= panel.step_center_x
        z += FACE + TAPE - panel.step_glass_z
        direction = 1 if y > 0 else -1
        underside = not portrait and direction < 0
        face = direction * case_width / 2 if portrait else (floor(z) if underside else top)
        reach = direction * (face - y)
        tip = reach - 1.05 if underside else reach + 0.85
        shoulder = reach - WALL - (1.1 if underside else 0.35)

        def place(shape, shift=0):
            placed = shape.rotate((0, 0, 0), (1, 0, 0), -90 * direction).translate(
                (x + shift, y, z)
            )
            if portrait:
                placed = placed.rotate((0, 0, 0), (0, 0, 1), -90)
            return placed

        if slider:
            opening = block(0, 0, -EPS, 8.3, 4.0, reach + 8)
            if underside:
                opening = opening.fuse(block(0, 0, reach - 1.7, 9.6, 5.2, 6))
            stem = block(0, 0, -0.65, 5.8, 3.4, tip + 0.65)
            flange = block(0, 0, shoulder - 1.1, 6.2, 5.6, 1.1)
            fork = block(0, 0, -0.7, 1.8, 3.6, 0.98)
            # Add shallow grip grooves to the exposed end of the slider.
            grooves = [
                block(0, offset, tip - 0.23, 3.9, 0.42, 0.4)
                for offset in (-0.8, 0, 0.8)
            ]
            cap = stem.fuse(flange).cut(fork, *grooves).clean()
            printable["power-slider"] = cap
            assembled.append(place(cap, panel.power_position))
            endpoints.extend(place(cap, shift) for shift in (-1.05, 1.05))
        else:
            opening = hole(0, 0, -EPS, 4.7, reach + 8)
            if underside:
                opening = opening.fuse(hole(0, 0, reach - 1.7, 5.55, 6))
            cap = hole(0, 0, 0.2, 4.2, tip - 0.2).fuse(
                hole(0, 0, shoulder - 0.9, 5.4, 0.9)
            ).clean()
            printable["button"] = cap
            assembled.append(place(cap))
            endpoints.append(place(cap.translate((0, 0, -0.45))))
        apertures.append(place(opening))
    return apertures, assembled, endpoints, printable


def make_case(panel, portrait, audio):
    width, height = (panel.width, panel.height) if portrait else (panel.height, panel.width)
    case_width = width + 16
    bottom = -height / 2 - 7
    top = height / 2 + 7
    floor_at_face = bottom - 39 if portrait else bottom + 0.8
    slope = tan(radians(TILT))
    top_depth = 35 if audio else 25
    low = floor_at_face - 2
    max_depth = 105
    span = case_width + 2

    def floor(z):
        return floor_at_face + z * slope

    def rear(y):
        return top_depth + (top - y) * TAPER

    def behind_floor(lift):
        return section(-span / 2, span, [
            (floor(0) + lift, 0), (top + 1, 0),
            (top + 1, max_depth), (floor(max_depth) + lift, max_depth),
        ])

    def rear_limit(offset):
        return section(-span / 2, span, [
            (low, -1), (top + 1, -1),
            (top + 1, rear(top + 1) + offset), (low, rear(low) + offset),
        ])

    def on_cover(shape, y):
        return shape.rotate((0, 0, 0), (1, 0, 0), -degrees(atan(TAPER))).translate(
            (0, y, rear(y))
        )

    screw_x = case_width / 2 - 4.3
    lower_screws = bottom - 7 if portrait else bottom + 24
    screws = [(x, y) for x in (-screw_x, screw_x) for y in (lower_screws, top - 6)]
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
        gauge = gauge.rotate((0, 0, 0), (0, 0, 1), -90)
        exit_z = (rear(floor_at_face) + LID) / (1 + TAPER * slope)
        # Open the rear at the desk edge so the cover can close over an
        # already-routed cable. The front edge remains continuous.
        exit_y = floor(exit_z)
        usb_relief = block(usb_y, exit_y + 5, exit_z - 12, 28, 24, 27)
    else:
        start, end = usb_x + 1, case_width / 2 + 2
        usb_relief = block((start + end) / 2, usb_y, usb_z - 6, end - start, 21, 12)

    if min(vertex.Y - floor(vertex.Z) for vertex in gauge.Vertices()) < 3:
        raise ValueError("USB-C overmold is too close to the desktop")

    outline = rounded(0, (top + low) / 2, 0, case_width, top - low, max_depth, 5)
    outer = outline.intersect(behind_floor(0)).intersect(rear_limit(LID))
    interior = rounded(
        0, (top + low) / 2, FACE + 2.2,
        case_width - 2 * WALL, top - low - 2 * WALL, max_depth, 3,
    ).intersect(behind_floor(WALL))
    if portrait:
        # Recess the cable bay behind the display's lower bezel. A diagonal
        # transition joins the bezel to the full-width front of the base.
        def front_relief(offset):
            return section(-span / 2, span, [
                (low, -1), (bottom + offset + 3, -1),
                (bottom + offset - 4, 2.5 + offset), (low, 2.5 + offset),
            ])

        outer = outer.cut(front_relief(0))
        interior = interior.cut(front_relief(WALL))

    split = rear_limit(0)
    body = outer.intersect(split)
    cover = outer.cut(split)
    seat = rounded(0, 0, FACE, width + 0.8, height + 0.8, 3, 2.9)
    window = rounded(0, 0, -EPS, width - 2.6, height - 2.6, FACE + 2 * EPS, 1.5)
    apertures, controls, endpoints, controls_for_print = make_controls(
        panel, portrait, case_width, top, floor
    )
    bosses = [hole(x, y, FACE, 6.8, max_depth) for x, y in screws]
    # Clip the bosses to the outside, including the desk and rear planes.
    body = body.cut(interior, seat, window).fuse(
        *(boss.intersect(body) for boss in bosses)
    ).cut(
        seat, window, usb_relief, *apertures,
        *(hole(x, y, rear(y) - 9, 2.6, 12) for x, y in screws),
    )
    cover = cover.cut(
        usb_relief,
        *(hole(x, y, rear(y) - 2, 3.4, 8) for x, y in screws),
        *(hole(x, y, rear(y) + 1.65, 6.4, 8) for x, y in screws),
    )

    vent_rows = [top - 8 - i * 3.6 for i in range(3)]
    if not audio:
        vent_rows += [lower_screws + i * 3.6 for i in range(3)]
    vents = [on_cover(rounded(0, 0, -EPS, 31, 1.9, LID + 1, 0.9), y) for y in vent_rows]
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
        speaker_y = bottom + (20 if portrait else 31)
        grill = [
            on_cover(hole(x, y, -EPS, 2.2, LID + 1), speaker_y)
            for x in range(-10, 11, 4) for y in range(-10, 11, 4)
            if x * x + y * y < 135
        ]
        speaker_tabs = [-18.3, 18.3]
        cover = cover.fuse(*(
            on_cover(hole(x, 0, -6.5, 6.0, 6.5 + EPS), speaker_y) for x in speaker_tabs
        )).cut(
            *grill, *(on_cover(hole(x, 0, -6.5 - EPS, 1.7, 5.9), speaker_y) for x in speaker_tabs),
        )
    parts = (body.clean(), cover.clean())
    for index, control in enumerate((*controls, *endpoints)):
        if any(part.intersect(control).Volume() > 0.05 for part in parts):
            raise ValueError(f"Printed control position {index + 1} intersects the enclosure")
    return parts, gauge, controls, controls_for_print


def export(parts, gauge, controls, controls_for_print, panel, portrait, audio, output, previews):
    orientation = "portrait" if portrait else "landscape"
    trim = "audio" if audio else "plain"
    stem = f"{panel.name.lower()}-{orientation}-{trim}"
    body, cover = parts
    # Print the canted rear face flat on the build plate.
    printable = {
        "bezel": body,
        "back": cover.rotate((0, 0, 0), (1, 0, 0), 180 + degrees(atan(TAPER))),
    }
    printable.update(controls_for_print)
    for name, part in printable.items():
        if len(part.Solids()) != 1 or not part.isValid():
            raise ValueError(f"{stem}-{name} must be one valid solid")
        part = part.translate((0, 0, -part.BoundingBox().zmin))
        prefix = f"{panel.name.lower()}-{orientation}" if name in controls_for_print else stem
        cq.exporters.export(part, str(output / "stl" / f"{prefix}-{name}.stl"), tolerance=0.06)
    if previews:
        from render import render_case
        render_case(panel, portrait, audio, parts, controls, gauge, output / "renders" / f"{stem}.png")


@lru_cache(maxsize=2)
def read_board(path):
    return cq.importers.importStep(str(path)).val()


def clearance(shape, board):
    distance = BRepExtrema_DistShapeShape()
    distance.LoadS1(shape.wrapped)
    distance.LoadS2(board.wrapped)
    distance.SetMultiThread(True)
    distance.Perform()
    return distance.Value()


def verify_fit(panel, portrait, parts, controls, gauge, step_directory):
    board = read_board(step_directory / f"{panel.name}.stp")
    board = board.translate((-panel.step_center_x, 0, FACE + TAPE - panel.step_glass_z))
    if portrait:
        board = board.rotate((0, 0, 0), (0, 0, 1), -90)
    for part in parts:
        if clearance(part, board) < 0.02:
            raise ValueError(f"{type(part).__name__} touches the reference board")
        if part.intersect(gauge).Volume() > 0.05:
            raise ValueError("Case obstructs the nominal USB-C plug")
    if clearance(gauge, board) < 0.02:
        raise ValueError("The nominal USB-C overmold crosses the reference board")
    for control in controls:
        if clearance(control, board) < 0.015:
            raise ValueError("Printed control touches the board at rest")
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
                parts, gauge, controls, controls_for_print = make_case(panel, portrait, audio)
                if args.check_step:
                    verify_fit(panel, portrait, parts, controls, gauge, args.check_step)
                export(parts, gauge, controls, controls_for_print, panel, portrait, audio, args.output, args.previews)


if __name__ == "__main__":
    main()
