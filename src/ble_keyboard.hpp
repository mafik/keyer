#pragma once

#include <bitset>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/timers.h>

#include "NimBLEHIDDevice.h"

#define BLEDevice NimBLEDevice
#define BLEServerCallbacks NimBLEServerCallbacks
#define BLECharacteristicCallbacks NimBLECharacteristicCallbacks
#define BLEHIDDevice NimBLEHIDDevice
#define BLECharacteristic NimBLECharacteristic
#define BLEAdvertising NimBLEAdvertising
#define BLEServer NimBLEServer

#include "keyboard.hpp"

#define BLE_KEYBOARD_VERSION "0.0.4"
#define BLE_KEYBOARD_VERSION_MAJOR 0
#define BLE_KEYBOARD_VERSION_MINOR 0
#define BLE_KEYBOARD_VERSION_REVISION 4

typedef uint8_t MediaKeyReport[2];

const MediaKeyReport KEY_MEDIA_NEXT_TRACK = {1, 0};
const MediaKeyReport KEY_MEDIA_PREVIOUS_TRACK = {2, 0};
const MediaKeyReport KEY_MEDIA_STOP = {4, 0};
const MediaKeyReport KEY_MEDIA_PLAY_PAUSE = {8, 0};
const MediaKeyReport KEY_MEDIA_MUTE = {16, 0};
const MediaKeyReport KEY_MEDIA_VOLUME_UP = {32, 0};
const MediaKeyReport KEY_MEDIA_VOLUME_DOWN = {64, 0};
const MediaKeyReport KEY_MEDIA_WWW_HOME = {128, 0};
const MediaKeyReport KEY_MEDIA_LOCAL_MACHINE_BROWSER = {
    0, 1}; // Opens "My Computer" on Windows
const MediaKeyReport KEY_MEDIA_CALCULATOR = {0, 2};
const MediaKeyReport KEY_MEDIA_WWW_BOOKMARKS = {0, 4};
const MediaKeyReport KEY_MEDIA_WWW_SEARCH = {0, 8};
const MediaKeyReport KEY_MEDIA_WWW_STOP = {0, 16};
const MediaKeyReport KEY_MEDIA_WWW_BACK = {0, 32};
const MediaKeyReport KEY_MEDIA_CONSUMER_CONTROL_CONFIGURATION = {
    0, 64}; // Media Selection
const MediaKeyReport KEY_MEDIA_EMAIL_READER = {0, 128};

//  Low level key report: up to 6 keys and shift, ctrl etc at once
struct KeyReport {
  uint8_t modifiers;
  uint8_t reserved;
  uint8_t keys[6];

  bool Contains(HID_Key key) const volatile {
    for (int i = 0; i < 6; ++i) {
      if (keys[i] == (uint8_t)key)
        return true;
    }
    return false;
  }

  int Count() const volatile {
    int count = 0;
    for (int i = 0; i < 6; ++i) {
      if (keys[i] != 0)
        ++count;
    }
    return count;
  }
};

class BleKeyboard : public BLEServerCallbacks,
                    public BLECharacteristicCallbacks {
private:
  BLEHIDDevice *hid;
  BLECharacteristic *inputKeyboard;
  BLECharacteristic *outputKeyboard;
  BLECharacteristic *inputMediaKeys;
  BLEAdvertising *advertising;
  KeyReport _keyReport;
  MediaKeyReport _mediaKeyReport;
  std::string deviceName;
  std::string deviceManufacturer;
  uint8_t batteryLevel;
  bool connected = false;
  SemaphoreHandle_t _txSem = nullptr;

  uint16_t vid = 0x05ac;
  uint16_t pid = 0x820a;
  uint16_t version = 0x0210;

public:
  BleKeyboard(std::string deviceName = "ESP32 Keyboard",
              std::string deviceManufacturer = "Espressif",
              uint8_t batteryLevel = 100);
  void begin(void);
  void end(void);
  void waitForTx();
  void sendReport(KeyReport *keys);
  void sendReport(MediaKeyReport *keys);
  size_t press(const MediaKeyReport k);
  size_t release(const MediaKeyReport k);
  size_t write(const MediaKeyReport c);
  void releaseAll(void);
  bool IsConnected(void);
  void SetBattery(uint8_t level);
  void setName(std::string deviceName);
  void set_vendor_id(uint16_t vid);
  void set_product_id(uint16_t pid);
  void set_version(uint16_t version);

  void Setup();

protected:
  virtual void onStarted(BLEServer *pServer) {};
  virtual void onConnect(BLEServer *pServer) override;
  virtual void onDisconnect(BLEServer *pServer) override;
  virtual void onDisconnect(BLEServer *pServer,
                            ble_gap_conn_desc *desc) override;
  virtual void onWrite(BLECharacteristic *me) override;
  virtual void onStatus(BLECharacteristic *pCharacteristic, Status s,
                        int code) override;
};

extern BleKeyboard ble_keyboard;
