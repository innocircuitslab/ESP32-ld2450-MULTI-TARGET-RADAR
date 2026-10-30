# Troubleshooting

| Symptom | Likely cause | What to do |
|---|---|---|
| TFT powers on but no radar points / `WAIT` | Wrong UART pins; radar power; wiring; baud | **LD2450 TX → GPIO1; LD2450 RX → GPIO2**, common GND, 5V on radar, UART **256000**. Don't use board `TX/RX` labels unless their actual GPIO numbers match. |
| No targets in app | Bluetooth/app permissions or settings | Power radar, scan nearby with **HLKRadarTool**, check **Multi-person ON**. |
| Only one target | Multi-target mode off, targets too close, mounting geometry | Use app to enable multi-target; separate people; adjust upright mounting height/aim. |
| Display is blank | TFT supply, SPI pins, Arduino board selection | Check CS8 RST9 DC10 MOSI11 SCLK12 BL13 and correct TFT supply. |
| Flicker | Repainting directly to screen | Use supplied **V3** that draws into `GFXcanvas16` and pushes the frame in one operation. |
| Framebuffer failed | Insufficient free RAM / allocation failed | Verify board settings and that other large RAM consumers are disabled; V3 needs **320×240×2 = 153600 bytes** for its canvas. |
| Encoder direction reversed | Encoder wiring/direction differ | Reverse direction in `pollEncoder()` (comment in source), or swap A/B pins. |
| Encoder too fast / no clicks | Encoder detents differ | Tune `ENC_STEPS` (default **4**) in V3 sketch. |
| USB Serial Monitor blank | Wrong baud or USB CDC/port setting | Set **115200**; verify ESP32-S3 board profile / USB CDC on boot when applicable. |
| Esp32 stops reading after phone changes baud | HLKRadarTool changed module UART | Restore module **256000 baud** and reboot module. |
| Shorter subject not detected | Radar elevation/orientation, motion/target profile | Try alternative mounting heights and aims; don't infer a fixed minimum person height from one test. |
| Door detection unreliable | Obstacle attenuation, reflections, multipath | Test your actual room/material; **not** guaranteed to detect through walls. |

This is a hobby demonstration and should not be the only detector used in any safety-critical system.
