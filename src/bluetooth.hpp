#pragma once

#include "common.hpp"
#include <memory>

namespace atmt {

class Bluetooth {
public:
  virtual ~Bluetooth() = default;

  //////////////////////////
  // Callback Interface
  //////////////////////////

  class Callbacks {
  public:
    virtual ~Callbacks() = default;

    virtual void OnDeviceFound(string_view address, string_view identifier) = 0;
    virtual void OnConnected(string_view address) = 0;
    virtual void OnDisconnected(string_view address) = 0;
    virtual void OnPaired(string_view address) = 0;
    virtual void OnBonded(string_view address) = 0;
    virtual void OnServicesResolved(string_view address) = 0;
    virtual void OnCharacteristicNotified(string_view address,
                                          string_view characteristic_uuid,
                                          string_view value) = 0;
    virtual void OnSubscribed(string_view address, string_view service_uuid,
                              string_view characteristic_uuid) = 0;
  };

  //////////////////////////
  // Scanning
  //////////////////////////

  virtual void StartScan() = 0;
  virtual void StopScan() = 0;

  //////////////////////////
  // Connection & Pairing
  //////////////////////////

  virtual bool Connect(string_view address) = 0;
  virtual bool Pair(string_view address) = 0;
  virtual bool Bond(string_view address) = 0;

  //////////////////////////
  // GATT Operations
  //////////////////////////

  virtual void ResolveServices(string_view address) = 0;
  virtual bool SubscribeCharacteristic(string_view address,
                                       string_view service_uuid,
                                       string_view characteristic_uuid) = 0;
  virtual bool WriteCharacteristic(string_view address,
                                   string_view service_uuid,
                                   string_view characteristic_uuid,
                                   string_view value) = 0;

  //////////////////////////
  // Factory Methods
  //////////////////////////

  static std::unique_ptr<Bluetooth> MakeBlueZ(Callbacks &callbacks);
};

} // namespace atmt
