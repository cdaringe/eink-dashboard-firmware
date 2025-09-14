#include "Inkplate.h"
#include "inkconfig.h"
#include "HTTPClient.h"
#include "ArduinoJson.h"
#include "WiFi.h"

MenuItem* menu_items = nullptr;
int menu_item_count = 0;
RTC_DATA_ATTR int current_dashboard_index = 0;

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
  bool is_pathname_mode = dashboard_uri[0] == '\0' || dashboard_uri[0] == '/';

  double voltage = display.readBattery();
  int remaining_percent = get_estimated_remaining_battery_percentage(voltage);

  char search_params[128];
  snprintf(search_params, sizeof(search_params),
    "?textoverlay=%.1fV,498,1175,16&batteryoverlay=%d,540,1172,x24",
    voltage, remaining_percent);

  if (is_pathname_mode) {
    snprintf(uri512buff, 512, "%s%s%s", DASHBOARD_BASE_URL, dashboard_uri, search_params);
  } else {
    snprintf(uri512buff, 512, "%s%s", dashboard_uri, search_params);
  }
}

bool load_menu_items_from_api()
{
  if (WiFi.status() != WL_CONNECTED) {
    msg_debug("WiFi not connected for menu", 2000);
    return false;
  }

  HTTPClient http;
  http.begin(DASHBOARD_ORIGIN "/api/dashboard/names");
  int httpCode = http.GET();

  if (httpCode != 200) {
    msg_debug("HTTP failed: " + String(httpCode), 2000);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    msg_debug("JSON parse failed: " + String(error.c_str()), 2000);
    return false;
  }

  if (!doc.is<JsonArray>()) {
    msg_debug("Response not JSON array", 2000);
    return false;
  }

  JsonArray array = doc.as<JsonArray>();
  int count = array.size();

  if (count == 0) {
    msg_debug("Empty menu array", 2000);
    return false;
  }

  cleanup_menu_items();

  menu_item_count = count + 2; // +2 for "Play Next" and "Resume Loop"
  menu_items = new MenuItem[menu_item_count];

  // Add "Play Next" as first item
  int i = 0;
  strlcpy(menu_items[i].title, "Play Next", sizeof(menu_items[i].title));
  strlcpy(menu_items[i].uri, "PLAY_NEXT", sizeof(menu_items[i].uri));
  i++;

  // Add "Resume Loop" as second item
  strlcpy(menu_items[i].title, "Resume Loop", sizeof(menu_items[i].title));
  menu_items[i].uri[0] = '\0';
  i++;

  // Add dashboard items
  for (JsonVariant value : array) {
    if (value.is<const char*>()) {
      const char* name = value.as<const char*>();

      strlcpy(menu_items[i].title, name, sizeof(menu_items[i].title));
      snprintf(menu_items[i].uri, sizeof(menu_items[i].uri), "/%s", name);

      i++;
    }
  }

  return true;
}

void cleanup_menu_items()
{
  if (menu_items != nullptr) {
    delete[] menu_items;
    menu_items = nullptr;
  }
  menu_item_count = 0;
}

const char* get_current_dashboard_kind()
{
  if (menu_items == nullptr || menu_item_count <= 2) {
    return ""; // Default to empty string (home dashboard)
  }

  // Dashboard items are at indices 2 to (menu_item_count - 1)
  // Index 0 is "Play Next", Index 1 is "Resume Loop"
  int dashboard_count = menu_item_count - 2;
  if (dashboard_count <= 0) {
    return "";
  }

  // Map dashboard_index to actual menu_items indices (2 to dashboard_count+1)
  int menu_index = 2 + (current_dashboard_index % dashboard_count);
  return menu_items[menu_index].uri;
}

const char* get_or_advance_dashboard_kind()
{
  if (menu_items == nullptr || menu_item_count <= 2) {
    return ""; // Default to empty string (home dashboard)
  }

  // Dashboard items are at indices 2 to (menu_item_count - 1)
  int dashboard_count = menu_item_count - 2;
  if (dashboard_count <= 0) {
    return "";
  }

  // Advance first for "Play Next"
  current_dashboard_index = (current_dashboard_index + 1) % dashboard_count;

  // Then get the dashboard at new position
  int menu_index = 2 + (current_dashboard_index % dashboard_count);
  return menu_items[menu_index].uri;
}

