#include "esp32_bluetooth.hpp"
#include "common_esp32.hpp"
#include "freertos/portmacro.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs_adv.h"
#include "main_loop.hpp"
#include <Arduino.h>
#include <condition_variable>
#include <esp_nimble_hci.h>
#include <host/ble_hs.h>
#include <host/ble_l2cap.h>
#include <host/ble_uuid.h>
#include <host/util/util.h>
#include <nimble/ble.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>

namespace atmt {

static void ble_host_task(void *param) {
  nimble_port_run(); // This function will return only when nimble_port_stop()
                     // is executed.
  nimble_port_freertos_deinit();
}

template <typename... Args> void MainDebugf(const char *format, Args... args) {
  RunOnMain([=]() { Debugf(format, args...); });
}

std::mutex ble_mutex;
std::condition_variable ble_cv;
bool ble_synchronized = false;

static const char *BleErrToStr(int err) {
  switch (err) {
  case 0:
    return "OK";
  case BLE_HS_EAGAIN:
    return "EAGAIN";
  case BLE_HS_EALREADY:
    return "EALREADY";
  case BLE_HS_EINVAL:
    return "EINVAL";
  case BLE_HS_EMSGSIZE:
    return "EMSGSIZE";
  case BLE_HS_ENOENT:
    return "ENOENT";
  case BLE_HS_ENOMEM:
    return "ENOMEM";
  case BLE_HS_ENOTCONN:
    return "ENOTCONN";
  case BLE_HS_ENOTSUP:
    return "ENOTSUP";
  case BLE_HS_EAPP:
    return "EAPP";
  case BLE_HS_EBADDATA:
    return "EBADDATA";
  case BLE_HS_EOS:
    return "EOS";
  case BLE_HS_ECONTROLLER:
    return "ECONTROLLER";
  case BLE_HS_ETIMEOUT:
    return "ETIMEOUT";
  case BLE_HS_EDONE:
    return "EDONE";
  case BLE_HS_EBUSY:
    return "EBUSY";
  case BLE_HS_EREJECT:
    return "EREJECT";
  case BLE_HS_EUNKNOWN:
    return "EUNKNOWN";
  case BLE_HS_EROLE:
    return "EROLE";
  case BLE_HS_ETIMEOUT_HCI:
    return "ETIMEOUT_HCI";
  case BLE_HS_ENOMEM_EVT:
    return "ENOMEM_EVT";
  case BLE_HS_ENOADDR:
    return "ENOADDR";
  case BLE_HS_ENOTSYNCED:
    return "ENOTSYNCED";
  case BLE_HS_EAUTHEN:
    return "EAUTHEN";
  case BLE_HS_EAUTHOR:
    return "EAUTHOR";
  case BLE_HS_EENCRYPT:
    return "EENCRYPT";
  case BLE_HS_EENCRYPT_KEY_SZ:
    return "EENCRYPT_KEY_SZ";
  case BLE_HS_ESTORE_CAP:
    return "ESTORE_CAP";
  case BLE_HS_ESTORE_FAIL:
    return "ESTORE_FAIL";
  case BLE_HS_EPREEMPTED:
    return "EPREEMPTED";
  case BLE_HS_EDISABLED:
    return "EDISABLED";
  case BLE_HS_ESTALLED:
    return "ESTALLED";
  }
  static char buf[100] = {};
  if (err >= BLE_HS_ERR_HW_BASE) {
    snprintf(buf, 100, "HW(%d)", err - BLE_HS_ERR_HW_BASE);
  } else if (err >= BLE_HS_ERR_SM_PEER_BASE) {
    snprintf(buf, 100, "SM_PEER(%d)", err - BLE_HS_ERR_SM_PEER_BASE);
  } else if (err >= BLE_HS_ERR_SM_US_BASE) {
    snprintf(buf, 100, "SM_US(%d)", err - BLE_HS_ERR_SM_US_BASE);
  } else if (err >= BLE_HS_ERR_L2C_BASE) {
    snprintf(buf, 100, "L2C(%d)", err - BLE_HS_ERR_L2C_BASE);
  } else if (err >= BLE_HS_ERR_HCI_BASE) {
    snprintf(buf, 100, "HCI(%d)", err - BLE_HS_ERR_HCI_BASE);
  } else if (err >= BLE_HS_ERR_ATT_BASE) {
    snprintf(buf, 100, "ATT(%d)", err - BLE_HS_ERR_ATT_BASE);
  } else {
    snprintf(buf, 100, "UNK(%d)", err);
  }
  return buf;
}

string ToStr(ble_uuid_any_t uuid) {
  char buf[BLE_UUID_STR_LEN];
  ble_uuid_to_str(&(uuid.u), buf);
  return buf;
}

ble_uuid_any_t ToUuid(string_view s) {
  ble_uuid_any_t ret;
  if (s.size() == 36) {
    ret.u128.u.type = BLE_UUID_TYPE_128;
    sscanf(s.data(),
           "%02hhx%02hhx%02hhx%02hhx-%02hhx%02hhx-%02hhx%02hhx-%02hhx%02hhx-%"
           "02hhx%02hhx%02hhx%02hhx%02hhx%02hhx",
           &ret.u128.value[15], &ret.u128.value[14], &ret.u128.value[13],
           &ret.u128.value[12], &ret.u128.value[11], &ret.u128.value[10],
           &ret.u128.value[9], &ret.u128.value[8], &ret.u128.value[7],
           &ret.u128.value[6], &ret.u128.value[5], &ret.u128.value[4],
           &ret.u128.value[3], &ret.u128.value[2], &ret.u128.value[1],
           &ret.u128.value[0]);

  } else {
    ret.u128.u.type = BLE_UUID_TYPE_128;
    for (int i = 0; i < 16; ++i) {
      ret.u128.value[i] = 0x42;
    }
  }
  return ret;
}

ESP32Bluetooth::ESP32Bluetooth(Callbacks &callbacks)
    : callbacks_(callbacks), write_limiter(3) {
  // Initialize NVS for Bluetooth
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ret = esp_nimble_hci_and_controller_init();
  if (ret != ESP_OK) {
    Debugf("esp_nimble_hci_and_controller_init() failed with error: %d", ret);
    return;
  }

  // Initialize Bluetooth controller
  nimble_port_init();

  // TODO: Initialize the required NimBLE host configuration parameters and
  // callbacks
  ble_hs_cfg.gatts_register_cb = [](ble_gatt_register_ctxt *ctx, void *arg) {
    MainDebugf("gatts_register_cb()\n");
  };

  ble_hs_cfg.reset_cb = [](int reason) {
    MainDebugf("reset_cb(%d)\n", reason);
  };

  ble_hs_cfg.sync_cb = []() {
    if (int err = ble_hs_util_ensure_addr(0)) {
      MainDebugf("ble_hs_util_ensure_addr() => %s\n", BleErrToStr(err));
    }
    {
      std::unique_lock lk(ble_mutex);
      ble_synchronized = true;
    }
    ble_cv.notify_one();
  };

  ble_hs_cfg.store_status_cb = [](ble_store_status_event *event, void *arg) {
    // auto *bt = static_cast<ESP32Bluetooth *>(arg);

    // This indicates that the operation may fail. But there's no reason to
    // panic. Just proceed as usual.
    if (event->event_code == BLE_STORE_EVENT_FULL) {
      return 0;
    }
    MainDebugf("store_status_cb(%d)\n", event->event_code);
    return 1; // aborts the store operation
  };
  ble_hs_cfg.store_status_arg = this;

  ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
  ble_hs_cfg.sm_bonding = 1;
  ble_hs_cfg.sm_our_key_dist = 1;
  ble_hs_cfg.sm_their_key_dist = 1;
  ble_hs_cfg.sm_mitm = 1;
  ble_hs_cfg.sm_sc = 1;

  // TODO: Perform application specific tasks/initialization

  // int n_bonded = esp_ble_get_bond_device_num();
  // Debugf("Found %d bonded devices\n", n_bonded);
  // esp_ble_bond_dev_t *bond_devices =
  //     (esp_ble_bond_dev_t *)malloc(sizeof(esp_ble_bond_dev_t) * n_bonded);
  // esp_ble_get_bond_device_list(&n_bonded, bond_devices);
  // nvs_handle_t handle;
  // ESP_ERROR_CHECK(nvs_open("ESP32Bluetooth", NVS_READONLY, &handle));
  // for (int i = 0; i < n_bonded; i++) {
  //   string key = {};
  //   for (int j = 0; j < 6; ++j)
  //     AppendHex(key, bond_devices[i].bd_addr[j]);
  //   size_t length = 0;
  //   if (nvs_get_str(handle, key.c_str(), nullptr, &length)) {
  //     Debugf("  - %s not found\n", key.c_str());
  //     continue; // device name was not recorded to nvs yet - just skip it
  //   }
  //   auto name = string(length - 1, '\0'); // -1 because std::string adds \0
  //   nvs_get_str(handle, key.c_str(), name.data(), &length);
  //   string address = BdaToString(bond_devices[i].bd_addr);
  //   Debugf("  - %s (%s) mapped to \"%s\"\n", key.c_str(), address.c_str(),
  //          name.c_str());

  //   auto &device = devices_[address];
  //   device.address_str = address;
  //   memcpy(device.bda, bond_devices[i].bd_addr, sizeof(esp_bd_addr_t));
  //   device.ble_addr_type = BLE_ADDR_TYPE_RANDOM;
  //   // device.name = name; // don't set the name - no need to re-write it to
  //   nvs

  //   callbacks_.OnDeviceFound(address, name);
  //   callbacks_.OnBonded(address);
  // }
  // nvs_close(handle);
  // free(bond_devices);

  nimble_port_freertos_init(ble_host_task);

  std::unique_lock lk(ble_mutex);
  ble_cv.wait(lk, [] { return ble_synchronized; });

  if (int err = ble_hs_id_infer_auto(0, &own_addr_type)) {
    MainDebugf("ble_hs_id_infer_auto() => %s\n", BleErrToStr(err));
  }
}

ESP32Bluetooth::~ESP32Bluetooth() {
  int ret = nimble_port_stop();
  if (ret == 0) {
    nimble_port_deinit();
    ret = esp_nimble_hci_and_controller_deinit();
    if (ret != ESP_OK) {
      Debugf("esp_nimble_hci_and_controller_deinit() failed with error: %s",
             BleErrToStr(ret));
    }
  }
}

void ESP32Bluetooth::StartScan() {

  ble_gap_disc_params params = ble_gap_disc_params{
      .itvl = 0x50,
      .window = 0x30,
      .filter_policy = 0,
      .limited = 0,
      .passive = 0,
      .filter_duplicates = 1,
  };

  if (int err = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &params,
                             OnGapEventThis, this)) {
    MainDebugf("ble_gap_disc() => %s\n", BleErrToStr(err));
    return;
  }
}

