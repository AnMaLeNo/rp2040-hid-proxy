// Proxy USB transparent pour souris : RP2040-Zero
//   cote PC     : USB-C natif (rhport 0, TinyUSB device), clone de la souris
//   cote souris : PIO-USB (rhport 1, TinyUSB host), D+ GP0, D- GP1
// Les descripteurs (device, configuration, chaines, rapports HID) sont lus sur la
// souris au branchement, puis presentes tels quels au PC. Les rapports IN sont
// relayes des leur reception, SET/GET_REPORT et SET_PROTOCOL sont transmis.
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include <stdarg.h>
#include <stdio.h>
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pio_usb.h"
#include "pio_usb_ll.h"
#include "tusb.h"

// LED RGB WS2812 de la RP2040-Zero (GP16), code d'etat :
//   blanc faible = demarre, bleu = hub vu, jaune = interface HID vue,
//   vert = descripteurs copies, cyan = PC connecte, rouge = souris debranchee
#define LED_BOOT   0x080808u
#define LED_HUB    0x000020u
#define LED_HID    0x201000u
#define LED_READY  0x002000u
#define LED_STACK  0x202020u   // blanc fort = pile device demarree, PC pas encore connecte
#define LED_PC     0x002020u
#define LED_LOST   0x200000u
static volatile uint32_t led_color = LED_BOOT;

//--------------------------------------------------------------------
// Mode diagnostic (DEBUG_CDC) : la carte se presente au PC comme port serie
// et y recopie le journal de la pile hote (CFG_TUSB_DEBUG) et les evenements
// du proxy. La souris n'est pas exposee au PC dans ce mode.
// Mode developpement (DEV_CDC) : souris clonee + port serie en plus (non transparent).
//--------------------------------------------------------------------
#if defined(DEBUG_CDC) || defined(DEV_CDC)
#define DBG_RING 8192
static uint8_t dbg_ring[DBG_RING];
static volatile uint32_t dbg_head, dbg_tail;
static spin_lock_t* dbg_lock;

int dbg_printf(const char* fmt, ...) {
  char tmp[256];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
  va_end(ap);
  if (n <= 0) return 0;
  if (n > (int) sizeof(tmp)) n = sizeof(tmp);
  uint32_t irq = spin_lock_blocking(dbg_lock);
  for (int i = 0; i < n; i++) {
    uint32_t next = (dbg_head + 1) % DBG_RING;
    if (next == dbg_tail) break;   // plein : on perd la fin
    dbg_ring[dbg_head] = (uint8_t) tmp[i];
    dbg_head = next;
  }
  spin_unlock(dbg_lock, irq);
  return n;
}

static void inject_set(bool pressed);

static void dbg_drain(void) {
  if (!tud_mounted()) return;
  // Commandes recues sur le port serie :
  //   'B' = retour en mode BOOTSEL (reflash sans debrancher)
  //   'P' / 'R' = clic gauche injecte presse / relache
  while (tud_cdc_available()) {
    uint8_t ch;
    if (tud_cdc_read(&ch, 1) != 1) break;
    if (ch == 'B') reset_usb_boot(0, 0);
    else if (ch == 'P') { inject_set(true); dbg_printf("[proxy] clic injecte: presse\r\n"); }
    else if (ch == 'R') { inject_set(false); dbg_printf("[proxy] clic injecte: relache\r\n"); }
  }
  uint8_t buf[64];
  uint32_t n = 0;
  uint32_t irq = spin_lock_blocking(dbg_lock);
  while (dbg_tail != dbg_head && n < sizeof(buf)) {
    buf[n++] = dbg_ring[dbg_tail];
    dbg_tail = (dbg_tail + 1) % DBG_RING;
  }
  spin_unlock(dbg_lock, irq);
  if (n) { tud_cdc_write(buf, n); tud_cdc_write_flush(); }
}
#define DBG(...) dbg_printf("[proxy] " __VA_ARGS__)

static tusb_desc_device_t const dbg_dev = {
  .bLength = sizeof(tusb_desc_device_t), .bDescriptorType = TUSB_DESC_DEVICE, .bcdUSB = 0x0200,
  .bDeviceClass = TUSB_CLASS_MISC, .bDeviceSubClass = MISC_SUBCLASS_COMMON, .bDeviceProtocol = MISC_PROTOCOL_IAD,
  .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE, .idVendor = 0x2E8A, .idProduct = 0x000A, .bcdDevice = 0x0100,
  .iManufacturer = 1, .iProduct = 2, .iSerialNumber = 3, .bNumConfigurations = 1
};
static uint8_t const dbg_cfg[] = {
  TUD_CONFIG_DESCRIPTOR(1, 2, 0, TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN, 0, 100),
  TUD_CDC_DESCRIPTOR(0, 4, 0x81, 8, 0x02, 0x82, 64)
};
static char const* dbg_str[] = { (char const[]) { 0x09, 0x04 }, "proxy-souris", "proxy-souris debug", "0001", "journal" };
static uint16_t dbg_str_buf[32];

static uint16_t const* dbg_string(uint8_t index) {
  if (index >= TU_ARRAY_SIZE(dbg_str)) return NULL;
  uint8_t len;
  if (index == 0) { memcpy(&dbg_str_buf[1], dbg_str[0], 2); len = 1; }
  else {
    len = (uint8_t) strlen(dbg_str[index]);
    if (len > 31) len = 31;
    for (uint8_t i = 0; i < len; i++) dbg_str_buf[1 + i] = (uint16_t) dbg_str[index][i];
  }
  dbg_str_buf[0] = (uint16_t) ((TUSB_DESC_STRING << 8) | (2 * len + 2));
  return dbg_str_buf;
}
#else
#define DBG(...) ((void) 0)
#endif

static void ws2812_put(uint32_t rgb) {
  uint32_t grb = ((rgb & 0xFF00u) << 8) | ((rgb >> 16) << 8) | (rgb & 0xFFu);   // G, R, B
  grb <<= 8;
  uint32_t irq = save_and_disable_interrupts();
  for (int i = 0; i < 24; i++) {
    bool one = grb & 0x80000000u;
    gpio_put(PICO_DEFAULT_WS2812_PIN, 1);
    busy_wait_at_least_cycles(one ? 96 : 48);    // 0,8 us / 0,4 us a 120 MHz
    gpio_put(PICO_DEFAULT_WS2812_PIN, 0);
    busy_wait_at_least_cycles(one ? 54 : 102);   // 0,45 us / 0,85 us
    grb <<= 1;
  }
  restore_interrupts(irq);
  busy_wait_us_32(80);
}

