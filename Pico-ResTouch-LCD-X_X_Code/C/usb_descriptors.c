#include <string.h>

#include "tusb.h"

#define USB_VID 0xCafe
#define USB_PID 0x4011
#define USB_BCD 0x0100

enum {
	ITF_NUM_CDC = 0,
	ITF_NUM_CDC_DATA,
	ITF_NUM_MIDI,
	ITF_NUM_MIDI_STREAMING,
	ITF_NUM_TOTAL
};

enum {
	STR_INDEX_LANGUAGE = 0,
	STR_INDEX_MANUFACTURER,
	STR_INDEX_PRODUCT,
	STR_INDEX_SERIAL,
	STR_INDEX_CDC,
	STR_INDEX_MIDI
};

#define CONFIG_TOTAL_LENGTH \
	(TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MIDI_DESC_LEN)

static const tusb_desc_device_t device_descriptor = {
	.bLength = sizeof(tusb_desc_device_t),
	.bDescriptorType = TUSB_DESC_DEVICE,
	.bcdUSB = 0x0200,
	.bDeviceClass = TUSB_CLASS_MISC,
	.bDeviceSubClass = MISC_SUBCLASS_COMMON,
	.bDeviceProtocol = MISC_PROTOCOL_IAD,
	.bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
	.idVendor = USB_VID,
	.idProduct = USB_PID,
	.bcdDevice = USB_BCD,
	.iManufacturer = STR_INDEX_MANUFACTURER,
	.iProduct = STR_INDEX_PRODUCT,
	.iSerialNumber = STR_INDEX_SERIAL,
	.bNumConfigurations = 1
};

static const uint8_t configuration_descriptor[] = {
	TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LENGTH,
						  TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
	TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, STR_INDEX_CDC, 0x81, 8,
					   0x02, 0x82, 64),
	TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, STR_INDEX_MIDI, 0x03, 0x83, 64)
};

static const char *const string_descriptors[] = {
	[STR_INDEX_MANUFACTURER] = "Pico Playground",
	[STR_INDEX_PRODUCT] = "Pico USB MIDI",
	[STR_INDEX_SERIAL] = "PICO-MIDI-01",
	[STR_INDEX_CDC] = "USB Serial",
	[STR_INDEX_MIDI] = "USB MIDI"
};

static uint16_t string_descriptor[32];

const uint8_t *tud_descriptor_device_cb(void)
{
	return (const uint8_t *)&device_descriptor;
}

const uint8_t *tud_descriptor_configuration_cb(uint8_t index)
{
	(void)index;
	return configuration_descriptor;
}

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
	(void)langid;

	size_t length;
	if (index == STR_INDEX_LANGUAGE) {
		string_descriptor[1] = 0x0409;
		length = 1;
	} else {
		if (index >= sizeof(string_descriptors) / sizeof(string_descriptors[0])) {
			return NULL;
		}

		const char *string = string_descriptors[index];
		length = strlen(string);
		if (length > 31) {
			length = 31;
		}
		for (size_t i = 0; i < length; i++) {
			string_descriptor[i + 1] = (uint8_t)string[i];
		}
	}

	string_descriptor[0] =
		(uint16_t)((TUSB_DESC_STRING << 8) | (2 * length + 2));
	return string_descriptor;
}
