# SPDX-License-Identifier: AGPL-3.0-only
"""Orthographic assembly renders from the same solids used for the STL export."""

from pathlib import Path

import cadquery as cq
import vtk
from PIL import Image, ImageDraw, ImageFont
from vtk.util.numpy_support import vtk_to_numpy

from enclosure import FACE, TAPE, TILT, rounded

BACKGROUND = (236, 241, 244)
TEXT = (37, 53, 64)


def actor(shape, rgb):
    vertices, triangles = shape.tessellate(0.075)
    points = vtk.vtkPoints()
    for vertex in vertices:
        points.InsertNextPoint(*vertex.toTuple())
    polygons = vtk.vtkCellArray()
    for face in triangles:
        polygons.InsertNextCell(3)
        for index in face:
            polygons.InsertCellPoint(index)
    poly = vtk.vtkPolyData()
    poly.SetPoints(points)
    poly.SetPolys(polygons)
    mapper = vtk.vtkPolyDataMapper()
    mapper.SetInputData(poly)
    model = vtk.vtkActor()
    model.SetMapper(mapper)
    material = model.GetProperty()
    material.SetColor(*(channel / 255 for channel in rgb))
    material.SetAmbient(0.35)
    material.SetDiffuse(0.7)
    material.SetSpecular(0.2)
    material.SetSpecularPower(26)
    return model


def model_view(actors, target, look, scale):
    renderer = vtk.vtkRenderer()
    renderer.SetBackground(*(channel / 255 for channel in BACKGROUND))
    for model in actors:
        renderer.AddActor(model)
    camera = renderer.GetActiveCamera()
    camera.SetFocalPoint(*target)
    camera.SetPosition(*(value + offset for value, offset in zip(target, look)))
    camera.SetViewUp(0, 1, 0)
    camera.ParallelProjectionOn()
    camera.SetParallelScale(scale)
    renderer.ResetCameraClippingRange()
    render = vtk.vtkRenderWindow()
    render.SetOffScreenRendering(1)
    render.SetSize(700, 590)
    render.SetMultiSamples(8)
    render.AddRenderer(renderer)
    render.Render()
    capture = vtk.vtkWindowToImageFilter()
    capture.SetInput(render)
    capture.ReadFrontBufferOff()
    capture.Update()
    output = capture.GetOutput()
    rows = vtk_to_numpy(output.GetPointData().GetScalars()).reshape(590, 700, 3)
    frame = Image.fromarray(rows[::-1].copy())
    render.Finalize()
    return frame


def render_case(panel, portrait, audio, parts, gauge, path):
    width, height = (panel.width, panel.height) if portrait else (panel.height, panel.width)
    active_w, active_h = panel.active if portrait else panel.active[::-1]
    glass = rounded(0, 0, FACE + TAPE, width, height, 0.7, 2.5)
    display = rounded(0, 0, FACE + TAPE - 0.06, active_w, active_h, 0.08, 0.25)
    solids = (*parts, glass, display)
    colors = ((75, 143, 150), (61, 113, 124), (46, 77, 88), (25, 31, 35), (42, 53, 62))
    actors = [
        actor(solid.rotate((0, 0, 0), (1, 0, 0), TILT), color)
        for solid, color in zip(solids, colors)
    ]
    bounds = cq.Compound.makeCompound(parts).rotate((0, 0, 0), (1, 0, 0), TILT).BoundingBox()
    target = (0, (bounds.ymin + bounds.ymax) / 2, (bounds.zmin + bounds.zmax) / 2)
    scale = max(bounds.ylen, bounds.xlen, bounds.zlen) * 0.64
    # The front view in landscape shows the connector side. The low rear
    # view in portrait shows the connector access underneath.
    front = (110 if not portrait else -110, 145 if audio else 63, -260)
    rear = (130, 33 if portrait else 86, 250)
    if portrait:
        rear = (130, -48, 250)
    combined = Image.new("RGB", (1400, 645), BACKGROUND)
    combined.paste(model_view(actors, target, front, scale), (0, 0))
    combined.paste(model_view(actors, target, rear, scale), (700, 0))
    draw = ImageDraw.Draw(combined)
    font = ImageFont.load_default(size=25)
    draw.text((350, 607), "FRONT", anchor="mm", fill=TEXT, font=font)
    draw.text((1050, 607), "REAR", anchor="mm", fill=TEXT, font=font)
    combined.save(path, optimize=True)
    if portrait and audio and panel.name == "ONX3248G035":
        plug = gauge.rotate((0, 0, 0), (1, 0, 0), TILT)
        center = plug.Center().toTuple()
        detail = model_view([*actors, actor(plug, (237, 149, 63))], center, (90, -68, -170), 27)
        detail.save(path.with_name("portrait-usb-detail.png"), optimize=True)


def contact_sheet(folder):
    """Build a matrix from the eight saved CAD previews."""
    sheet = Image.new("RGB", (1600, 1518), BACKGROUND)
    draw = ImageDraw.Draw(sheet)
    title = ImageFont.load_default(size=34)
    label = ImageFont.load_default(size=23)
    draw.text((32, 22), "Room Companion desktop enclosures", fill=TEXT, font=title)
    for column, model in enumerate(("onx2432g028", "onx3248g035")):
        left = 15 + column * 795
        draw.text((left + 385, 76), model.upper(), anchor="mm", fill=TEXT, font=label)
        for row, (orientation, trim) in enumerate((
            ("portrait", "plain"), ("portrait", "audio"),
            ("landscape", "plain"), ("landscape", "audio"),
        )):
            top = 95 + row * 348
            with Image.open(folder / f"{model}-{orientation}-{trim}.png") as render:
                # Omit the repeated view labels, keeping the full CAD area.
                preview = render.crop((0, 0, 1400, 580)).resize((780, 323), Image.Resampling.LANCZOS)
            sheet.paste(preview, (left, top))
            draw.text((left + 390, top + 315), f"{orientation.title()} / {trim}",
                      anchor="mm", fill=TEXT, font=label)
    sheet.save(folder / "overview.png", optimize=True)


if __name__ == "__main__":
    contact_sheet(Path(__file__).resolve().parent / "renders")
