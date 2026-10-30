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
#include "hardware/adc.h"
#include "pico/stdlib.h"
#include "tusb.h"
#include <stdio.h>
#include <string.h>

extern LCD_DIS sLCD_DIS;

#define BUTTON_ROWS 3
#define BUTTON_COLUMNS 3
#define BUTTON_COUNT (BUTTON_ROWS * BUTTON_COLUMNS)
#define BUTTON_GAP 6
#define BUTTON_MARGIN 8
#define MIDI_CC_CHANNEL 0
#define MIDI_CC_NUMBER 48
#define MIDI_CC_SELECTED_VALUE 127
#define VU_ADC_GPIO 26 /* Pico ADC0 input; keep the input between 0 and 3.3V. */
#define VU_ADC_CHANNEL 0
#define VU_ADC_MAX_MILLIVOLTS 3300u
#define VU_ADC_MAX_READING 4095u
#define VU_SIM_STEP_US 250000u
#define VU_BAR_WIDTH 40
#define VU_BAR_SEGMENTS 9
#define VU_BAR_GAP 3

static void send_control_change(uint8_t value)
{
	if (tud_mounted()) {
		uint8_t message[] = {
			(uint8_t)(0xB0 | MIDI_CC_CHANNEL),
			MIDI_CC_NUMBER,
			value
		};
		tud_midi_stream_write(0, message, sizeof(message));
	}
}

static void draw_button(POINT x, POINT y, POINT width, POINT height,
						const char *label, bool selected)
{
	GUI_DrawRectangle(x, y, x + width, y + height, WHITE,
					  selected ? DRAW_FULL : DRAW_EMPTY, DOT_PIXEL_1X1);
	POINT text_width = Font12.Width * strlen(label);
	GUI_DisString_EN(x + (width - text_width) / 2,
					 y + (height - Font12.Height) / 2,
					 label, &Font12, selected ? WHITE : BLACK,
					 selected ? BLACK : WHITE);
}

static void draw_status(POINT grid_x, const char *message)
{
	GUI_DrawRectangle(0, 0, grid_x - 8, 34, BLACK,
					  DRAW_FULL, DOT_PIXEL_1X1);
	GUI_DisString_EN(10, 10, message, &Font12, BLACK, WHITE);
}

static void draw_vu_meter_frame(POINT grid_x, POINT screen_height)
{
	const POINT meter_top = 72;
	const POINT meter_bottom = screen_height - 48;
	const POINT meter_x = (grid_x - VU_BAR_WIDTH) / 2;
	const POINT segment_height =
		(meter_bottom - meter_top - 1 -
		 (VU_BAR_SEGMENTS - 1) * VU_BAR_GAP) / VU_BAR_SEGMENTS;
	const char *title = "ADC VU";
	POINT title_width = Font12.Width * strlen(title);

	GUI_DisString_EN((grid_x - title_width) / 2, 42, title,
					 &Font12, BLACK, WHITE);
	GUI_DisString_EN(8, meter_top - 5, "3.3V", &Font12, BLACK, WHITE);
	GUI_DisString_EN(8, meter_bottom - 6, "0V", &Font12, BLACK, WHITE);
	for (uint8_t segment = 0; segment < VU_BAR_SEGMENTS; segment++) {
		POINT segment_bottom = meter_bottom - 1 -
			segment * (segment_height + VU_BAR_GAP);
		GUI_DrawRectangle(meter_x, segment_bottom - segment_height + 1,
						  meter_x + VU_BAR_WIDTH - 1, segment_bottom,
						  WHITE, DRAW_EMPTY, DOT_PIXEL_1X1);
	}
}