// Bouton de test : GP3 relie a GND = clic injecte sur la souris
#define PIN_INJECT        3
// Ligne de reflash : GP2 tire a GND (par la carte de controle) = retour en BOOTSEL.
// Verifiee tout au debut de main() et a chaque tour de boucle, dans toutes les
// variantes, donc aussi la transparente. Le chien de garde (3 s) ramene au test
// de demarrage si le firmware se fige.
#define PIN_BOOTSEL_REQ   2
#if defined(CTRL_CLICK)
// Variante clic par fil : GP2 a 0 = clic gauche injecte des le front (la ligne est
// lue a chaque tour de boucle du coeur 0), GP2 a 1 = relache. Si la ligne reste a 0
// plus de 2 s, retour en BOOTSEL. Le clic ne paie donc aucun delai pour le BOOT.
#define CTRL_BOOT_HOLD_MS 2000
// Le clic de la carte de controle n'est relaye que si l'un des deux boutons « d'inclinaison » de la molette est maintenu (decision de
// l'utilisateur). La G502 les presente au PC comme un defilement horizontal (AC Pan, +1 a droite, -1 a gauche) qu'elle repete toutes les
// ~100 ms (112 ms au plus, mesure Raw Input sous Windows) tant que la molette est inclinee : « maintenu » = un rapport avec un panoramique
// non nul recu depuis moins de TILT_HOLD_US. Les clics de la vraie souris ne sont jamais filtres.
#define TILT_HOLD_US 200000
static volatile uint32_t tilt_last_us;   // ecrit par le coeur 1 (reception des rapports), lu par le coeur 0
static volatile bool     tilt_seen;
static inline bool tilt_held(void) {
  return tilt_seen && (uint32_t) (time_us_32() - tilt_last_us) < TILT_HOLD_US;
}
static void inject_set(bool pressed);
static inline void check_bootsel_req(void) {
  static bool low;
  static bool clicking;   // un clic a effectivement ete injecte pour ce front descendant
  static uint32_t low_since;
  bool now_low = !gpio_get(PIN_BOOTSEL_REQ);
  if (now_low != low) {
    low = now_low;
    low_since = to_ms_since_boot(get_absolute_time());
    if (now_low) {
      // decision au front : si la molette n'est pas inclinee a cet instant, le clic est refuse en entier
      clicking = tilt_held();
      DBG("GP2 front : appui, molette inclinee=%u -> %s\r\n", clicking, clicking ? "clic injecte" : "clic refuse");
      if (clicking) inject_set(true);
    } else {
      DBG("GP2 front : relache\r\n");
      if (clicking) inject_set(false);
      clicking = false;
    }
  } else if (low && to_ms_since_boot(get_absolute_time()) - low_since > CTRL_BOOT_HOLD_MS) {
    DBG("GP2 a 0 depuis 2 s : retour en BOOTSEL\r\n");
    reset_usb_boot(0, 0);
  }
}
#else
static inline void check_bootsel_req(void) {
  if (!gpio_get(PIN_BOOTSEL_REQ)) {
    DBG("GP2 a 0 : retour en BOOTSEL\r\n");
    reset_usb_boot(0, 0);
  }
}
#endif

#define MAX_HID_ITF       CFG_TUD_HID
#define MAX_STRINGS       8
#define CFG_BUF_LEN       512
#define REPORT_DESC_LEN   256
#define REPORT_LEN        CFG_TUD_HID_EP_BUFSIZE

typedef struct {
  bool     present;
  uint8_t  itf_num;
  uint8_t  protocol;
  uint16_t desc_len;
  uint8_t  desc[REPORT_DESC_LEN];
  // Position du bouton 1 (page Button, usage 1) dans le rapport d'entree,
  // trouvee en parcourant le descripteur de rapport
  bool     btn_found;
  uint8_t  btn_report_id;   // 0 = pas d'identifiant de rapport
  uint8_t  btn_byte;        // index dans le rapport tel que recu (ID compris)
  uint8_t  btn_mask;
  uint8_t  btn_report_len; // longueur du rapport d'entree qui contient le bouton (ID compris)
  // Position du champ « panoramique » (page Consumer, usage AC Pan 0x0238) : c'est l'inclinaison
  // de la molette de la G502, repetee par la souris tant qu'elle est inclinee
  bool     pan_found;
  uint8_t  pan_report_id;
  uint8_t  pan_byte;        // index dans le rapport tel que recu (ID compris)
  uint8_t  pan_size;        // 1 ou 2 octets (champ entier signe, aligne sur un octet)
  uint16_t last_len;
  uint8_t  last[REPORT_LEN];
  volatile bool pending;
  uint16_t pending_len;
  uint8_t  pending_buf[REPORT_LEN];
  // Horodatage (time_us_32) : reception depuis la souris, rapport en vol vers le PC, rapport en attente
  uint32_t t_rx, t_inflight, t_pending;
} hid_itf_t;

// Latence ajoutee par le proxy : de la reception du rapport (coeur 1) a la fin de
// la lecture par le PC (tud_hid_report_complete_cb). Histogramme par pas de 50 us.
#define LAT_BUCKET_US 50
#define LAT_BUCKETS   64
static struct {
  uint32_t n, sum, min, max, over;
  uint32_t hist[LAT_BUCKETS];
} lat;

static void lat_add(uint32_t dt) {
  lat.n++;
  lat.sum += dt;
  if (lat.n == 1 || dt < lat.min) lat.min = dt;
  if (dt > lat.max) lat.max = dt;
  uint32_t b = dt / LAT_BUCKET_US;
  if (b < LAT_BUCKETS) lat.hist[b]++; else lat.over++;
}

// Asservissement de phase : le SOF hote (vers la souris) est cale sur le SOF du PC
// avec une phase cible. Le PC lit notre endpoint a un instant fixe de sa trame ; en
// placant la reception du rapport souris juste avant, la latence ajoutee tombe vers
// la cible au lieu de glisser de 0 a 1 ms.
#define PHASE_LAT_TARGET_US 200
static volatile uint32_t pc_sof_us;         // instant du dernier SOF recu du PC (coeur 0)
static volatile bool     pc_sof_seen;
static volatile int32_t  phase_target = 500; // phase cible SOF hote - SOF PC, en us, 0..999
static volatile int32_t  phase_now;          // phase mesuree
static volatile int32_t  sof_period = 1000;  // periode appliquee au timer SOF hote
static volatile uint32_t phase_lat_avg;      // derniere latence moyenne utilisee pour regler la cible

