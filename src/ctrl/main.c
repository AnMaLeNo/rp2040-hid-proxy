// Carte de controle (seconde RP2040-Zero), version HID vendeur sans port serie.
// Cablage vers le proxy : GND <-> GND, GP13 -> GP2 du proxy (ligne clic / BOOTSEL),
// facultatif GP14 -> patte RUN du bouton RESET, GP15 -> patte QSPI_SS du bouton BOOT.
// Sorties en collecteur ouvert : entree flottante au repos, sortie a 0 pour "appuyer".
//
// Commandes : un rapport de sortie HID (SET_REPORT, endpoint de controle) dont le premier
// octet est la commande, applique dans le callback, donc des la fin du transfert :
//   P = ligne a 0 (clic presse)     R = ligne a 1 (relache)     k = impulsion 60 ms
//   g = ligne a 0 pendant 4 s (BOOTSEL du proxy ; variante click : clic 2 s puis BOOTSEL)
//   r = RUN a 0 pendant 100 ms      b = BOOT + RUN (BOOTSEL par les boutons)
//   B = cette carte repasse en BOOTSEL      ? = rien (etat lu par GET_REPORT)
// Rapport d'entree (GET_REPORT) : "REQ=x RUN=x BOOT=x".
#include <string.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "tusb.h"

#define PIN_REQ   13
#define PIN_RUN   14
#define PIN_BOOT  15

static void press(uint pin)   { gpio_put(pin, 0); gpio_set_dir(pin, GPIO_OUT); }
static void release(uint pin) { gpio_set_dir(pin, GPIO_IN); }

static volatile uint32_t req_release_at, run_release_at, boot_release_at, bootsel_at;
static volatile uint32_t last_cmd_us;
static volatile uint8_t  last_cmd;

static inline uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }

void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t type,
                           uint8_t const* buf, uint16_t len) {
  (void) itf; (void) report_id; (void) type;
  if (len == 0) return;
  uint8_t c = buf[0];
  last_cmd = c; last_cmd_us = time_us_32();
  uint32_t t = now_ms();
  switch (c) {
    case 'P': press(PIN_REQ); req_release_at = 0; break;
    case 'R': release(PIN_REQ); req_release_at = 0; break;
    case 'k': press(PIN_REQ); req_release_at = t + 60; break;
    case 'g': press(PIN_REQ); req_release_at = t + 4000; break;
    case 'r': press(PIN_RUN); run_release_at = t + 100; break;
    case 'b': press(PIN_BOOT); press(PIN_RUN); run_release_at = t + 100; boot_release_at = t + 300; break;
    case 'B': bootsel_at = t + 50; break;
    default: break;
  }
}

uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t type,
                               uint8_t* buf, uint16_t reqlen) {
  (void) itf; (void) report_id; (void) type;
  char s[32];
  int n = snprintf(s, sizeof s, "REQ=%d RUN=%d BOOT=%d c=%c", gpio_get(PIN_REQ), gpio_get(PIN_RUN),
                   gpio_get(PIN_BOOT), last_cmd ? last_cmd : '-');
  if (n > reqlen) n = reqlen;
  memcpy(buf, s, (size_t) n);
  return (uint16_t) n;
}

int main(void) {
  for (uint p = PIN_REQ; p <= PIN_BOOT; p++) { gpio_init(p); gpio_set_dir(p, GPIO_IN); gpio_disable_pulls(p); gpio_put(p, 0); }
  tusb_init();
  while (true) {
    tud_task();
    uint32_t t = now_ms();
    if (req_release_at && (int32_t) (t - req_release_at) >= 0) { release(PIN_REQ); req_release_at = 0; }
    if (run_release_at && (int32_t) (t - run_release_at) >= 0) { release(PIN_RUN); run_release_at = 0; }
    if (boot_release_at && (int32_t) (t - boot_release_at) >= 0) { release(PIN_BOOT); boot_release_at = 0; }
    if (bootsel_at && (int32_t) (t - bootsel_at) >= 0) reset_usb_boot(0, 0);
  }
}
