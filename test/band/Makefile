# Native (host gcc) harness for the ST7735S band buffer — see README.md.
# Compiles the real driver sources against stub transport + stub Zephyr headers,
# replays the SPI stream into a fake panel, and checks the result pixel-by-pixel.
#
#   make -C test/band run
SRC := ../../src/display/st7735s
CFLAGS := -O1 -Wall -Wextra -Wno-unused-parameter -Ifake_zephyr -I$(SRC) -I../../src/display

band_test: band_test.c $(SRC)/st7735s.c $(SRC)/gfx.c $(SRC)/fonts.c
	$(CC) $(CFLAGS) $^ -o $@

run: band_test
	./band_test

# Renders the clock face's wear indicator (wear_glyph.h) as ASCII so a human
# can see whether it still reads as a check and a cross. Not an assertion —
# look at the output. See wear_glyph_test.c.
wear_glyph_test: wear_glyph_test.c $(SRC)/st7735s.c $(SRC)/gfx.c $(SRC)/fonts.c
	$(CC) $(CFLAGS) $^ -o $@

run-wear: wear_glyph_test
	./wear_glyph_test

clean:
	rm -f band_test wear_glyph_test

.PHONY: run run-wear clean
