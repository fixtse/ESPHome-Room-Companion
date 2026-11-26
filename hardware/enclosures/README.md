# Room Companion desktop enclosures

Print a matching **bezel and base** and **back cover** for your panel. The
base is built into the bezel and holds the display at a 12° lean. The printed
power slider and BOOT and RESET buttons operate the original board controls.
The glass sits nearly flush inside a narrow printed rim. The portrait shell
has a shallower taper and a short cable pocket at the bottom. Audio variants
use the same front outline, with extra room behind the top edge for the
upward-facing mic and a rear grille for the Nextion BOX Speaker.

The visible printed rim is about 2 mm wide along the sides and top. The
portrait shell is 60.6 mm wide on the 2.8″ and 65.6 mm wide on the 3.5″.
The corresponding desk depths are 42.4 and 44.2 mm for the plain cases,
or 56.1 and 57.9 mm for the audio cases, excluding controls and cables.

![Eight Room Companion enclosure variants shown from the front and back](renders/overview.png)

| Panel | Orientation | Version | STL files | Preview |
| --- | --- | --- | --- | --- |
| ONX2432G028 (2.8″) | Portrait | Plain | [Bezel and base](stl/onx2432g028-portrait-plain-bezel.stl) · [Back cover](stl/onx2432g028-portrait-plain-back.stl) | [Front and rear](renders/onx2432g028-portrait-plain.png) |
| ONX2432G028 (2.8″) | Portrait | Mic + speaker | [Bezel and base](stl/onx2432g028-portrait-audio-bezel.stl) · [Back cover](stl/onx2432g028-portrait-audio-back.stl) | [Front and rear](renders/onx2432g028-portrait-audio.png) |
| ONX2432G028 (2.8″) | Landscape | Plain | [Bezel and base](stl/onx2432g028-landscape-plain-bezel.stl) · [Back cover](stl/onx2432g028-landscape-plain-back.stl) | [Front and rear](renders/onx2432g028-landscape-plain.png) |
| ONX2432G028 (2.8″) | Landscape | Mic + speaker | [Bezel and base](stl/onx2432g028-landscape-audio-bezel.stl) · [Back cover](stl/onx2432g028-landscape-audio-back.stl) | [Front and rear](renders/onx2432g028-landscape-audio.png) |
| ONX3248G035 (3.5″) | Portrait | Plain | [Bezel and base](stl/onx3248g035-portrait-plain-bezel.stl) · [Back cover](stl/onx3248g035-portrait-plain-back.stl) | [Front and rear](renders/onx3248g035-portrait-plain.png) |
| ONX3248G035 (3.5″) | Portrait | Mic + speaker | [Bezel and base](stl/onx3248g035-portrait-audio-bezel.stl) · [Back cover](stl/onx3248g035-portrait-audio-back.stl) | [Front and rear](renders/onx3248g035-portrait-audio.png) |
| ONX3248G035 (3.5″) | Landscape | Plain | [Bezel and base](stl/onx3248g035-landscape-plain-bezel.stl) · [Back cover](stl/onx3248g035-landscape-plain-back.stl) | [Front and rear](renders/onx3248g035-landscape-plain.png) |
| ONX3248G035 (3.5″) | Landscape | Mic + speaker | [Bezel and base](stl/onx3248g035-landscape-audio-bezel.stl) · [Back cover](stl/onx3248g035-landscape-audio-back.stl) | [Front and rear](renders/onx3248g035-landscape-audio.png) |

## Printed controls

Print **two copies of the button** and **one power slider**. The controls are
shared by the audio and plain cases of the same panel and orientation.

| Panel | Orientation | Button | Power slider |
| --- | --- | --- | --- |
| ONX2432G028 (2.8″) | Portrait | [Button STL](stl/onx2432g028-portrait-button.stl) | [Slider STL](stl/onx2432g028-portrait-power-slider.stl) |
| ONX2432G028 (2.8″) | Landscape | [Button STL](stl/onx2432g028-landscape-button.stl) | [Slider STL](stl/onx2432g028-landscape-power-slider.stl) |
| ONX3248G035 (3.5″) | Portrait | [Button STL](stl/onx3248g035-portrait-button.stl) | [Slider STL](stl/onx3248g035-portrait-power-slider.stl) |
| ONX3248G035 (3.5″) | Landscape | [Button STL](stl/onx3248g035-landscape-button.stl) | [Slider STL](stl/onx3248g035-landscape-power-slider.stl) |

## Print and assemble

1. Import the two matching shell STLs and the control STLs in **millimetres**. The bezel prints face down,
   and the back cover prints exterior side down. A 0.4 mm nozzle, 0.2 mm
   layers, and three walls in PETG are a starting point. Preview the base,
   USB-C and control openings in your slicer and add local supports where
   needed. Keep the glass pocket clear of brim and elephant's foot.
2. Test-fit the printed controls before mounting the board. Insert the two
   plungers and the slider from inside the shell, with the retaining flanges
   inside. Remove any burrs, especially between the two teeth of the slider.
