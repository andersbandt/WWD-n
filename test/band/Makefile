# Native (host gcc) harness for the ST7735S band buffer — see README.md.
# Compiles the real driver sources against stub transport + stub Zephyr headers,
# replays the SPI stream into a fake panel, and checks the result pixel-by-pixel.
#
#   make -C test/band run
SRC := ../../src/display/st7735s
CFLAGS := -O1 -Wall -Wextra -Wno-unused-parameter -Ifake_zephyr -I$(SRC)

band_test: band_test.c $(SRC)/st7735s.c $(SRC)/gfx.c $(SRC)/fonts.c
	$(CC) $(CFLAGS) $^ -o $@

run: band_test
	./band_test

clean:
	rm -f band_test

.PHONY: run clean
