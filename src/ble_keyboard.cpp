#include "ble_keyboard.hpp"

#include <freertos/timers.h>

#include "HIDTypes.h"
#include "sdkconfig.h"
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <NimBLEServer.h>
#include <NimBLEUtils.h>
#include <driver/adc.h>
#include <esp_pm.h>

#include "common_esp32.hpp"

static esp_pm_lock_handle_t s_pm_lock = nullptr;
static TimerHandle_t s_conn_param_timer = nullptr;

BleKeyboard ble_keyboard{"maf.klaw"};

static void connParamTimerCb(TimerHandle_t xTimer) {
  auto *pServer = NimBLEDevice::getServer();
  if (!pServer || pServer->getConnectedCount() == 0)
    return;
  auto handle = pServer->getPeerInfo(0).getConnHandle();
  // interval 7.5ms (6 units), latency 4, supervision timeout 2s
  pServer->updateConnParams(handle, 6, 6, 4, 200);
}

#if defined(CONFIG_ARDUHAL_ESP_LOG)
#include "esp32-hal-log.h"
#define LOG_TAG ""
#else
#include "esp_log.h"
static const char *LOG_TAG = "BLEDevice";
#endif

// Report IDs:
#define KEYBOARD_ID 0x01
#define MEDIA_KEYS_ID 0x02

