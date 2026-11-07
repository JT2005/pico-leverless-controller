#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "bsp/board_api.h"
#include "tusb.h"

#include "usb_descriptors.h"

// Button pins (active low: pressed = 0, using internal pull-ups)
#define PIN_LEFT   2
#define PIN_DOWN   3
#define PIN_RIGHT  4
#define PIN_LP     5
#define PIN_UP     6   // not wired yet; the pull-up keeps it "not pressed"
#define PIN_MP     7
#define PIN_HP     8
#define PIN_LK     9
#define PIN_MK     10
#define PIN_HK     11

static const uint8_t button_pins[] = {
  PIN_LEFT, PIN_DOWN, PIN_RIGHT, PIN_UP,
  PIN_LP, PIN_MP, PIN_HP, PIN_LK, PIN_MK, PIN_HK
};
#define NUM_BUTTONS (sizeof(button_pins) / sizeof(button_pins[0]))

#if TUSB_VERSION_NUMBER > 1800
#define board_millis tusb_time_millis_api
#endif

enum {
  BLINK_NOT_MOUNTED = 250,
  BLINK_MOUNTED = 1000,
  BLINK_SUSPENDED = 2500,
};

static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;

void led_blinking_task(void);
void hid_task(void);

int main(void)
{
  board_init();

  // All buttons: inputs with internal pull-ups
  for (size_t i = 0; i < NUM_BUTTONS; i++) {
    gpio_init(button_pins[i]);
    gpio_set_dir(button_pins[i], GPIO_IN);
    gpio_pull_up(button_pins[i]);
  }

  const tusb_rhport_init_t rh_init = {
    .role = TUSB_ROLE_DEVICE,
    .speed = TUD_OPT_HIGH_SPEED ? TUSB_SPEED_HIGH : TUSB_SPEED_FULL
  };
  TU_ASSERT(tud_rhport_init(BOARD_TUD_RHPORT, &rh_init));
  board_init_after_tusb();

  while (1)
  {
    tud_task();
    led_blinking_task();
    hid_task();
  }
}

// Device callbacks: change the LED blink speed to show USB state
void tud_mount_cb(void)   { blink_interval_ms = BLINK_MOUNTED; }
void tud_umount_cb(void)  { blink_interval_ms = BLINK_NOT_MOUNTED; }
void tud_suspend_cb(bool remote_wakeup_en) { (void) remote_wakeup_en; blink_interval_ms = BLINK_SUSPENDED; }
void tud_resume_cb(void)  { blink_interval_ms = tud_mounted() ? BLINK_MOUNTED : BLINK_NOT_MOUNTED; }

// Turn four direction booleans into a hat switch value
static hid_gamepad_hat_t hat_from_dirs(bool up, bool down, bool left, bool right)
{
  if (up && right)   return GAMEPAD_HAT_UP_RIGHT;
  if (up && left)    return GAMEPAD_HAT_UP_LEFT;
  if (down && right) return GAMEPAD_HAT_DOWN_RIGHT;
  if (down && left)  return GAMEPAD_HAT_DOWN_LEFT;
  if (up)    return GAMEPAD_HAT_UP;
  if (down)  return GAMEPAD_HAT_DOWN;
  if (left)  return GAMEPAD_HAT_LEFT;
  if (right) return GAMEPAD_HAT_RIGHT;
  return GAMEPAD_HAT_CENTERED;
}

// Read all buttons and send a gamepad report only when something changes
void hid_task(void)
{
  static hid_gamepad_report_t last_report = { 0 };  // all zeros = centered, nothing pressed

  bool left  = !gpio_get(PIN_LEFT);
  bool down  = !gpio_get(PIN_DOWN);
  bool right = !gpio_get(PIN_RIGHT);
  bool up    = !gpio_get(PIN_UP);

  bool lp = !gpio_get(PIN_LP);
  bool mp = !gpio_get(PIN_MP);
  bool hp = !gpio_get(PIN_HP);
  bool lk = !gpio_get(PIN_LK);
  bool mk = !gpio_get(PIN_MK);
  bool hk = !gpio_get(PIN_HK);

  // SOCD cleaning: left+right cancel to neutral, up+down resolves to up
  if (left && right) { left = false; right = false; }
  if (up && down)    { down = false; }

  hid_gamepad_report_t report = { 0 };
  report.hat = hat_from_dirs(up, down, left, right);

  report.buttons = 0;
  if (lp) report.buttons |= GAMEPAD_BUTTON_X;
  if (mp) report.buttons |= GAMEPAD_BUTTON_Y;
  if (hp) report.buttons |= GAMEPAD_BUTTON_TR;
  if (lk) report.buttons |= GAMEPAD_BUTTON_A;
  if (mk) report.buttons |= GAMEPAD_BUTTON_B;
  if (hk) report.buttons |= GAMEPAD_BUTTON_TR2;

  // Only send when something changed
  if (memcmp(&report, &last_report, sizeof(report)) == 0) return;
  if (!tud_hid_ready()) return;

  if (tud_hid_report(REPORT_ID_GAMEPAD, &report, sizeof(report))) {
    last_report = report;
  }
}

// Required callbacks (unused for now)
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen)
{
  (void) instance; (void) report_id; (void) report_type; (void) buffer; (void) reqlen;
  return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize)
{
  (void) instance; (void) report_id; (void) report_type; (void) buffer; (void) bufsize;
}

void led_blinking_task(void)
{
  static uint32_t start_ms = 0;
  static bool led_state = false;

  if (!blink_interval_ms) return;
  if (board_millis() - start_ms < blink_interval_ms) return;
  start_ms += blink_interval_ms;

  board_led_write(led_state);
  led_state = !led_state;
}