#include "Inkplate.h"

int get_estimated_remaining_battery_percentage(double voltage)
{
  // Define the minimum and maximum voltage levels
  const double min_voltage = 3.0; // Voltage at 0% battery
  const double max_voltage = 4.2; // Voltage at 100% battery

  // Ensure the voltage is within the expected range
  if (voltage < min_voltage)
  {
    return 0;
  }
  else if (voltage > max_voltage)
  {
    return 100;
  }

  // Calculate the battery percentage
  int percentage = (int)((voltage - min_voltage) / (max_voltage - min_voltage) * 100);

  return percentage;
}

void *write_uri_string(Inkplate &display, char* uri512buff)
{
  double voltage = display.readBattery();
  int remaining_percent = get_estimated_remaining_battery_percentage(voltage);
  sprintf(uri512buff, "http://192.168.1.10:8000/dashboard/airquality.png?textoverlay=%.1fV,498,1175,16&batteryoverlay=%d,540,1172,x24", voltage, remaining_percent);
}

