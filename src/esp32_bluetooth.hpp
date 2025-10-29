#pragma once

#include "bluetooth.hpp"
#include "common.hpp"
#include <esp_bt.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
// #include <esp_bt_main.h>
// #include <esp_gap_ble_api.h>
// #include <esp_gattc_api.h>
#include <condition_variable>
#include <host/ble_gap.h>
#include <map>
#include <mutex>
#include <vector>

namespace atmt {

class TicketDispenser {
public:
  TicketDispenser(int max_tickets)
      : max_ticxets(max_tickets), remaining_tickets(max_tickets) {}

  // may block until enough tickets are available
  void AcquireTicket() {
    std::unique_lock lock(m);
    cv.wait(lock, [&]() { return remaining_tickets > 0; });
    remaining_tickets--;
  }
  void ReleaseTicket() {
    {
      std::unique_lock lock(m);
      remaining_tickets++;
    }
    cv.notify_one();
  }

  int Acquired() {
    std::unique_lock lock(m);
    return max_ticxets - remaining_tickets;
  };

  int Remaining() {
    std::unique_lock lock(m);
    return remaining_tickets;
  };

private:
  std::mutex m;
  std::condition_variable cv;
  int max_ticxets;
  int remaining_tickets;
};

class ESP32Bluetooth : public Bluetooth {
public:
  ESP32Bluetooth(Callbacks &callbacks);
  ~ESP32Bluetooth() override;

  // Scanning
  void StartScan() override;
  void StopScan() override;

  // Connection & Pairing
  bool Connect(string_view address) override;
  bool Pair(string_view address) override;
  bool Bond(string_view address) override;
  // bool IsPaired(string_view address) const override;

  // GATT Operations
  void ResolveServices(string_view address) override;
  bool SubscribeCharacteristic(string_view address, string_view service_uuid,
                               string_view characteristic_uuid) override;
  bool WriteCharacteristic(string_view address, string_view service_uuid,
                           string_view characteristic_uuid,
                           string_view value) override;

private:
  static int OnGapEventThis(ble_gap_event *, void *);
  int OnGapEvent(ble_gap_event *);

  static int OnGattSvcDiscoveredThis(uint16_t conn_handle,
                                     const struct ble_gatt_error *,
                                     const struct ble_gatt_svc *, void *);
  int OnGattSvcDiscovered(uint16_t conn_handle, const struct ble_gatt_error *,
                          const struct ble_gatt_svc *);

  static int OnGattChrDiscoveredThis(uint16_t conn_handle,
                                     const struct ble_gatt_error *,
                                     const struct ble_gatt_chr *, void *);
  int OnGattChrDiscovered(uint16_t conn_handle, const struct ble_gatt_error *,
                          const struct ble_gatt_chr *);

  static int OnGattWrittenThis(uint16_t conn_handle,
                               const struct ble_gatt_error *,
                               struct ble_gatt_attr *, void *);
  int OnGattWritten(uint16_t conn_handle, const struct ble_gatt_error *,
                    struct ble_gatt_attr *);

  Callbacks &callbacks_;

  uint8_t own_addr_type;
  string connecting_address;

  struct Device {
    struct Characteristic {
      ble_gatt_chr chr;
    };

    struct Service {
      ble_gatt_svc svc;
      std::vector<Characteristic> characteristics;
    };
    std::vector<Service> services;
    int active_tasks = 0;
  };

  std::map<uint16_t, Device> devices_;

  TicketDispenser write_limiter;
};

} // namespace atmt
