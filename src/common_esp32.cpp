#include "common_esp32.hpp"

#include <esp_pm.h>

#include "ble_keyboard.hpp"

namespace atmt {

bool debug_line_start = true;

std::mutex serial_mtx;

static void ReadBattery(void *) {
  int rawValue = analogRead(BATTERY_PIN);
  float voltage = (rawValue * 4.187) / 2441.0; // measured with a multimeter
  int batteryPercent =
      map(constrain(voltage * 1000, 3000, 4185), 3000, 4185, 0, 100);

  Debugf("Battery at %f V, %d%%\n", voltage, batteryPercent);
  ble_keyboard.SetBattery(batteryPercent);
}

void InitESP32() {
  if constexpr (kDebug) {
    Serial.begin(115200);
    delay(1000);
  }
  pinMode(BATTERY_PIN, INPUT);

  // Enable automatic light-sleep (modem-sleep)
  // Read and dump initial PM configuration
  esp_pm_config_esp32s3_t initial_pm_config;
  esp_err_t get_err = esp_pm_get_configuration(&initial_pm_config);
  if (get_err == ESP_OK) {
    Debugf("Initial PM configuration:\n");
    Debugf("  max_freq_mhz: %d\n", initial_pm_config.max_freq_mhz);
    Debugf("  min_freq_mhz: %d\n", initial_pm_config.min_freq_mhz);
    Debugf("  light_sleep_enable: %d\n", initial_pm_config.light_sleep_enable);
  } else {
    Debugf("Failed to get initial PM config: %d\n", get_err);
  }

  esp_sleep_enable_gpio_wakeup();

  if constexpr (true) { // Set CPU frequency to 80..40 MHz with automatic light
                        // sleep enabled
    esp_pm_config_esp32s3_t pm_config = {
        .max_freq_mhz = 80, .min_freq_mhz = 40, .light_sleep_enable = true};
    esp_err_t err = esp_pm_configure(&pm_config);
    if (err == ESP_OK) {
      Debugf("Automatic light-sleep enabled (modem-sleep)\n");
    } else {
      Debugf("Failed to enable light-sleep: %d\n", err);
    }
  }

  if constexpr (true) { // Create a battery reading timer
    auto timer_args = esp_timer_create_args_t{
        .callback = ReadBattery,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "ReadBattery",
        .skip_unhandled_events = true,
    };
    esp_timer_handle_t battery_timer; // we don't have to keep it
    esp_err_t err = esp_timer_create(&timer_args, &battery_timer);
    if (err == ESP_OK) {
      Debugf("Battery timer created\n");
      esp_timer_start_periodic(battery_timer, 5000000);
    } else {
      Debugf("Failed to create battery timer: %d\n", err);
    }
  }
}

} // namespace atmt
