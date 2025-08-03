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

// Menu layout constants
const int MENU_TITLE_X = 60;
const int MENU_TITLE_Y = 20;
const int MENU_TITLE_SIZE = 4;
const int MENU_ITEM_X = 60;
const int MENU_ITEM_SIZE = 2;
const int MENU_POINTER_X = 20;
const int PIXELS_PER_TEXT_SIZE = 8;

char msgbuff[256];
char uribuff[512];

void clear_buffer(char *buffer, size_t size)
{
  memset(buffer, 0, size); // Clear the buffer by setting all elements to '\0'
}

int calculate_menu_items_start_y()
{
  // Title height + blank line height
  int title_height = MENU_TITLE_SIZE * PIXELS_PER_TEXT_SIZE;
  int blank_line_height = MENU_TITLE_SIZE * PIXELS_PER_TEXT_SIZE;
  return MENU_TITLE_Y + title_height + blank_line_height;
}

int calculate_menu_line_height()
{
  return MENU_ITEM_SIZE * PIXELS_PER_TEXT_SIZE;
}

String resolve_redirect_url(const char* original_uri, const String& location)
{
  // Check if location is absolute URL (contains ://)
  if (location.indexOf("://") > 0) {
    return location; // Already absolute
  }

  // Handle relative URL - need to extract base from original URI
  String original_url = String(original_uri);

  // Find the base URL (protocol + host)
  int protocol_end = original_url.indexOf("://");
  if (protocol_end < 0) {
    return location; // Can't resolve, return as-is
  }

  int path_start = original_url.indexOf("/", protocol_end + 3);
  String base_url;

  if (path_start > 0) {
    base_url = original_url.substring(0, path_start);
  } else {
    base_url = original_url; // No path in original URL
  }

  // Handle different types of relative URLs
  if (location.startsWith("/")) {
    // Absolute path: http://example.com + /new/path
    return base_url + location;
  } else {
    // Relative path: http://example.com/old/ + new/path
    // For simplicity, treat as absolute path
    return base_url + "/" + location;
  }
}

bool is_button_currently_pressed()
{
  return digitalRead(GPIO_NUM_36) == LOW;
}

void handle_button_input()
{
  static unsigned long last_successful_poll = 0;
  bool wake_button_pressed_curr = is_button_currently_pressed();
  unsigned long current_time_ms = millis();

  // STATE MACHINE: Monitor button press/release transitions

  // Reset state if polling has been blocked too long (display operations)
  if (current_time_ms - last_successful_poll > 1000) {
    wake_button_pressed_prev = false;
    last_button_release_time_ms = 0;
  }

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

  // Update successful poll timestamp
  last_successful_poll = current_time_ms;
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
    update_menu_pointer();
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

  // Set title position and style (1BIT colors: 0=black, 1=white)
  display.setCursor(MENU_TITLE_X, MENU_TITLE_Y);
  display.setTextSize(MENU_TITLE_SIZE);
  display.setTextColor(1, 0);

  display.println("MENU");
  display.println(); // Add some space after title

  // Menu items - use shared layout calculations
  display.setTextSize(MENU_ITEM_SIZE);

  int current_y = calculate_menu_items_start_y();
  int line_height = calculate_menu_line_height();

  for (int i = 0; i < MENU_ITEM_COUNT; i++) {
    display.setCursor(MENU_ITEM_X, current_y);
    display.println(menu_items[i].title);
    current_y += line_height; // Increment Y for next item
  }

  // Draw initial pointer
  draw_menu_pointer();

  display.display();
}

void execute_normal_dashboard_operation()
{
  init_wifi();
  write_dashboard_uri_string(display, "", uribuff);
  draw_png_from_web(uribuff, true);
  deep_sleep();
}

void draw_menu_pointer()
{
  display.setTextColor(1, 0); // Black text on white background
  // Use shared layout calculations
  int menu_start_y = calculate_menu_items_start_y();
  int line_height = calculate_menu_line_height();

  int pointer_y = menu_start_y + (current_menu_selection_index * line_height);

  // Clear the entire pointer column (white background - 1BIT mode)
  int total_menu_height = MENU_ITEM_COUNT * line_height;
  display.fillRect(MENU_POINTER_X - 5, menu_start_y - 5, 40, total_menu_height + 10, 1);

  // Draw pointer arrow ">" at calculated position (1BIT colors)
  display.setCursor(MENU_POINTER_X, pointer_y);
  display.setTextSize(MENU_ITEM_SIZE);
  display.print(">");
}

