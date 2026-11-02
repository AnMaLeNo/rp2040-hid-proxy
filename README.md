# Proxy USB transparent pour Logitech G502 HERO

Carte : RP2040-Zero (clone AYWHP, brochage Waveshare).

- Port USB-C natif : cote PC (la carte se presente comme la souris).
- Port soude sur GP0 (D+), GP1 (D-), 5V, GND : cote souris (hote PIO-USB).
- GP2 relie a GND : demande de retour en BOOTSEL (reflash), lue au demarrage et a chaque tour de boucle, toutes variantes. Pilotee par la carte de controle (son fil est soude sur la pastille 2 du proxy).
- GP3 relie a GND : bouton de test, injecte un clic gauche tant qu'il est enfonce (non cable pour l'instant ; l'injection se teste par le port serie, `P`/`R`).

## Carte de controle (seconde RP2040-Zero)

Permet de reflasher le proxy sans toucher a l'USB. Firmware `firmware/proxy_ctrl.uf2`, sources `src/ctrl/`. Branchee au Mac par son USB-C, port serie USB.

Cablage vers la carte proxy : GND vers GND ; GP13 (controle) vers GP2 (proxy) ; facultatif GP14 vers la patte utile du bouton RESET (signal RUN) et GP15 vers la patte utile du bouton BOOT (signal QSPI_SS). Sorties en collecteur ouvert (flottantes au repos, 0 pour appuyer).

Schema Waveshare RP2040-Zero (RP2040_Zero.pdf) : Key2 (RESET) entre RUN et GND, Key1 (BOOT) entre QSPI_SS_N et GND. Wiki Waveshare : BOOT + RESET, relacher RESET puis BOOT, fait apparaitre le disque RPI-RP2 sans debrancher.

Commandes serie : `g` = GP2 du proxy a 0 pendant 4 s (BOOTSEL logiciel), `r` = reset par RUN, `b` = BOOTSEL par les boutons, `?` = etat des lignes.

## Fichiers

- `firmware/proxy_souris.uf2` : firmware proxy transparent (D+ GP0, D- GP1). Valide avec la G502 le 2026-11-02 : le Mac enumere G502 HERO SE 046d:c08b, classe 0, 2 interfaces HID, aucun port serie. Pas de reflash par port serie : BOOT manuel.
- `firmware/proxy_souris_dmdp.uf2` : idem avec D+ et D- echanges (D- GP0, D+ GP1).
- `firmware/proxy_souris_debug.uf2`, `proxy_souris_dmdp_debug.uf2` : port serie seul + journal complet TinyUSB, la souris n'est pas exposee au PC. Taper `B` sur le port serie = retour en BOOTSEL.
- `firmware/proxy_souris_dev.uf2` : souris clonee + port serie ajoute en fin de configuration (composite, non transparent). Journal reduit aux erreurs, `B` = BOOTSEL. Sert a iterer sans toucher la carte.
- `tools/reflash.sh <uf2> [secondes] [log]` : envoie `B` au port serie, attend `RPI-RP2`, copie le uf2, puis capture le port serie.
- `tools/serial_capture.py <port> <secondes> <fichier>` : lecture du port serie avec DTR/RTS actifs.
- `tools/button_watch <secondes>` (source `button_watch.swift`) : affiche les transitions du bouton gauche vues par macOS (CGEventSource), sert a verifier un clic injecte.
- Commandes du port serie (variantes debug et dev) : `B` = BOOTSEL, `P` = clic gauche injecte presse, `R` = relache.
- `firmware/test_host_hid_to_device_cdc.uf2` : exemple Pico-PIO-USB utilise pour valider la carte.
- `src/` : sources du proxy (main.c, tusb_config.h, CMakeLists.txt). Compile sur le VM Devin avec pico-sdk 2.1.1, TinyUSB 0.18, Pico-PIO-USB, `-DPICO_BOARD=waveshare_rp2040_zero`.
- `tools/capture_g502.sh` : dump des descripteurs de la souris branchee au Mac.

## Flasher

1. Debrancher l'USB-C de la carte.
2. Maintenir BOOT, rebrancher, relacher. Un disque `RPI-RP2` apparait.
3. `cp firmware/proxy_souris.uf2 /Volumes/RPI-RP2/`

## Fonctionnement du firmware v1

Coeur 1 : hote PIO-USB. Au branchement de la souris il lit le descripteur de peripherique, le descripteur de configuration, les chaines (langues, fabricant, produit, serie, interfaces) et le descripteur de rapport de chaque interface HID. Il relance une lecture interrupt IN sur chaque interface.