3. Test-fit the display through the front of the bezel. Line the slider's
   fork up with the board's power tab. Check that the slider reaches both
   positions and both buttons click and release. Use 0.25 mm thick removable
   double-sided tape on the two side ledges under the black glass border.
   Press only around the edge of the glass.
4. On an audio version, center the [Nextion Dual MIC Board](https://itead.cc/product/nextion-dual-mic-board/)
   inside the top wall, behind the display. Slide its PCB into the two printed
   channels from the rear, with the acoustic ports facing up. The back cover
   traps the PCB in the guides. Route the flex through the center gap and
   down to MIC_PDM without a sharp fold. The nominal PCB clearance is
   47.8 × 9.8 × 1.8 mm. Check your board against the pocket before assembly. [Microphone guide detail](renders/mic-guide-detail.png)

   Point the [Nextion BOX Speaker](https://itead.cc/wp-content/uploads/2022/03/Box-Speaker-Dimension.pdf)
   into the back cover's grille with a thin foam gasket around the opening.
   Use two M2 × 6 mm thread-forming screws in the printed pilot holes.
   Check that the speaker seats against the gasket before tightening. Connect
   it to SPK and tuck the wires away from the cover rim and USB socket.
5. In portrait, connect a low-profile 90° USB-C plug with the back cover open
   and fit the cable into the left-side notch from below. Seat the cover and
   hand-tighten four M2 × 6 mm thread-forming screws through the sides into
   the cover tabs. In landscape the USB-C plug inserts from the left side
   when facing the display.
6. Set the screen orientation in Room Companion.

## Access the board controls

The round button closer to the board's USB-C socket is **BOOT**, and the
farther one is **RESET**. The rectangular tab slides the stock **power switch**.
Remove the back cover to reach the microSD card.

| Panel | Portrait, as viewed from the front | Landscape, as viewed from the front |
| --- | --- | --- |
| ONX2432G028 (2.8″) | BOOT, RESET and power on the right edge | BOOT, RESET and power on the underside |
| ONX3248G035 (3.5″) | BOOT and RESET on the left edge, power on the right edge | BOOT and RESET on the top edge, power on the underside |

Tilt the case back to reach underside controls in landscape. [2.8″ board controls](https://github.com/OpenNextion/OpenNextion-SKU-ONX2432G028/blob/main/Product%20Images/ONX2432G028-3.jpg) · [3.5″ board controls](https://open-nextion-app-store.nextion.tech/devices/onx3248g035/#interfaces)

## USB-C clearance

The **portrait** case takes a low-profile right-angle plug with its cable
leaving to the left, parallel to the bottom edge of the display. The tested
CAD envelope has a neck up to 12.5 × 5.5 mm between 1.5 and 5.5 mm from the
socket face, an elbow up to 18 × 9 mm through 11.8 mm from the socket face,
and a cable up to 4.4 mm in diameter. Check the molded shoulder and strain
relief on your cable. [Portrait USB-C cutaway](renders/portrait-usb-detail.png)

The **landscape** shell takes a straight plug through a 21 × 12 mm side
opening. The glass extends about 1.9 mm past the socket on the 2.8″ and
7.4 mm on the 3.5″.

- **2.8″** — 18 × 10 mm plug body up to 31 mm long, starting 1.5 mm from the
  socket face when seated.
- **3.5″** — neck up to 12.5 × 5.5 mm between 1.5 and 5.5 mm from the socket
  face. The wider 18 × 10 mm plug body can start 5.5 mm from the socket face.

## CAD sources and validation

[enclosure.py](enclosure.py) is the parametric source. It uses the glass
outline, front datum, and USB-C receptacle position from the manufacturer
[ONX2432G028 drawings and STEP](https://github.com/OpenNextion/OpenNextion-SKU-ONX2432G028/tree/main/Drawings)
and [ONX3248G035 drawings and STEP](https://github.com/OpenNextion/OpenNextion-SKU-ONX3248G035/tree/main/Drawings).
The rounded desktop format and the need for extra portrait plug access were
informed by the
[OpenNextion desktop shell](https://makerworld.com/en/models/3211049-open-nextion-3-5-esp32-s3-display-desktop-shell#profileId-3634703)
and [horizontal/vertical desktop case](https://makerworld.com/en/models/3509686-onx3248g035-desktop-case-horizontal-vertical#profileId-4010326).

Generate STLs with CadQuery. The PNG renderer uses VTK (included with CadQuery) and Pillow. For the optional fit
check, save the official STEP files as `ONX2432G028.stp` and
`ONX3248G035.stp` in a separate directory.

```sh
python -m pip install 'cadquery==2.8.0' 'pillow>=10.1'
python hardware/enclosures/enclosure.py --previews --check-step /path/to/board-steps
python hardware/enclosures/render.py
```

All eight variants were checked against the manufacturer STEP files and the
nominal USB-C overmold. The STLs were checked for closed, single-part meshes.
Physical fit, switch actuation, cable flexibility, print settings, and audio
performance still need to be verified on a print.