string ToStr(ble_addr_t &addr) {
  string s = "(?)??:??:??:??:??:??";
  snprintf(s.data(), s.size() + 1, "(%d)%02X:%02X:%02X:%02X:%02X:%02X",
           addr.type < 10 ? addr.type : 9, addr.val[5], addr.val[4],
           addr.val[3], addr.val[2], addr.val[1], addr.val[0]);
  return s;
}

ble_addr_t ToAddr(string_view addr) {
  ble_addr_t ret;
  ret.type = BLE_ADDR_RANDOM_ID;
  sscanf(addr.data(), "(%hhu)%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:%02hhX",
         &ret.type, &ret.val[5], &ret.val[4], &ret.val[3], &ret.val[2],
         &ret.val[1], &ret.val[0]);
  return ret;
}

int ESP32Bluetooth::OnGapEventThis(ble_gap_event *event, void *arg) {
  return static_cast<ESP32Bluetooth *>(arg)->OnGapEvent(event);
}

static uint16_t ToConnHandle(string_view addr_str) {
  ble_gap_conn_desc desc{};
  auto addr = ToAddr(addr_str);
  if (ble_gap_conn_find_by_addr(&addr, &desc)) {
    return 0;
  }
  return desc.conn_handle;
}

static string ToAddrStr(uint16_t conn_handle) {
  ble_gap_conn_desc desc{};
  if (ble_gap_conn_find(conn_handle, &desc)) {
    return "(0)00:00:00:00:00:00";
  }
  return ToStr(desc.peer_id_addr);
}