Coeur 0 : peripherique USB natif. Il ne demarre que lorsque les descripteurs sont copies, puis les renvoie tels quels au PC. VID/PID, chaines et numero de serie sont donc ceux de la souris.

Relais :
- rapports IN souris vers PC : envoyes des reception, sans copie intermediaire si l'endpoint est libre, sinon mis en attente jusqu'au prochain polling du PC (compteur `dropped` si deux rapports s'accumulent).
- SET_REPORT, GET_REPORT, SET_PROTOCOL du PC : transmis a la souris par le coeur 1 (necessaire pour G HUB / HID++).
- SET_IDLE : accepte localement, non transmis.
- Debranchement de la souris : la carte se deconnecte du PC, puis se reconnecte quand une souris revient.

Injection : la position du bouton 1 (page Button, usage 1) est trouvee en parcourant le descripteur de rapport de l'interface souris (`find_button1`), identifiant de rapport compris. Verifie hors carte sur le descripteur du recepteur Keychron Link (rapport ID 3, octet 1, bit 0) et sur un descripteur souris boot sans ID (octet 0, bit 0).

## Captures

- `captures/keychron_link_3434_d030.txt` : recepteur Keychron Link (3434:d030), 3 interfaces HID, bcdUSB 2.00, EP0 8 octets, pas de numero de serie.
- `captures/g502_via_proxy_debug_log.txt` : journal de `proxy_souris_debug` avec la G502 sur le connecteur USB-C soude (2026-11-02). `ready=1 n_itf=2`, rapports de 4 octets recus en continu sur EP 81. Le debut (enumeration) a ete perdu, tampon de 8 Ko sature.

## Etat au 2026-11-02

- Cablage valide : connecteur USB-C femelle soude sur 5V, GND, GP0 (D+), GP1 (D-), CC1/CC2 en l'air, adaptateur USB-C male vers USB-A femelle, G502 dessus. La carte enumere la souris (2 interfaces HID) avec la variante standard DPDM.
- La variante DMDP avait un bug : `pin_dp = 0` avec `PIO_USB_PINOUT_DMDP` donnait `pin_dm = 255`. Corrige avec `pin_dp = 1`.
- La G502 reste eteinte (aucune LED) tant qu'un hote ne lui parle pas : ce n'est pas un signe d'absence de 5 V.
- `proxy_souris_dev.uf2` valide avec la G502 : le Mac enumere `G502 HERO SE` 046d:c08b (bcdDevice 0x2703, serie 176F31473832, 2 interfaces HID usage souris et clavier, montees par AppleUserUSBHostHIDDevice), le curseur suit la souris. Journal : `captures/dev_log1.txt`, dump ioreg : `captures/g502_via_proxy_dev_ioreg.txt`.
- Clic injecte valide : `P` puis `R` envoyes sur le port serie, macOS a vu PRESSE 1 ms apres l'ecriture de `P` et RELACHE 1 ms apres `R` (horloge du Mac, `captures/click_watch1.txt`). Ce n'est pas une mesure de latence du relais souris.
- Cause du « Mac ne voit rien » avec la variante transparente (journal `captures/mac_accessory_security_log.txt`) : la securite accessoires de macOS (IOAccessoryManager). A chaque connexion de la carte avec les descripteurs purs de la G502, le port passe `authorizationStatus: 1 [Unauthorized]`, `transportRestricted: NO -> YES`, et le Mac n'enumere pas. Les variantes avec port serie avaient ete autorisees (clic « Autoriser » a 21:36), la variante transparente est traitee comme un nouvel accessoire. Remede : Reglages Systeme > Confidentialite et securite > « Autoriser les accessoires a se connecter » = Toujours, ou cliquer la demande d'autorisation. Ce n'est pas un bug du firmware.
- Apres reglage « Toujours » et rebranchement (22:00) : le Mac enumere la carte avec les descripteurs purs de la G502 (`proxy_souris_dev_plainfirst`, phase pure) : 046d:c08b, bDeviceClass 0, bcdDevice 0x2703, serie 176F31473832, interface 0 = HID boot souris (sous-classe 1, protocole 2), interface 1 = HID (sous-classe 0, protocole 0), les deux montees par AppleUserUSBHostHIDDevice, curseur fonctionnel, aucun port serie expose. Port en `authorizationStatus: 2 [Policy Authorized]`.
- Au passage, main.c a ete unifie : toutes les variantes font `tud_init` au demarrage, `tud_disconnect` tant que la souris n'est pas copiee, puis `tud_connect` (variante DEV : d'abord le port serie seul, sauf `DEV_LATE`). La sequence a ete verifiee avec `proxy_souris_dev_late` (G502 enumeree en 2 s). `proxy_souris_dev_plainfirst` est une variante d'experience (descripteurs purs 5 s, puis bascule souris + port serie).
- Le message Finder « Ejectez RPI-RP2 avant de le deconnecter » apparait a chaque flash : la carte redemarre d'elle-meme apres la copie du uf2. Sans consequence.

