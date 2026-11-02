// Carte de controle : seconde RP2040-Zero reliee au Mac par son USB-C, qui
// "appuie" sur les boutons RESET et BOOT de la carte proxy.
//   GP13 -> GP2 du proxy (ligne "demande BOOTSEL" lue par le firmware proxy)
//   GP14 -> patte utile du bouton RESET du proxy (signal RUN), optionnel
//   GP15 -> patte utile du bouton BOOT du proxy (signal QSPI_SS), optionnel
//   GND  -> GND du proxy
// Chaque sortie est en collecteur ouvert : entree flottante au repos (le bouton
// reste utilisable a la main), sortie a 0 pour "appuyer". Jamais de niveau haut
// force, le proxy a ses propres resistances de tirage.
// Commandes sur le port serie (115200, peu importe) :
//   g = demande BOOTSEL par logiciel : GP2 du proxy tire a 0 pendant 4 s
//   r = reset simple (RUN bas 100 ms)
//   b = passage en BOOTSEL par les boutons (BOOT bas, RUN bas 100 ms, BOOT relache 500 ms apres)
//   ? = etat des lignes
#include <stdio.h>
#include "pico/stdlib.h"

#define PIN_REQ   13
#define PIN_RUN   14
#define PIN_BOOT  15
#define PIN_LED   16   // WS2812 de la RP2040-Zero, non utilise ici

static void press(uint pin)   { gpio_put(pin, 0); gpio_set_dir(pin, GPIO_OUT); }
static void release(uint pin) { gpio_set_dir(pin, GPIO_IN); }

int main(void) {
  stdio_init_all();
  for (uint p = PIN_REQ; p <= PIN_BOOT; p++) {
    gpio_init(p);
    gpio_disable_pulls(p);
    release(p);
  }
  while (true) {
    int c = getchar_timeout_us(100000);
    if (c == PICO_ERROR_TIMEOUT) continue;
    if (c == 'g') {
      press(PIN_REQ); sleep_ms(50);
      int lvl = gpio_get(PIN_REQ);   // niveau reel de la ligne pendant l'appui (doit etre 0)
      sleep_ms(3950); release(PIN_REQ);
      printf("bootsel logiciel ok, ligne pendant l'appui=%d\r\n", lvl);
    } else if (c == 'r') {
      press(PIN_RUN); sleep_ms(100); release(PIN_RUN);
      printf("reset ok\r\n");
    } else if (c == 'b') {
      press(PIN_BOOT); sleep_ms(20);
      press(PIN_RUN); sleep_ms(100); release(PIN_RUN);
      sleep_ms(500); release(PIN_BOOT);
      printf("bootsel ok\r\n");
    } else if (c == '?') {
      printf("REQ=%d RUN=%d BOOT=%d\r\n", gpio_get(PIN_REQ), gpio_get(PIN_RUN), gpio_get(PIN_BOOT));
    }
  }
}
