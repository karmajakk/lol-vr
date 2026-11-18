# Lands of Lore VR (lol-vr)

*Lands of Lore: The Throne of Chaos* (Westwood, 1993) in virtual reality on Meta Quest 3,
standalone (no PC needed). The original game runs on [ScummVM](https://www.scummvm.org).

**The game files are not included.** You need your own copy of the game (CD or GOG version).

The app speaks English by default; French is available from the gear on your left wrist.

## Project status

Version details: [CHANGELOG.md](CHANGELOG.md). Documentation: [docs/wiki](docs/wiki/Home.md).

| Version | Content | Status |
|---|---|---|
| 0.1 | The full game on a floating screen in VR, controllers as pointer, sound, saves | done |
| 0.2 | Medieval library (changes with the game area), frame, gloves, mixed reality, CRT monitor, haptics, spatial sound | done |
| 0.3 | Window mode (enlarged dungeon, wrist interface) | dropped |
| 0.4 – 0.10 | Detached interface in movable windows, keyboard, movable and resizable screen, 486 PC, your own pictures, button combat, wrist menu, English and French | testing |
| 1.0 | The dungeon in true 3D around you | later |

## Installation (nothing to compile)

1. Download `lol-vr.apk` from the
   [latest release](https://github.com/karmajakk/lol-vr/releases/latest).
2. Connect the headset to your PC, open [SideQuest](https://sidequestvr.com) and install the APK
   (the "Install APK file from folder" icon).
3. In the headset, launch **Lands of Lore VR** (Library → "Unknown sources" filter).
   A screen tells you the game files are missing: that's expected. This first launch creates
   the folder where they go.
4. On the PC, copy **the whole content of the game CD** (the `.PAK` files, including
   `GENERAL.PAK`) into this folder of the headset:

   `Android/data/com.karmajakk.lolvr/files/lol`

   (with SideQuest's file manager, or Windows Explorer once the headset is connected and file
   access is allowed).

   **GOG version:** the `.PAK` files are packed inside `GAME.DAT`, a CD image. Open `GAME.DAT`
   with [7-Zip](https://www.7-zip.org) and copy the content of its `DATA` folder (including the
   `ENG` subfolder).
5. The game starts by itself as soon as the files are in place.

Saves are kept in `Android/data/com.karmajakk.lolvr/files/sauvegardes`.
Warning: uninstalling the app erases this folder. To update, just install the new APK over the
old one.

## Controls

| Controller | Action |
|---|---|
| Aim + trigger | Left click (the hand that clicks becomes the pointer) |
| Aim + grip | Right click |
| Left stick | Walk forward, back, strafe |
| Right stick (left/right) | Turn |
| Right stick (up/down) | In the dungeon: next / previous character (framed in yellow in the party window) |
| A | In the dungeon: **attack** with the selected character. Elsewhere: Enter |
| B | In the dungeon: open the selected character's **magic**. Elsewhere: Escape (skip a cutscene) |
| Y | Map |
| **X** | **Right click** where you aim: drink a potion, use an item (same as the grip) |
| Menu button (left controller) | ScummVM menu: save, load, quit |
| **Gear on the left wrist** | Look at your left hand and click the gear: language (English / French), classic screen on/off, keyboard (to name a save; its top row holds the fire sound, panels and reset layout), controller mapping |
| Right stick click | Recenter the screen, the room and the windows in front of you |
| Short left stick click | Change scenery: library → mixed reality with frame → mixed reality with CRT monitor |
| Left stick click held + right stick | Up/down: screen size; left/right: distance |

### Moving and resizing the screen and windows

The screen and the keyboard carry a **brass bar** above them; the game windows carry it on their
side, so it never gets in the pointer's way between the screen and the windows. As soon as your
finger leaves the screen and the windows, the pointer beam gets longer to show where you aim.
Aim at the bar, squeeze the trigger and keep it squeezed: the object follows your hand. While you
hold it, the right stick pushes or pulls it (up/down) and shrinks or enlarges it (left/right).
Release the trigger to drop it. Everything is remembered between sessions.

To **resize** the screen or a window, aim at one of its corners, just outside (on the frame, for
the screen): a brass bracket appears. Squeeze the trigger and move your hand away or closer; the
window grows or shrinks around its center.

## The detached interface

While exploring, the screen only shows the dungeon view. The rest of the game interface is spread
over windows you can place wherever you like: the party (messages and portraits), the inventory,
the spell scroll, the controls (arrows, options, rest) and the compass. At first they sit around
the screen: the pointer moves from one to the other with no offset, so you can pick up an item
on the floor and drop it into the inventory.
Each window is independent: moving or resizing the screen doesn't move the others. In the
library, they don't go through the floor or the walls.
During cutscenes, menus, the map and full-width scenes, the whole image comes back by itself.
A character sheet is shown in full on the screen (which widens to the right); the windows stay
where they are and the compass window goes dark.

"Classic screen" in the wrist menu (or "Panels" on the keyboard) shows the whole image all the time, and "Reset layout"
arranges the windows around the screen, wherever it is.

## Scenery

- **Medieval library**: a closed room with a fireplace, a window, shelves, paintings and banners.
  The view through the window, the light, a painting, the banners and the soundscape change with
  the game area.
- **Mixed reality**: your real room, with the game in its golden frame.
- **CRT monitor**: your real room, with the game on a curved 90s screen standing on its desktop
  computer.

### Your own pictures in the library (optional)

You can add pictures to the headset folder `Android/data/com.karmajakk.lolvr/files/decor`
(PNG or JPEG):

| File name | Where it goes |
|---|---|
| `fenetre` | the view through the window |
| `portrait` | the painting above the fireplace |
| `tableau1` | left wall, in place of the map |
| `tableau2` | back wall, left of the door |
| `tableau3` | left wall, in place of the landscape |
| `tableau4` | back wall, right of the door (the frame only appears if the picture exists) |

Six pictures at most. The name can also contain `castle` or `king` (for the window and the
portrait); a picture with any other name takes the first free spot. The app picks them up a few
seconds after you copy them, no restart needed. Under the keyboard (wrist menu), two lines show which
pictures are displayed and which files the app sees in the folder.

These pictures don't ship with the app: like the game files, everyone brings their own.

The controllers vibrate when the party hits or gets hurt, when doors open, on levers and spells.
The game sound comes from the screen: it stays in place when you turn your head. The only
ambient sound is the fireplace, off by default (keyboard, "Fire" button).

## How it's built

- `app/`: the Quest app (C++, OpenXR, OpenGL ES). `core_host.cpp` runs ScummVM, `main_xr.cpp`
  handles the headset, the controllers, the layers and the haptics. The scenery is entirely
  generated by code, with no image or sound file: `scene.cpp` (rendering), `room.cpp` and
  `props.cpp` (room, frame, monitor, gloves), `proctex.cpp` (textures), `ambience.cpp` (spatial
  sound and ambience), `ui.cpp` (windows, bars, keyboard, wrist menu, pointer).
- `app/test/`: test benches that run on a PC without a headset (scenery images, sound, game core).
- `core/` and `patches/`: settings and patches applied to ScummVM (libretro core, limited to the
  Lands of Lore engine). The second patch tells the app the game area and events (hits, sounds)
  for the scenery and the haptics.
- `scripts/build.sh`: builds the APK. GitHub runs it on every change
  (`.github/workflows/build.yml`); a new version is published to the Releases when the `VERSION`
  file changes (notes taken from `CHANGELOG.md`).
- `docs/wiki/`: the pages of the project wiki.
- `keystore/debug.keystore`: a public test signing key, only used so updates can be installed
  over the previous version. It protects nothing.

## License

GPL version 3 or later, like ScummVM (see `COPYING`). *Lands of Lore* is a trademark of its
respective owners; this project is not affiliated with Westwood, Electronic Arts or the ScummVM
team.
