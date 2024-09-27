#include <stdint.h>
#include "inkconfig.h"

#if !defined(ARDUINO_INKPLATE10) && !defined(ARDUINO_INKPLATE10V2)
#error "Wrong board selection for this example, please select e-radionica Inkplate10 or Soldered Inkplate10 in the boards menu."
#endif

#include "HTTPClient.h"
#include "Inkplate.h"
#include "WiFi.h"

Inkplate display(INKPLATE_3BIT); // grayscale

#define micros_per_s 1000000LL

typedef enum {
  GET_CURRENT,
  GET_FORCED_REFRESH
} RefreshStrategy;

char msgbuff[256];
char uribuff[512];

void clear_buffer(char *buffer, size_t size)
{
  memset(buffer, 0, size); // Clear the buffer by setting all elements to '\0'
}

void msg(String text)
{
  display.clearDisplay();
  display.setCursor(20, 20);
  display.setTextSize(4);
  display.println(text);
  bool leave_on = true;
  display.display(leave_on);
  clear_buffer(msgbuff, sizeof(msgbuff));
}

void msg_partial(String text)
{
  display.setCursor(20, 20);
  display.setTextSize(4);
  display.println(text);
  bool forced = false;
  bool leave_on = true;
  display.partialUpdate(forced, leave_on);
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

void setup()
{
  display.begin();
  display.setRotation(3);
  display.setTextColor(0, 7);
  RefreshStrategy refresh_strategy = get_refresh_strategy_from_wakeup();
  init_wifi();
  write_uri_string(display, uribuff);
  draw_png_from_web(uribuff, true);
  WiFi.mode(WIFI_OFF);
  esp_sleep_enable_timer_wakeup(micros_per_s * 60LL * 60LL);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_36, LOW);
  esp_deep_sleep_start();
}

void init_wifi()
{
  WiFi.mode(WIFI_MODE_STA);
  WiFi.begin(EINK_WIFI_SSID, EINK_WIFI_PASSWORD);
  sprintf(msgbuff, "WiFi connecting to: %s", EINK_WIFI_SSID);
  msg_partial(msgbuff);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    display.print(".");
    display.partialUpdate();
  }
}

RefreshStrategy get_refresh_strategy_from_wakeup()
{
  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause();
  switch (wakeup_reason)
  {
  case ESP_SLEEP_WAKEUP_EXT0:
    msg_partial("[wakeup] manual");
    return GET_FORCED_REFRESH;
  case ESP_SLEEP_WAKEUP_TIMER:
    msg_partial("[wakeup] timer");
    return GET_CURRENT;
  default:
    msg_partial("[wakep] boot");
    return GET_CURRENT;
  }
}

void draw_png_from_web(const char *uri, bool load_fallback_on_fail)
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

  if (load_fallback_on_fail) {
    return draw_png_from_web(EINK_FALLBACK_IMAGE, false);
  }
}

void loop()
{
}