## Mesure de latence (2026-11-02, premiere mesure)

Methode : dans le firmware, chaque rapport de l'interface souris est horodate (`time_us_32`) a sa reception depuis la souris sur le coeur 1 (`tuh_hid_report_received_cb`) et a la fin de sa lecture par le PC (`tud_hid_report_complete_cb`). La difference est la latence ajoutee par le proxy (traitement + attente du prochain polling du PC). Histogramme par pas de 50 us, statistiques par fenetre d'une seconde, imprimees sur le port serie en variante dev (`lat n=... min avg p50 p99 max`). Fichier : `captures/latence4.txt`.

Resultat, 30 s, environ 15 000 rapports, G502 via le proxy vers le Mac : latence ajoutee entre 0,07 et 1,05 ms, moyenne environ 0,5 ms, 0 rapport perdu. Dans chaque fenetre d'une seconde, min et max different de 20 a 30 us seulement : la valeur glisse lentement de 0,07 a 1,05 ms puis repart a 0,07 ms, signature du dephasage entre le polling 1 kHz du proxy vers la souris et le polling 1 kHz du Mac vers le proxy (deux horloges independantes). Pas de gigue mesurable au-dela de ce glissement.

Limites : mesure en variante dev (port serie actif sur le coeur 0), horloge du proxy uniquement (pas de mesure de bout en bout avec un instrument externe). A refaire en variante transparente avec une methode externe. Piste d'amelioration : caler la phase du polling souris sur les SOF du PC pour rester vers 0,1 a 0,2 ms.

## Asservissement de phase (2026-11-02, soir)

Principe : le PC envoie un SOF par ms ; TinyUSB le signale par `tud_sof_cb` (coeur 0, `tud_sof_cb_enable(true)`, a refaire dans `tud_mount_cb` car le reset de bus efface l'abonnement). Cote souris, Pico-PIO-USB cadence son SOF avec un timer repetitif de 1000 us ; patch `src/pico-pio-usb-sof-phase.patch` (a appliquer sur Pico-PIO-USB 0.7.3) qui expose l'instant du SOF hote (`pio_usb_host_sof_us`) et la periode (`pio_usb_host_set_sof_period_us`). Le coeur 1 mesure la phase (SOF hote - SOF PC, mod 1000) et corrige la periode de +/-1 us par trame vers une phase cible. La phase cible est ajustee chaque seconde a partir de la latence moyenne mesuree (cible `PHASE_LAT_TARGET_US` = 200 us). Actif dans toutes les variantes.

Resultat (`captures/phase2.txt`, 45 s, ~30 000 rapports, variante dev) : latence ajoutee entre 196 et 205 us, moyenne 200 us, phase tenue a +/-4 us, 0 rapport perdu, quelques pics isoles jusqu'a 430 us. Avant asservissement (`captures/latence4.txt`) : 70 a 1050 us en glissement.

## Problemes ouverts

- Au demarrage du proxy avec la souris deja branchee, il est arrive (2 fois sur ~8 demarrages) que la souris ne soit pas vue (`n_itf=0`, un `umount` sans montage HID) : TinyUSB ne relance jamais une enumeration echouee. Correctif (2026-11-02, soir) : si aucun appareil n'est monte 4 s apres le demarrage ou le dernier essai, `tuh_rhport_reset_bus(1, true/false)` force SE0 20 ms ; Pico-PIO-USB emet deconnexion puis connexion et l'enumeration repart. Le cas « interface vue mais copie non terminee en 3 s » redemarre la carte. Verifie sur 3 redemarrages consecutifs (souris vue a chaque fois, reset non declenche) ; le defaut d'origine n'a pas ete reproduit depuis, donc le correctif n'est pas encore prouve sur un cas reel.

## Hypotheses encore a verifier

- La G502 n'a que des interfaces HID : confirme par le descripteur de configuration copie (59 octets, 2 interfaces HID).
- Le relais des commandes G HUB / HID++ (SET_REPORT, GET_REPORT vers l'interface 1) n'a pas ete teste avec le logiciel Logitech.
- La latence n'a ete mesuree qu'avec l'horloge du proxy, en variante dev (voir plus haut).
