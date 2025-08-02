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

typedef enum {
  NORMAL_MODE,
  MENU_MODE
} OperatingMode;

typedef struct {
  const char* title;
  const char* url;
} MenuItem;

const MenuItem menu_items[] = {
  {"Air Stats", MENU_AIR_STATS},
  {"Onion", MENU_ONION},
  {"Recipes", MENU_RECIPES},
  {"Family Portrait", MENU_FAMILY_PORTRAIT},
  {"Run Standard Loop", ""}
};

const int MENU_ITEM_COUNT = sizeof(menu_items) / sizeof(menu_items[0]);

// Operating state
OperatingMode current_operating_mode = NORMAL_MODE;
int current_menu_selection_index = 0;

// Button state machine variables
bool wake_button_pressed_prev = false;
unsigned long button_press_start_time_ms = 0;
unsigned long last_button_release_time_ms = 0;

// Button timing constants (in milliseconds)
const unsigned long LONG_PRESS_THRESHOLD_MS = 1000;
const unsigned long DOUBLE_CLICK_WINDOW_MS = 500;
const unsigned long SINGLE_CLICK_DELAY_MS = 200;

char msgbuff[256];
char uribuff[512];

void clear_buffer(char *buffer, size_t size)
{
  memset(buffer, 0, size); // Clear the buffer by setting all elements to '\0'
}

bool is_button_currently_pressed()
{
  return digitalRead(GPIO_NUM_36) == LOW;
}

void handle_button_input()
{
  bool wake_button_pressed_curr = is_button_currently_pressed();
  unsigned long current_time_ms = millis();

  // STATE MACHINE: Monitor button press/release transitions

  // TRANSITION: Button pressed (rising edge detection)
  if (wake_button_pressed_curr && !wake_button_pressed_prev) {
    // STATE: Button just pressed down
    wake_button_pressed_prev = true;
    button_press_start_time_ms = current_time_ms;
  }
  // TRANSITION: Button released (falling edge detection)
  else if (!wake_button_pressed_curr && wake_button_pressed_prev) {
    // STATE: Button just released
    wake_button_pressed_prev = false;
    unsigned long button_press_duration_ms = current_time_ms - button_press_start_time_ms;

    // DECISION: Was this a long press?
    if (button_press_duration_ms > LONG_PRESS_THRESHOLD_MS) {
      // ACTION: Handle long press immediately
      handle_long_press_action();
    }
    else {
      // DECISION: Was this part of a double-click sequence?
      unsigned long time_since_last_release_ms = current_time_ms - last_button_release_time_ms;

      if (time_since_last_release_ms < DOUBLE_CLICK_WINDOW_MS && last_button_release_time_ms > 0) {
        // ACTION: Handle double click immediately
        handle_double_click_action();
        last_button_release_time_ms = 0; // Reset to prevent triple clicks
      }
      else {
        // STATE: Potential single click - start timer to wait for possible second click
        last_button_release_time_ms = current_time_ms;
      }
    }
  }

  // TIMEOUT CHECK: Single click timer expired?
  if (last_button_release_time_ms > 0 &&
      current_time_ms - last_button_release_time_ms > SINGLE_CLICK_DELAY_MS) {
    // ACTION: Handle confirmed single click
    handle_single_click_action();
    last_button_release_time_ms = 0; // Reset timer
  }
}

void handle_single_click_action()
{
  if (current_operating_mode == NORMAL_MODE) {
    current_operating_mode = MENU_MODE;
    current_menu_selection_index = 0;
    display_menu(); // Full display on first show
  }
  else if (current_operating_mode == MENU_MODE) {
    // Navigate to next menu item
    current_menu_selection_index = (current_menu_selection_index + 1) % MENU_ITEM_COUNT;

    // Debug: Show selection index temporarily
    snprintf(msgbuff, sizeof(msgbuff), "Sel: %d", current_menu_selection_index);
    display.setCursor(200, 20);
    display.setTextSize(1);
    display.setTextColor(0, 7);
    display.print(msgbuff);

    update_menu_pointer(); // Fast pointer update only
  }
}

void handle_double_click_action()
{
  if (current_operating_mode == MENU_MODE) {
    // Select current menu item
    select_menu_item(current_menu_selection_index);
  }
}

void handle_long_press_action()
{
  if (current_operating_mode == MENU_MODE) {
    current_operating_mode = NORMAL_MODE;
    display.clearDisplay();
    esp_deep_sleep_start();
  }
}

