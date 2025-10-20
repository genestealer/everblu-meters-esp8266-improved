#ifndef __CC1101_H__
#define __CC1101_H__

struct tmeter_data {
  int liters;
  int reads_counter; // how many times the meter has been read
  int battery_left; // in months
  int time_start; // like 8 (8am, but in 24 hour format)
  int time_end; // like 18 (6pm, but in 24 hour format)
  int rssi; // Radio signal strength indicator
  int rssi_dbm; // RSSI in dBm
  int lqi; // Link quality indicator 0-255
};

void setMHZ(float mhz);
bool cc1101_init(float freq);
struct tmeter_data get_meter_data(void);

#endif // __CC1101_H__