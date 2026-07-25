# WWD-n Cleanup TODO
_Generated after migrating GitHub repo → NCS repo and porting nRF52832 → nRF52833._

---

## 1. Delete stale files

Run these from the NCS repo root (`~/Documents/NCS/WWD-n`):

```bash
rm src/display/st7789.c
rm src/display/st7789.h
rm dts/bindings/spi/waveshare,st7789v2.yaml
rm boards/96b_nitrogen_nrf52832.overlay
```

Why: ST7789 was fully replaced by ST7735S and deleted from CMakeLists.txt. The overlay is for the old nRF52832 nitrogen board that no longer exists in this project.

---

## 2. Fix .gitignore

Current `.gitignore` only covers `build/*`. The new build directory is `build_n33/` and VS Code may create others.

Replace `.gitignore` with:

```
build/
build_n33/
build_*/
```

Or just:

```
build*/
```

---

## 3. Delete stale build directory

`build/` was configured against the old 96b_nitrogen / nRF52832 board by a previous VS Code session. It will give wrong results if you flash from it.

```bash
rm -rf build/
```

`build_n33/` is the clean, verified build. Keep it, or let VS Code recreate it via "Build [pristine]" with board set to `nrf52833_ders/nrf52833`.

---

## 4. Update VS Code build config

If you use the nRF Connect VS Code extension, open `.vscode-nrf-connect.json` (or the extension's Build Configuration panel) and confirm:

- **Board**: `nrf52833_ders/nrf52833`
- **Build directory**: `build_n33` (or let pristine rebuild recreate `build/` fresh)
- **Board Root**: `/home/anders/Documents/NCS/WWD-n`

---

## 5. Archive / delete GitHub repo folder

`~/Documents/GitHub/WWD-n` is now fully superseded. Everything in commit `a8cc321` (power.c, display cleanup, RV-3028) has been ported to the NCS repo.

```bash
rm -rf ~/Documents/GitHub/WWD-n
```

Or move it somewhere safe first if you want a reference copy.

---

## 6. Pending firmware TODOs (not cleanup, just tracking)

- **Wire `clock_set_time()` in UIFunctions.c** — the "confirm time" UI action needs to call `clock_set_time(time_offset)`, which should eventually call `rv3028_set_time()` once the hardware is verified.
- **Switch `get_current_time()` to RV-3028** — currently calls counter-based `rtc_get_time()`. Change to `rv3028_get_time()` in `src/peripheral/clock.c` after hardware bringup confirms the RV-3028 is responding.
- **BQ25120A charger driver** — `battery_charging()` in `power.c` is a stub. Needs a Zephyr I2C port of the old TI SDK driver.