void update_menu_pointer()
{
  draw_menu_pointer();

  // Use partial update for faster menu navigation in 1BIT mode
  display.partialUpdate();
}

void select_menu_item(int index)
{
  bool is_last_entry = index == MENU_ITEM_COUNT - 1;
  display.clearDisplay();
  if (is_last_entry) {
    current_operating_mode = NORMAL_MODE;
    msg_debug("Resuming normal operation");
    execute_normal_dashboard_operation();
    return;
  }
  msg("Loading: " + String(menu_items[index].title));
  init_wifi();
  write_dashboard_uri_string(display, menu_items[index].uri, uribuff);
  draw_png_from_web(uribuff, true);
  deep_sleep();
}

void deep_sleep() {
  WiFi.mode(WIFI_OFF);
  esp_sleep_enable_timer_wakeup(micros_per_s * EINK_REFRESH_INTERVAL_S);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_36, LOW);
  esp_deep_sleep_start();
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

void msg_debug(String text) {
  if (LOG_LEVEL_DEBUG) msg(text);
}

void setup()
{
  display.begin();
  display.selectDisplayMode(INKPLATE_1BIT); // Default to 1BIT for fast updates
  display.setRotation(3);
  display.setTextColor(1, 0); // 1BIT colors: black text on white background

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
    execute_normal_dashboard_operation();
  }
}

void init_wifi()
{
  WiFi.mode(WIFI_MODE_STA);
  WiFi.begin(EINK_WIFI_SSID, EINK_WIFI_PASSWORD);
  snprintf(msgbuff, sizeof(msgbuff), "WiFi connecting to: %s", EINK_WIFI_SSID);
  msg_debug(msgbuff);

  unsigned long wifi_start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifi_start < 30000)
  {
    delay(100);
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
    msg_debug("[wakeup] manual");
    return GET_FORCED_REFRESH;
  case ESP_SLEEP_WAKEUP_TIMER:
    msg_debug("[wakeup] timer");
    return GET_CURRENT;
  default:
    msg_debug("[wakep] boot");
    return GET_CURRENT;
  }
}

void draw_png_from_web(const char *uri, bool load_fallback_on_fail, int redirect_count)
{
  // Prevent infinite redirect loops
  if (redirect_count > 5) {
    msg("Too many redirects");
    if (load_fallback_on_fail) {
      return draw_png_from_web(EINK_FALLBACK_IMAGE, false, 0);
    }
    return;
  }

  HTTPClient http;
  http.getStream().setNoDelay(true);
  http.getStream().setTimeout(1);
  http.begin(uri);

  // Check response code.
  int http_code = http.GET();

  if (http_code >300 && http_code < 400) {
    String redirect_url = http.getLocation();
    http.end();

    if (redirect_url.length() > 0) {
      // Resolve relative/absolute URL
      String resolved_url = resolve_redirect_url(uri, redirect_url);
      snprintf(msgbuff, sizeof(msgbuff), "Redirect %d (%d/5)", http_code, redirect_count + 1);
      msg_debug(msgbuff);
      return draw_png_from_web(resolved_url.c_str(), load_fallback_on_fail, redirect_count + 1);
    }
    else {
      snprintf(msgbuff, sizeof(msgbuff), "Redirect %d but no location", http_code);
      msg_debug(msgbuff);

      http.end();

      if (load_fallback_on_fail) {
        return draw_png_from_web(EINK_FALLBACK_IMAGE, false, 0);
      }
      return;
    }
  }
  else if (http_code == 200)
  {
    redirect_count = 0; // Reset redirect count on successful response
    int32_t len = http.getSize();
    if (len > 0)
    {
      display.selectDisplayMode(INKPLATE_3BIT);
      int render_image_code = display.drawPngFromWeb(http.getStreamPtr(), 0, 0, len);
      if (!render_image_code)
      {
        display.selectDisplayMode(INKPLATE_1BIT);
        snprintf(msgbuff, sizeof(msgbuff), "Image open error (%d) (%s)", render_image_code, uri);
        msg(msgbuff);
      }
      else
      {
        display.display();
        display.selectDisplayMode(INKPLATE_1BIT);
        http.end();
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

  http.end();

  if (load_fallback_on_fail) {
    return draw_png_from_web(EINK_FALLBACK_IMAGE, false, 0);
  }
}

void loop()
{
  // Loop is not used since everything happens in setup() and then deep sleep
}
