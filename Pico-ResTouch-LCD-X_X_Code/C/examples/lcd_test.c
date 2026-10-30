/*****************************************************************************
* | File      	:   lcd_2in8.c
* | Author      :   Waveshare team
* | Function    :   2.9inch e-paper V2 test demo
* | Info        :
*----------------
* |	This version:   V1.0
* | Date        :   2021-01-20
* | Info        :
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documnetation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to  whom the Software is
# furished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS OR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#
******************************************************************************/
#include "main.h"
#include "LCD_Driver.h"
#include "LCD_GUI.h"
#include "LCD_Touch.h"
#include "DEV_Config.h"
#include "pico/stdlib.h"
#include <string.h>

extern LCD_DIS sLCD_DIS;

#define BUTTON_ROWS 3
#define BUTTON_COLUMNS 3
#define BUTTON_COUNT (BUTTON_ROWS * BUTTON_COLUMNS)
#define BUTTON_GAP 6
#define BUTTON_MARGIN 8

static void draw_button(POINT x, POINT y, POINT width, POINT height,
						const char *label)
{
	GUI_DrawRectangle(x, y, x + width, y + height,
					  WHITE, DRAW_EMPTY, DOT_PIXEL_1X1);
	POINT text_width = Font12.Width * strlen(label);
	GUI_DisString_EN(x + (width - text_width) / 2,
					 y + (height - Font12.Height) / 2,
					 label, &Font12, BLACK, WHITE);
}

static void draw_status(POINT grid_x, const char *message)
{
	GUI_DrawRectangle(0, 70, grid_x - 8, 120, BLACK,
					  DRAW_FULL, DOT_PIXEL_1X1);
	GUI_DisString_EN(10, 90, message, &Font16, BLACK, WHITE);
}

int lcd_test(void)
{
	System_Init();
	LCD_SCAN_DIR lcd_scan_dir = D2U_L2R;
	LCD_Init(lcd_scan_dir,800);
	TP_Init(lcd_scan_dir);
	TP_GetAdFac();

	gpio_init(PICO_DEFAULT_LED_PIN);
	gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
	gpio_put(PICO_DEFAULT_LED_PIN, 0);

	GUI_Clear(BLACK);
	POINT grid_x = sLCD_DIS.LCD_Dis_Column / 2 + BUTTON_MARGIN;
	POINT grid_y = BUTTON_MARGIN;
	POINT button_width =
		(sLCD_DIS.LCD_Dis_Column - grid_x - BUTTON_MARGIN -
		 (BUTTON_COLUMNS - 1) * BUTTON_GAP) / BUTTON_COLUMNS;
	POINT button_height =
		(sLCD_DIS.LCD_Dis_Page - 2 * BUTTON_MARGIN -
		 (BUTTON_ROWS - 1) * BUTTON_GAP) / BUTTON_ROWS;
	const char *button_labels[BUTTON_COUNT] = {
		"LED ON", "LED OFF", "RES 3",
		"RES 4", "RES 5", "RES 6",
		"RES 7", "RES 8", "RES 9"
	};

	for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
		POINT column = i % BUTTON_COLUMNS;
		POINT row = i / BUTTON_COLUMNS;
		POINT x = grid_x + column * (button_width + BUTTON_GAP);
		POINT y = grid_y + row * (button_height + BUTTON_GAP);
		draw_button(x, y, button_width, button_height, button_labels[i]);
	}

	draw_status(grid_x, "LED OFF");
	bool touch_latched = false;
	POINT touch_x;
	POINT touch_y;
	while(1) {
		if (TP_GetTouchPoint(&touch_x, &touch_y)) {
			if (!touch_latched && touch_x >= grid_x && touch_y >= grid_y) {
				POINT local_x = touch_x - grid_x;
				POINT local_y = touch_y - grid_y;
				POINT column = local_x / (button_width + BUTTON_GAP);
				POINT row = local_y / (button_height + BUTTON_GAP);
				if (column < BUTTON_COLUMNS && row < BUTTON_ROWS &&
					local_x % (button_width + BUTTON_GAP) < button_width &&
					local_y % (button_height + BUTTON_GAP) < button_height) {
					POINT button_index = row * BUTTON_COLUMNS + column;
					if (button_index == 0) {
						gpio_put(PICO_DEFAULT_LED_PIN, 1);
						draw_status(grid_x, "LED ON");
					} else if (button_index == 1) {
						gpio_put(PICO_DEFAULT_LED_PIN, 0);
						draw_status(grid_x, "LED OFF");
					}
				}
			}
			touch_latched = true;
		} else {
			touch_latched = false;
		}

		sleep_ms(20);
	}

	return 0;
}