int ESP32Bluetooth::OnGapEvent(ble_gap_event *event) {
  switch (event->type) {
  case BLE_GAP_EVENT_CONNECT: {
    // MainDebugf("OnGapEvent(BLE_GAP_EVENT_CONNECT, handle=%d, status=%d)\n",
    //            event->connect.conn_handle, event->connect.status);
    auto &connect = event->connect;
    if (connect.status == 0) {
      ble_gattc_exchange_mtu(connect.conn_handle, nullptr, nullptr);
      // callbacks_.OnConnected(connecting_address);
    } else {
      callbacks_.OnDisconnected(connecting_address);
    }
    connecting_address.clear();
    break;
  }
  case BLE_GAP_EVENT_DISCONNECT: {
    auto &disconnect = event->disconnect;
    MainDebugf(
        "OnGapEvent(BLE_GAP_EVENT_DISCONNECT, conn_handle=%d, reason=%d)\n",
        disconnect.conn.conn_handle, disconnect.reason);
    auto addr = ToStr(disconnect.conn.peer_id_addr);
    callbacks_.OnDisconnected(addr);
    break;
  }
  case BLE_GAP_EVENT_CONN_UPDATE: {
    // MainDebugf("OnGapEvent(BLE_GAP_EVENT_CONN_UPDATE)\n");
    break;
  }
  case BLE_GAP_EVENT_CONN_UPDATE_REQ: {
    // MainDebugf("OnGapEvent(BLE_GAP_EVENT_CONN_UPDATE_REQ)\n");
    break;
  }
  case BLE_GAP_EVENT_L2CAP_UPDATE_REQ: {
    // auto &req = event->conn_update_req;
    // auto &params = *req.peer_params;
    // MainDebugf("OnGapEvent(BLE_GAP_EVENT_L2CAP_UPDATE_REQ, conn_handle=%d, "
    //            "itvl_min=%d, itvl_max=%d, latency=%d, supervision_timeout=%d,
    //            " "min_ce_len=%d, max_ce_len=%d)\n", req.conn_handle,
    //            params.itvl_min, params.itvl_max, params.latency,
    //            params.supervision_timeout, params.min_ce_len,
    //            params.max_ce_len);
    return 0;
  }
  case BLE_GAP_EVENT_TERM_FAILURE: {
    MainDebugf("OnGapEvent(BLE_GAP_EVENT_TERM_FAILURE)\n");
    break;
  }
  case BLE_GAP_EVENT_DISC: {
    // MainDebugf("OnGapEvent(BLE_GAP_EVENT_DISC)");
    auto addr = ToStr(event->disc.addr);
    ble_hs_adv_fields fields;
    if (int err = ble_hs_adv_parse_fields(&fields, event->disc.data,
                                          event->disc.length_data)) {
      MainDebugf("ble_hs_adv_parse_fields() => %s\n", BleErrToStr(err));
      break;
    }
    if (fields.name != nullptr) {
      callbacks_.OnDeviceFound(
          addr, string_view((char *)fields.name, fields.name_len));
    }
    break;
  }
  case BLE_GAP_EVENT_DISC_COMPLETE: {
    MainDebugf("OnGapEvent(BLE_GAP_EVENT_DISC_COMPLETE)\n");
    break;
  }
  case BLE_GAP_EVENT_ADV_COMPLETE: {
    MainDebugf("OnGapEvent(BLE_GAP_EVENT_ADV_COMPLETE)\n");
    break;
  }
  case BLE_GAP_EVENT_ENC_CHANGE: {
    auto &enc_change = event->enc_change;
    // MainDebugf("OnGapEvent(BLE_GAP_EVENT_ENC_CHANGE, handle=%d,
    // status=%s)\n",
    //            enc_change.conn_handle, BleErrToStr(enc_change.status));
    auto addr = ToAddrStr(enc_change.conn_handle);
    if (enc_change.status == 0) {
      callbacks_.OnPaired(addr);
    } else {
    }
    break;
  }
  case BLE_GAP_EVENT_PASSKEY_ACTION: {
    MainDebugf("OnGapEvent(BLE_GAP_EVENT_PASSKEY_ACTION)\n");
    break;
  }
  case BLE_GAP_EVENT_NOTIFY_RX: {
    auto &rx = event->notify_rx;
    size_t size = 0;
    int chunks = 0;
    for (os_mbuf *it = rx.om; it; it = it->om_next.sle_next) {
      size += it->om_len;
      ++chunks;
    }
    // MainDebugf(
    //     "OnGapEvent(BLE_GAP_EVENT_NOTIFY_RX, conn=%d, attr=%d size=%d)\n",
    //     rx.conn_handle, rx.attr_handle, size);

    if (chunks > 1) {
      MainDebugf("  ERROR - received message in >1 chunk\n");
      return 1;
    }
    auto address = ToAddrStr(rx.conn_handle);
    string characteristic_uuid;
    auto &device = devices_[rx.conn_handle];
    for (auto &service : device.services) {
      for (auto &characteristic : service.characteristics) {
        if (characteristic.chr.val_handle == rx.attr_handle) {
          characteristic_uuid = ToStr(characteristic.chr.uuid);
        }
      }
    }
    auto value = string_view((char *)rx.om->om_data, rx.om->om_len);
    callbacks_.OnCharacteristicNotified(address, characteristic_uuid, value);
    return 0;
  }
  case BLE_GAP_EVENT_NOTIFY_TX: {
    MainDebugf("OnGapEvent(BLE_GAP_EVENT_NOTIFY_TX)\n");
    break;
  }
  case BLE_GAP_EVENT_SUBSCRIBE: {
    MainDebugf("OnGapEvent(BLE_GAP_EVENT_SUBSCRIBE)\n");
    break;
  }
  case BLE_GAP_EVENT_MTU: {
    auto &mtu = event->mtu;
    // MainDebugf("OnGapEvent(BLE_GAP_EVENT_MTU, conn_handle=%d, channel_id=%d,
    // "
    //            "value=%d)\n",
    //            mtu.conn_handle, mtu.channel_id, mtu.value);
    auto addr_str = ToAddrStr(mtu.conn_handle);
    callbacks_.OnConnected(addr_str);
    break;
  }
  case BLE_GAP_EVENT_IDENTITY_RESOLVED: {
    MainDebugf("OnGapEvent(BLE_GAP_EVENT_IDENTITY_RESOLVED)\n");
    break;
  }
  case BLE_GAP_EVENT_REPEAT_PAIRING: {
    MainDebugf("OnGapEvent(BLE_GAP_EVENT_REPEAT_PAIRING)\n");
    break;
  }
  case BLE_GAP_EVENT_PHY_UPDATE_COMPLETE: {
    MainDebugf("OnGapEvent(BLE_GAP_EVENT_PHY_UPDATE_COMPLETE)\n");
    break;
  }
  case BLE_GAP_EVENT_PERIODIC_SYNC: {
    MainDebugf("OnGapEvent(BLE_GAP_EVENT_PERIODIC_SYNC)\n");
    break;
  }
  case BLE_GAP_EVENT_PERIODIC_REPORT: {
    MainDebugf("OnGapEvent(BLE_GAP_EVENT_PERIODIC_REPORT)\n");
    break;
  }
  case BLE_GAP_EVENT_PERIODIC_SYNC_LOST: {
    MainDebugf("OnGapEvent(BLE_GAP_EVENT_PERIODIC_SYNC_LOST)\n");
    break;
  }
  case BLE_GAP_EVENT_SCAN_REQ_RCVD: {
    MainDebugf("OnGapEvent(BLE_GAP_EVENT_SCAN_REQ_RCVD)\n");
    break;
  }
  case BLE_GAP_EVENT_PERIODIC_TRANSFER: {
    MainDebugf("OnGapEvent(BLE_GAP_EVENT_PERIODIC_TRANSFER)\n");
    break;
  }
  case BLE_GAP_EVENT_REATTEMPT_COUNT: {
    MainDebugf("OnGapEvent(BLE_GAP_EVENT_REATTEMPT_COUNT)\n");
    return 0;
  }
  default: {
    MainDebugf("OnGapEvent(%d)\n", event->type);
    break;
  }
  }
  return 1;
}