void tud_sof_cb(uint32_t frame_count) {
  (void) frame_count;
  pc_sof_us = time_us_32();
  pc_sof_seen = true;
}

// Appele par le coeur 1 a chaque tour : une correction de +/-1 us par trame
static void phase_lock_step(void) {
  static uint32_t last_host_sof;
  uint32_t h = pio_usb_host_sof_us;
  if (h == last_host_sof || !pc_sof_seen) return;
  last_host_sof = h;
  int32_t ph = (int32_t) ((h - pc_sof_us) % 1000u);
  phase_now = ph;
  int32_t err = ph - phase_target;
  if (err > 500) err -= 1000;
  if (err < -500) err += 1000;
  int32_t period = 1000;
  if (err > 3) period = 999;        // SOF hote trop tard : raccourcir
  else if (err < -3) period = 1001; // trop tot : allonger
  if (period != sof_period) { sof_period = period; pio_usb_host_set_sof_period_us(period); }
}

// Regle la phase cible a partir de la latence moyenne mesuree sur la derniere seconde.
// Reception plus tardive (phase plus grande) = latence plus faible, jusqu'au moment ou
// le rapport manque le polling du PC (latence ~1 ms) : on corrige doucement.
static void phase_target_update(uint32_t lat_avg) {
  phase_lat_avg = lat_avg;
  int32_t delta = ((int32_t) lat_avg - PHASE_LAT_TARGET_US) / 2;
  if (delta > 150) delta = 150;
  if (delta < -150) delta = -150;
  int32_t t = phase_target + delta;
  while (t < 0) t += 1000;
  while (t >= 1000) t -= 1000;
  phase_target = t;
}

static uint32_t lat_percentile(uint32_t pct) {
  uint32_t target = (lat.n * pct + 99) / 100, acc = 0;
  for (uint32_t b = 0; b < LAT_BUCKETS; b++) {
    acc += lat.hist[b];
    if (acc >= target) return (b + 1) * LAT_BUCKET_US;
  }
  return LAT_BUCKETS * LAT_BUCKET_US;
}

static struct {
  volatile bool ready;
  uint8_t  daddr;
  tusb_desc_device_t dev;
  uint16_t cfg_len;
  uint8_t  cfg[CFG_BUF_LEN];
  uint8_t  n_str;
  struct { uint8_t index; uint8_t data[256]; } str[MAX_STRINGS];
  uint8_t  n_itf;
  hid_itf_t itf[MAX_HID_ITF];        // index = idx cote hote
  int8_t   itf_to_inst[MAX_HID_ITF]; // idx hote -> instance device
  int8_t   inst_to_itf[MAX_HID_ITF]; // instance device -> idx hote
  int8_t   mouse_itf;                // idx hote de l'interface protocole souris
  uint32_t dropped;
  bool     ep0_patched;
} g;

// Requetes de controle venant du PC, executees par le coeur 1 (pile hote)
enum { REQ_FREE = 0, REQ_PENDING, REQ_SENT, REQ_DONE, REQ_FAIL };
enum { REQ_SET_REPORT = 1, REQ_GET_REPORT, REQ_SET_PROTOCOL };
typedef struct {
  volatile uint8_t state;
  uint8_t  type;
  uint8_t  idx;
  uint8_t  report_id;
  uint8_t  report_type;
  uint16_t len;
  uint8_t  data[REPORT_LEN];
} host_req_t;
#define N_REQ 4
static host_req_t reqs[N_REQ];
static volatile bool inject_active;

static host_req_t* req_alloc(void) {
  for (int i = 0; i < N_REQ; i++) {
    if (reqs[i].state == REQ_FREE) return &reqs[i];
  }
  return NULL;
}

//--------------------------------------------------------------------
// Coeur 1 : hote PIO-USB vers la souris
//--------------------------------------------------------------------
static volatile uint8_t pending_mount;

// Parcours minimal du descripteur de rapport : cherche le premier champ Input
// de la page Button dont l'usage minimum est 1, et calcule sa position.
static void find_button1(hid_itf_t* it) {
  uint16_t usage_page = 0, usage_min = 0, report_size = 0, report_count = 0;
  uint8_t  report_id = 0;
  uint32_t bit_pos = 0;         // position dans le rapport d'entree courant
  uint8_t  cur_id = 0;
  uint8_t  btn_id = 0;
  uint16_t usages[8];           // items Usage (locaux) du prochain champ Input, avec leur page
  uint16_t usage_pages[8];
  uint8_t  n_usages = 0;
  it->btn_found = false;
  it->btn_report_len = 0;
  it->pan_found = false;
  uint16_t i = 0;
  while (i < it->desc_len) {
    uint8_t item = it->desc[i++];
    uint8_t size = item & 3;
    if (size == 3) size = 4;
    if (i + size > it->desc_len) break;
    uint32_t data = 0;
    for (uint8_t k = 0; k < size; k++) data |= (uint32_t) it->desc[i + k] << (8 * k);
    i += size;
    switch (item & 0xFC) {
      case 0x04: usage_page = (uint16_t) data; break;          // Usage Page
      case 0x08:                                               // Usage (4 octets : la page est dans les 16 bits hauts)
        if (n_usages < 8) {
          usages[n_usages] = (uint16_t) data;
          usage_pages[n_usages] = size == 4 ? (uint16_t) (data >> 16) : usage_page;
          n_usages++;
        }
        break;
      case 0x18: usage_min = (uint16_t) data; break;           // Usage Minimum
      case 0x74: report_size = (uint16_t) data; break;         // Report Size
      case 0x94: report_count = (uint16_t) data; break;        // Report Count
      case 0x84:                                               // Report ID
        report_id = (uint8_t) data;
        if (report_id != cur_id) { cur_id = report_id; bit_pos = 0; }
        break;
      case 0x80:                                               // Input
        if (!it->btn_found && usage_page == 0x09 && usage_min == 1 && !(data & 1)) {
          uint32_t pos = bit_pos + (report_id ? 8 : 0);
          it->btn_report_id = report_id;
          it->btn_byte = (uint8_t) (pos / 8);
          it->btn_mask = (uint8_t) (1u << (pos % 8));
          it->btn_found = true;
          btn_id = cur_id;
        }
        if (!it->pan_found && !(data & 1) && (report_size == 8 || report_size == 16)) {
          for (uint8_t u = 0; u < n_usages && u < report_count; u++) {
            if (usage_pages[u] != 0x0C || usages[u] != 0x0238) continue;
            uint32_t pos = bit_pos + (uint32_t) u * report_size + (report_id ? 8 : 0);
            it->pan_report_id = report_id;
            it->pan_byte = (uint8_t) (pos / 8);
            it->pan_size = (uint8_t) (report_size / 8);
            it->pan_found = true;
          }
        }
        bit_pos += (uint32_t) report_size * report_count;
        if (it->btn_found && cur_id == btn_id)
          it->btn_report_len = (uint8_t) ((bit_pos + 7) / 8 + (btn_id ? 1 : 0));
        break;
      case 0xA0: case 0xC0: break;                             // Collection, End
      default: break;
    }
    // Usage Minimum est un item local, remis a zero apres chaque main item
    if ((item & 0xFC) == 0x80 || (item & 0xFC) == 0x90 || (item & 0xFC) == 0xB0) usage_min = 0;
    // Les Usage sont des items locaux : remis a zero apres chaque item principal, collections comprises
    if ((item & 0xFC) == 0x80 || (item & 0xFC) == 0x90 || (item & 0xFC) == 0xB0 || (item & 0xFC) == 0xA0) n_usages = 0;
  }
}

