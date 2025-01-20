 /*  the radian_trx SW shall not be distributed  nor used for commercial product*/
 /*  it is exposed just to demonstrate CC1101 capability to reader water meter indexes */
 /*  there is no Warranty on radian_trx SW */

#include <arduino.h>

 
void show_in_hex(uint8_t* buffer, size_t len);
void show_in_hex_array(uint8_t* buffer, size_t len);
void show_in_hex_one_line(uint8_t* buffer, size_t len);
void show_in_hex_one_line_GET(uint8_t* buffer, size_t len);
void show_in_bin(uint8_t* buffer, size_t len);
void echo_debug(T_BOOL l_flag, char *fmt, ...);
void print_time(void);


static void init_crc_tab(void);
uint16_t crc_kermit(const unsigned char *input_ptr, size_t num_bytes);
int encode2serial_1_3(uint8_t *inputBuffer, int inputBufferLen, uint8_t *outputBuffer);
int Make_Radian_Master_req(uint8_t *outputBuffer, uint8_t year, uint32_t serial);