void ESP32Bluetooth::StopScan() {
  if (int err = ble_gap_disc_cancel()) {
    MainDebugf("ble_gap_disc_cancel() => %s\n", BleErrToStr(err));
  }
}

bool ESP32Bluetooth::Connect(string_view address_text) {
  auto addr = ToAddr(address_text);
  if (int err = ble_gap_connect(own_addr_type, &addr, 30000, nullptr,
                                OnGapEventThis, this)) {
    MainDebugf("ble_gap_connect() => %s\n", BleErrToStr(err));
    return false;
  }
  connecting_address = address_text;
  return true;
}

bool ESP32Bluetooth::Pair(string_view addr_str) {
  auto conn_handle = ToConnHandle(addr_str);
  if (conn_handle == 0) {
    MainDebugf("Attempted to pair with non-connected address!/n");
    return false;
  }
  if (int err = ble_gap_security_initiate(conn_handle)) {
    MainDebugf("ble_gap_security_initiate(%d) => %s\n", conn_handle,
               BleErrToStr(err));
    return false;
  }
  return true;
}

bool ESP32Bluetooth::Bond(string_view address) {
  // On ESP32, bonding happens automatically during pairing
  callbacks_.OnBonded(address);
  return true;
}

void ESP32Bluetooth::ResolveServices(string_view addr_str) {
  auto conn_handle = ToConnHandle(addr_str);
  if (conn_handle == 0) {
    MainDebugf("Attempted to discover services on non-connected address!/n");
    return;
  }
  devices_[conn_handle].services.clear();
  devices_[conn_handle].active_tasks = 1;
  if (int err =
          ble_gattc_disc_all_svcs(conn_handle, OnGattSvcDiscoveredThis, this)) {
    MainDebugf("ble_gattc_disc_all_svcs(%d) => %s\n", conn_handle,
               BleErrToStr(err));
  }
}