static bool inject_bit(hid_itf_t const* it, uint8_t* rep, uint16_t len, bool set) {
  if (!it->btn_found || len <= it->btn_byte) return false;
  if (it->btn_report_id && rep[0] != it->btn_report_id) return false;
  if (set) rep[it->btn_byte] |= it->btn_mask;
  else rep[it->btn_byte] &= (uint8_t) ~it->btn_mask;
  return true;
}

static void add_string(uint8_t index, uint16_t langid) {
  if (index == 0 || g.n_str >= MAX_STRINGS) return;
  for (int i = 0; i < g.n_str; i++) if (g.str[i].index == index) return;
  uint8_t* buf = g.str[g.n_str].data;
  if (tuh_descriptor_get_string_sync(g.daddr, index, langid, buf, 256) != XFER_RESULT_SUCCESS) return;
  g.str[g.n_str].index = index;
  g.n_str++;
}

static void build_itf_map(void) {
  for (int i = 0; i < MAX_HID_ITF; i++) { g.itf_to_inst[i] = -1; g.inst_to_itf[i] = -1; }
  uint8_t const* p = g.cfg + g.cfg[0];
  uint8_t const* end = g.cfg + g.cfg_len;
  uint8_t inst = 0;
  while (p + 1 < end && p[0] >= 2 && p + p[0] <= end) {
    if (p[1] == TUSB_DESC_INTERFACE) {
      tusb_desc_interface_t const* d = (tusb_desc_interface_t const*) p;
      if (d->bInterfaceClass == TUSB_CLASS_HID && d->bAlternateSetting == 0 && inst < MAX_HID_ITF) {
        for (int idx = 0; idx < MAX_HID_ITF; idx++) {
          if (g.itf[idx].present && g.itf[idx].itf_num == d->bInterfaceNumber) {
            g.itf_to_inst[idx] = (int8_t) inst;
            g.inst_to_itf[inst] = (int8_t) idx;
          }
        }
        inst++;
      }
    }
    p += p[0];
  }
}

static void copy_descriptors(uint8_t daddr) {
  g.daddr = daddr;
  g.n_str = 0;
  if (tuh_descriptor_get_device_sync(daddr, &g.dev, sizeof(g.dev)) != XFER_RESULT_SUCCESS) return;
  // TinyUSB device utilise une taille EP0 fixe (CFG_TUD_ENDPOINT0_SIZE) ; si la
  // souris en declare une autre, on l'aligne pour que l'enumeration reste coherente
  if (g.dev.bMaxPacketSize0 != CFG_TUD_ENDPOINT0_SIZE) {
    g.ep0_patched = true;
    g.dev.bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE;
  }
  if (tuh_descriptor_get_configuration_sync(daddr, 0, g.cfg, CFG_BUF_LEN) != XFER_RESULT_SUCCESS) return;
  g.cfg_len = tu_le16toh(((tusb_desc_configuration_t*) g.cfg)->wTotalLength);
  if (g.cfg_len > CFG_BUF_LEN) g.cfg_len = CFG_BUF_LEN;

  // chaine 0 = liste des langues, puis toutes les chaines referencees
  uint16_t langid = 0x0409;
  if (tuh_descriptor_get_string_sync(daddr, 0, 0, g.str[0].data, 256) == XFER_RESULT_SUCCESS) {
    g.str[0].index = 0;
    g.n_str = 1;
    if (g.str[0].data[0] >= 4) langid = tu_le16toh(*(uint16_t const*) &g.str[0].data[2]);
  }
  add_string(g.dev.iManufacturer, langid);
  add_string(g.dev.iProduct, langid);
  add_string(g.dev.iSerialNumber, langid);
  add_string(((tusb_desc_configuration_t*) g.cfg)->iConfiguration, langid);
  uint8_t const* p = g.cfg + g.cfg[0];
  uint8_t const* end = g.cfg + g.cfg_len;
  while (p + 1 < end && p[0] >= 2 && p + p[0] <= end) {
    if (p[1] == TUSB_DESC_INTERFACE) add_string(((tusb_desc_interface_t const*) p)->iInterface, langid);
    p += p[0];
  }
  build_itf_map();
  g.ready = true;
  led_color = LED_READY;
  DBG("descripteurs copies: dev %04x:%04x ep0=%u(patch=%u) cfg_len=%u n_str=%u mouse_itf=%d\r\n",
      g.dev.idVendor, g.dev.idProduct, g.dev.bMaxPacketSize0, g.ep0_patched, g.cfg_len, g.n_str, g.mouse_itf);
}

static void process_requests(void) {
  for (int i = 0; i < N_REQ; i++) {
    host_req_t* r = &reqs[i];
    if (r->state != REQ_PENDING) continue;
    bool ok = false;
    switch (r->type) {
      case REQ_SET_REPORT:
        ok = tuh_hid_set_report(g.daddr, r->idx, r->report_id, r->report_type, r->data, r->len);
        break;
      case REQ_GET_REPORT:
        ok = tuh_hid_get_report(g.daddr, r->idx, r->report_id, r->report_type, r->data, r->len);
        break;
      case REQ_SET_PROTOCOL:
        ok = tuh_hid_set_protocol(g.daddr, r->idx, r->data[0]);
        break;
    }
    r->state = ok ? REQ_SENT : REQ_FAIL;
    if (!ok) DBG("requete vers la souris refusee (type %u itf %u)\r\n", r->type, r->idx);
  }
}

