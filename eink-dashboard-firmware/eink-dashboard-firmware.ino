#include <stdint.h>
#include "secrets.h"

#if !defined(ARDUINO_INKPLATE10) && !defined(ARDUINO_INKPLATE10V2)
#error "Wrong board selection for this example, please select e-radionica Inkplate10 or Soldered Inkplate10 in the boards menu."
#endif

#include "HTTPClient.h" //Include library for HTTPClient
#include "Inkplate.h"   //Include Inkplate library to the sketch
#include "WiFi.h"       //Include library for WiFi

Inkplate display(INKPLATE_3BIT); // grayscale

#define uS_TO_S_FACTOR 1000000LL

const char *dashboard_image_uri_template = "http://192.168.1.10:8000/dashboard/airquality.png?textoverlay=%.1fv,508,1175,16&batteryoverlay=%d,540,1172,x24";
const char *fallback_images[] = {
    "https://static.cdaringe.com/img/ds9-kd.png"};
const int fallback_images_size = sizeof(fallback_images) / sizeof(fallback_images[0]);

char msgbuff[256];

void clear_buffer(char *buffer, size_t size)
{
  memset(buffer, 0, size); // Clear the buffer by setting all elements to '\0'
}

int get_estimated_remaining_battery_percentage(double voltage)
{
  // Define the minimum and maximum voltage levels
  const double minVoltage = 3.0; // Voltage at 0% battery
  const double maxVoltage = 4.2; // Voltage at 100% battery

  // Ensure the voltage is within the expected range
  if (voltage < minVoltage)
  {
    return 0;
  }
  else if (voltage > maxVoltage)
  {
    return 100;
  }

  // Calculate the battery percentage
  int percentage = (int)((voltage - minVoltage) / (maxVoltage - minVoltage) * 100);

  return percentage;
}

char *get_uri_string()
{
  double voltage = display.readBattery();
  int remainingPercent = get_estimated_remaining_battery_percentage(voltage);

  int length = snprintf(NULL, 0, dashboard_image_uri_template, voltage, remainingPercent);

  // Allocate memory for the new string
  char *result = (char *)malloc(length + 1); // +1 for the null terminator

  if (result == NULL)
  {
    fprintf(stderr, "Memory allocation failed\n");
    return NULL;
  }

  // Fill in the template
  sprintf(result, dashboard_image_uri_template, voltage, remainingPercent);

  return result;
}

void msg(String text)
{
  display.clearDisplay();
  display.setCursor(20, 20);
  display.setTextSize(4);
  display.println(text);
  display.display();
  clear_buffer(msgbuff, sizeof(msgbuff));
}

int get_random_index(int size)
{
  if (size <= 0)
  {
    return -1; // Return -1 if the array is empty
  }
  srand(time(NULL));    // Seed the random number generator
  return rand() % size; // Return a random index
};

static int current_index = get_random_index(fallback_images_size);

const char *get_next_image()
{
  current_index = (current_index + 1) % fallback_images_size; // Update the index
  return fallback_images[current_index];                      // Return the next image
}

void setup()
{
  display.begin();
  display.setRotation(3);
  display.setTextColor(0, 7);
  displayInfo();
  init_wifi();
  delay(1000);
  char *uri = get_uri_string();
  drawPngFromWeb(uri, true);
  free(uri);
  WiFi.mode(WIFI_OFF);
  esp_sleep_enable_timer_wakeup(uS_TO_S_FACTOR * 60LL * 60LL);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_36, LOW);
  esp_deep_sleep_start();
}

void init_wifi()
{
  WiFi.mode(WIFI_MODE_STA);
  WiFi.begin(EINK_WIFI_SSID, EINK_WIFI_PASSWORD);
  sprintf(msgbuff, "WiFi connecting to: %s", EINK_WIFI_SSID);
  display.println(msgbuff);
  display.partialUpdate();
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    display.print(".");
    display.partialUpdate();
  }
}

void displayInfo()
{
  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause();
  switch (wakeup_reason)
  {
  case ESP_SLEEP_WAKEUP_EXT0:
    msg("[wakeup] Yes, my lord? STOP POKING MEEEEEEEE!");
    break;
  case ESP_SLEEP_WAKEUP_TIMER:
    msg("[wakeup] timer tick");
    break;
  default:
    msg("[wakep] good old fashioned wakeup");
    break;
  }
  delay(1000);
}

void drawPngFromWeb(const char *uri, bool retry)
{
  HTTPClient http;
  http.getStream().setNoDelay(true);
  http.getStream().setTimeout(1);
  http.begin(uri);

  // Check response code.
  int http_code = http.GET();
  if (http_code == 200)
  {
    int32_t len = http.getSize();
    if (len > 0)
    {
      int render_image_code = display.drawPngFromWeb(http.getStreamPtr(), 0, 0, len);
      if (!render_image_code)
      {
        sprintf(msgbuff, "Image open error (%d) (%s)", render_image_code, uri);
        msg(msgbuff);
      }
      else
      {
        display.display();
        return;
      }
    }
    else
    {
      msg("Invalid response length");
    }
  }
  else
  {
    sprintf(msgbuff, "http code %d (%s)", http_code, uri);
    msg(msgbuff);
  }

  delay(2000);

  if (retry)
    return drawPngFromWeb(get_next_image(), false);
}

void loop()
{
}
