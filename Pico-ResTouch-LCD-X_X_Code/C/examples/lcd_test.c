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

#define BUTTON_ROWS 9
#define BUTTON_COLUMNS 3
#define FOUR_BUTTON_ROWS 3
#define OSC1_WAVE_BUTTON_START BUTTON_COLUMNS
#define OSC1_WAVE_BUTTON_COUNT BUTTON_COLUMNS
#define OSC2_WAVE_BUTTON_START (BUTTON_COLUMNS * 4)
#define OSC2_WAVE_BUTTON_COUNT BUTTON_COLUMNS
#define COLOR_BUTTON_START ((BUTTON_ROWS - FOUR_BUTTON_ROWS) * BUTTON_COLUMNS)
#define TOGGLE_BUTTON_START (COLOR_BUTTON_START + 8)
#define BUTTON_COUNT (TOGGLE_BUTTON_START + 4)
#define BUTTON_GAP 6
#define BUTTON_MARGIN 8
#define MIDI_CC_CHANNEL 0
#define OSC_VIEW_CC_NUMBER 46
#define RESET_CC_NUMBER 58
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

static uint8_t apply_adc_offset(uint8_t value, uint16_t adc_reading,
								uint8_t scale, bool inverted)
{
	uint16_t offset =
		(uint32_t)adc_reading * scale / VU_ADC_MAX_READING;
	if (inverted) {
		return offset > value ? 0 : (uint8_t)(value - offset);
	}
	uint16_t adjusted_value = value + offset;
	return adjusted_value > 127u ? 127u : (uint8_t)adjusted_value;
}

static bool has_audio_control(uint8_t button_index)
{
	return button_index <= 2 ||
		(button_index >= 6 && button_index <= 11) ||
		(button_index >= 15 && button_index < TOGGLE_BUTTON_START);
}

static bool has_dual_slider_audio(uint8_t button_index)
{
	return button_index >= COLOR_BUTTON_START &&
		button_index < TOGGLE_BUTTON_START;
}

static uint8_t get_slider_cc_number(uint8_t button_index,
									uint8_t slider_index,
									const uint8_t *button_cc_numbers)
{
	if (has_dual_slider_audio(button_index)) {
		if (button_index < COLOR_BUTTON_START + 4) {
			return slider_index == 0 ? 22 : 126;
		}
		return slider_index == 0 ? 23 : 127;
	}
	return button_cc_numbers[button_index];
}