static host_req_t* find_sent(uint8_t idx, uint8_t type) {
  for (int i = 0; i < N_REQ; i++) {
    if (reqs[i].state == REQ_SENT && reqs[i].idx == idx && reqs[i].type == type) return &reqs[i];
  }
  return NULL;
}

void tuh_hid_set_report_complete_cb(uint8_t daddr, uint8_t idx, uint8_t report_id, uint8_t report_type, uint16_t len) {
  (void) daddr; (void) report_id; (void) report_type; (void) len;
  host_req_t* r = find_sent(idx, REQ_SET_REPORT);
  if (r) r->state = REQ_FREE;
}

void tuh_hid_get_report_complete_cb(uint8_t daddr, uint8_t idx, uint8_t report_id, uint8_t report_type, uint16_t len) {
  (void) daddr; (void) report_id; (void) report_type;
  host_req_t* r = find_sent(idx, REQ_GET_REPORT);
  if (r) { r->len = len; r->state = REQ_DONE; }
}

void tuh_hid_set_protocol_complete_cb(uint8_t daddr, uint8_t idx, uint8_t protocol) {
  (void) daddr; (void) protocol;
  host_req_t* r = find_sent(idx, REQ_SET_PROTOCOL);
  if (r) r->state = REQ_FREE;
}

// Appele aussi pour un hub : on ne copie que l'appareil qui a des interfaces HID
void tuh_mount_cb(uint8_t daddr) {
  uint16_t vid = 0, pid = 0;
  tuh_vid_pid_get(daddr, &vid, &pid);
  DBG("mount daddr=%u vid=%04x pid=%04x n_itf=%u ready=%u\r\n", daddr, vid, pid, g.n_itf, g.ready);
  if (g.n_itf > 0 && g.itf[0].present && !g.ready) pending_mount = daddr;
  else if (!g.ready) led_color = LED_HUB;
}

void tuh_umount_cb(uint8_t daddr) {
  DBG("umount daddr=%u\r\n", daddr);
  if (daddr != g.daddr) return;
  g.ready = false;
  led_color = LED_LOST;
  g.n_itf = 0;
  g.mouse_itf = -1;
  for (int i = 0; i < MAX_HID_ITF; i++) g.itf[i].present = false;
}

void tuh_hid_mount_cb(uint8_t daddr, uint8_t idx, uint8_t const* desc_report, uint16_t desc_len) {
  if (idx >= MAX_HID_ITF) return;
  hid_itf_t* it = &g.itf[idx];
  tuh_itf_info_t info;
  if (!tuh_hid_itf_get_info(daddr, idx, &info)) return;
  it->itf_num = info.desc.bInterfaceNumber;
  it->protocol = tuh_hid_interface_protocol(daddr, idx);
  it->desc_len = desc_len > REPORT_DESC_LEN ? REPORT_DESC_LEN : desc_len;
  memcpy(it->desc, desc_report, it->desc_len);
  find_button1(it);
  it->last_len = 0;
  it->pending = false;
  it->present = true;
  g.daddr = daddr;
  led_color = LED_HID;
  DBG("hid mount daddr=%u idx=%u itf=%u proto=%u desc_len=%u btn=%u/%u/%02x len=%u pan=%u/%u/%u\r\n", daddr, idx, it->itf_num,
      it->protocol, desc_len, it->btn_found, it->btn_byte, it->btn_mask, it->btn_report_len, it->pan_found, it->pan_byte, it->pan_size);
  if (it->protocol == HID_ITF_PROTOCOL_MOUSE) g.mouse_itf = (int8_t) idx;
  if (idx + 1 > g.n_itf) g.n_itf = idx + 1;
  tuh_hid_receive_report(daddr, idx);
}

void tuh_hid_umount_cb(uint8_t daddr, uint8_t idx) {
  (void) daddr;
  if (idx < MAX_HID_ITF) g.itf[idx].present = false;
}

// Envoi vers le PC ; si l'endpoint est encore occupe, le rapport attend
// tud_hid_report_complete_cb au lieu d'etre perdu.
static void send_to_pc(uint8_t idx, uint8_t const* report, uint16_t len) {
  int8_t inst = g.itf_to_inst[idx];
  if (inst < 0 || !tud_mounted()) return;
  if (len > REPORT_LEN) len = REPORT_LEN;
  hid_itf_t* it = &g.itf[idx];
  if (tud_hid_n_report((uint8_t) inst, 0, report, len)) {
    it->t_inflight = it->t_rx;
  } else {
    if (it->pending) g.dropped++;
    memcpy(it->pending_buf, report, len);
    it->pending_len = len;
    it->t_pending = it->t_rx;
    it->pending = true;
  }
}

void tuh_hid_report_received_cb(uint8_t daddr, uint8_t idx, uint8_t const* report, uint16_t len) {
  if (idx >= MAX_HID_ITF) return;
  hid_itf_t* it = &g.itf[idx];
  it->t_rx = time_us_32();
  if (len > REPORT_LEN) len = REPORT_LEN;
  memcpy(it->last, report, len);
  it->last_len = len;
  tuh_hid_receive_report(daddr, idx);

#if defined(CTRL_CLICK)
  // Inclinaison de la molette : panoramique non nul dans ce rapport (l'ID de rapport, s'il y en a un, doit correspondre)
  if ((int8_t) idx == g.mouse_itf && it->pan_found && len >= (uint16_t) (it->pan_byte + it->pan_size) &&
      (!it->pan_report_id || it->last[0] == it->pan_report_id)) {
    bool tilt = it->last[it->pan_byte] != 0 || (it->pan_size == 2 && it->last[it->pan_byte + 1] != 0);
    if (tilt) {
      tilt_last_us = it->t_rx;
      tilt_seen = true;
    }
  }
#endif

  uint8_t mod[REPORT_LEN];
  if (inject_active && (int8_t) idx == g.mouse_itf) {
    memcpy(mod, report, len);
    if (inject_bit(it, mod, len, true)) report = mod;
  }
  send_to_pc(idx, report, len);
}

