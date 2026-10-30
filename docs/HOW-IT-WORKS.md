# How the LD2450 / display code works

## Radar data stream

The LD2450 is a **24 GHz FMCW mmWave moving-target radar module**. Its UART stream reports **up to 3 target slots** at approximately **10 frames/s**, each with X, Y, speed and a resolution field.

- UART: **256000 baud**, 8N1.
- Frame: **30 bytes**.
- Header: `AA FF 03 00` (4 bytes).
- Slots: 3 × 8 bytes (X, Y, velocity, resolution, each a 16-bit little-endian field).
- Footer: `55 CC` (2 bytes).
- Empty target slot: eight zero bytes.

The protocol's **X/Y/speed format is sign-and-magnitude encoded**, not ordinary two's-complement `int16_t`. In the **original sketch** `decodeValue()` extracts the magnitude and uses the upper bit to determine the sign.

## Plotting a point

The LD2450 provides **X and Y in millimeters**, relative to the radar. The sketch maps them to a screen position, with a display origin `(ox, oy)`:

```cpp
int x = ox + lroundf(target.sx * scale);
int y = oy - lroundf(target.sy * scale);
```

For `X = 642 mm`, `Y = 1347 mm`, a 2D straight-line distance is:

```text
d = sqrt(642² + 1347²) / 1000 ≈ 1.49 m
```

This is **2D sensor-relative range**, not automatically the same as ground-floor tape distance, especially when the radar is mounted above the floor.

## Draw pipeline

- TFT landscape size = **320×240**.
- `GFXcanvas16 canvas(320, 240)` creates a memory buffer for the screen.
- Every **125 ms**, `drawUI()` draws to the buffer and sends the complete image to ILI9341 via `tft.drawRGBBitmap(...)`.
- `smoothFactor = 0.45f` applies exponential smoothing to **displayed X and Y**, reducing visible dot jitter. The raw target coordinates used by the detail page are unchanged.
- Radar-view 2/3/5 m values control the **display scale only**; not the radar's detection configuration.

## Controls

- Mechanical rotary encoder on GPIO4/GPIO5 uses quadrature transition decoding.
- Encoder switch GPIO6 is debounced (~25 ms).
- Short press: flip radar vs target-details page.
- Long press >=1200 ms: restore 5m display and radar page.

## Tracking-slot caveat

Target slots `T1`,`T2`,`T3` are not stable person identities. Under crossing paths, lost detections and reacquisitions, a person may be assigned a different slot.

## Original vs optional debug sketch

- [`firmware/Radar_UI_V3/Radar_UI_V3.ino`](../firmware/Radar_UI_V3/Radar_UI_V3.ino) is the **unaltered actual V3 display sketch**. Its Serial Monitor reports `Targets: N | Range: Xm | View: RADAR/DETAIL` every ~500 ms.
- [`extras/LD2450_Serial_Debug/LD2450_Serial_Debug.ino`](../extras/LD2450_Serial_Debug/LD2450_Serial_Debug.ino) is a **separate optional** sketch that reports every active target's X/Y/speed for debugging. It does not render the display; uploading it replaces the running V3 firmware until you upload V3 again.
