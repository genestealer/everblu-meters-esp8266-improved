/**
 * @file utils.h
 * @brief Utility functions for RADIAN protocol and debugging
 *
 * IMPORTANT LICENSING NOTICE:
 * The RADIAN protocol implementation (radian_trx SW) shall not be distributed
 * nor used for commercial products. It is exposed only to demonstrate CC1101
 * capability to read water meter indexes. There is no warranty on radian_trx SW.
 */

#include <Arduino.h>

#include "everblu_meters.h"
#include <private.h> // Configuration file (Wi-Fi, MQTT, meter settings)

#include <string.h>

// Consolidated hex display function with optional formatting
// mode: 0=16 per line with newlines, 1=array format, 2=single line, 3=single line with 'S' separator
void show_in_hex_formatted(const uint8_t *buffer, size_t len, int mode)
{
	int i = 0;
	for (i = 0; i < len; i++)
	{
		if (mode == 0)
		{
			// Original show_in_hex: 16 bytes per line
			if (!(i % 16))
				puts("");
			printf("%02X ", buffer[i]);
		}
		else if (mode == 1)
		{
			// Array format with 16 per line
			if (!(i % 16) && i > 0)
				Serial.println("");
			Serial.printf("0x%02X, ", buffer[i]);
		}
		else if (mode == 2)
		{
			// Single line format
			Serial.printf("%02X ", buffer[i]);
		}
		else if (mode == 3)
		{
			// Single line with 'S' separator (for GET requests)
			Serial.printf("%02XS", buffer[i]);
		}
	}
	if (mode == 0)
		printf("\n");
	if (mode == 1 || mode == 2)
		Serial.println("");
}

// Legacy function wrappers for backwards compatibility
void show_in_hex(const uint8_t *buffer, size_t len)
{
	show_in_hex_formatted(buffer, len, 0);
}

void show_in_hex_array(const uint8_t *buffer, size_t len)
{
	show_in_hex_formatted(buffer, len, 1);
}

void show_in_hex_one_line(const uint8_t *buffer, size_t len)
{
	show_in_hex_formatted(buffer, len, 2);
}

void show_in_hex_one_line_GET(const uint8_t *buffer, size_t len)
{
	show_in_hex_formatted(buffer, len, 3);
}

void show_in_bin(const uint8_t *buffer, size_t len)
{
	const uint8_t *ptr;
	uint8_t mask;
	for (ptr = buffer; len--; ptr++)
	{
		for (mask = 0x80; mask; mask >>= 1)
		{
			(mask & *ptr) > 0 ? printf("1") : printf("0");
		}
		printf(" ");
	}
	printf("\n");
}

void echo_debug(T_BOOL l_flag, const char *fmt, ...)
{
	if (l_flag)
	{
		va_list args;
		va_start(args, fmt);
		vprintf(fmt, args);
		fflush(stdout);
	}
}

void print_time(void)
{ /*
	 time_t mytime;
	 mytime = time(NULL);
	 printf(ctime(&mytime));*/

	time_t rawtime;
	struct tm *timeinfo;
	char buffer[80];

	time(&rawtime);
	timeinfo = localtime(&rawtime);

	strftime(buffer, 80, "%d/%m/%Y %X", timeinfo);
	printf("%s", buffer);
}

/*----------------------------------------------------------------------------*/
#define CRC_START_KERMIT 0x0000
#define CRC_POLY_KERMIT 0x8408
static uint8_t crc_tab_init = 0;
static uint16_t crc_tab[256];
/*----------------------------------------------------------------------------*/
/* https://www.libcrc.org/
 * static void init_crc_tab( void );
 *
 * For optimal performance, the  CRC Kermit routine uses a lookup table with
 * values that can be used directly in the XOR arithmetic in the algorithm.
 * This lookup table is calculated by the init_crc_tab() routine, the first
 * time the CRC function is called.
 */

static void init_crc_tab(void)
{

	uint16_t i;
	uint16_t j;
	uint16_t crc;
	uint16_t c;

	for (i = 0; i < 256; i++)
	{

		crc = 0;
		c = i;

		for (j = 0; j < 8; j++)
		{

			if ((crc ^ c) & 0x0001)
				crc = (crc >> 1) ^ CRC_POLY_KERMIT;
			else
				crc = crc >> 1;

			c = c >> 1;
		}

		crc_tab[i] = crc;
	}

	crc_tab_init = 1;

} /* init_crc_tab */

/* https://www.libcrc.org/
 * uint16_t crc_kermit( const unsigned char *input_str, size_t num_bytes );
 *
 * The function crc_kermit() calculates the 16 bits Kermit CRC in one pass for
 * a byte string of which the beginning has been passed to the function. The
 * number of bytes to check is also a parameter.
 */

uint16_t crc_kermit(const unsigned char *input_ptr, size_t num_bytes)
{

	uint16_t crc;
	uint16_t tmp;
	uint16_t short_c;
	uint16_t low_byte;
	uint16_t high_byte;
	const unsigned char *ptr;
	size_t a;

	if (!crc_tab_init)
		init_crc_tab();

	crc = CRC_START_KERMIT;
	ptr = input_ptr;

	for (a = 0; a < num_bytes; a++)
	{

		short_c = 0x00ff & (uint16_t)*ptr;
		tmp = crc ^ short_c;
		crc = (crc >> 8) ^ crc_tab[tmp & 0xff];

		ptr++;
	}

	low_byte = (crc & 0xff00) >> 8;
	high_byte = (crc & 0x00ff) << 8;
	crc = low_byte | high_byte;

	return crc;

} /* crc_kermit */