void display_menu()
{
  display.clearDisplay();
  display.setCursor(60, 20);
  display.setTextSize(3);
  display.setTextColor(0, 7);

  display.println("MENU");
  display.println();

  // Show all menu items in a vertical list
  int y_start = 80;
  int line_height = 60;

  for (int i = 0; i < MENU_ITEM_COUNT; i++) {
    display.setCursor(60, y_start + (i * line_height));
    display.setTextSize(2);
    display.setTextColor(0, 7);
    display.println(menu_items[i].title);
  }

  // Draw initial pointer
  draw_menu_pointer();

  display.display();
}

void draw_menu_pointer()
{
  // Match the exact layout from display_menu()
  int pointer_x = 20;
  int y_start = 80;
  int line_height = 60;
  int pointer_y = y_start + (current_menu_selection_index * line_height);

  // Clear the entire pointer column (white background)
  display.fillRect(pointer_x - 5, y_start - 10, 40, MENU_ITEM_COUNT * line_height + 20, 7);

  // Draw pointer arrow ">" at exact same position as text
  display.setCursor(pointer_x, pointer_y);
  display.setTextSize(2);
  display.setTextColor(0, 7);
  display.print(">");
}

void update_menu_pointer()
{
  // Redraw just the pointer column
  draw_menu_pointer();

  // Use partial update for speed - only update the pointer area
  display.partialUpdate();
}

void select_menu_item(int index)
{
  if (index == MENU_ITEM_COUNT - 1) {
    // "Run Standard Loop" selected
    current_operating_mode = NORMAL_MODE;
    display.clearDisplay();
    msg("Resuming normal operation");
    delay(1000);

    // Continue with normal operation
    init_wifi();
    write_uri_string(display, uribuff);
    draw_png_from_web(uribuff, true);
    WiFi.mode(WIFI_OFF);
    esp_sleep_enable_timer_wakeup(micros_per_s * EINK_REFRESH_INTERVAL_S);
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_36, LOW);
    esp_deep_sleep_start();
  }
  else {
    // Display selected image
    display.clearDisplay();
    snprintf(msgbuff, sizeof(msgbuff), "Loading %s...", menu_items[index].title);
    msg(msgbuff);
    
    // Initialize WiFi before attempting to download image
    init_wifi();
    draw_png_from_web(menu_items[index].url, true);
    WiFi.mode(WIFI_OFF);
    esp_sleep_enable_timer_wakeup(micros_per_s * EINK_REFRESH_INTERVAL_S);
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_36, LOW);
    esp_deep_sleep_start();
  }
}

void msg(String text)
{
  display.clearDisplay();
  display.setCursor(20, 20);
  display.setTextSize(2);
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


void setup()
{
  display.begin();
  display.setRotation(3);
  display.setTextColor(0, 7);

  pinMode(GPIO_NUM_36, INPUT_PULLUP);

  RefreshStrategy refresh_strategy = get_refresh_strategy_from_wakeup();

  // Check if button triggered wakeup (manual wakeup)
  if (refresh_strategy == GET_FORCED_REFRESH) {
    // Manual button press - enter menu mode
    current_operating_mode = MENU_MODE;
    display_menu();

    // Stay awake to handle button interactions
    while (current_operating_mode == MENU_MODE) {
      handle_button_input();
      delay(20); // Reduced delay for more responsive input
    }
  }
  else {
    // Timer wakeup or boot - run normal operation
    current_operating_mode = NORMAL_MODE;
    init_wifi();
    write_uri_string(display, uribuff);
    draw_png_from_web(uribuff, true);
    WiFi.mode(WIFI_OFF);
    esp_sleep_enable_timer_wakeup(micros_per_s * EINK_REFRESH_INTERVAL_S);
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_36, LOW);
    esp_deep_sleep_start();
  }
}

void init_wifi()
{
  WiFi.mode(WIFI_MODE_STA);
  WiFi.begin(EINK_WIFI_SSID, EINK_WIFI_PASSWORD);
  snprintf(msgbuff, sizeof(msgbuff), "WiFi connecting to: %s", EINK_WIFI_SSID);
  msg_partial(msgbuff);

  unsigned long wifi_start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifi_start < 30000)
  {
    delay(500);
    display.print(".");
    display.partialUpdate();
  }

  if (WiFi.status() != WL_CONNECTED) {
    msg("WiFi connection timeout");
    esp_deep_sleep_start();
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
        snprintf(msgbuff, sizeof(msgbuff), "Image open error (%d) (%s)", render_image_code, uri);
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
    snprintf(msgbuff, sizeof(msgbuff), "http code %d (%s)", http_code, uri);
    msg(msgbuff);
  }

  if (load_fallback_on_fail) {
    return draw_png_from_web(EINK_FALLBACK_IMAGE, false);
  }
}

void loop()
{
  // Loop is not used since everything happens in setup() and then deep sleep
}