static const uint8_t _hidReportDescriptor[] = {
    USAGE_PAGE(1), 0x01, // USAGE_PAGE (Generic Desktop Ctrls)
    USAGE(1), 0x06,      // USAGE (Keyboard)
    COLLECTION(1), 0x01, // COLLECTION (Application)
    // ------------------------------------------------- Keyboard
    REPORT_ID(1), KEYBOARD_ID, //   REPORT_ID (1)
    USAGE_PAGE(1), 0x07,       //   USAGE_PAGE (Kbrd/Keypad)
    USAGE_MINIMUM(1), 0xE0,    //   USAGE_MINIMUM (0xE0)
    USAGE_MAXIMUM(1), 0xE7,    //   USAGE_MAXIMUM (0xE7)
    LOGICAL_MINIMUM(1), 0x00,  //   LOGICAL_MINIMUM (0)
    LOGICAL_MAXIMUM(1), 0x01,  //   Logical Maximum (1)
    REPORT_SIZE(1), 0x01,      //   REPORT_SIZE (1)
    REPORT_COUNT(1), 0x08,     //   REPORT_COUNT (8)
    HIDINPUT(1), 0x02,         //   INPUT (Data,Var,Abs,No Wrap,Linear,Preferred
                               //   State,No Null Position)
    REPORT_COUNT(1), 0x01,     //   REPORT_COUNT (1) ; 1 byte (Reserved)
    REPORT_SIZE(1), 0x08,      //   REPORT_SIZE (8)
    HIDINPUT(1), 0x01,      //   INPUT (Const,Array,Abs,No Wrap,Linear,Preferred
                            //   State,No Null Position)
    REPORT_COUNT(1), 0x05,  //   REPORT_COUNT (5) ; 5 bits (Num lock, Caps lock,
                            //   Scroll lock, Compose, Kana)
    REPORT_SIZE(1), 0x01,   //   REPORT_SIZE (1)
    USAGE_PAGE(1), 0x08,    //   USAGE_PAGE (LEDs)
    USAGE_MINIMUM(1), 0x01, //   USAGE_MINIMUM (0x01) ; Num Lock
    USAGE_MAXIMUM(1), 0x05, //   USAGE_MAXIMUM (0x05) ; Kana
    HIDOUTPUT(1), 0x02,     //   OUTPUT (Data,Var,Abs,No Wrap,Linear,Preferred
                            //   State,No Null Position,Non-volatile)
    REPORT_COUNT(1), 0x01,  //   REPORT_COUNT (1) ; 3 bits (Padding)
    REPORT_SIZE(1), 0x03,   //   REPORT_SIZE (3)
    HIDOUTPUT(1), 0x01,    //   OUTPUT (Const,Array,Abs,No Wrap,Linear,Preferred
                           //   State,No Null Position,Non-volatile)
    REPORT_COUNT(1), 0x06, //   REPORT_COUNT (6) ; 6 bytes (Keys)
    REPORT_SIZE(1), 0x08,  //   REPORT_SIZE(8)
    LOGICAL_MINIMUM(1), 0x00, //   LOGICAL_MINIMUM(0)
    LOGICAL_MAXIMUM(1), 0x65, //   LOGICAL_MAXIMUM(0x65) ; 101 keys
    USAGE_PAGE(1), 0x07,      //   USAGE_PAGE (Kbrd/Keypad)
    USAGE_MINIMUM(1), 0x00,   //   USAGE_MINIMUM (0)
    USAGE_MAXIMUM(1), 0x65,   //   USAGE_MAXIMUM (0x65)
    HIDINPUT(1), 0x00, //   INPUT (Data,Array,Abs,No Wrap,Linear,Preferred
                       //   State,No Null Position)
    END_COLLECTION(0), // END_COLLECTION
    // ------------------------------------------------- Media Keys
    USAGE_PAGE(1), 0x0C,         // USAGE_PAGE (Consumer)
    USAGE(1), 0x01,              // USAGE (Consumer Control)
    COLLECTION(1), 0x01,         // COLLECTION (Application)
    REPORT_ID(1), MEDIA_KEYS_ID, //   REPORT_ID (3)
    USAGE_PAGE(1), 0x0C,         //   USAGE_PAGE (Consumer)
    LOGICAL_MINIMUM(1), 0x00,    //   LOGICAL_MINIMUM (0)
    LOGICAL_MAXIMUM(1), 0x01,    //   LOGICAL_MAXIMUM (1)
    REPORT_SIZE(1), 0x01,        //   REPORT_SIZE (1)
    REPORT_COUNT(1), 0x10,       //   REPORT_COUNT (16)
    USAGE(1), 0xB5,              //   USAGE (Scan Next Track)     ; bit 0: 1
    USAGE(1), 0xB6,              //   USAGE (Scan Previous Track) ; bit 1: 2
    USAGE(1), 0xB7,              //   USAGE (Stop)                ; bit 2: 4
    USAGE(1), 0xCD,              //   USAGE (Play/Pause)          ; bit 3: 8
    USAGE(1), 0xE2,              //   USAGE (Mute)                ; bit 4: 16
    USAGE(1), 0xE9,              //   USAGE (Volume Increment)    ; bit 5: 32
    USAGE(1), 0xEA,              //   USAGE (Volume Decrement)    ; bit 6: 64
    USAGE(2), 0x23, 0x02,        //   Usage (WWW Home)            ; bit 7: 128
    USAGE(2), 0x94, 0x01,        //   Usage (My Computer) ; bit 0: 1
    USAGE(2), 0x92, 0x01,        //   Usage (Calculator)  ; bit 1: 2
    USAGE(2), 0x2A, 0x02,        //   Usage (WWW fav)     ; bit 2: 4
    USAGE(2), 0x21, 0x02,        //   Usage (WWW search)  ; bit 3: 8
    USAGE(2), 0x26, 0x02,        //   Usage (WWW stop)    ; bit 4: 16
    USAGE(2), 0x24, 0x02,        //   Usage (WWW back)    ; bit 5: 32
    USAGE(2), 0x83, 0x01,        //   Usage (Media sel)   ; bit 6: 64
    USAGE(2), 0x8A, 0x01,        //   Usage (Mail)        ; bit 7: 128
    HIDINPUT(1), 0x02, //   INPUT (Data,Var,Abs,No Wrap,Linear,Preferred
                       //   State,No Null Position)
    END_COLLECTION(0)  // END_COLLECTION
};

BleKeyboard::BleKeyboard(std::string deviceName, std::string deviceManufacturer,
                         uint8_t batteryLevel)
    : hid(0), deviceName(std::string(deviceName).substr(0, 15)),
      deviceManufacturer(std::string(deviceManufacturer).substr(0, 15)),
      batteryLevel(batteryLevel) {}