static bool is_three_column_slider_button(uint8_t button_index)
{
	return button_index < COLOR_BUTTON_START &&
		has_audio_control(button_index);
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

static void draw_grid_button(POINT grid_x, POINT grid_y, POINT button_width,
							 POINT final_row_button_width, POINT button_height,
							 uint8_t button_index, const char *label,
							 bool selected, bool send_enabled,
							 bool audio_enabled)
{
	POINT row = button_index < COLOR_BUTTON_START
		? button_index / BUTTON_COLUMNS
		: BUTTON_ROWS - FOUR_BUTTON_ROWS +
			(button_index - COLOR_BUTTON_START) / 4;
	POINT column = button_index < COLOR_BUTTON_START
		? button_index % BUTTON_COLUMNS
		: (button_index - COLOR_BUTTON_START) % 4;
	POINT current_button_width = button_index >= COLOR_BUTTON_START
		? final_row_button_width : button_width;
	POINT x = grid_x + column * (current_button_width + BUTTON_GAP);
	POINT y = grid_y + row * (button_height + BUTTON_GAP);
	draw_button(x, y, current_button_width, button_height, label, selected);
	if (is_three_column_slider_button(button_index) &&
		!selected && send_enabled && audio_enabled) {
		POINT text_width = Font12.Width * strlen(label);
		GUI_DisString_EN(x + (current_button_width - text_width) / 2,
			y + (button_height - Font12.Height) / 2,
			label, &Font12, WHITE, GREEN);
	}
}

static void draw_osc_view_button(POINT x, POINT y, POINT width, POINT height,
								 bool osc2_view_enabled)
{
	draw_button(x, y, width, height,
				osc2_view_enabled ? "OSC2 VIEW" : "OSC1 VIEW",
				osc2_view_enabled);
}

static void draw_reset_button(POINT x, POINT y, POINT width, POINT height,
							  bool pressed)
{
	draw_button(x, y, width, height, "RESET", pressed);
}

static void draw_audio_control_button(POINT x, POINT y, POINT width,
									  POINT height, bool enabled,
									  bool visible)
{
	if (!visible) {
		GUI_DrawRectangle(x - 3, y - 3, x + width + 3, y + height + 3,
						  BLACK, DRAW_FULL, DOT_PIXEL_1X1);
		return;
	}

	draw_button(x, y, width, height, "AUDIO", enabled);
}

static void draw_audio_invert_button(POINT x, POINT y, POINT width,
									 POINT height, bool inverted,
									 bool visible)
{
	if (!visible) {
		GUI_DrawRectangle(x - 2, y - 2, x + width + 2, y + height + 2,
						  BLACK, DRAW_FULL, DOT_PIXEL_1X1);
		return;
	}

	draw_button(x, y, width, height, "INV", inverted);
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
	POINT button_table_width =
		BUTTON_COLUMNS * button_width + (BUTTON_COLUMNS - 1) * BUTTON_GAP;
	POINT final_row_button_width =
		(button_table_width - 3 * BUTTON_GAP) / 4;
	POINT button_height =
		(sLCD_DIS.LCD_Dis_Page - 2 * BUTTON_MARGIN -
		 (BUTTON_ROWS - 1) * BUTTON_GAP) / BUTTON_ROWS;
	draw_vu_meter_frame(grid_x, sLCD_DIS.LCD_Dis_Page);
	const char *button_labels[BUTTON_COUNT] = {
		"OSC1 AMP", "OSC1 MOVE", "OSC1 PM",
		"OSC1 SQR", "OSC1 SAW", "OSC1 M",
		"OSC1 RATE", "OSC1 FREQ", "OSC1 RM",
		"OSC2 AMP", "OSC2 MOVE", "OSC2 PM",
		"OSC2 SQR", "OSC2 SAW", "OSC2 M",
		"OSC2 RATE", "OSC2 FREQ", "OSC2 RM",
		"SAT", "BAND1", "BAND2", "BAND3",
		"rM", "MrM", "IFB", "mIFB",
		"OSC1 FB", "OSC2 FB", "M/DIST", "F/DIST"
	};
	const uint8_t button_cc_numbers[BUTTON_COUNT] = {
		120, 121, 122, 0, 0, 0, 16, 17, 18, 123, 124, 125,
		0, 0, 0, 19, 20, 21, 22, 22, 22, 22, 23, 23, 23, 23,
		0, 0, 0, 0
	};
	const uint8_t button_toggle_cc_numbers[BUTTON_COUNT] = {
		0, 0, 0, 48, 64, 32, 0, 0, 0, 0, 0, 0,
		51, 67, 35, 0, 0, 0, 70, 0, 38, 54, 0, 39, 55, 71,
		43, 44, 60, 61
	};
	int8_t selected_button = -1;
	int8_t selected_four_button = -1;
	uint8_t midi_slider_values[BUTTON_COUNT][2] = { { 0 } };
	midi_slider_values[0][0] = 127;
	midi_slider_values[9][0] = 127;
	midi_slider_values[6][0] = 63;
	midi_slider_values[7][0] = 63;
	midi_slider_values[8][0] = 63;
	midi_slider_values[15][0] = 63;
	midi_slider_values[16][0] = 63;
	midi_slider_values[17][0] = 63;
	bool button_toggle_states[BUTTON_COUNT] = { false };
	const POINT left_meter_x = 12;
	const POINT meter_end_x = left_meter_x + VU_BAR_WIDTH + 44;
	const POINT slider_width = 24;
	const POINT slider_height = 150;
	const POINT slider_x = ((grid_x + meter_end_x) / 2) -
		(slider_width / 2) - 38;
	const POINT second_slider_x = slider_x + slider_width + 10;
	const POINT slider_y = 90;
	const POINT view_button_width = Font12.Width * strlen("OSC1 VIEW") + 8;
	const POINT view_button_x = grid_x - view_button_width - BUTTON_GAP;
	const POINT view_button_y = grid_y;
	const POINT reset_button_width = view_button_width;
	const POINT reset_button_x = view_button_x - reset_button_width - BUTTON_GAP;
	const POINT reset_button_y = grid_y;
	const POINT al_button_width = Font12.Width * strlen("AL") + 8;
	const POINT al_button_height = button_height;
	const POINT al_button_x = 10;
	const POINT al_button_y = 40;
	bool al_button_enabled = false;
	bool osc2_view_enabled = false;
	const POINT toggle_width = slider_width;
	const POINT toggle_height = 26;
	const POINT toggle_x = slider_x;
	const POINT second_toggle_x = second_slider_x;
	const POINT toggle_y = slider_y + slider_height + 12;
	const POINT audio_button_width = Font12.Width * strlen("AUDIO") + 8;
	const POINT audio_button_height = 26;
	const POINT audio_button_x = second_slider_x;
	const POINT audio_button_y = slider_y;
	const POINT audio_scale_slider_x = second_slider_x;
	const POINT dual_audio_button_x[2] = {
		slider_x - audio_button_width - BUTTON_GAP,
		second_slider_x + slider_width + BUTTON_GAP
	};
	const POINT dual_audio_scale_x[2] = {
		dual_audio_button_x[0] + (audio_button_width - slider_width) / 2,
		dual_audio_button_x[1] + (audio_button_width - slider_width) / 2
	};
	const POINT audio_scale_slider_y =
		audio_button_y + audio_button_height + 8;
	const POINT audio_scale_slider_height =
		slider_height - audio_button_height - 8;
	const POINT audio_invert_button_y = toggle_y;
	const POINT audio_invert_button_height = 22;
	bool audio_control_enabled[BUTTON_COUNT][2] = { { false } };
	bool audio_control_inverted[BUTTON_COUNT][2] = { { false } };
	uint8_t audio_scale_values[BUTTON_COUNT][2] = { { 0 } };
	bool cc_send_enabled[BUTTON_COUNT][2] = { { false } };
	bool startup_toggle_reset_sent = false;
	uint8_t last_audio_cc_values[BUTTON_COUNT][2];
	memset(last_audio_cc_values, UINT8_MAX, sizeof(last_audio_cc_values));
	int8_t displayed_audio_button = -1;
	bool displayed_audio_enabled[2] = { false, false };
	bool displayed_audio_scale_visible[2] = { false, false };
	uint8_t displayed_audio_scale[2] = { UINT8_MAX, UINT8_MAX };
	bool displayed_audio_invert_visible[2] = { false, false };
	bool displayed_audio_inverted[2] = { false, false };
	bool second_slider_was_visible = false;

	for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
		POINT row = i < COLOR_BUTTON_START
			? i / BUTTON_COLUMNS
			: BUTTON_ROWS - FOUR_BUTTON_ROWS +
				(i - COLOR_BUTTON_START) / 4;
		POINT column = i < COLOR_BUTTON_START
			? i % BUTTON_COLUMNS : (i - COLOR_BUTTON_START) % 4;
		POINT current_button_width = i >= COLOR_BUTTON_START
			? final_row_button_width : button_width;
		POINT x = grid_x + column * (current_button_width + BUTTON_GAP);
		POINT y = grid_y + row * (button_height + BUTTON_GAP);
		draw_button(x, y, current_button_width, button_height,
			button_labels[i], button_toggle_states[i]);
	}
	draw_osc_view_button(view_button_x, view_button_y, view_button_width,
		button_height, false);
	draw_reset_button(reset_button_x, reset_button_y, reset_button_width,
		button_height, false);
	draw_cc_slider(slider_x, slider_y, slider_width, slider_height,
					0, false);
	draw_cc_slider(second_slider_x, slider_y, slider_width, slider_height,
					0, false);
	draw_cc_send_toggle(toggle_x, toggle_y, toggle_width, toggle_height,
							false, false);
	draw_cc_send_toggle(second_toggle_x, toggle_y, toggle_width, toggle_height,
							false, false);
	draw_audio_control_button(audio_button_x, audio_button_y,
		audio_button_width, audio_button_height, false, false);

	draw_status(grid_x, "ADC INPUT");
	draw_osc_view_button(view_button_x, view_button_y, view_button_width,
		button_height, osc2_view_enabled);
	draw_reset_button(reset_button_x, reset_button_y, reset_button_width,
		button_height, false);
	draw_button(al_button_x, al_button_y, al_button_width,
		al_button_height, "AL", al_button_enabled);
	bool touch_latched = false;
	bool touch_ready = false;
	bool reset_button_pressed = false;
	POINT touch_x;
	POINT touch_y;
	while(1) {
		uint16_t adc_reading = adc_read();
		if (!startup_toggle_reset_sent && tud_mounted()) {
			for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
				if (button_toggle_cc_numbers[i] != 0) {
					send_control_change(button_toggle_cc_numbers[i], 0);
				}
			}
			startup_toggle_reset_sent = true;
		}
		if (TP_GetTouchPoint(&touch_x, &touch_y)) {
			if (!touch_ready) {
				touch_latched = true;
			}
			if (!touch_latched) {
				if (touch_x >= reset_button_x &&
					touch_x <= reset_button_x + reset_button_width &&
					touch_y >= reset_button_y &&
					touch_y <= reset_button_y + button_height) {
					reset_button_pressed = true;
					if (selected_button >= 0 &&
						button_toggle_cc_numbers[selected_button] != 0) {
						send_control_change(
							button_toggle_cc_numbers[selected_button], 0);
					}
					for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
						if (button_toggle_cc_numbers[i] != 0) {
							send_control_change(
								button_toggle_cc_numbers[i], 0);
						}
					}
					if (osc2_view_enabled) {
						send_control_change(OSC_VIEW_CC_NUMBER, 0);
					}
					memset(midi_slider_values, 0, sizeof(midi_slider_values));
					midi_slider_values[0][0] = 127;
					midi_slider_values[9][0] = 127;
					midi_slider_values[6][0] = 63;
					midi_slider_values[7][0] = 63;
					midi_slider_values[8][0] = 63;
					midi_slider_values[15][0] = 63;
					midi_slider_values[16][0] = 63;
					midi_slider_values[17][0] = 63;
					memset(button_toggle_states, 0,
						sizeof(button_toggle_states));
					memset(audio_control_enabled, 0,
						sizeof(audio_control_enabled));
					memset(audio_control_inverted, 0,
						sizeof(audio_control_inverted));
					memset(audio_scale_values, 0,
						sizeof(audio_scale_values));
					memset(last_audio_cc_values, UINT8_MAX,
						sizeof(last_audio_cc_values));
					memset(cc_send_enabled, 0, sizeof(cc_send_enabled));
					selected_button = -1;
					selected_four_button = -1;
					al_button_enabled = false;
					if (osc2_view_enabled) {
						osc2_view_enabled = false;
					}
					for (uint8_t i = 0; i < COLOR_BUTTON_START; i++) {
						if (button_cc_numbers[i] != 0) {
							send_control_change(button_cc_numbers[i],
								midi_slider_values[i][0]);
						}
					}
					send_control_change(22, 0);
					send_control_change(126, 0);
					send_control_change(23, 0);
					send_control_change(127, 0);
					for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
						POINT button_row = i < COLOR_BUTTON_START
							? i / BUTTON_COLUMNS
							: BUTTON_ROWS - FOUR_BUTTON_ROWS +
								(i - COLOR_BUTTON_START) / 4;
						POINT button_column = i < COLOR_BUTTON_START
							? i % BUTTON_COLUMNS
							: (i - COLOR_BUTTON_START) % 4;
						POINT current_button_width = i >= COLOR_BUTTON_START
							? final_row_button_width : button_width;
						POINT x = grid_x + button_column *
							(current_button_width + BUTTON_GAP);
						POINT y = grid_y + button_row *
							(button_height + BUTTON_GAP);
						draw_button(x, y, current_button_width, button_height,
							button_labels[i], false);
					}
					send_control_change(RESET_CC_NUMBER, 127);
					draw_status(grid_x, "RESET");
					draw_osc_view_button(view_button_x, view_button_y,
						view_button_width, button_height, false);
					draw_reset_button(reset_button_x, reset_button_y,
						reset_button_width, button_height, true);
					draw_button(al_button_x, al_button_y, al_button_width,
						al_button_height, "AL", al_button_enabled);
				} else if (touch_x >= view_button_x &&
					touch_x <= view_button_x + view_button_width &&
					touch_y >= view_button_y &&
					touch_y <= view_button_y + button_height) {
					osc2_view_enabled = !osc2_view_enabled;
					send_control_change(OSC_VIEW_CC_NUMBER,
						osc2_view_enabled ? 127 : 0);
					draw_status(grid_x, osc2_view_enabled
						? "OSC 2 VIEW" : "OSC1 VIEW");
					draw_osc_view_button(view_button_x, view_button_y,
						view_button_width, button_height,
						osc2_view_enabled);
					draw_reset_button(reset_button_x, reset_button_y,
						reset_button_width, button_height,
						reset_button_pressed);
				} else if (touch_x >= al_button_x &&
					touch_x <= al_button_x + al_button_width &&
					touch_y >= al_button_y &&
					touch_y <= al_button_y + al_button_height) {
					al_button_enabled = !al_button_enabled;
					draw_button(al_button_x, al_button_y, al_button_width,
						al_button_height, "AL", al_button_enabled);
				} else if (touch_x >= grid_x && touch_y >= grid_y) {
					POINT local_x = touch_x - grid_x;
					POINT local_y = touch_y - grid_y;
					POINT row = local_y / (button_height + BUTTON_GAP);
					if (row < BUTTON_ROWS &&
						local_y % (button_height + BUTTON_GAP) < button_height) {
						bool is_four_button_row =
							row >= BUTTON_ROWS - FOUR_BUTTON_ROWS;
						POINT current_button_width = is_four_button_row
							? final_row_button_width : button_width;
						POINT row_columns = is_four_button_row
							? 4 : BUTTON_COLUMNS;
						POINT column = local_x /
							(current_button_width + BUTTON_GAP);
						if (column < row_columns &&
							local_x % (current_button_width + BUTTON_GAP) <
								current_button_width) {
							POINT button_index = is_four_button_row
								? COLOR_BUTTON_START +
									(row - (BUTTON_ROWS -
										FOUR_BUTTON_ROWS)) * 4 + column
								: row * BUTTON_COLUMNS + column;
							bool is_toggle_button =
								(button_index >= OSC1_WAVE_BUTTON_START &&
								 button_index < OSC1_WAVE_BUTTON_START +
									OSC1_WAVE_BUTTON_COUNT) ||
								(button_index >= OSC2_WAVE_BUTTON_START &&
								 button_index < OSC2_WAVE_BUTTON_START +
									OSC2_WAVE_BUTTON_COUNT) ||
								button_index >= TOGGLE_BUTTON_START;
							if (is_toggle_button) {
								button_toggle_states[button_index] =
									!button_toggle_states[button_index];
								send_control_change(
									button_toggle_cc_numbers[button_index],
									button_toggle_states[button_index] ? 127 : 0);
								POINT current_button_width = is_four_button_row
									? final_row_button_width : button_width;
								POINT x = grid_x + column *
									(current_button_width + BUTTON_GAP);
								POINT y = grid_y + row *
									(button_height + BUTTON_GAP);
								draw_button(x, y, current_button_width,
									button_height, button_labels[button_index],
									button_toggle_states[button_index]);
								char status_text[32];
								snprintf(status_text, sizeof(status_text),
									"CC %u = %u",
									(unsigned int)button_toggle_cc_numbers[button_index],
									button_toggle_states[button_index] ? 127u : 0u);
								draw_status(grid_x, status_text);
								draw_osc_view_button(view_button_x, view_button_y,
									view_button_width, button_height,
									osc2_view_enabled);
								draw_reset_button(reset_button_x, reset_button_y,
									reset_button_width, button_height,
									reset_button_pressed);
							} else {
								bool deselecting =
									selected_button == (int8_t)button_index;
								int8_t previous_selected_button =
									selected_button;
								int8_t previous_four_button =
									selected_four_button;
								if (button_index >= COLOR_BUTTON_START) {
									if (selected_four_button >=
											COLOR_BUTTON_START &&
										selected_four_button !=
											(int8_t)button_index) {
										if ((cc_send_enabled[
												selected_four_button][0] ||
											 cc_send_enabled[
												selected_four_button][1]) &&
											button_toggle_cc_numbers[
												selected_four_button] != 0) {
											send_control_change(
												button_toggle_cc_numbers[
													selected_four_button],
												0);
										}
										cc_send_enabled[selected_four_button][0] =
											false;
										cc_send_enabled[selected_four_button][1] =
											false;
									}
									selected_four_button = deselecting
										? -1 : (int8_t)button_index;
								}
								selected_button = deselecting
									? -1 : (int8_t)button_index;
								if (previous_selected_button >= 0) {
									draw_grid_button(grid_x, grid_y,
										button_width, final_row_button_width,
										button_height,
										(uint8_t)previous_selected_button,
										button_labels[previous_selected_button],
										button_toggle_states[
											previous_selected_button] ||
											previous_selected_button ==
												selected_four_button,
										cc_send_enabled[
												previous_selected_button][0],
										audio_control_enabled[
												previous_selected_button][0]);
								}
								if (previous_four_button >=
										COLOR_BUTTON_START &&
									previous_four_button !=
										selected_four_button) {
									draw_grid_button(grid_x, grid_y,
										button_width, final_row_button_width,
										button_height,
										(uint8_t)previous_four_button,
										button_labels[previous_four_button],
										button_toggle_states[
											previous_four_button],
										cc_send_enabled[
											previous_four_button][0],
										audio_control_enabled[
											previous_four_button][0]);
								}
								if (!deselecting) {
									draw_grid_button(grid_x, grid_y,
										button_width, final_row_button_width,
										button_height,
										(uint8_t)button_index,
										button_labels[button_index],
										true,
										cc_send_enabled[button_index][0],
										audio_control_enabled[button_index][0]);
								}
								if (deselecting) {
									draw_status(grid_x, "DESELECTED");
								} else {
									char status_text[32];
									snprintf(status_text, sizeof(status_text),
										"CC %u SEL",
										(unsigned int)button_cc_numbers[button_index]);
									draw_status(grid_x, status_text);
								}
								draw_osc_view_button(view_button_x, view_button_y,
									view_button_width, button_height,
									osc2_view_enabled);
								draw_reset_button(reset_button_x, reset_button_y,
									reset_button_width, button_height,
									reset_button_pressed);
							}
						}
					}
				} else if (selected_button >= 0 &&
					has_audio_control((uint8_t)selected_button) &&
					touch_y >= audio_invert_button_y &&
					touch_y <= audio_invert_button_y +
						audio_invert_button_height &&
					((has_dual_slider_audio((uint8_t)selected_button) &&
					  ((touch_x >= dual_audio_scale_x[0] &&
						touch_x <= dual_audio_scale_x[0] +
							slider_width) ||
					   (touch_x >= dual_audio_scale_x[1] &&
						touch_x <= dual_audio_scale_x[1] +
							slider_width))) ||
					 (!has_dual_slider_audio((uint8_t)selected_button) &&
					  touch_x >= audio_scale_slider_x &&
					  touch_x <= audio_scale_slider_x +
						slider_width))) {
					uint8_t slider_index = has_dual_slider_audio(
						(uint8_t)selected_button) &&
						touch_x >= dual_audio_scale_x[1] ? 1 : 0;
					audio_control_inverted[selected_button][slider_index] =
						!audio_control_inverted[selected_button][slider_index];
					if (cc_send_enabled[selected_button][slider_index]) {
						uint8_t value = apply_adc_offset(
							midi_slider_values[selected_button][slider_index],
							adc_reading,
							audio_scale_values[selected_button][slider_index],
							audio_control_inverted[selected_button][slider_index]);
						send_control_change(
							get_slider_cc_number(
								(uint8_t)selected_button, slider_index,
								button_cc_numbers),
							value);
						last_audio_cc_values[selected_button][slider_index] =
							value;
					}
					char status_text[32];
					snprintf(status_text, sizeof(status_text), "ADC INV %s",
						audio_control_inverted[selected_button][slider_index]
							? "ON" : "OFF");
					draw_status(grid_x, status_text);
					draw_osc_view_button(view_button_x, view_button_y,
						view_button_width, button_height,
						osc2_view_enabled);
					draw_reset_button(reset_button_x, reset_button_y,
						reset_button_width, button_height,
						reset_button_pressed);
				} else if (selected_button >= 0 &&
					has_audio_control((uint8_t)selected_button) &&
					touch_y >= audio_scale_slider_y &&
					touch_y <= audio_scale_slider_y +
						audio_scale_slider_height &&
					((has_dual_slider_audio((uint8_t)selected_button) &&
					  ((touch_x >= dual_audio_scale_x[0] &&
						touch_x <= dual_audio_scale_x[0] + slider_width &&
						audio_control_enabled[selected_button][0]) ||
					   (touch_x >= dual_audio_scale_x[1] &&
						touch_x <= dual_audio_scale_x[1] + slider_width &&
						audio_control_enabled[selected_button][1]))) ||
					 (!has_dual_slider_audio((uint8_t)selected_button) &&
					  audio_control_enabled[selected_button][0] &&
					  touch_x >= audio_scale_slider_x &&
					  touch_x <= audio_scale_slider_x + slider_width))) {
					uint8_t slider_index = has_dual_slider_audio(
						(uint8_t)selected_button) &&
						touch_x >= dual_audio_scale_x[1] ? 1 : 0;
					POINT active_scale_x =
						has_dual_slider_audio((uint8_t)selected_button)
						? dual_audio_scale_x[slider_index]
						: audio_scale_slider_x;
					uint32_t adjusted_range =
						(uint32_t)(audio_scale_slider_height - 8);
					uint32_t relative =
						(uint32_t)(touch_y - (audio_scale_slider_y + 4));
					if (relative > adjusted_range) {
						relative = adjusted_range;
					}
					audio_scale_values[selected_button][slider_index] =
						(uint8_t)(127u -
						((relative * 127u) / adjusted_range));
					if (cc_send_enabled[selected_button][slider_index]) {
						uint8_t value = apply_adc_offset(
							midi_slider_values[selected_button][slider_index],
							adc_reading,
							audio_scale_values[selected_button][slider_index],
							audio_control_inverted[selected_button][slider_index]);
						send_control_change(
							get_slider_cc_number(
								(uint8_t)selected_button, slider_index,
								button_cc_numbers),
							value);
						last_audio_cc_values[selected_button][slider_index] =
							value;
					}
					draw_cc_slider(active_scale_x, audio_scale_slider_y,
						slider_width, audio_scale_slider_height,
						audio_scale_values[selected_button][slider_index],
						true);
					char status_text[32];
					snprintf(status_text, sizeof(status_text),
						"ADC SCALE %u",
						(unsigned int)audio_scale_values[selected_button][slider_index]);
					draw_status(grid_x, status_text);
					draw_osc_view_button(view_button_x, view_button_y,
						view_button_width, button_height,
						osc2_view_enabled);
					draw_reset_button(reset_button_x, reset_button_y,
						reset_button_width, button_height,
						reset_button_pressed);
				} else if (selected_button >= 0 &&
					touch_y >= slider_y && touch_y <= slider_y + slider_height &&
					((touch_x >= slider_x &&
					  touch_x <= slider_x + slider_width) ||
					 (selected_button >= COLOR_BUTTON_START &&
					  selected_button < TOGGLE_BUTTON_START &&
					  touch_x >= second_slider_x &&
					  touch_x <= second_slider_x + slider_width))) {
					uint8_t slider_index = touch_x >= second_slider_x ? 1 : 0;
					uint8_t slider_cc_number =
						get_slider_cc_number(
							(uint8_t)selected_button, slider_index,
							button_cc_numbers);
					POINT active_slider_x =
						slider_index == 1 ? second_slider_x : slider_x;
					uint32_t adjusted_range = (uint32_t)(slider_height - 8);
					uint32_t relative = (uint32_t)(touch_y - (slider_y + 4));
					if (relative > adjusted_range) {
						relative = adjusted_range;
					}
					midi_slider_values[selected_button][slider_index] = (uint8_t)(127u -
						((relative * 127u) / adjusted_range));
					if (cc_send_enabled[selected_button][slider_index]) {
						uint8_t value =
							has_audio_control((uint8_t)selected_button) &&
								audio_control_enabled[selected_button][slider_index]
							? apply_adc_offset(
								midi_slider_values[selected_button][slider_index],
								adc_reading,
								audio_scale_values[selected_button][slider_index],
								audio_control_inverted[selected_button][slider_index])
							: midi_slider_values[selected_button][slider_index];
						send_control_change(
							slider_cc_number,
							value);
						if (has_audio_control((uint8_t)selected_button) &&
							audio_control_enabled[selected_button][slider_index]) {
							last_audio_cc_values[selected_button][slider_index] =
								value;
						}
					}
					draw_cc_slider(active_slider_x, slider_y, slider_width,
						slider_height,
						midi_slider_values[selected_button][slider_index], true);
					char status_text[32];
					snprintf(status_text, sizeof(status_text), "CC %u %s %u",
						 (unsigned int)slider_cc_number,
						 cc_send_enabled[selected_button][slider_index]
							? "=" : "//",
						 (unsigned int)midi_slider_values[selected_button][slider_index]);
					draw_status(grid_x, status_text);
					draw_osc_view_button(view_button_x, view_button_y,
						view_button_width, button_height,
						osc2_view_enabled);
					draw_reset_button(reset_button_x, reset_button_y,
						reset_button_width, button_height,
						reset_button_pressed);
				} else if (selected_button >= 0 &&
					has_audio_control((uint8_t)selected_button) &&
					touch_y >= audio_button_y &&
					touch_y <= audio_button_y + audio_button_height &&
					((has_dual_slider_audio((uint8_t)selected_button) &&
					  ((touch_x >= dual_audio_button_x[0] &&
						touch_x <= dual_audio_button_x[0] +
							audio_button_width) ||
					   (touch_x >= dual_audio_button_x[1] &&
						touch_x <= dual_audio_button_x[1] +
							audio_button_width))) ||
					 (!has_dual_slider_audio((uint8_t)selected_button) &&
					  touch_x >= audio_button_x &&
					  touch_x <= audio_button_x + audio_button_width))) {
					uint8_t slider_index = has_dual_slider_audio(
						(uint8_t)selected_button) &&
						touch_x >= dual_audio_button_x[1] ? 1 : 0;
					audio_control_enabled[selected_button][slider_index] =
						!audio_control_enabled[selected_button][slider_index];
					if (cc_send_enabled[selected_button][slider_index]) {
						uint8_t value =
							audio_control_enabled[selected_button][slider_index]
							? apply_adc_offset(
								midi_slider_values[selected_button][slider_index],
								adc_reading,
								audio_scale_values[selected_button][slider_index],
								audio_control_inverted[selected_button][slider_index])
							: midi_slider_values[selected_button][slider_index];
						send_control_change(
							get_slider_cc_number(
								(uint8_t)selected_button, slider_index,
								button_cc_numbers),
							value);
						last_audio_cc_values[selected_button][slider_index] =
							value;
					}
					draw_status(grid_x,
						audio_control_enabled[selected_button][slider_index]
						? "A_CTRL ON" : "A_CTRL OFF");
					draw_osc_view_button(view_button_x, view_button_y,
						view_button_width, button_height,
						osc2_view_enabled);
					draw_reset_button(reset_button_x, reset_button_y,
						reset_button_width, button_height,
						reset_button_pressed);
				} else if (selected_button >= 0 &&
					touch_y >= toggle_y && touch_y <= toggle_y + toggle_height &&
					((touch_x >= toggle_x &&
					  touch_x <= toggle_x + toggle_width) ||
					 (selected_button >= COLOR_BUTTON_START &&
					  selected_button < TOGGLE_BUTTON_START &&
					  touch_x >= second_toggle_x &&
					  touch_x <= second_toggle_x + toggle_width))) {
					uint8_t slider_index =
						touch_x >= second_toggle_x ? 1 : 0;
					uint8_t slider_cc_number =
						get_slider_cc_number(
							(uint8_t)selected_button, slider_index,
							button_cc_numbers);
					POINT active_toggle_x =
						slider_index == 1 ? second_toggle_x : toggle_x;
					cc_send_enabled[selected_button][slider_index] =
						!cc_send_enabled[selected_button][slider_index];
					if (button_toggle_cc_numbers[selected_button] != 0) {
						send_control_change(
							button_toggle_cc_numbers[selected_button],
							cc_send_enabled[selected_button][slider_index]
								? 127 : 0);
					}
					if (cc_send_enabled[selected_button][slider_index]) {
						uint8_t value =
							has_audio_control((uint8_t)selected_button) &&
								audio_control_enabled[selected_button][slider_index]
							? apply_adc_offset(
								midi_slider_values[selected_button][slider_index],
								adc_reading,
								audio_scale_values[selected_button][slider_index],
								audio_control_inverted[selected_button][slider_index])
							: midi_slider_values[selected_button][slider_index];
						send_control_change(
							slider_cc_number, value);
						if (has_audio_control((uint8_t)selected_button) &&
							audio_control_enabled[selected_button][slider_index]) {
							last_audio_cc_values[selected_button][slider_index] =
								value;
						}
					}
					draw_cc_send_toggle(active_toggle_x, toggle_y,
						toggle_width, toggle_height,
						cc_send_enabled[selected_button][slider_index], true);
					char status_text[32];
					if (cc_send_enabled[selected_button][slider_index] &&
						button_toggle_cc_numbers[selected_button] != 0) {
						snprintf(status_text, sizeof(status_text), "CC %u = 127",
							(unsigned int)button_toggle_cc_numbers[selected_button]);
					} else {
						snprintf(status_text, sizeof(status_text), "CC SEND %s",
							cc_send_enabled[selected_button][slider_index]
								? "ON" : "OFF");
					}
					draw_status(grid_x, status_text);
					draw_osc_view_button(view_button_x, view_button_y,
						view_button_width, button_height,
						osc2_view_enabled);
					draw_reset_button(reset_button_x, reset_button_y,
						reset_button_width, button_height,
						reset_button_pressed);
				}
			}
			touch_latched = true;
		} else {
			touch_ready = true;
			if (reset_button_pressed) {
				reset_button_pressed = false;
				draw_reset_button(reset_button_x, reset_button_y,
					reset_button_width, button_height, false);
			}
			touch_latched = false;
		}

		draw_cc_slider(slider_x, slider_y, slider_width, slider_height,
				selected_button >= 0
					? midi_slider_values[selected_button][0] : 0,
				selected_button >= 0);
		bool right_send_toggle_overlaps_invert =
			selected_button >= 0 &&
			has_audio_control((uint8_t)selected_button) &&
			!has_dual_slider_audio((uint8_t)selected_button);
		if (!right_send_toggle_overlaps_invert) {
			draw_cc_send_toggle(second_toggle_x, toggle_y, toggle_width,
				toggle_height,
				selected_button >= 0 &&
					cc_send_enabled[selected_button][1],
				selected_button >= COLOR_BUTTON_START &&
					selected_button < TOGGLE_BUTTON_START);
		}
		if (selected_button >= 0 &&
			has_audio_control((uint8_t)selected_button)) {
			bool dual_audio = has_dual_slider_audio(
				(uint8_t)selected_button);
			bool audio_layout_changed = displayed_audio_button >= 0 &&
				has_dual_slider_audio((uint8_t)displayed_audio_button) !=
					dual_audio;
			if (audio_layout_changed) {
				if (has_dual_slider_audio((uint8_t)displayed_audio_button)) {
					draw_cc_slider(second_slider_x, slider_y, slider_width,
						slider_height, 0, false);
					for (uint8_t i = 0; i < 2; i++) {
						draw_audio_control_button(dual_audio_button_x[i],
							audio_button_y, audio_button_width,
							audio_button_height, false, false);
						draw_cc_slider(dual_audio_scale_x[i],
							audio_scale_slider_y, slider_width,
							audio_scale_slider_height, 0, false);
						draw_audio_invert_button(dual_audio_scale_x[i],
							audio_invert_button_y, slider_width,
							audio_invert_button_height, false, false);
					}
				} else {
					draw_audio_control_button(audio_button_x,
						audio_button_y, audio_button_width,
						audio_button_height, false, false);
					draw_cc_slider(audio_scale_slider_x,
						audio_scale_slider_y, slider_width,
						audio_scale_slider_height, 0, false);
					draw_audio_invert_button(audio_scale_slider_x,
						audio_invert_button_y, slider_width,
						audio_invert_button_height, false, false);
				}
				if (!dual_audio) {
					draw_cc_send_toggle(second_toggle_x, toggle_y,
						toggle_width, toggle_height, false, false);
				}
			}
			if (dual_audio) {
				draw_cc_slider(second_slider_x, slider_y, slider_width,
					slider_height,
					midi_slider_values[selected_button][1], true);
				for (uint8_t i = 0; i < 2; i++) {
					bool audio_enabled =
						audio_control_enabled[selected_button][i];
					bool button_changed =
						displayed_audio_button != selected_button ||
						displayed_audio_enabled[i] != audio_enabled;
					if (button_changed) {
						draw_audio_control_button(dual_audio_button_x[i],
							audio_button_y, audio_button_width,
							audio_button_height, audio_enabled, true);
					}
					if (audio_enabled &&
						(displayed_audio_button != selected_button ||
						 !displayed_audio_scale_visible[i] ||
						 displayed_audio_scale[i] !=
							audio_scale_values[selected_button][i])) {
						draw_cc_slider(dual_audio_scale_x[i],
							audio_scale_slider_y, slider_width,
							audio_scale_slider_height,
							audio_scale_values[selected_button][i], true);
					} else if (!audio_enabled &&
						displayed_audio_scale_visible[i]) {
						draw_cc_slider(dual_audio_scale_x[i],
							audio_scale_slider_y, slider_width,
							audio_scale_slider_height, 0, false);
					}
					if (displayed_audio_button != selected_button ||
						displayed_audio_invert_visible[i] != audio_enabled ||
						displayed_audio_inverted[i] !=
							audio_control_inverted[selected_button][i]) {
						draw_audio_invert_button(dual_audio_scale_x[i],
							audio_invert_button_y, slider_width,
							audio_invert_button_height,
							audio_control_inverted[selected_button][i],
							audio_enabled);
					}
					displayed_audio_enabled[i] = audio_enabled;
					displayed_audio_scale_visible[i] = audio_enabled;
					displayed_audio_scale[i] =
						audio_scale_values[selected_button][i];
					displayed_audio_invert_visible[i] = audio_enabled;
					displayed_audio_inverted[i] =
						audio_control_inverted[selected_button][i];
				}
				second_slider_was_visible = true;
			} else {
				if (second_slider_was_visible) {
					draw_cc_slider(second_slider_x, slider_y,
						slider_width, slider_height, 0, false);
				}
				bool audio_enabled =
					audio_control_enabled[selected_button][0];
				if (audio_layout_changed ||
					displayed_audio_button != selected_button ||
					displayed_audio_enabled[0] != audio_enabled) {
					draw_audio_control_button(audio_button_x, audio_button_y,
						audio_button_width, audio_button_height,
						audio_enabled, true);
				}
				if (audio_enabled &&
					(displayed_audio_button != selected_button ||
					 !displayed_audio_scale_visible[0] ||
					 displayed_audio_scale[0] !=
						audio_scale_values[selected_button][0])) {
					draw_cc_slider(audio_scale_slider_x,
						audio_scale_slider_y, slider_width,
						audio_scale_slider_height,
						audio_scale_values[selected_button][0], true);
				} else if (!audio_enabled &&
					displayed_audio_scale_visible[0]) {
					draw_cc_slider(audio_scale_slider_x,
						audio_scale_slider_y, slider_width,
						audio_scale_slider_height, 0, false);
				}
				if (audio_layout_changed ||
					displayed_audio_button != selected_button ||
					displayed_audio_invert_visible[0] != audio_enabled ||
					displayed_audio_inverted[0] !=
						audio_control_inverted[selected_button][0]) {
					draw_audio_invert_button(audio_scale_slider_x,
						audio_invert_button_y, slider_width,
						audio_invert_button_height,
						audio_control_inverted[selected_button][0],
						audio_enabled);
				}
				displayed_audio_enabled[0] = audio_enabled;
				displayed_audio_scale_visible[0] = audio_enabled;
				displayed_audio_scale[0] =
					audio_scale_values[selected_button][0];
				displayed_audio_invert_visible[0] = audio_enabled;
				displayed_audio_inverted[0] =
					audio_control_inverted[selected_button][0];
				second_slider_was_visible = false;
			}
			displayed_audio_button = selected_button;
		} else {
			if (displayed_audio_button >= 0) {
				if (has_dual_slider_audio(
					(uint8_t)displayed_audio_button)) {
					for (uint8_t i = 0; i < 2; i++) {
						draw_audio_control_button(dual_audio_button_x[i],
							audio_button_y, audio_button_width,
							audio_button_height, false, false);
						draw_cc_slider(dual_audio_scale_x[i],
							audio_scale_slider_y, slider_width,
							audio_scale_slider_height, 0, false);
						draw_audio_invert_button(dual_audio_scale_x[i],
							audio_invert_button_y, slider_width,
							audio_invert_button_height, false, false);
					}
				} else {
					draw_audio_control_button(audio_button_x,
						audio_button_y, audio_button_width,
						audio_button_height, false, false);
					draw_cc_slider(audio_scale_slider_x,
						audio_scale_slider_y, slider_width,
						audio_scale_slider_height, 0, false);
					draw_audio_invert_button(audio_scale_slider_x,
						audio_invert_button_y, slider_width,
						audio_invert_button_height, false, false);
				}
				displayed_audio_button = -1;
				displayed_audio_enabled[0] = false;
				displayed_audio_enabled[1] = false;
				displayed_audio_scale_visible[0] = false;
				displayed_audio_scale_visible[1] = false;
				displayed_audio_scale[0] = UINT8_MAX;
				displayed_audio_scale[1] = UINT8_MAX;
				displayed_audio_invert_visible[0] = false;
				displayed_audio_invert_visible[1] = false;
				displayed_audio_inverted[0] = false;
				displayed_audio_inverted[1] = false;
			}
			bool second_slider_visible =
				selected_button >= COLOR_BUTTON_START &&
				selected_button < TOGGLE_BUTTON_START;
			if (second_slider_visible) {
				draw_cc_slider(second_slider_x, slider_y, slider_width,
					slider_height,
					midi_slider_values[selected_button][1], true);
			} else if (second_slider_was_visible) {
				draw_cc_slider(second_slider_x, slider_y, slider_width,
					slider_height, 0, false);
			}
			second_slider_was_visible = second_slider_visible;
		}
		draw_cc_send_toggle(toggle_x, toggle_y, toggle_width, toggle_height,
			selected_button >= 0 &&
				cc_send_enabled[selected_button][0], selected_button >= 0);

		for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
			if (has_audio_control(i)) {
				uint8_t slider_count = has_dual_slider_audio(i) ? 2 : 1;
				for (uint8_t slider_index = 0;
					 slider_index < slider_count; slider_index++) {
					if (audio_control_enabled[i][slider_index] &&
						cc_send_enabled[i][slider_index]) {
						uint8_t value = apply_adc_offset(
							midi_slider_values[i][slider_index],
							adc_reading,
							audio_scale_values[i][slider_index],
							audio_control_inverted[i][slider_index]);
						if (value != last_audio_cc_values[i][slider_index]) {
							send_control_change(
								get_slider_cc_number(i, slider_index,
									button_cc_numbers),
								value);
							last_audio_cc_values[i][slider_index] = value;
						}
					} else if (!cc_send_enabled[i][slider_index]) {
						last_audio_cc_values[i][slider_index] = UINT8_MAX;
					}
				}
			}
		}
		draw_vu_meter(grid_x, sLCD_DIS.LCD_Dis_Page, adc_reading);

		sleep_ms(20);
	}

	return 0;
}
