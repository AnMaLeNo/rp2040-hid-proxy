// Carte de controle : peripherique HID vendeur (page 0xFF00), rapport d'entree et de
// sortie de 32 octets, sans endpoint OUT : les commandes du Mac arrivent par SET_REPORT
// sur l'endpoint de controle (transfert termine dans la trame courante, pas de polling).
#include "tusb.h"

#define CTRL_VID 0x2E8A
#define CTRL_PID 0x10C1

static tusb_desc_device_t const desc_device = {
  .bLength = sizeof(tusb_desc_device_t), .bDescriptorType = TUSB_DESC_DEVICE, .bcdUSB = 0x0200,
  .bDeviceClass = 0, .bDeviceSubClass = 0, .bDeviceProtocol = 0, .bMaxPacketSize0 = 64,
  .idVendor = CTRL_VID, .idProduct = CTRL_PID, .bcdDevice = 0x0100,
  .iManufacturer = 1, .iProduct = 2, .iSerialNumber = 3, .bNumConfigurations = 1 };

uint8_t const* tud_descriptor_device_cb(void) { return (uint8_t const*) &desc_device; }

static uint8_t const desc_hid_report[] = { TUD_HID_REPORT_DESC_GENERIC_INOUT(32) };
uint8_t const* tud_hid_descriptor_report_cb(uint8_t itf) { (void) itf; return desc_hid_report; }

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)
static uint8_t const desc_configuration[] = {
  TUD_CONFIG_DESCRIPTOR(1, 1, 0, CONFIG_TOTAL_LEN, 0, 100),
  // interface 0, pas de chaine, protocole none, rapport, EP IN 0x81, taille 32, intervalle 1 ms
  TUD_HID_DESCRIPTOR(0, 0, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_report), 0x81, 32, 1) };
uint8_t const* tud_descriptor_configuration_cb(uint8_t index) { (void) index; return desc_configuration; }

static char const* string_desc[] = { (const char[]) { 0x09, 0x04 }, "proxy-souris", "proxy-ctrl", "CTRL0001" };
static uint16_t desc_str[32];
uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void) langid;
  uint8_t len;
  if (index == 0) { memcpy(&desc_str[1], string_desc[0], 2); len = 1; }
  else {
    if (index >= sizeof(string_desc) / sizeof(string_desc[0])) return NULL;
    const char* s = string_desc[index];
    len = (uint8_t) strlen(s); if (len > 31) len = 31;
    for (uint8_t i = 0; i < len; i++) desc_str[1 + i] = s[i];
  }
  desc_str[0] = (uint16_t) ((TUSB_DESC_STRING << 8) | (2 * len + 2));
  return desc_str;
}