static void core1_main(void) {
  sleep_ms(10);
  pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
#ifdef SWAP_DPDM
  // D- sur GP0, D+ sur GP1. pin_dm = pin_dp - 1 dans Pico-PIO-USB, donc pin_dp doit valoir 1
  pio_cfg.pin_dp = 1;
  pio_cfg.pinout = PIO_USB_PINOUT_DMDP;
#endif
  tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);
  // La souris doit parler le protocole « report » (rapports de 8 octets decrits par son descripteur), pas le protocole « boot » (4 octets) :
  // par defaut l'hote TinyUSB met les interfaces boot en protocole boot a l'enumeration (hid_host.c : _hidh_default_protocol = BOOT). macOS envoie
  // ensuite SET_PROTOCOL(report), que le proxy relaie a la souris ; Windows ne l'envoie jamais : la souris restait en boot, ses rapports de 4 octets
  // etaient lus avec le descripteur de 8 octets et ignores (curseur immobile, boutons injectes sans effet).
  tuh_hid_set_default_protocol(HID_PROTOCOL_REPORT);
  tuh_init(1);
  // Reprise : si une interface HID est vue mais que la copie des descripteurs ne
  // se termine pas en 3 s (echec d'une requete pendant l'enumeration, observe
  // avec la G502 : process_set_config ASSERT FAILED), la carte redemarre. Rien
  // n'est encore expose au PC a ce stade, le redemarrage est invisible pour lui.
  uint32_t stall_since = 0;
  // Reprise : si aucun appareil n'est monte 4 s apres le demarrage ou apres le
  // dernier essai, on force un reset du bus souris. Pico-PIO-USB voit SE0 puis le
  // retour au repos : evenement de deconnexion puis de connexion, TinyUSB relance
  // l'enumeration (il ne reessaie jamais seul apres un echec, observe : umount sans
  // montage HID au demarrage avec la G502 deja branchee).
  uint32_t last_bus_reset = 0;
  while (true) {
    tuh_task();
    phase_lock_step();
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (g.n_itf == 0 && !g.ready && now - last_bus_reset > 4000) {
      last_bus_reset = now;
      if (now > 4000) {
        // Observe : apres certains demarrages le port racine PIO reste "connecte" alors
        // que TinyUSB n'a aucun appareil (enumeration avortee) ; un reset de bus ne
        // suffit pas car la detection de deconnexion est suspendue pendant le reset.
        // On simule une deconnexion : Pico-PIO-USB emet l'evenement, TinyUSB ferme
        // l'appareil, puis la ligne au repos (D+ haut) redeclenche une connexion et
        // une enumeration, comme un debranchement/rebranchement de la souris.
        root_port_t* root = PIO_USB_ROOT_PORT(0);
        static uint8_t attempts;
        attempts++;
        DBG("aucun appareil souris monte (essai %u, racine connectee=%u) : deconnexion simulee\r\n",
            attempts, root->connected);
        if (attempts >= 4) {
          DBG("toujours rien : redemarrage de la carte\r\n");
          sleep_ms(300);
          watchdog_reboot(0, 0, 0);
          while (true) tight_loop_contents();
        }
        if (root->connected) {
          root->connected = false;
          root->suspended = true;
          root->ints |= PIO_USB_INTS_DISCONNECT_BITS;
        } else {
          tuh_rhport_reset_bus(1, true);
          sleep_ms(20);
          tuh_rhport_reset_bus(1, false);
        }
      }
    }
    if (g.n_itf > 0 && !g.ready) {
      if (stall_since == 0) stall_since = now;
      else if (now - stall_since > 3000) {
        DBG("enumeration souris bloquee (n_itf=%u) : redemarrage\r\n", g.n_itf);
        sleep_ms(300);   // laisse le coeur 0 vider le journal
        watchdog_reboot(0, 0, 0);
        while (true) tight_loop_contents();
      }
    } else {
      stall_since = 0;
    }
    if (pending_mount) {
      uint8_t d = pending_mount;
      pending_mount = 0;
      copy_descriptors(d);
    }
    if (g.ready) process_requests();
  }
}

//--------------------------------------------------------------------
// Coeur 0 : peripherique USB natif vers le PC
//--------------------------------------------------------------------
#ifdef DEBUG_CDC
uint8_t const* tud_descriptor_device_cb(void) { return (uint8_t const*) &dbg_dev; }
uint8_t const* tud_descriptor_configuration_cb(uint8_t index) { (void) index; return dbg_cfg; }
uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) { (void) langid; return dbg_string(index); }
#elif defined(DEV_CDC)
// Descripteurs de la souris + un port serie CDC ajoute en fin de configuration.
// Avant la copie des descripteurs, la carte se presente comme le port serie seul.
#define DEV_EP_NOTIF  0x8E
#define DEV_EP_DATA   0x0F
static uint8_t dev_cfg[CFG_BUF_LEN + TUD_CDC_DESC_LEN];
static uint16_t dev_cfg_len;
static tusb_desc_device_t dev_dev;
static volatile bool dev_built;

static void dev_build_descriptors(bool with_cdc) {
  memcpy(&dev_dev, &g.dev, sizeof(dev_dev));
  memcpy(dev_cfg, g.cfg, g.cfg_len);
  dev_cfg_len = g.cfg_len;
  dev_built = true;
  if (!with_cdc) return;   // descripteurs de la souris tels quels (identiques a la variante transparente)
  dev_dev.bDeviceClass = TUSB_CLASS_MISC;
  dev_dev.bDeviceSubClass = MISC_SUBCLASS_COMMON;
  dev_dev.bDeviceProtocol = MISC_PROTOCOL_IAD;
  tusb_desc_configuration_t* c = (tusb_desc_configuration_t*) dev_cfg;
  uint8_t itf = c->bNumInterfaces;
  uint8_t const cdc[] = { TUD_CDC_DESCRIPTOR(itf, 0, DEV_EP_NOTIF, 8, DEV_EP_DATA, 0x80 | DEV_EP_DATA, 64) };
  memcpy(dev_cfg + g.cfg_len, cdc, sizeof(cdc));
  dev_cfg_len = (uint16_t) (g.cfg_len + sizeof(cdc));
  c->bNumInterfaces = (uint8_t) (itf + 2);
  c->wTotalLength = tu_htole16(dev_cfg_len);
}

uint8_t const* tud_descriptor_device_cb(void) {
  DBG("PC demande le descripteur device (dev_built=%u)\r\n", dev_built);
  return (uint8_t const*) (dev_built ? &dev_dev : &dbg_dev);
}
uint8_t const* tud_descriptor_configuration_cb(uint8_t index) {
  DBG("PC demande la configuration %u\r\n", index);
  return dev_built ? dev_cfg : dbg_cfg;
}
uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void) langid;
  DBG("PC demande la chaine %u\r\n", index);
  if (!dev_built) return dbg_string(index);
  for (int i = 0; i < g.n_str; i++) {
    if (g.str[i].index == index) return (uint16_t const*) g.str[i].data;
  }
  return NULL;
}
#else
uint8_t const* tud_descriptor_device_cb(void) { return (uint8_t const*) &g.dev; }

uint8_t const* tud_descriptor_configuration_cb(uint8_t index) { (void) index; return g.cfg; }

uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void) langid;
  for (int i = 0; i < g.n_str; i++) {
    if (g.str[i].index == index) return (uint16_t const*) g.str[i].data;
  }
  return NULL;
}
#endif

void tud_mount_cb(void) {
  tud_sof_cb_enable(true);   // le reset de bus du PC efface le consommateur SOF de TinyUSB
 DBG("PC : SET_CONFIGURATION recu, carte montee\r\n"); }
void tud_umount_cb(void) { DBG("PC : carte demontee\r\n"); }

uint8_t const* tud_hid_descriptor_report_cb(uint8_t instance) {
  int8_t idx = instance < MAX_HID_ITF ? g.inst_to_itf[instance] : -1;
  DBG("PC demande le descripteur de rapport HID, instance %u -> itf %d\r\n", instance, idx);
  return idx < 0 ? NULL : g.itf[idx].desc;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen) {
  int8_t idx = instance < MAX_HID_ITF ? g.inst_to_itf[instance] : -1;
  if (idx < 0 || !g.ready) return 0;
  host_req_t* r = req_alloc();
  if (!r) return 0;
  r->type = REQ_GET_REPORT;
  r->idx = (uint8_t) idx;
  r->report_id = report_id;
  r->report_type = (uint8_t) report_type;
  // La souris repond avec l'octet d'identifiant en tete ; TinyUSB device l'ajoute lui-meme
  // avant nos donnees (hid_device.c), donc on demande un octet de plus et on le retire.
  uint16_t off = report_id ? 1 : 0;
  r->len = reqlen + off > REPORT_LEN ? REPORT_LEN : (uint16_t) (reqlen + off);
  r->state = REQ_PENDING;
  absolute_time_t deadline = make_timeout_time_ms(200);
  while (r->state != REQ_DONE && r->state != REQ_FAIL && !time_reached(deadline)) tight_loop_contents();
  uint16_t n = 0;
  if (r->state == REQ_DONE && r->len > off) {
    n = r->len - off > reqlen ? reqlen : (uint16_t) (r->len - off);
    memcpy(buffer, r->data + off, n);
  }
  r->state = REQ_FREE;
  DBG("PC GET_REPORT itf=%d id=%u type=%u demande=%u -> %u octets\r\n", idx, report_id, report_type, reqlen, n);
  return n;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize) {
  int8_t idx = instance < MAX_HID_ITF ? g.inst_to_itf[instance] : -1;
  if (idx < 0 || !g.ready) return;
  host_req_t* r = req_alloc();
  if (!r) return;
  r->type = REQ_SET_REPORT;
  r->idx = (uint8_t) idx;
  r->report_id = report_id;
  r->report_type = (uint8_t) report_type;
  // TinyUSB (hid_device.c) retire l'octet d'identifiant de rapport avant d'appeler ce
  // callback, alors que tuh_hid_set_report envoie le tampon tel quel : on remet l'octet
  // en tete, sinon la souris recoit un rapport HID++ de 6 octets au lieu de 7 et l'ignore.
  uint16_t off = report_id ? 1 : 0;
  r->len = bufsize + off > REPORT_LEN ? REPORT_LEN : (uint16_t) (bufsize + off);
  if (off) r->data[0] = report_id;
  memcpy(r->data + off, buffer, r->len - off);
  r->state = REQ_PENDING;
  DBG("PC SET_REPORT itf=%d id=%u type=%u len=%u : %02x %02x %02x %02x %02x %02x %02x\r\n", idx, report_id, report_type, bufsize,
      bufsize > 0 ? buffer[0] : 0, bufsize > 1 ? buffer[1] : 0, bufsize > 2 ? buffer[2] : 0, bufsize > 3 ? buffer[3] : 0,
      bufsize > 4 ? buffer[4] : 0, bufsize > 5 ? buffer[5] : 0, bufsize > 6 ? buffer[6] : 0);
}

void tud_hid_set_protocol_cb(uint8_t instance, uint8_t protocol) {
  int8_t idx = instance < MAX_HID_ITF ? g.inst_to_itf[instance] : -1;
  if (idx < 0 || !g.ready) return;
  host_req_t* r = req_alloc();
  if (!r) return;
  r->type = REQ_SET_PROTOCOL;
  r->idx = (uint8_t) idx;
  r->data[0] = protocol;
  r->state = REQ_PENDING;
}

bool tud_hid_set_idle_cb(uint8_t instance, uint8_t idle_rate) {
  (void) instance; (void) idle_rate;
  return true;
}

void tud_hid_report_complete_cb(uint8_t instance, uint8_t const* report, uint16_t len) {
  (void) report; (void) len;
  int8_t idx = instance < MAX_HID_ITF ? g.inst_to_itf[instance] : -1;
  if (idx < 0) return;
  hid_itf_t* it = &g.itf[idx];
  if ((int8_t) idx == g.mouse_itf) lat_add(time_us_32() - it->t_inflight);
  if (it->pending) {
    it->pending = false;
    it->t_inflight = it->t_pending;
    tud_hid_n_report(instance, 0, it->pending_buf, it->pending_len);
  }
}

// Bouton de test : front descendant = clic enfonce, front montant = relache.
// Sans mouvement de souris il faut emettre nous-memes un rapport.
static void inject_set(bool pressed) {
  inject_active = pressed;
  DBG("inject_set %u : mouse_itf=%d last_len=%u mounted=%u\r\n", pressed, g.mouse_itf,
      g.mouse_itf >= 0 ? g.itf[g.mouse_itf].last_len : 0, tud_mounted());
  if (g.mouse_itf < 0) return;
  hid_itf_t* it = &g.itf[g.mouse_itf];
  // Rapport neutre (pas de rejeu du dernier deplacement) : tout a 0, sauf l'identifiant
  // de rapport et l'octet des boutons repris du dernier rapport s'il existe, puis le bit
  // injecte. Sans rapport recu depuis le demarrage, la longueur vient du descripteur.
  bool have_last = it->last_len != 0 && (!it->btn_report_id || it->last[0] == it->btn_report_id);
  uint16_t len = have_last ? it->last_len : it->btn_report_len;
  if (len == 0 || len > REPORT_LEN) return;
  uint8_t rep[REPORT_LEN];
  memset(rep, 0, len);
  if (it->btn_report_id) rep[0] = it->btn_report_id;
  if (have_last && it->btn_byte < len) rep[it->btn_byte] = it->last[it->btn_byte];
  if (inject_bit(it, rep, len, pressed)) {
    it->t_rx = time_us_32();   // la statistique "lat" mesure alors : injection -> lecture par le PC
    send_to_pc((uint8_t) g.mouse_itf, rep, len);
  }
}