int ESP32Bluetooth::OnGattSvcDiscoveredThis(uint16_t conn_handle,
                                            const struct ble_gatt_error *err,
                                            const struct ble_gatt_svc *svc,
                                            void *arg) {
  return static_cast<ESP32Bluetooth *>(arg)->OnGattSvcDiscovered(conn_handle,
                                                                 err, svc);
}
int ESP32Bluetooth::OnGattSvcDiscovered(uint16_t conn_handle,
                                        const struct ble_gatt_error *error,
                                        const struct ble_gatt_svc *service) {
  switch (error->status) {
  case 0: {
    auto &device = devices_[conn_handle];
    device.services.push_back(Device::Service());
    device.services.back().svc = *service;
    // MainDebugf("Discovered service start_handle=%d end_handle=%d\n",
    //            service->start_handle, service->end_handle);
    // auto uuid = ToStr(service->uuid);
    // RunOnMain([=]() { Debugf("  UUID=%s\n", uuid.c_str()); });
    break;
  }
  case BLE_HS_EDONE: {
    auto &device = devices_[conn_handle];

    uint16_t start_handle = UINT16_MAX;
    uint16_t end_handle = 0;
    for (auto &svc : device.services) {
      start_handle = std::min(start_handle, svc.svc.start_handle);
      end_handle = std::max(end_handle, svc.svc.end_handle);
    }
    device.active_tasks++;
    // MainDebugf(
    //     "Starting characteristic discovery start_handle=%d
    //     end_handle=%d...\n", start_handle, end_handle);
    if (int err = ble_gattc_disc_all_chrs(conn_handle, start_handle, end_handle,
                                          OnGattChrDiscoveredThis, this)) {
      MainDebugf("ble_gattc_disc_all_chrs(%d) => %s\n", conn_handle,
                 BleErrToStr(err));
    }

    if (--device.active_tasks == 0) {
      auto addr_str = ToAddrStr(conn_handle);
      callbacks_.OnServicesResolved(addr_str);
    }
    break;
  }
  default:
    MainDebugf("OnGattSvcDiscovered(%s)\n", BleErrToStr(error->status));
    return error->status;
  }
  return 0;
}

