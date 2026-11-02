#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#define CFG_TUSB_OS                 OPT_OS_PICO
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN          __attribute__ ((aligned(4)))

// Cote PC : controleur USB natif (rhport 0), clone de la souris
#define CFG_TUD_ENABLED             1
#define CFG_TUD_ENDPOINT0_SIZE      64
#define CFG_TUD_HID                 3
#define CFG_TUD_HID_EP_BUFSIZE      64

// Variante diagnostic (DEBUG_CDC) : port serie seul + journal complet de la pile.
// Variante developpement (DEV_CDC) : souris clonee + port serie ajoute (journal, 'B' = BOOTSEL).
#if defined(DEBUG_CDC) || defined(DEV_CDC)
#define CFG_TUD_CDC                 1
#define CFG_TUD_CDC_RX_BUFSIZE      64
#define CFG_TUD_CDC_TX_BUFSIZE      1024
#undef  CFG_TUSB_DEBUG            // le SDK Pico force CFG_TUSB_DEBUG=0 en ligne de commande
#define CFG_TUSB_DEBUG_PRINTF       dbg_printf
#endif
#ifdef DEBUG_CDC
#define CFG_TUSB_DEBUG              2
#define CFG_TUD_LOG_LEVEL           3
#define CFG_TUH_LOG_LEVEL           2
#endif
#ifdef DEV_CDC
#ifdef DEV_HOST_LOG
#define CFG_TUSB_DEBUG              2   // diagnostic : requetes de la pile hote detaillees (lourd)
#define CFG_TUD_LOG_LEVEL           1
#define CFG_TUH_LOG_LEVEL           2
#else
#define CFG_TUSB_DEBUG              1   // erreurs seulement : le journal ne doit pas freiner le relais
#endif
#endif

// Cote souris : hote PIO-USB (rhport 1), D+ sur GP0, D- sur GP1
#define CFG_TUH_ENABLED             1
#define CFG_TUH_RPI_PIO_USB         1
#define CFG_TUH_ENUMERATION_BUFSIZE 512
// Hub tolere pour les tests (boitier de partage) ; la souris doit etre seule derriere
#define CFG_TUH_HUB                 1
#define CFG_TUH_DEVICE_MAX          2
#define CFG_TUH_HID                 4
#define CFG_TUH_HID_EPIN_BUFSIZE    64
#define CFG_TUH_HID_EPOUT_BUFSIZE   64

#endif
