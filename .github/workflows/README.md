# Sketch Delay v2 (VST3 / AU)

Draw every echo. Four separate rate modes, each with its own sketch and its own bar count:

| Mode | Delay time | Bars in the sketcher |
|------|-----------|----------------------|
| 1/4  | quarter note   | 4  |
| 1/8  | eighth note    | 8  |
| 1/16 | sixteenth note | 16 |
| 1/32 | 32nd note      | 32 |

Every mode spans exactly one bar of music (4 beats), and the sketcher shows the beat lines 1-2-3-4 so each bar
sits where the echo really lands. Switching modes switches to that mode's own drawing (level + pitch, saved with the project).

## Sketcher
- **LEVEL**: drag across the bars to draw each echo's volume (60 dB scale). Shift+click/drag = straight line. Double-click = reset.
  Presets: Natural, Linear, Curve Dn/Up, Ramp Up (build-up), Gate, Bounce, Random, Smooth, Reverse.
- **PITCH**: draw -24..+24 semitones per echo (each echo is shifted once, from the clean signal). STACK makes shifts accumulate.
  Presets: Reset, Octaves, Arp, Ramp, Random scale, Reverse.
- **PRE EQ / POST EQ**: 6 bands each (bell, shelves, 6-48 dB cuts, notch, band-pass, tilt) with live spectrum.
  Double-click = add/remove, wheel = Q, right-click = type/slope.
- Hover a bar to see its value.

## Sound
TAPE / ANALOG / DIGITAL character + COLOR, DRIVE (4x oversampled, level matched), WIDTH, SPREAD 0/1/2, IMAGER,
PING PONG, DUCK, MIX, SYNC (or free TIME), STR/DOT/TRI note types, LINK L/R.

## Get the installers (no compiler needed)
1. New GitHub repository -> upload everything in this folder EXCEPT `PASTE-THIS-build.yml.txt`.
2. In the repo: Add file -> Create new file -> name it `.github/workflows/build.yml` -> paste the contents of
   `PASTE-THIS-build.yml.txt` -> Commit.
3. Actions tab -> Build installers -> wait ~10-15 minutes -> download `SketchDelay-Windows` (Setup .exe) / `SketchDelay-macOS` (.pkg).

Windows installs to `C:\Program Files\Common Files\VST3`. In Live: Preferences -> Plug-Ins -> enable VST3 system folders -> Rescan.
