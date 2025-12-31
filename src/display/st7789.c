//*****************************************************************************
//!
//! @file st7789.c
//! @brief st7789v2 driver
//! @author Marian Hrinko
//! @author Anders Bandt
//! @version 2.0
//! @date March 2023
//! @note Updated December 2025 for use with Zephyr
//!
//*****************************************************************************

/* standard C file */
#include <stddef.h>

 /* Zephyr files */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>

/* My driver files */
 #include "st7789.h"


// SPI parameters
#define SPI_DEV DT_COMPAT_GET_ANY_STATUS_OKAY(waveshare_st7789v2)
#define SPI_OP SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_LINES_SINGLE
static struct spi_dt_spec spi_dev = SPI_DT_SPEC_GET(SPI_DEV, SPI_OP, 0);

// configure GPIO
#define DISP_NODE DT_ALIAS(display1)

static const struct gpio_dt_spec dc_dt = GPIO_DT_SPEC_GET(DISP_NODE, dc_gpios);
static const struct gpio_dt_spec rs_dt = GPIO_DT_SPEC_GET(DISP_NODE, reset_gpios);
static const struct gpio_dt_spec bl_dt = GPIO_DT_SPEC_GET(DISP_NODE, bl_gpios);


/** @array Init command */
const uint8_t INIT_ST7789[] = {
  // NUMBER OF COMMANDS
  // ---------------------------------------
  5,                                                    // number of initializers
  // COMMANDS WITH ARGUMENTS & DELAY
  // ---------------------------------------
  ST77XX_SWRESET, 0, 150,                               // Software reset, no arguments, delay >120ms
  ST77XX_SLPOUT, 0, 150,                                // Out of sleep mode, no arguments, delay >120ms
  ST77XX_COLMOD, 1, 0x55, 10,                           // Set color mode, RGB565
  ST77XX_INVON, 0, 150,                                 // Set invert color mode
  ST77XX_DISPON, 0, 200                                 // Display turn on
};

/** @var Location definition */
uint16_t cacheIndexRow = 0;                             // @var array cache memory char index row
uint16_t cacheIndexCol = 0;                             // @var array cache memory char index column

/** @var Screen definition */
struct S_SCREEN Screen = {
  .width = ST7789_WIDTH,
  .height = ST7789_HEIGHT,
  .marginX = ST7789_MARGIN_X,
  .marginY = ST7789_MARGIN_Y
};


/**
 * @desc    SPI Init
 *
 * @return  void
 */
void SPI_Init () {
  if (!spi_is_ready_dt(&spi_dev)) {
    // TODO: figure out how to handle this error
    while (1) { }
  }
}

/**
 * @desc    SPI Send & Receive Byte
 *
 * @param   uint8_t
 *
 * @return  uint8_t
 */