int ESP32Bluetooth::OnGattChrDiscoveredThis(uint16_t conn_handle,
                                            const struct ble_gatt_error *err,
                                            const struct ble_gatt_chr *chr,
                                            void *arg) {
  return static_cast<ESP32Bluetooth *>(arg)->OnGattChrDiscovered(conn_handle,
                                                                 err, chr);
}

int ESP32Bluetooth::OnGattChrDiscovered(uint16_t conn_handle,
                                        const struct ble_gatt_error *error,
                                        const struct ble_gatt_chr *chr) {
  switch (error->status) {
  case 0: {
    auto &device = devices_[conn_handle];
    Device::Service *srv = nullptr;
    for (int i = 0; i < device.services.size(); ++i) {
      auto &curr_srv = device.services[i].svc;
      if (chr->def_handle >= curr_srv.start_handle &&
          chr->def_handle < curr_srv.end_handle) {
        srv = &device.services[i];
        break;
      }
    }
    if (srv == nullptr) {
      MainDebugf("couldn't find service for handle %d\n", chr->def_handle);
      break;
    }
    srv->characteristics.push_back(Device::Characteristic());
    auto &characteristic = srv->characteristics.back();
    characteristic.chr = *chr;
    // MainDebugf(
    //     "Discovered characteristic def_handle=%d val_handle=%d props=%d\n",
    //     chr->def_handle, chr->val_handle, chr->properties);
    // auto uuid = ToStr(chr->uuid);
    // RunOnMain([=]() { Debugf("  UUID=%s\n", uuid.c_str()); });
    // TODO: maybe also discover characteristic descriptors
    break;
  }
  case BLE_HS_EDONE: {
    auto &device = devices_[conn_handle];
    if (--device.active_tasks == 0) {
      auto addr_str = ToAddrStr(conn_handle);
      callbacks_.OnServicesResolved(addr_str);
    }
    break;
  }
  default:
    MainDebugf("OnGattChrDiscovered(%s)\n", BleErrToStr(error->status));
    return error->status;
  }
  return 0;
}

bool ESP32Bluetooth::SubscribeCharacteristic(string_view address,
                                             string_view service_uuid,
                                             string_view characteristic_uuid) {
  // Note: G1 seems to report notifications without subscribing to anything.
  auto conn_handle = ToConnHandle(address);
  Device *device = nullptr;
  if (auto it = devices_.find(conn_handle); it != devices_.end()) {
    device = &it->second;
  } else {
    MainDebugf("Device not found!\n");
    return false;
  }

  // Find the characteristic handle
  uint16_t char_val_handle = 0;
  auto chr_uuid = ToUuid(characteristic_uuid);

  for (const auto &service : device->services) {
    for (const auto &ch : service.characteristics) {
      if (ble_uuid_cmp(&ch.chr.uuid.u, &chr_uuid.u) == 0) {
        char_val_handle = ch.chr.val_handle;
        break;
      }
    }
    if (char_val_handle != 0)
      break;
  }

  if (char_val_handle == 0) {
    MainDebugf("Characteristic not found!\n");
    return false;
  }

  // The CCCD (Client Characteristic Configuration Descriptor) is typically
  // at val_handle + 1. Write 0x0001 to enable notifications.
  uint16_t cccd_handle = char_val_handle + 1;
  uint8_t notify_value[2] = {0x01, 0x00}; // Enable notifications

  MainDebugf(
      "Subscribing to characteristic %s, val_handle=%d, cccd_handle=%d\n",
      characteristic_uuid.data(), char_val_handle, cccd_handle);

  int err = ble_gattc_write_flat(conn_handle, cccd_handle, notify_value,
                                 sizeof(notify_value), nullptr, nullptr);
  if (err) {
    MainDebugf("ble_gattc_write_flat() for subscription => %s\n",
               BleErrToStr(err));
    return false;
  }

  callbacks_.OnSubscribed(address, service_uuid, characteristic_uuid);
  return true;
}

int ESP32Bluetooth::OnGattWrittenThis(uint16_t conn_handle,
                                      const struct ble_gatt_error *error,
                                      struct ble_gatt_attr *attr, void *arg) {
  return static_cast<ESP32Bluetooth *>(arg)->OnGattWritten(conn_handle, error,
                                                           attr);
}

int ESP32Bluetooth::OnGattWritten(uint16_t conn_handle,
                                  const struct ble_gatt_error *error,
                                  struct ble_gatt_attr *attr) {
  write_limiter.ReleaseTicket();
  return 0;
}