static void poll_inject_button(void) {
  static bool last_pressed;
  bool pressed = !gpio_get(PIN_INJECT);
  if (pressed == last_pressed) return;
  last_pressed = pressed;
  inject_set(pressed);
}

int main(void) {
  gpio_init(PIN_BOOTSEL_REQ);
  gpio_set_dir(PIN_BOOTSEL_REQ, GPIO_IN);
  gpio_pull_up(PIN_BOOTSEL_REQ);
  sleep_ms(2);
  check_bootsel_req();
  set_sys_clock_khz(120000, true);
  sleep_ms(10);
  gpio_init(PIN_INJECT);
  gpio_set_dir(PIN_INJECT, GPIO_IN);
  gpio_pull_up(PIN_INJECT);
  gpio_init(PICO_DEFAULT_WS2812_PIN);
  gpio_set_dir(PICO_DEFAULT_WS2812_PIN, GPIO_OUT);
  g.mouse_itf = -1;
  uint32_t shown = 0xFFFFFFFFu;

  // Un blocage (panic, hard fault) redemarre la carte au lieu de la figer
  watchdog_enable(3000, true);
#if defined(DEBUG_CDC) || defined(DEV_CDC)
  dbg_lock = spin_lock_instance(spin_lock_claim_unused(true));
#endif
  multicore_reset_core1();
  multicore_launch_core1(core1_main);

#ifdef DEBUG_CDC
  tud_init(0);
  uint32_t next_beat = 0;
  while (true) {
    watchdog_update();
    check_bootsel_req();
    tud_task();
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now >= next_beat) {
      next_beat = now + 1000;
      // niveaux bruts des fils de donnees : hub/souris FS au repos = D+ haut, D- bas
      DBG("t=%lu ms led=%06lx ready=%u n_itf=%u GP0=%u GP1=%u\r\n", (unsigned long) now, (unsigned long) led_color,
          g.ready, g.n_itf, gpio_get(0), gpio_get(1));
    }
    dbg_drain();
    if (led_color != shown) { shown = led_color; ws2812_put(shown); }
  }
#endif
  // Variantes DEV_CDC et transparente : meme boucle. La pile device demarre tout
  // de suite ; rien n'est montre au PC (pull-up coupe) tant que les descripteurs
  // de la souris ne sont pas copies, sauf en DEV_CDC sans DEV_LATE ou le port
  // serie seul est expose en attendant.
  tud_init(0);
  tud_sof_cb_enable(true);
#if !defined(DEV_CDC) || defined(DEV_LATE)
  tud_disconnect();
#endif
  uint32_t next_phase_update = 0;
  bool exposed = false;   // souris presente dans les descripteurs donnes au PC
#if defined(DEV_CDC)
  uint32_t next_beat = 0;
#endif
#if defined(DEV_PLAIN_FIRST)
  // Experience : d'abord les descripteurs de la souris tels quels (comme la variante
  // transparente) ; si le PC ne monte pas la carte en 5 s, bascule sur souris + port serie
  bool plain_phase = false;
  uint32_t exposed_at = 0;
#endif
  while (true) {
    watchdog_update();
    check_bootsel_req();
    tud_task();
#if defined(DEV_CDC)
    dbg_drain();
#endif
    if (exposed && !g.ready) {
      tud_disconnect();
#if defined(DEV_CDC)
      dev_built = false;
#endif
      exposed = false;
      DBG("souris perdue : deconnexion du PC\r\n");
    } else if (!exposed && g.ready) {
      DBG("souris copiee : connexion au PC\r\n");
      tud_disconnect();
#if defined(DEV_PLAIN_FIRST)
      dev_build_descriptors(false);
      plain_phase = true;
      exposed_at = to_ms_since_boot(get_absolute_time());
#elif defined(DEV_CDC)
      dev_build_descriptors(true);
#endif
      sleep_ms(300);
      tud_connect();
      exposed = true;
    }
#if defined(DEV_PLAIN_FIRST)
    if (exposed && plain_phase && !tud_mounted() && to_ms_since_boot(get_absolute_time()) - exposed_at > 5000) {
      DBG("pas monte en 5 s avec les descripteurs purs (connected=%u) : bascule souris + port serie\r\n", tud_connected());
      tud_disconnect();
      dev_build_descriptors(true);
      plain_phase = false;
      sleep_ms(300);
      tud_connect();
    }
#endif
    if (exposed) poll_inject_button();
    {
      uint32_t tnow = to_ms_since_boot(get_absolute_time());
      if (tnow >= next_phase_update) {
        next_phase_update = tnow + 1000;
        if (lat.n >= 50) phase_target_update(lat.sum / lat.n);
#if !defined(DEV_CDC)
        memset(&lat, 0, sizeof(lat));   // en dev c'est le battement qui remet a zero
#endif
      }
    }
#if defined(DEV_CDC)
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now >= next_beat) {
      next_beat = now + 1000;
      DBG("t=%lu ms led=%06lx ready=%u n_itf=%u exposed=%u connected=%u mounted=%u cfg=%u GP2=%u\r\n", (unsigned long) now,
          (unsigned long) led_color, g.ready, g.n_itf, exposed, tud_connected(), tud_mounted(), dev_cfg_len, gpio_get(PIN_BOOTSEL_REQ));
      if (lat.n) {
        DBG("lat n=%lu min=%lu avg=%lu p50<=%lu p99<=%lu max=%lu over3200=%lu dropped=%lu (us, souris->PC)\r\n",
            (unsigned long) lat.n, (unsigned long) lat.min, (unsigned long) (lat.sum / lat.n),
            (unsigned long) lat_percentile(50), (unsigned long) lat_percentile(99), (unsigned long) lat.max,
            (unsigned long) lat.over, (unsigned long) g.dropped);
        memset(&lat, 0, sizeof(lat));
      }
      DBG("phase now=%ld target=%ld period=%ld pc_sof=%u\r\n", (long) phase_now, (long) phase_target, (long) sof_period, pc_sof_seen);
    }
#endif
    // LED : etat du coeur 1 (blanc faible, bleu, jaune, vert, rouge) tant que le PC
    // n'est pas sollicite, puis blanc fort = pull-up active, cyan = PC a configure la carte
    uint32_t c = !exposed ? led_color : (tud_mounted() ? LED_PC : LED_STACK);
    if (c != shown) { shown = c; ws2812_put(shown); }
  }
  return 0;
}
