# Band buffer test harness

Host-native regression test for the ST7735S off-screen composite band
(`ST7735S_bandBegin()` / `ST7735S_bandEnd()`, see `src/display/st7735s/st7735s.h`).

```
make -C test/band run
```

## Why this exists

The band buffer is pure display-output geometry: the only thing that can go
wrong is that pixels land in the wrong place, and the only way to see that on
hardware is to look at the screen. This harness compiles the **real**
`st7735s.c` / `gfx.c` / `fonts.c` against stub transport functions, decodes the
CASET / RASET / RAMWR stream the driver emits into a fake 128x160 panel array,
and then asserts on individual pixels — including the ones just *outside* the
band, which is where a stride or off-by-one bug would show up first.

It also counts panel-update passes, which is the actual point of the feature: a
field redraw that used to reach the panel as ~88 separate transfers (the flicker
you can see) now arrives as one.

## What it stubs, and what it does not

Stubbed: `SPI_*` / `Pin_*` from `st7735s_compat.c` (defined in `band_test.c`),
and just enough of `zephyr/kernel.h` for the mutex and the sleeps
(`fake_zephyr/`). Everything else — the run-length machine, the band, the
window tracking, `filledRect()`, `drawText()`, the fonts — is the shipping code.

The stub `k_mutex` counts lock depth but does not block, so this harness proves
geometry, **not** the concurrency behaviour. The band holding `hvbuffer_mutex`
from begin to end is what makes a composite atomic against the other drawing
threads, and that still has to be reasoned about (or observed on hardware).

## Not wired into CI

There is no CI here, and this is not part of the Zephyr build — it is a
`make` you run by hand when you touch the driver.