uint8_t SPI_Transfer(uint8_t data)
{
    struct spi_buf buf = {
        .buf = &data,
        .len = 1,
    };
    struct spi_buf_set set = {
        .buffers = &buf,
        .count = 1,
    };

    return spi_write_dt(&spi_dev, &set);
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! STATIC FUNCTIONS ------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// TODO: are the delays in the DC functions below needed?

/* Command Active */
static inline void ST7789_DC_Command() { 
  gpio_pin_set_dt(&dc_dt, 0);
}

/* Data Active */
static inline void ST7789_DC_Data () { 
  gpio_pin_set_dt(&dc_dt, 1);
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! PUBLIC FUNCTIONS ------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @desc    Init st7789 driver
 *
 * @param   uint8_t madctl
 *
 * @return  void
 */
void ST7789_Init(uint8_t madctl)
{
  // initialize the SPI interface
  SPI_Init();

  // initialize GPIO
  // TODO: make this cleaner and take up less lines
    if (!gpio_is_ready_dt(&dc_dt)) {
		return 0;
	}
    if (!gpio_is_ready_dt(&bl_dt)) {
		return 0;
	}
    if (!gpio_is_ready_dt(&rs_dt)) {
		return 0;
	}
  gpio_pin_configure_dt(&dc_dt, GPIO_OUTPUT_ACTIVE);
  gpio_pin_configure_dt(&bl_dt, GPIO_OUTPUT_ACTIVE);
  gpio_pin_configure_dt(&rs_dt, GPIO_OUTPUT_INACTIVE);

  // power up time delay
  k_msleep(10);

  // reset
  // ST7789_Reset_HW();
  ST7789_Reset_SW();

  // init sequence
  ST7789_Init_Sequence(INIT_ST7789);
  
  // set configuration
  ST7789_Set_MADCTL(madctl);
}


/**
 * @desc    Set text position x, y
 *
 * @param   uint8_t x - position
 * @param   uint8_t y - position
 *
 * @return  char
 */
char ST7789_SetPosition (uint8_t x, uint8_t y)
{
  if ((x > ST7789_WIDTH) && (y > ST7789_HEIGHT)) {
    return ST77XX_ERROR;                                // check if coordinates is out of range
  } 
  else if ((x > ST7789_WIDTH) && (y <= ST7789_HEIGHT)) {
    cacheIndexRow = y;                                  // set position y
    cacheIndexCol = 2;                                  // set position x
  } else {
    cacheIndexRow = y;                                  // set position y 
    cacheIndexCol = x;                                  // set position x
  }

  return ST77XX_SUCCESS;
}

/**
 * @desc    Draw String
 *
 * @param   char * string
 * @param   uint16_t color
 * @param   enum S_SIZE (X1, X2, X3)
 *
 * @return  uint8_t
 */
uint8_t ST7789_DrawString(char * str, uint16_t color, enum S_SIZE size)
{
  uint8_t delta_y = CHARS_ROWS_LEN + (size >> 4);
  uint16_t i = 0;
  uint16_t x;
  uint16_t y;

  while (str[i] != '\0') {

    x = cacheIndexCol + (CHARS_COLS_LEN << (size & 0x0F)) + Screen.marginX;
    y = cacheIndexRow + delta_y + Screen.marginY;

    if (x > Screen.width) {
      if (y > Screen.height) {
        return ST77XX_ERROR;
      } else {
        cacheIndexRow += delta_y;
        cacheIndexCol = Screen.marginX;
      } 
    }
    
    ST7789_DrawChar (str[i++], color, size);
  }

  return ST77XX_SUCCESS;
}

/**
 * @desc    Draw character
 *
 * @param   char character
 * @param   uint16_t color
 *
 * @return  void
 */
char ST7789_DrawChar(char character, uint16_t color, enum S_SIZE size) {
  uint8_t letter, idxCol, idxRow;                       // variables
  
  if ((character < 0x20) &&
      (character > 0x7f)) { 
    return ST77XX_ERROR;                                // out of range
  }
  
  idxCol = CHARS_COLS_LEN;                              // last column of character array - 5 columns 
  idxRow = CHARS_ROWS_LEN;                              // last row of character array - 8 rows / bits


  // --------------------------------------
  // SIZE X1 - normal font 1x high, 1x wide
  // --------------------------------------
  if (size == X1) { 
    while (idxCol--) {
      letter = FONTS[character - 32][idxCol];
      while (idxRow--) {
        if (letter & (1 << idxRow)) {
          ST7789_Set_Window (cacheIndexCol+idxCol, cacheIndexCol+idxCol, cacheIndexRow+idxRow, cacheIndexRow+idxRow);
          ST7789_Send_Color_565 (color, 1);
        }
      }
      idxRow = CHARS_ROWS_LEN;
    }
    cacheIndexCol += CHARS_COLS_LEN + 1;
  // --------------------------------------
  // SIZE X2 - font 2x higher, normal wide
  // --------------------------------------
  } else if (size == X2) {
    while (idxCol--) {
      letter = FONTS[character - 32][idxCol];
      while (idxRow--) {
        if (letter & (1 << idxRow)) {
          ST7789_Set_Window (cacheIndexCol+idxCol, cacheIndexCol+idxCol, cacheIndexRow+(idxRow<<1), cacheIndexRow+(idxRow<<1)+1);
          ST7789_Send_Color_565 (color, 2);
        }
      }
      idxRow = CHARS_ROWS_LEN;
    }
    cacheIndexCol += CHARS_COLS_LEN + 1;

  // --------------------------------------
  // SIZE X3 - font 2x higher, 2x wider
  // --------------------------------------
  } else if (size == X3) {
    while (idxCol--) {
      letter = FONTS[character - 32][idxCol];
      while (idxRow--) {
        if (letter & (1 << idxRow)) {
          ST7789_Set_Window (cacheIndexCol+(idxCol<<1), cacheIndexCol+(idxCol<<1)+1, cacheIndexRow+(idxRow<<1), cacheIndexRow+(idxRow<<1)+1);
          ST7789_Send_Color_565 (color, 4);
        }
      }
      idxRow = CHARS_ROWS_LEN;
    }
    cacheIndexCol += CHARS_COLS_LEN + CHARS_COLS_LEN + 1;
  }

  return ST77XX_SUCCESS;
}

/**
 * @desc    Clear screen
 *
 * @param   uint16_t color
 *
 * @return  void
 */
void ST7789_ClearScreen(uint16_t color)
{
  ST7789_Set_Window(0, Screen.width, 0, Screen.height);
  ST7789_Send_Color_565(color, WINDOW_PIXELS);
}

/**
 * @desc    Draw line by Bresenham algoritm
 * @surce   https://en.wikipedia.org/wiki/Bresenham%27s_line_algorithm
 *
 * @param   uint16_t x start position / 0 <= cols <= MAX_X-1
 * @param   uint16_t x end position   / 0 <= cols <= MAX_X-1
 * @param   uint16_t y start position / 0 <= rows <= MAX_Y-1
 * @param   uint16_t y end position   / 0 <= rows <= MAX_Y-1
 * @param   uint16_t color
 *
 * @return  void
 */
char ST7789_DrawLine(uint16_t x1, uint16_t x2, uint16_t y1, uint16_t y2, uint16_t color)
{
  int16_t D;                                            // determinant
  int16_t delta_x, delta_y;                             // deltas
  int16_t trace_x = 1, trace_y = 1;                     // steps

  delta_x = x2 - x1;                                    // delta x
  delta_y = y2 - y1;                                    // delta y

  if (delta_x < 0) {                                    // check if x2 > x1
    delta_x = -delta_x;                                 // inversion delta x
    trace_x = -trace_x;                                 // inversion step x
  }

  if (delta_y < 0) {                                    // check if y2 > y1
    delta_y = -delta_y;                                 // inversion detla y
    trace_y = -trace_y;                                 // inversion step y
  }

  // Bresenham condition for m < 1 (dy < dx)
  // ---------------------------------------
  if (delta_y < delta_x) {
    D = (delta_y << 1) - delta_x;                       // calculate determinant
    ST7789_Set_Window(x1, x1, y1, y1);            // set window
    ST7789_Send_Color_565(color, 1);              // draw pixel by 565 mode
    while (x1 != x2) {                                  // check if x1 equal x2
      x1 += trace_x;                                    // update x1
      if (D >= 0) {                                     // check if determinant is positive
        y1 += trace_y;                                  // update y1
        D -= 2*delta_x;                                 // update determinant
      }
      D += 2*delta_y;                                   // update deteminant
      ST7789_Set_Window(x1, x1, y1, y1);          // set window
      ST7789_Send_Color_565(color, 1);            // draw pixel by 565 mode
    }
  // Bresenham condition for m > 1 (dy > dx)
  // ---------------------------------------
  } else {
    D = delta_y - (delta_x << 1);                       // calculate determinant
    ST7789_Set_Window (x1, x1, y1, y1);            // set window
    ST7789_Send_Color_565 (color, 1);              // draw pixel by 565 mode
    while (y1 != y2) {                                  // check if y2 equal y1
      y1 += trace_y;                                    // update y1
      if (D <= 0) {                                     // check if determinant is positive
        x1 += trace_x;                                  // update y1
        D += 2*delta_y;                                 // update determinant
      }
      D -= 2*delta_x;                                   // update deteminant
      ST7789_Set_Window (x1, x1, y1, y1);          // set window
      ST7789_Send_Color_565 (color, 1);            // draw pixel by 565 mode
    }
  }

  return ST77XX_SUCCESS;                                // success return
}

/**
 * @desc    Fast Draw Line Horizontal
 *
 * @param   uint16_t xs - start position
 * @param   uint16_t xe - end position
 * @param   uint16_t y - position
 * @param   uint16_t color
 *
 * @return void
 */
void ST7789_FastLineHorizontal(uint16_t xs, uint16_t xe, uint16_t y, uint16_t color)
{
  if (xs > xe) {                                        // check if start is > as end
    uint8_t temp = xs;                                  // temporary safe
    xe = xs;                                            // start change for end
    xs = temp;                                          // end change for start
  }
  ST7789_Set_Window(xs, xe, y, y);                // set window
  ST7789_Send_Color_565(color, xe - xs);          // draw pixel by 565 mode
}

/**
 * @desc    Fast Draw Line Vertical
 *
 * @param   uint16_t x - position
 * @param   uint16_t ys - start position
 * @param   uint16_t ye - end position
 * @param   uint16_t color
 *
 * @return  void
 */
void ST7789_FastLineVertical(uint16_t x, uint16_t ys, uint16_t ye, uint16_t color)
{
  if (ys > ye) {                                        // check if start is > as end
    uint8_t temp = ys;                                  // temporary safe
    ye = ys;                                            // start change for end
    ys = temp;                                          // end change for start
  }
  ST7789_Set_Window(x, x, ys, ye);                // set window
  ST7789_Send_Color_565(color, ye - ys);          // draw pixel by 565 mode
}

/**
 * @desc    Draw pixel
 *
 * @param   uint16_t x position / 0 <= cols <= MAX_X-1
 * @param   uint8_t y position / 0 <= rows <= MAX_Y-1
 * @param   uint16_t color
 *
 * @return  void
 */
void ST7789_DrawPixel (uint16_t x, uint8_t y, uint16_t color)
{
  ST7789_Set_Window(x, x, y, y);                  // set window
  ST7789_Send_Color_565(color, 1);                // draw pixel by 565 mode
}

/**
 * @desc    RAM Content Show
 *
 * @return  void
 */
void ST7789_RAM_ContentShow (void)
{
  ST7789_Send_Command(ST77XX_DISPON);             // display content on
}

/**
 * @desc    RAM Content Hide
 *
 * @return  void
 */
void ST7789_RAM_ContentHide(void)
{
  ST7789_Send_Command(ST77XX_DISPOFF);            // display content off
}

/**
 * @desc    Inversion On
 *
 * @return  void
 */
void ST7789_InvertColorOn(void)
{
  ST7789_Send_Command(ST77XX_INVON);              // inversion on
}

/**
 * @desc    Inversion Off
 *
 * @return  void
 */
void ST7789_InvertColorOff (void)
{
  ST7789_Send_Command(ST77XX_INVOFF);             // inversion off
}


/**
 * --------------------------------------------------------------------------------------------+
 * PRIVATE FUNCTIONS
 * --------------------------------------------------------------------------------------------+
 */

/**
 * @desc    Set Configuration LCD
 *
 * @param   uint8_t
 *
 * @return  void
 */
void ST7789_Set_MADCTL(uint8_t madctl)
{
  ST7789_DC_Command ();                              // command (active low)
  SPI_Transfer(ST77XX_MADCTL);                         // Memory Data Access Control
  ST7789_DC_Data ();                                 // data (active high)
  SPI_Transfer(madctl);                                // set configuration like rotation, refresh,...

  if (((0xF0 & madctl) == ST77XX_ROTATE_90) ||
      ((0xF0 & madctl) == ST77XX_ROTATE_270)) {
    Screen.width = ST7789_HEIGHT;
    Screen.height = ST7789_WIDTH;
    Screen.marginX = (Screen.marginX << 1) + 10;
    Screen.marginY = 20;
  }
}

/**
 * @desc    Set window
 *
 * @param   uint16_t xs - start position
 * @param   uint16_t xe - end position
 * @param   uint16_t ys - start position
 * @param   uint16_t ye - end position
 *
 * @return  uint8_t
 */
uint8_t ST7789_Set_Window (uint16_t xs, uint16_t xe, uint16_t ys, uint16_t ye)
{
  if ((xs > xe) || (xe > Screen.width) ||
      (ys > ye) || (ye > Screen.height)) {
    return ST77XX_ERROR;                                // out of range
  }

  // CASET
  // --------------------------------------
  ST7789_DC_Command();                              // command (active low)
  SPI_Transfer(ST77XX_CASET);                          // command

  ST7789_DC_Data();                                 // data (active high)
  SPI_Transfer((uint8_t) (xs >> 8));                   // transfer High Byte
  SPI_Transfer((uint8_t) xs);                          // transfer low Byte
  SPI_Transfer((uint8_t) (xe >> 8));                   // transfer High Byte
  SPI_Transfer((uint8_t) xe);                          // transfer low Byte

  // RASET
  ST7789_DC_Command();                              // command (active low)
  SPI_Transfer(ST77XX_RASET);                          // command

  ST7789_DC_Data();                                 // data (active high)
  SPI_Transfer((uint8_t) (ys >> 8));                   // transfer High Byte
  SPI_Transfer((uint8_t) ys);                          // transfer low Byte
  SPI_Transfer((uint8_t) (ye >> 8));                   // transfer High Byte
  SPI_Transfer((uint8_t) ye);                          // transfer low Byte

  return ST77XX_SUCCESS;                                // success
}

/**
 * @desc    Write Color Pixels
 *
 * @param   uint16_t color
 * @param   uint32_t counter
 *
 * @return  void
 */
void ST7789_Send_Color_565(uint16_t color, uint32_t count)
{
  // RAMWR
  // --------------------------------------
  ST7789_DC_Command();                              // command (active low)
  SPI_Transfer(ST77XX_RAMWR);                          // command

  ST7789_DC_Data();                                 // data (active high)
  while (count--) {
    SPI_Transfer((uint8_t) (color >> 8));              // transfer High Byte
    SPI_Transfer((uint8_t) color);                     // transfer low Byte
  }
}

/**
 * --------------------------------------------------------------------------------------------+
 * PRIMITIVE / PRIVATE FUNCTIONS
 * --------------------------------------------------------------------------------------------+
 */

/**
 * @desc    Hardware Reset Sequence
 *
 *              | >10us | >120ms|
 *          ----        --------
 *              \______/
 *
 * @return  void
 */
void ST7789_Reset_HW(void)
{
  gpio_pin_set_dt(&rs_dt, 0);
  k_usleep(100);  // >10us
  gpio_pin_set_dt(&rs_dt, 1);
  k_msleep(120);  // >120 ms
}


void ST7789_Reset_SW(void)
{
  ST7789_Send_Command(ST77XX_SWRESET);
  k_msleep(120);
  ST7789_Send_Command(ST77XX_SLPOUT);
  k_msleep(120);
}



/**
 * @desc    Init sequence
 *
 * @param   const uint8_t *
 *
 * @return  void
 */
void ST7789_Init_Sequence(const uint8_t * list)
{
  uint8_t arguments;
  uint8_t commands = *list++;

  while (commands--) {
    // COMMAND
    // ------------------------------------
    ST7789_Send_Command(*list++);
    // ARGUMENTS
    // ------------------------------------
    arguments = *list++;
    while (arguments--) {
      ST7789_Send_Data_Byte(*list++);
    }
    // DELAY
    // ------------------------------------
    ST7789_Delay_ms(*list++);
  }
}

/**
 * @desc    Command send
 *
 * @param   uint8_t
 *
 * @return  void
 */
void ST7789_Send_Command(uint8_t data)
{
  ST7789_DC_Command();
  SPI_Transfer(data);
}

/**
 * @desc    8bits data send
 *
 * @param   uint8_t
 *
 * @return  void
 */
void ST7789_Send_Data_Byte(uint8_t data)
{
  ST7789_DC_Data();
  SPI_Transfer (data);
}

/**
 * @desc    Delay
 *
 * @param   uint8_t time in milliseconds / max 256ms
 *
 * @return  void
 */
void ST7789_Delay_ms (uint8_t time) {
  while (time--) {
    k_msleep(1);                                         // 1ms delay
  }
}