void BleKeyboard::begin(void) {
  BLEDevice::init(deviceName);
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(this);

  hid = new BLEHIDDevice(pServer);
  inputKeyboard =
      hid->inputReport(KEYBOARD_ID); // <-- input REPORTID from report map
  outputKeyboard = hid->outputReport(KEYBOARD_ID);
  inputMediaKeys = hid->inputReport(MEDIA_KEYS_ID);

  outputKeyboard->setCallbacks(this);
  inputKeyboard->setCallbacks(this);
  inputMediaKeys->setCallbacks(this);

  hid->manufacturer()->setValue(deviceManufacturer);

  hid->pnp(0x02, vid, pid, version);
  hid->hidInfo(0x00, 0x01);

  BLEDevice::setSecurityAuth(true, true, true);

  hid->reportMap((uint8_t *)_hidReportDescriptor, sizeof(_hidReportDescriptor));
  hid->startServices();

  onStarted(pServer);

  advertising = pServer->getAdvertising();
  advertising->setAppearance(HID_KEYBOARD);
  advertising->addServiceUUID(hid->hidService()->getUUID());
  advertising->setScanResponse(false);
  advertising->start();
  hid->setBatteryLevel(batteryLevel);

  esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "ble_connected", &s_pm_lock);

  s_conn_param_timer = xTimerCreate("conn_param", pdMS_TO_TICKS(2000), pdFALSE,
                                    nullptr, connParamTimerCb);

  _txSem = xSemaphoreCreateBinary();
  xSemaphoreGive(_txSem); // start as available

  ESP_LOGD(LOG_TAG, "Advertising started!");
}

void BleKeyboard::end() {}

bool BleKeyboard::IsConnected() { return this->connected; }

void BleKeyboard::SetBattery(uint8_t level) {
  this->batteryLevel = level;
  if (hid != 0)
    this->hid->setBatteryLevel(this->batteryLevel);
}

// must be called before begin in order to set the name
void BleKeyboard::setName(std::string deviceName) {
  this->deviceName = deviceName;
}

void BleKeyboard::set_vendor_id(uint16_t vid) { this->vid = vid; }

void BleKeyboard::set_product_id(uint16_t pid) { this->pid = pid; }

void BleKeyboard::set_version(uint16_t version) { this->version = version; }

void BleKeyboard::waitForTx() {
  // Wait for the previous notification to be transmitted before sending the
  // next one.  The semaphore is given back by onStatus() when the BLE stack
  // confirms the notification was sent.  Timeout guards against a lost
  // callback.
  if (_txSem) {
    auto ret = xSemaphoreTake(_txSem, pdMS_TO_TICKS(50));
    if (ret != pdTRUE) {
      Debugln("waitForTx: timeout waiting for previous transmission");
    }
  }
}

void BleKeyboard::sendReport(KeyReport *keys) {
  if (this->IsConnected()) {
    this->inputKeyboard->setValue((uint8_t *)keys, sizeof(KeyReport));
    this->inputKeyboard->notify();
  }
}

void BleKeyboard::sendReport(MediaKeyReport *keys) {
  if (this->IsConnected()) {
    this->inputMediaKeys->setValue((uint8_t *)keys, sizeof(MediaKeyReport));
    this->inputMediaKeys->notify();
  }
}

uint8_t USBPutChar(uint8_t c);

size_t BleKeyboard::press(const MediaKeyReport k) {
  uint16_t k_16 = k[1] | (k[0] << 8);
  uint16_t mediaKeyReport_16 = _mediaKeyReport[1] | (_mediaKeyReport[0] << 8);

  mediaKeyReport_16 |= k_16;
  _mediaKeyReport[0] = (uint8_t)((mediaKeyReport_16 & 0xFF00) >> 8);
  _mediaKeyReport[1] = (uint8_t)(mediaKeyReport_16 & 0x00FF);

  sendReport(&_mediaKeyReport);
  return 1;
}

size_t BleKeyboard::release(const MediaKeyReport k) {
  uint16_t k_16 = k[1] | (k[0] << 8);
  uint16_t mediaKeyReport_16 = _mediaKeyReport[1] | (_mediaKeyReport[0] << 8);
  mediaKeyReport_16 &= ~k_16;
  _mediaKeyReport[0] = (uint8_t)((mediaKeyReport_16 & 0xFF00) >> 8);
  _mediaKeyReport[1] = (uint8_t)(mediaKeyReport_16 & 0x00FF);

  sendReport(&_mediaKeyReport);
  return 1;
}