bool ESP32Bluetooth::WriteCharacteristic(string_view address,
                                         string_view service_uuid,
                                         string_view characteristic_uuid,
                                         string_view value) {
  auto conn_handle = ToConnHandle(address);
  Device *device = nullptr;
  if (auto it = devices_.find(conn_handle); it != devices_.end()) {
    device = &it->second;
  } else {
    MainDebugf("Device not found!\n");
    return false;
  }

  // // Find the characteristic handle
  uint16_t char_handle = 0;
  // auto srv_uuid = ToUuid(service_uuid);
  auto chr_uuid = ToUuid(characteristic_uuid);

  auto uuid = ToStr(chr_uuid);

  for (const auto &service : device->services) {
    for (const auto &ch : service.characteristics) {
      auto uuid = ToStr(ch.chr.uuid);
      if (ble_uuid_cmp(&ch.chr.uuid.u, &chr_uuid.u) == 0) {
        char_handle = ch.chr.val_handle;
        break;
      }
    }
    if (char_handle != 0)
      break;
  }

  if (char_handle == 0) {
    MainDebugf("Characteristic not found!\n");
    return false;
  }
  int enomems = 0;

  write_limiter.AcquireTicket();

try_again:
  int err = ble_gattc_write_flat(conn_handle, char_handle, value.data(),
                                 value.size(), OnGattWrittenThis, this);
  if (err == BLE_HS_ENOMEM && enomems < 5) {
    MainDebugf("ble_gattc_write_flat() => ENOMEM - retry %d\n", enomems);
    constexpr static int backoff[] = {10, 40, 80, 160, 200};
    vTaskDelay(backoff[enomems] / portTICK_PERIOD_MS);
    ++enomems;
    goto try_again;
  }
  if (err) {
    MainDebugf("ble_gattc_write_flat() => %s\n", BleErrToStr(err));
    return false;
  }

  return true;
}

// void ESP32Bluetooth::HandleGapEvent(esp_gap_ble_cb_event_t event,
//                                     esp_ble_gap_cb_param_t *param) {
//   // RunOnMain([event]() { Debugf("BLE event: %s\n", BleEventToStr(event));
//   }); switch (event) { case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
//     if (param->scan_param_cmpl.status == ESP_BT_STATUS_SUCCESS) {
//       esp_ble_gap_start_scanning(0); // Scan indefinitely
//     }
//     break;

//   case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
//     if (param->scan_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
//       RunOnMain([]() {
//         Debugln("Error in ESP32Bluetooth: unable to start BLE scan");
//       });
//     }
//     break;

//   case ESP_GAP_BLE_SCAN_RESULT_EVT: {
//     auto *scan_result = &param->scan_rst;
//     if (scan_result->search_evt == ESP_GAP_SEARCH_INQ_RES_EVT) {
//       string address = BdaToString(scan_result->bda);
//       string name;

//       // Extract device name from advertisement data
//       uint8_t *adv_name = nullptr;
//       uint8_t adv_name_len = 0;
//       adv_name = esp_ble_resolve_adv_data(
//           scan_result->ble_adv, ESP_BLE_AD_TYPE_NAME_CMPL, &adv_name_len);
//       if (adv_name) {
//         name = string((char *)adv_name, adv_name_len);
//       }

//       DeviceInfo &info = devices_[address];

//       // Store device info
//       memcpy(info.bda, scan_result->bda, sizeof(esp_bd_addr_t));
//       info.ble_addr_type = scan_result->ble_addr_type;
//       info.address_str = address;
//       info.name = name;

//       // Check if device was previously bonded
//       bool bonded = false;
//       int n_bonded = esp_ble_get_bond_device_num();
//       esp_ble_bond_dev_t *bond_devices =
//           (esp_ble_bond_dev_t *)malloc(sizeof(esp_ble_bond_dev_t) *
//           n_bonded);
//       esp_ble_get_bond_device_list(&n_bonded, bond_devices);
//       for (int i = 0; i < n_bonded; i++) {
//         if (memcmp(bond_devices[i].bd_addr, info.bda, 6) == 0) {
//           bonded = true;
//         }
//       }
//       free(bond_devices);

//       // Notify callback on main thread
//       callbacks_.OnDeviceFound(address, name);
//       if (bonded) {
//         callbacks_.OnBonded(address);
//       }
//     }
//     break;
//   }

//   case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
//     break;

//   case ESP_GAP_BLE_AUTH_CMPL_EVT: {
//     string address = BdaToString(param->ble_security.auth_cmpl.bd_addr);
//     if (param->ble_security.auth_cmpl.success) {
//       callbacks_.OnConnected(address);
//       callbacks_.OnBonded(address);
//     }
//     break;
//   }

//   case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT: {
//     RunOnMain([params = param->update_conn_params]() {
//       Debugf("UPDATE_CONN_PARAMS  conn_int=%d  latency=%d  max_int=%d  "
//              "min_int=%d  status=%d  timeout=%d\n",
//              params.conn_int, params.latency, params.max_int, params.min_int,
//              params.status, params.timeout);
//     });
//     break;
//   }

