#ifndef __CC1101_H__
#define __CC1101_H__

struct tmeter_data {
  int liters;
  int reads_counter; // how many times the meter has been read
  int battery_left; // in months
  int time_start; // like 8am
  int time_end; // like 4pm
};

void setMHZ(float mhz);
void  cc1101_init(float freq);
struct tmeter_data get_meter_data(void);

#endif // __CC1101_H__