void BleKeyboard::releaseAll(void) {
  _keyReport.keys[0] = 0;
  _keyReport.keys[1] = 0;
  _keyReport.keys[2] = 0;
  _keyReport.keys[3] = 0;
  _keyReport.keys[4] = 0;
  _keyReport.keys[5] = 0;
  _keyReport.modifiers = 0;
  _mediaKeyReport[0] = 0;
  _mediaKeyReport[1] = 0;
  sendReport(&_keyReport);
  sendReport(&_mediaKeyReport);
}

size_t BleKeyboard::write(const MediaKeyReport c) {
  uint16_t p = press(c); // Keydown
  release(c);            // Keyup
  return p; // just return the result of press() since release() almost always
            // returns 1
}

void BleKeyboard::onConnect(BLEServer *pServer) {
  Debugf("Connected to %s\n",
         pServer->getPeerInfo(0).getAddress().toString().c_str());
  this->connected = true;
  esp_pm_lock_acquire(s_pm_lock);
  pServer->advertiseOnDisconnect(true);

  // Stop advertising so no other device can connect while we're connected
  advertising->stop();
  // Delay connection parameter update to avoid racing with encryption and
  // GATT discovery (immediate updateConnParams causes HCI 0x28 "Instant
  // Passed" disconnects).
  if (s_conn_param_timer)
    xTimerStart(s_conn_param_timer, 0);
}

void BleKeyboard::onDisconnect(BLEServer *pServer) {
  this->connected = false;
  esp_pm_lock_release(s_pm_lock);
  if (s_conn_param_timer)
    xTimerStop(s_conn_param_timer, 0);
}

void BleKeyboard::onDisconnect(BLEServer *pServer, ble_gap_conn_desc *desc) {
  // reason sits right before conn in ble_gap_event::disconnect
  int reason = reinterpret_cast<int *>(desc)[-1];
  Debugf("Disconnected: reason=0x%x (%d)\n", reason, reason);
  Debugf("  peer_id_addr=%02x:%02x:%02x:%02x:%02x:%02x (type=%d)\n",
         desc->peer_id_addr.val[5], desc->peer_id_addr.val[4],
         desc->peer_id_addr.val[3], desc->peer_id_addr.val[2],
         desc->peer_id_addr.val[1], desc->peer_id_addr.val[0],
         desc->peer_id_addr.type);
  Debugf(
      "  conn_handle=%d conn_itvl=%d conn_latency=%d supervision_timeout=%d\n",
      desc->conn_handle, desc->conn_itvl, desc->conn_latency,
      desc->supervision_timeout);
  Debugf("  encrypted=%d authenticated=%d bonded=%d\n",
         desc->sec_state.encrypted, desc->sec_state.authenticated,
         desc->sec_state.bonded);
}

void BleKeyboard::onWrite(BLECharacteristic *me) {
  uint8_t *value = (uint8_t *)(me->getValue().c_str());
  (void)value;
  ESP_LOGI(LOG_TAG, "special keys: %d", *value);
}

void BleKeyboard::onStatus(BLECharacteristic *pCharacteristic, Status s,
                           int code) {
  if (code != 0) {
    Debugf("BleKeyboard::onStatus code=%d\n", code);
  }
  // Called by NimBLE when a notification has been transmitted (or an
  // indication acknowledged).  Release the semaphore so the next sendReport
  // can proceed.
  if (_txSem)
    xSemaphoreGive(_txSem);
}

void BleKeyboard::Setup() {
  setName("𝖒𝖆𝖋.🎹");
  begin();

  int num_bonds = NimBLEDevice::getNumBonds();
  Debugf("> %d bonded device(s):\n", num_bonds);
  for (int i = 0; i < num_bonds; i++) {
    auto addr = NimBLEDevice::getBondedAddress(i);
    Debugf(">   %d: %s\n", i, addr.toString().c_str());
  }
}