static void draw_vu_meter(POINT grid_x, POINT screen_height,
						  uint16_t adc_reading)
{
	static uint8_t previous_segments = UINT8_MAX;
	static uint16_t previous_display_mv = UINT16_MAX;
	const POINT meter_top = 72;
	const POINT meter_bottom = screen_height - 48;
	const POINT meter_x = (grid_x - VU_BAR_WIDTH) / 2;
	const uint32_t millivolts =
		(uint32_t)adc_reading * VU_ADC_MAX_MILLIVOLTS / VU_ADC_MAX_READING;
	const uint16_t display_mv = (uint16_t)((millivolts / 10u) * 10u);
	const uint8_t active_segments =
		(uint8_t)((uint32_t)adc_reading * VU_BAR_SEGMENTS /
				  VU_ADC_MAX_READING);

	if (active_segments == previous_segments &&
		display_mv == previous_display_mv) {
		return;
	}

	const POINT segment_height =
		(meter_bottom - meter_top - 1 -
		 (VU_BAR_SEGMENTS - 1) * VU_BAR_GAP) / VU_BAR_SEGMENTS;
	for (uint8_t segment = 0; segment < VU_BAR_SEGMENTS; segment++) {
		POINT segment_bottom = meter_bottom - 1 -
			segment * (segment_height + VU_BAR_GAP);
		POINT segment_top = segment_bottom - segment_height + 1;
		COLOR color = segment >= 7 ? RED : segment >= 5 ? YELLOW : GREEN;
		GUI_DrawRectangle(meter_x + 1, segment_top + 1,
						  meter_x + VU_BAR_WIDTH - 2, segment_bottom - 1,
						  segment < active_segments ? color : BLACK,
						  DRAW_FULL, DOT_PIXEL_1X1);
	}

	char value_text[16];
	snprintf(value_text, sizeof(value_text), "%u.%02u V",
			 (unsigned int)(display_mv / 1000u),
			 (unsigned int)((display_mv % 1000u) / 10u));
	POINT value_width = Font12.Width * strlen(value_text);
	POINT value_x = (grid_x - value_width) / 2;
	POINT value_y = screen_height - 30;
	GUI_DrawRectangle(value_x, value_y, value_x + value_width,
					  value_y + Font12.Height, BLACK, DRAW_FULL,
					  DOT_PIXEL_1X1);
	GUI_DisString_EN(value_x, value_y, value_text, &Font12, BLACK, WHITE);

	previous_segments = active_segments;
	previous_display_mv = display_mv;
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

	adc_init();
	adc_gpio_init(VU_ADC_GPIO);
	adc_select_input(VU_ADC_CHANNEL);

	GUI_Clear(BLACK);
	POINT grid_x = sLCD_DIS.LCD_Dis_Column / 2 + BUTTON_MARGIN;
	POINT grid_y = BUTTON_MARGIN;
	POINT button_width =
		(sLCD_DIS.LCD_Dis_Column - grid_x - BUTTON_MARGIN -
		 (BUTTON_COLUMNS - 1) * BUTTON_GAP) / BUTTON_COLUMNS;
	POINT button_height =
		(sLCD_DIS.LCD_Dis_Page - 2 * BUTTON_MARGIN -
		 (BUTTON_ROWS - 1) * BUTTON_GAP) / BUTTON_ROWS;
	draw_vu_meter_frame(grid_x, sLCD_DIS.LCD_Dis_Page);
	const char *button_labels[BUTTON_COUNT] = {
		"LED ON", "LED OFF", "RES 3",
		"SQUARE", "VU SIM", "RES 6",
		"RES 7", "RES 8", "RES 9"
	};
	bool res4_selected = false;
	bool vu_sim_enabled = false;
	uint8_t simulated_level = 0;
	int8_t simulated_direction = 1;
	uint32_t last_simulated_step_us = time_us_32();

	for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
		POINT column = i % BUTTON_COLUMNS;
		POINT row = i / BUTTON_COLUMNS;
		POINT x = grid_x + column * (button_width + BUTTON_GAP);
		POINT y = grid_y + row * (button_height + BUTTON_GAP);
		draw_button(x, y, button_width, button_height, button_labels[i],
					false);
	}

	draw_status(grid_x, "ADC INPUT");
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
					} else if (button_index == 3) {
						res4_selected = !res4_selected;
						send_control_change(res4_selected
							? MIDI_CC_SELECTED_VALUE : 0);

						POINT x = grid_x + column * (button_width + BUTTON_GAP);
						POINT y = grid_y + row * (button_height + BUTTON_GAP);
						draw_button(x, y, button_width, button_height,
									button_labels[button_index],
									res4_selected);
						draw_status(grid_x, res4_selected
							? "CC 48 = 127" : "CC 48 = 0");
					} else if (button_index == 4) {
						vu_sim_enabled = !vu_sim_enabled;
						simulated_level = 0;
						simulated_direction = 1;
						last_simulated_step_us = time_us_32();

						POINT x = grid_x + column * (button_width + BUTTON_GAP);
						POINT y = grid_y + row * (button_height + BUTTON_GAP);
						draw_button(x, y, button_width, button_height,
									button_labels[button_index],
									vu_sim_enabled);
						draw_status(grid_x, vu_sim_enabled
							? "VU SIM ON" : "ADC INPUT");
					}
				}
			}
			touch_latched = true;
		} else {
			touch_latched = false;
		}

		uint16_t adc_reading;
		if (vu_sim_enabled) {
			uint32_t now_us = time_us_32();
			if ((uint32_t)(now_us - last_simulated_step_us) >=
				VU_SIM_STEP_US) {
				if (simulated_level == 0) {
					simulated_direction = 1;
				} else if (simulated_level == VU_BAR_SEGMENTS) {
					simulated_direction = -1;
				}
				simulated_level += simulated_direction;
				last_simulated_step_us = now_us;
			}
			adc_reading = (uint16_t)((uint32_t)simulated_level *
				VU_ADC_MAX_READING / VU_BAR_SEGMENTS);
		} else {
			adc_reading = adc_read();
		}
		draw_vu_meter(grid_x, sLCD_DIS.LCD_Dis_Page, adc_reading);

		sleep_ms(20);
	}

	return 0;
}