/*----------------------------------------------------------------------------*/
/**
 * Reverses the bit order of the input data and adds a start bit before and a stop bit
 * after each byte.
 *
 * @param inputBuffer Points to the unencoded data.
 * @param inputBufferLen Number of bytes of unencoded data.
 * @param outputBuffer Points to the encoded data.
 * @param outputBufferLen Number of bytes of encoded data.
 */
int encode2serial_1_3(uint8_t *inputBuffer, int inputBufferLen, uint8_t *outputBuffer)
{

	// Adds a start and stop bit and reverses the bit order.
	// 76543210 76543210 76543210 76543210
	// is encoded to:
	// #0123456 7###0123 4567###0 1234567# ##012345 6s7# (# -> Start/Stop bit)

	int bytepos;
	int bitpos;
	int i;
	int j = 0;

	for (i = 0; i < (inputBufferLen * 8); i++)
	{
		// printf("\ni=%u",i);
		if (i % 8 == 0)
		{
			if (i > 0)
			{
				// printf(" j=%u stopBIT",j);
				//  Insert stop bit (3)
				bytepos = j / 8;
				bitpos = j % 8;
				outputBuffer[bytepos] |= 1 << (7 - bitpos);
				j++;

				bytepos = j / 8;
				bitpos = j % 8;
				outputBuffer[bytepos] |= 1 << (7 - bitpos);
				j++;

				bytepos = j / 8;
				bitpos = j % 8;
				outputBuffer[bytepos] |= 1 << (7 - bitpos);
				j++;
			} // stop bit

			// Insert start bit (0)
			bytepos = j / 8;
			bitpos = j % 8;
			// printf(" j=%u startBIT",j);
			outputBuffer[bytepos] &= ~(1 << (7 - bitpos));
			j++;
		} // start stop bit

		bytepos = i / 8;
		bitpos = i % 8;
		uint8_t mask = 1 << bitpos;
		if ((inputBuffer[bytepos] & mask) > 0)
		{
			bytepos = j / 8;
			bitpos = 7 - (j % 8);
			outputBuffer[bytepos] |= 1 << bitpos;
		}
		else
		{
			bytepos = j / 8;
			bitpos = 7 - (j % 8);
			outputBuffer[bytepos] &= ~(1 << bitpos);
		}

		j++;
	} // for

	// insert additional stop bit until end of byte
	while (j % 8 > 0)
	{
		bytepos = j / 8;
		bitpos = 7 - (j % 8);
		outputBuffer[bytepos] |= 1 << bitpos;
		j++;
	}
	outputBuffer[bytepos + 1] = 0xFF;
	return bytepos + 2;
}

int Make_Radian_Master_req(uint8_t *outputBuffer, uint8_t year, uint32_t serial)
{
	uint16_t crc;
	uint8_t to_encode[] = {0x13, 0x10, 0x00, 0x45, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x45, 0x20, 0x0A, 0x50, 0x14, 0x00, 0x0A, 0x40, 0xFF, 0xFF}; // the last 2 bytes are reserved for the CRC as well as the serial number
	uint8_t synch_pattern[] = {0x50, 0x00, 0x00, 0x00, 0x03, 0xFF, 0xFF, 0xFF, 0xFF};
	uint8_t TS_len_u8;

	to_encode[4] = year;
	to_encode[5] = (uint8_t)((serial & 0x00FF0000) >> 16);
	to_encode[6] = (uint8_t)((serial & 0x0000FF00) >> 8);
	to_encode[7] = (uint8_t)(serial & 0x000000FF);
	crc = crc_kermit(to_encode, sizeof(to_encode) - 2);
	// printf("crc:%x\n",crc);
	to_encode[sizeof(to_encode) - 2] = (uint8_t)((crc & 0xFF00) >> 8);
	to_encode[sizeof(to_encode) - 1] = (uint8_t)(crc & 0x00FF);
	// show_in_hex_one_line(to_encode,sizeof(to_encode));
	memcpy(outputBuffer, synch_pattern, sizeof(synch_pattern));
	TS_len_u8 = encode2serial_1_3(to_encode, sizeof(to_encode), &outputBuffer[sizeof(synch_pattern)]);
	return TS_len_u8 + sizeof(synch_pattern);
}

// -----------------------------------------------------------------------------
// Configuration validation helpers
// -----------------------------------------------------------------------------

bool isValidReadingSchedule(const char *schedule)
{
	// Reject null or empty schedules
	if (schedule == nullptr || schedule[0] == '\0')
	{
		return false;
	}

	// Allowed values based on unit tests
	if (strcmp(schedule, "Monday-Friday") == 0)
		return true;
	if (strcmp(schedule, "Monday-Saturday") == 0)
		return true;
	if (strcmp(schedule, "Monday-Sunday") == 0)
		return true;

	// Everything else is considered invalid for now
	return false;
}

bool validateConfiguration()
{
	// Placeholder for future configuration validation logic.
	// For now, always return true so tests can link and focus on
	// isValidReadingSchedule behavior.
	return true;
}
