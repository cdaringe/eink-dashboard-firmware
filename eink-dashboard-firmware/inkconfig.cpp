#include "Inkplate.h"
#include "inkconfig.h"

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

void write_dashboard_uri_string(Inkplate &display, const char* dashboard_uri, char* uri512buff)
{
  if (dashboard_uri[0] == '/') {
    double voltage = display.readBattery();
    int remaining_percent = get_estimated_remaining_battery_percentage(voltage);
    snprintf(uri512buff, 512, "%s%s?textoverlay=%.1fV,498,1175,16&batteryoverlay=%d,540,1172,x24", DASHBOARD_BASE_URL, dashboard_uri, voltage, remaining_percent);
  } else {
    snprintf(uribuff, 512, "%s", dashboard_uri);
  }
}

