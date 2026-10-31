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
#define VU_ADC_GPIO 26 /* Pico ADC0 input; keep the input between 0 and 3.3V. */
#define VU_ADC_CHANNEL 0
#define VU_ADC_MAX_MILLIVOLTS 3300u
#define VU_ADC_MAX_READING 4095u
#define VU_BAR_WIDTH 40
#define VU_BAR_SEGMENTS 9
#define VU_BAR_GAP 3

static void send_control_change(uint8_t cc_number, uint8_t value)
{
	if (tud_mounted()) {
		uint8_t message[] = {
			(uint8_t)(0xB0 | MIDI_CC_CHANNEL),
			cc_number,
			value
		};
		tud_midi_stream_write(0, message, sizeof(message));
	}
}

static void draw_button(POINT x, POINT y, POINT width, POINT height,
						const char *label, bool selected)
{
	if (!selected) {
		GUI_DrawRectangle(x, y, x + width, y + height, BLACK,
						  DRAW_FULL, DOT_PIXEL_1X1);
	}
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

static void draw_cc_slider(POINT x, POINT y, POINT width, POINT height,
						  uint8_t value, bool visible)
{
	if (!visible) {
		GUI_DrawRectangle(x - 3, y - 3, x + width + 3, y + height + 3, BLACK,
					  DRAW_FULL, DOT_PIXEL_1X1);
		return;
	}

	GUI_DrawRectangle(x, y, x + width, y + height, WHITE,
					  DRAW_EMPTY, DOT_PIXEL_1X1);
	POINT inner_x0 = x + 3;
	POINT inner_x1 = x + width - 3;
	POINT track_top = y + 4;
	POINT track_bottom = y + height - 4;
	POINT fill_height = (POINT)((uint32_t)(value * (track_bottom - track_top + 1)) /
							   127u);
	POINT fill_top = track_bottom - fill_height + 1;
	GUI_DrawRectangle(inner_x0, track_top, inner_x1, fill_top - 1,
					  BLACK, DRAW_FULL, DOT_PIXEL_1X1);
	GUI_DrawRectangle(inner_x0, fill_top, inner_x1, track_bottom,
					  GREEN, DRAW_FULL, DOT_PIXEL_1X1);
}

static void draw_cc_send_toggle(POINT x, POINT y, POINT width, POINT height,
								bool enabled, bool visible)
{
	if (!visible) {
		GUI_DrawRectangle(x - 2, y - 2, x + width + 2, y + height + 2,
						  BLACK, DRAW_FULL, DOT_PIXEL_1X1);
		return;
	}

	COLOR background = enabled ? WHITE : BLACK;
	GUI_DrawRectangle(x + 1, y + 1, x + width - 1, y + height - 1,
					  background, DRAW_FULL, DOT_PIXEL_1X1);
	GUI_DrawRectangle(x, y, x + width, y + height, WHITE,
					  DRAW_EMPTY, DOT_PIXEL_1X1);
}

static void draw_vu_meter_frame(POINT grid_x, POINT screen_height)
{
	const POINT meter_top = 72;
	const POINT meter_bottom = screen_height - 48;
	const POINT meter_x = 12;
	const POINT label_x = meter_x + VU_BAR_WIDTH + 8;
	const POINT segment_height =
		(meter_bottom - meter_top - 1 -
		 (VU_BAR_SEGMENTS - 1) * VU_BAR_GAP) / VU_BAR_SEGMENTS;

	GUI_DisString_EN(label_x, meter_top - 5, "3.3V", &Font12, BLACK, WHITE);
	GUI_DisString_EN(label_x, meter_bottom - 6, "0V", &Font12, BLACK, WHITE);
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
	const POINT meter_x = 12;
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
	POINT value_x = meter_x + (VU_BAR_WIDTH - value_width) / 2;
	POINT value_y = meter_bottom + 8;
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
		"AMP", "VU SIM", "RES 6",
		"RES 7", "RES 8", "RES 9"
	};
	const uint8_t button_cc_numbers[BUTTON_COUNT] = {
		16, 121, 122, 120, 123, 124, 125, 126, 127
	};
	int8_t selected_button = -1;
	uint8_t midi_slider_values[BUTTON_COUNT] = { 0 };
	const POINT left_meter_x = 12;
	const POINT meter_end_x = left_meter_x + VU_BAR_WIDTH + 44;
	const POINT slider_width = 24;
	const POINT slider_height = 150;
	const POINT slider_x = ((grid_x + meter_end_x) / 2) - (slider_width / 2);
	const POINT slider_y = 90;
	const POINT toggle_width = slider_width;
	const POINT toggle_height = 26;
	const POINT toggle_x = slider_x;
	const POINT toggle_y = slider_y + slider_height + 12;
	bool cc_send_enabled = false;

	for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
		POINT column = i % BUTTON_COLUMNS;
		POINT row = i / BUTTON_COLUMNS;
		POINT x = grid_x + column * (button_width + BUTTON_GAP);
		POINT y = grid_y + row * (button_height + BUTTON_GAP);
		draw_button(x, y, button_width, button_height, button_labels[i],
					false);
	}
	draw_cc_slider(slider_x, slider_y, slider_width, slider_height,
					0, false);
	draw_cc_send_toggle(toggle_x, toggle_y, toggle_width, toggle_height,
							false, false);

	draw_status(grid_x, "ADC INPUT");
	bool touch_latched = false;
	POINT touch_x;
	POINT touch_y;
	while(1) {
		if (TP_GetTouchPoint(&touch_x, &touch_y)) {
			if (!touch_latched) {
				if (touch_x >= grid_x && touch_y >= grid_y) {
					POINT local_x = touch_x - grid_x;
					POINT local_y = touch_y - grid_y;
					POINT column = local_x / (button_width + BUTTON_GAP);
					POINT row = local_y / (button_height + BUTTON_GAP);
					if (column < BUTTON_COLUMNS && row < BUTTON_ROWS &&
						local_x % (button_width + BUTTON_GAP) < button_width &&
						local_y % (button_height + BUTTON_GAP) < button_height) {
						POINT button_index = row * BUTTON_COLUMNS + column;
						bool deselecting = selected_button == (int8_t)button_index;
						selected_button = deselecting
							? -1 : (int8_t)button_index;
						for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
							POINT x = grid_x + (i % BUTTON_COLUMNS) * (button_width + BUTTON_GAP);
							POINT y = grid_y + (i / BUTTON_COLUMNS) * (button_height + BUTTON_GAP);
							draw_button(x, y, button_width, button_height,
										button_labels[i],
										!deselecting && i == button_index);
						}
						if (deselecting) {
							draw_status(grid_x, "BUTTON DESELECTED");
						} else {
							char status_text[32];
							snprintf(status_text, sizeof(status_text),
									 "CC %u SELECTED",
									 (unsigned int)button_cc_numbers[button_index]);
							draw_status(grid_x, status_text);
						}
					}
				} else if (selected_button >= 0 && touch_x >= slider_x &&
					touch_x <= slider_x + slider_width &&
					touch_y >= slider_y && touch_y <= slider_y + slider_height) {
					uint32_t adjusted_range = (uint32_t)(slider_height - 8);
					uint32_t relative = (uint32_t)(touch_y - (slider_y + 4));
					if (relative > adjusted_range) {
						relative = adjusted_range;
					}
					midi_slider_values[selected_button] = (uint8_t)(127u -
						((relative * 127u) / adjusted_range));
					if (cc_send_enabled) {
						send_control_change(
							button_cc_numbers[selected_button],
							midi_slider_values[selected_button]);
					}
					draw_cc_slider(slider_x, slider_y, slider_width, slider_height,
						midi_slider_values[selected_button], true);
					char status_text[32];
					snprintf(status_text, sizeof(status_text), "CC %u %s %u",
						 (unsigned int)button_cc_numbers[selected_button],
						 cc_send_enabled ? "=" : "held at",
						 (unsigned int)midi_slider_values[selected_button]);
					draw_status(grid_x, status_text);
				} else if (selected_button >= 0 &&
					touch_x >= toggle_x && touch_x <= toggle_x + toggle_width &&
					touch_y >= toggle_y && touch_y <= toggle_y + toggle_height) {
					cc_send_enabled = !cc_send_enabled;
					if (cc_send_enabled) {
						send_control_change(
							button_cc_numbers[selected_button],
							midi_slider_values[selected_button]);
					}
					draw_cc_send_toggle(toggle_x, toggle_y, toggle_width,
										toggle_height, cc_send_enabled, true);
					draw_status(grid_x, cc_send_enabled
						? "CC SEND ON" : "CC SEND OFF");
				}
			}
			touch_latched = true;
		} else {
			touch_latched = false;
		}

		draw_cc_slider(slider_x, slider_y, slider_width, slider_height,
				selected_button >= 0 ? midi_slider_values[selected_button] : 0,
				selected_button >= 0);
		draw_cc_send_toggle(toggle_x, toggle_y, toggle_width, toggle_height,
							cc_send_enabled, selected_button >= 0);

		uint16_t adc_reading = adc_read();
		draw_vu_meter(grid_x, sLCD_DIS.LCD_Dis_Page, adc_reading);

		sleep_ms(20);
	}

	return 0;
}