//   default:
//     break;
//   }
// }

// void ESP32Bluetooth::HandleGattcEvent(esp_gattc_cb_event_t event,
//                                       esp_gatt_if_t gattc_if,
//                                       esp_ble_gattc_cb_param_t *param) {
//   // RunOnMain([event]() { Debugf("GATTC event: %s\n",
//   GattcEventToStr(event));
//   // });
//   switch (event) {
//   case ESP_GATTC_REG_EVT:
//     if (param->reg.status == ESP_GATT_OK) {
//       this->gatt_if = gattc_if;
//     }
//     break;

//   case ESP_GATTC_OPEN_EVT: {
//     if (param->open.status == ESP_GATT_OK) {
//       string address = BdaToString(param->open.remote_bda);
//       auto *device = FindDeviceByAddress(address);
//       if (device) {
//         device->conn_id = param->open.conn_id;

//         // esp_gap_conn_params_t params;
//         // esp_ble_get_current_conn_params(device->bda, &params);
//         // RunOnMain([params]() {
//         //   Debugf("INITIAL PARAMS\n  latency=%d\n  interval=%d\n
//         //   timeout=%d\n",
//         //          params.latency, params.interval, params.timeout);
//         // });
//       }
//     }
//     break;
//   }

//   case ESP_GATTC_CFG_MTU_EVT: {
//     if (param->cfg_mtu.status == ESP_GATT_OK) {
//       RunOnMain(
//           [mtu = param->cfg_mtu.mtu]() { Debugf("MTU set to %d\n", mtu); });
//     }
//     break;
//   }

//   case ESP_GATTC_NOTIFY_EVT: {
//     auto *device = FindDeviceByConnId(param->notify.conn_id);
//     if (device) {
//       string address = device->address_str;
//       uint16_t handle = param->notify.handle;

//       // Look up UUID from handle
//       auto it = handle_to_uuid_.find(handle);
//       if (it != handle_to_uuid_.end()) {
//         string char_uuid = it->second;
//         string value((char *)param->notify.value, param->notify.value_len);

//         callbacks_.OnCharacteristicNotified(address, char_uuid, value);
//       }
//     }
//     break;
//   }

//   case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
//     if (param->reg_for_notify.status == ESP_GATT_OK) {
//       uint16_t handle = param->reg_for_notify.handle;
//       // Now we need to write to the CCCD to enable notifications
//       // CCCD is typically at handle+1, but we should search for it
//       auto *device = FindDeviceByConnId(gattc_if); // Need to find by gatt_if
//       if (!device) {
//         // Try to find device by scanning all devices
//         for (auto &[addr, dev] : devices_) {
//           if (gatt_if == gattc_if) {
//             device = &dev;
//             break;
//           }
//         }
//       }

//       // Get the descriptor (CCCD) for this characteristic
//       uint16_t count = 0;
//       esp_gatt_status_t status = esp_ble_gattc_get_attr_count(
//           gattc_if, *device->conn_id, ESP_GATT_DB_DESCRIPTOR, handle,
//           handle + 5, handle, &count);

//       if (status == ESP_GATT_OK && count > 0) {
//         esp_gattc_descr_elem_t *descr_elems = new
//         esp_gattc_descr_elem_t[count]; uint16_t actual_count = count;

//         status = esp_ble_gattc_get_all_descr(gattc_if, *device->conn_id,
//         handle,
//                                              descr_elems, &actual_count, 0);

//         if (status == ESP_GATT_OK) {
//           // Find CCCD (UUID 0x2902)
//           for (uint16_t i = 0; i < actual_count; i++) {
//             if (descr_elems[i].uuid.len == ESP_UUID_LEN_16 &&
//                 descr_elems[i].uuid.uuid.uuid16 == 0x2902) {
//               // Write 0x0001 to enable notifications
//               uint8_t notify_en[] = {0x01, 0x00};
//               esp_ble_gattc_write_char_descr(
//                   gattc_if, *device->conn_id, descr_elems[i].handle,
//                   sizeof(notify_en), notify_en, ESP_GATT_WRITE_TYPE_RSP,
//                   ESP_GATT_AUTH_REQ_NONE);
//               break;
//             }
//           }
//         }

//         delete[] descr_elems;
//       } else {
//         RunOnMain([]() {
//           Debugln("Error in "
//                   "ESP32Bluetooth::HandleGattcEvent(ESP_GATTC_REG_FOR_NOTIFY_"
//                   "EVT) - couldn't write to CCCD");
//         });
//       }

//       // Call the callback now
//       auto it = pending_subscriptions_.find(handle);
//       if (it != pending_subscriptions_.end()) {
//         PendingSubscription pending = it->second;
//         pending_subscriptions_.erase(it);

//         callbacks_.OnSubscribed(pending.address, pending.service_uuid,
//                                 pending.char_uuid);
//       }
//     }
//     break;
//   }

//   default:
//     break;
//   }
// }

} // namespace atmt
