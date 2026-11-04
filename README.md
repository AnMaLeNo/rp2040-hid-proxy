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

## Variante clic par le fil (`proxy_souris_click`, 2026-11-03)

Transparente (identite G502 pure, asservissement de phase) avec GP2 a double usage, lu a chaque tour de boucle du coeur 0 : front descendant = clic gauche injecte immediatement, front montant = relache, ligne maintenue a 0 plus de 2 s (`CTRL_BOOT_HOLD_MS`) = retour en BOOTSEL. Le clic ne paie donc aucun delai pour conserver le reflash autonome. `proxy_souris_click_dev` = la meme avec port serie et journal.

Le rapport injecte est neutre (deplacements a 0, octet des boutons repris du dernier rapport recu, bit 1 force) ; sans rapport recu depuis le demarrage, sa longueur vient du descripteur (`btn_report_len`, calcule dans `find_button1`). Avant cette correction le clic etait perdu si la souris n'avait pas bouge depuis le flash.

Carte de controle (`proxy_ctrl.uf2`, sources `src/ctrl/`) : depuis le 2026-11-03 c'est un peripherique HID vendeur (2E8A:10C1, produit `proxy-ctrl`), plus de port serie ni de stdio (le stdio USB du SDK tourne sur un timer d'1 ms). Les commandes arrivent par SET_REPORT sur l'endpoint de controle et sont appliquees dans le callback ; `tud_task` en boucle serree. Commandes : `P` ligne a 0 (appui), `R` ligne a 1 (relache), `k` impulsion 60 ms, `g` 4 s a 0 (sur cette variante : clic de 2 s puis BOOTSEL), `r` RUN 100 ms, `b` BOOT+RUN, `B` la carte de controle elle-meme repasse en BOOTSEL, `?` etat par GET_REPORT. Outil Mac : `tools/ctrlhid` (IOKit HID, sans dependance ; `tools/ctrl.sh` l'appelle), `tools/ctrlhid bench n` horodate n commandes et mesure la duree de chaque SET_REPORT.

Mesures du clic (`tools/click_latency.py`, horloge epoch du Mac pour la commande et pour l'evenement bouton vu par `button_watch`) :

- Port serie (ancienne carte de controle, `captures/click_latency1.txt`) : 10/10 vus, 1,6 a 4,3 ms, mediane 2,2 ms.
- HID (`captures/click_latency4.txt`, `click_latency5.txt`) : 10/10 vus, 1,40 a 3,12 ms, mediane 1,8 a 2,1 ms. Aller-retour Mac -> carte 0,35 a 0,65 ms. Appui 58 a 63 ms pour 60 demandes.
- Troncon proxy -> lecture par le Mac, mesure dans le proxy (statistique `lat` sur le rapport injecte, `captures/click_wire_log4.txt`) : 115 a 1059 us, reparti dans la trame de 1 ms du polling du Mac (intervalle minimal en Full Speed), moyenne environ 0,5 ms. Irreductible cote proxy.
- Reste, par difference : environ 1 ms dans macOS (SET_REPORT et remontee de l'evenement HID).

Un premier essai HID (`click_latency2.txt`) a montre 2 clics sur 10 non vus et un appui de 1,8 s ; non reproduit ensuite sur 30 clics avec journal (chaque front vu par le proxy). Cause non identifiee.

## G HUB a travers le proxy (2026-11-03)

Valide : G HUB detecte la G502 a travers le proxy et les reglages fonctionnent (effets lumineux, intensite, frequence, affichage du DPI qui suit les boutons de la souris). En 40 s, 60 commandes HID++ variees (rapports 0x10 et 0x11 sur l'interface 1) ont ete relayees, `captures/ghub4.txt`.

Deux obstacles rencontres, dans l'ordre :

1. macOS refusait a `lghub_agent` l'ouverture des peripheriques HID (`TCC deny IOHIDDeviceOpen` dans `log show`). G HUB montrait alors « G502 HERO, inactif » ou « connecte ton equipement ». Correction cote Mac : Reglages Systeme, Confidentialite et securite, Surveillance de l'entree, activer lghub et lghub_agent, puis relancer G HUB. Rien a voir avec le proxy.
2. Avec l'autorisation, G HUB repetait la meme requete d'identification (`ff 00 1c ...`) sans reponse. Cause dans le proxy : TinyUSB cote PC (`hid_device.c`) retire l'octet d'identifiant de rapport avant d'appeler `tud_hid_set_report_cb`, alors que `tuh_hid_set_report` envoie le tampon tel quel, donc la souris recevait 6 octets au lieu de 7 et ignorait la commande. Correction : l'identifiant est remis en tete avant l'envoi a la souris ; pour GET_REPORT, TinyUSB device ajoute lui-meme l'identifiant, donc le proxy demande un octet de plus a la souris et retire le premier.

## Problemes ouverts

- Au demarrage du proxy avec la souris deja branchee, il est arrive (2 fois sur ~8 demarrages) que la souris ne soit pas vue (`n_itf=0`, un `umount` sans montage HID) : TinyUSB ne relance jamais une enumeration echouee. Correctif (2026-11-02, soir) : si aucun appareil n'est monte 4 s apres le demarrage ou le dernier essai, `tuh_rhport_reset_bus(1, true/false)` force SE0 20 ms ; Pico-PIO-USB emet deconnexion puis connexion et l'enumeration repart. Le cas « interface vue mais copie non terminee en 3 s » redemarre la carte. Le 2026-11-03 le defaut est reapparu (`captures/click_wire_log2.txt`) et le reset de bus, declenche toutes les 4 s, n'a rien recupere : pendant un reset Pico-PIO-USB suspend la detection de deconnexion, donc le port racine reste marque connecte et aucun evenement de connexion n'est reemis. Correctif suivant : deconnexion simulee (`PIO_USB_ROOT_PORT(0)->connected = false`, `suspended = true`, bit `PIO_USB_INTS_DISCONNECT_BITS`), ce qui reproduit un debranchement ; apres 4 essais sans succes la carte redemarre. Ce correctif n'a pas encore ete observe en action sur un cas reel (la souris etait vue au demarrage suivant, sans que l'on sache si c'est grace a lui).

## Hypotheses encore a verifier

- La G502 n'a que des interfaces HID : confirme par le descripteur de configuration copie (59 octets, 2 interfaces HID).
- Le relais des commandes G HUB / HID++ est valide pour SET_REPORT (voir plus haut) ; GET_REPORT n'a pas ete observe dans les echanges de G HUB, son chemin corrige n'est donc pas encore exerce.
- La latence n'a ete mesuree qu'avec l'horloge du proxy, en variante dev (voir plus haut).

## Windows : la souris restait en protocole boot (2026-11-03, soir)

Constat : branché à un PC Windows, le proxy était énuméré (G502 HERO 046d:c08b, souris HID « OK » dans le gestionnaire de périphériques) mais le curseur ne bougeait pas. Mesures (`tools/win_mouse_watch.py`, évènements Raw Input par appareil) : aucun déplacement reçu ; journal de la variante `_dev` : `last_len=0`, et dans `captures/g502_via_proxy_debug_log.txt` des transferts « on EP 81 with 4 bytes » alors que le descripteur de rapport annonce des rapports de **8 octets** (16 boutons, X et Y sur 16 bits, molette, panoramique).

Cause : l'hôte TinyUSB met par défaut les interfaces de classe boot en protocole **boot** à l'énumération (`hid_host.c` : `_hidh_default_protocol = HID_PROTOCOL_BOOT`, appliqué dans `process_set_config`). La souris envoyait donc des rapports de 4 octets (boutons, X, Y, molette en 8 bits), lus avec le descripteur de 8 octets. macOS envoie ensuite lui-même `SET_PROTOCOL(report)`, que le proxy relaie à la souris : c'est pourquoi tout marchait sur le Mac. Windows ne l'envoie jamais.

Correction (`src/main.c`, une ligne avant `tuh_init`) : `tuh_hid_set_default_protocol(HID_PROTOCOL_REPORT)`. Mesure après correction (Windows, 30 s, souris bougée à la main) : 124 rapports reçus dont 118 avec déplacement, `last_len=8` côté proxy ; clics injectés par la carte de contrôle vus par Windows (3 impulsions `k` sur 3 : appui 48 à 64 ms). Latence ajoutée mesurée par le proxy : 230 à 440 µs.
`firmware/proxy_souris_click.uf2` et `proxy_souris_click_dev.uf2` sont recompilés avec ce correctif ; **les autres variantes ne le sont pas** (même défaut sous Windows : à recompiler au besoin).

## Clic filtré par la molette (variantes `click` et `click_dev`, 2026-11-04)

Demande : le clic envoyé par la carte de contrôle ne doit être relayé au PC que **tant que l'un des deux boutons d'inclinaison de la molette (G502 HERO) est maintenu**. Les clics de la vraie souris passent toujours, sans condition.

Fait établi (`tools/win_mouse_watch.py`, Raw Input sous Windows) : l'inclinaison n'est **pas un bouton** pour le PC mais un défilement horizontal (`WM_MOUSEHWHEEL` ±120) que la souris **répète toutes les ~100 ms** (95 à 112 ms mesurés) tant que la molette reste inclinée. Dans le descripteur de rapport de l'interface souris (rapport de 8 octets, sans identifiant) c'est le champ *AC Pan* (page Consumer, usage 0x0238) : **octet 7**, entier signé 8 bits, gauche = −1, droite = +1. Boutons 0–1, X 2–3, Y 4–5, molette 6.

Mise en œuvre (`src/main.c`) :
- `find_button1` repère aussi le champ AC Pan (`pan_found`, `pan_byte`, `pan_size`) à partir du descripteur ; testé hors carte sur le descripteur de la G502 (octet 7, taille 1) et sans régression sur les boutons.
- Coeur 1, à chaque rapport reçu de la souris : si le champ Pan est non nul, l'instant est mémorisé (`tilt_last_us`). La molette est « maintenue » si ce dernier évènement a moins de `TILT_HOLD_US` = 200 ms (les répétitions sont espacées de 112 ms au plus). Le relâchement est donc détecté en moins de 200 ms.
- Coeur 0, au front descendant de GP2 : si la molette est maintenue à cet instant le clic est injecté comme avant, sinon il est **refusé en entier** (aucun appui, aucun rapport). Au front montant, le relâchement n'est émis que si un appui a été injecté. Un clic déjà commencé va jusqu'au bout même si la molette est relâchée entre-temps.
- **L'inclinaison n'est pas relayée au PC** (modification du 2026-11-04, à la demande de l'utilisateur) : dans la copie envoyée au PC le champ Pan est remis à 0 ; déplacements, boutons et molette verticale restent tels quels. Le proxy continue de la lire dans le rapport d'origine pour le filtre. Un rapport qui ne contenait que l'inclinaison part donc au PC sous forme de rapport « sans changement ». Valable pour les variantes `click` et `click_dev` uniquement.
- Inchangés : le retour en BOOTSEL (GP2 à 0 plus de 2 s) et les commandes `P` / `R` du port série de la variante `_dev` (injection non filtrée, pour les essais). **Conséquence utile : `ctrl.sh g` (reflash) n'envoie plus de clic de 2 s au PC tant que la molette n'est pas inclinée.**

Mesures (Windows, `tools/win_click_sink.py` + `win_mouse_watch.py`, `captures/win_tilt_gate_c.txt`) :
- sans inclinaison : 5 `k` sur 5 refusés (0 clic) ;
- inclinaison à gauche, puis à droite, puis à gauche (2 à 4 s chacune) avec un `k` toutes les ~1,5 s : 20 `k` envoyés, 10 clics injectés, **tous pendant une inclinaison** (dernier évènement d'inclinaison 0 à 80 ms avant l'appui, appui de 46 à 64 ms) ; 9 `k` (avant la première inclinaison, entre deux inclinaisons, après la dernière) n'ont produit aucun appui. Gauche et droite se comportent pareil.
- un onzième appui visible par Windows (424 ms, avec déplacements de la souris) est un clic physique de l'utilisateur : il passe, comme prévu. Le 20e `k` (envoyé 1,3 s après la dernière inclinaison) est parti pendant ce clic : son résultat propre n'est pas démontrable (son impulsion de 60 ms se serait confondue avec l'appui physique).
- Inclinaison non relayée (`captures/win_tilt_gate_d.txt`, test avec `win_mouse_watch.py` + `win_click_sink.py`, 8 `k`, l'utilisateur incline en cours de test) : **0 évènement d'inclinaison vu par Windows**, 5 `k` refusés avant l'inclinaison, 3 clics injectés après (le filtre lit donc bien l'inclinaison alors que le PC ne la reçoit plus). Le reflash par `ctrl.sh g` sans inclinaison : 0 clic reçu.
- Non mesuré : l'instant précis de refus après le relâchement de la molette (borne : 200 ms par construction). Le profil embarqué de la souris peut être changé avec G HUB : si l'inclinaison est un jour assignée à un bouton, ce filtre ne la verrait plus (l'octet 7 resterait à 0).

### Compiler (sans droits administrateur, testé sur le Mac, 2026-11-03)

`cmake` et `ninja` (brew), toolchain `arm-gnu-toolchain-14.2.rel1-darwin-arm64-arm-none-eabi` (archive, extraite dans `~/pico/toolchain`), `pico-sdk` 2.1.1 (avec le sous-module `lib/tinyusb`), `Pico-PIO-USB` 0.7.3 cloné dans `./Pico-PIO-USB` + `git apply src/pico-pio-usb-sof-phase.patch`, `pico_sdk_import.cmake` copié de `pico-sdk/external/` dans `src/`. Puis :

```
PATH=$HOME/pico/toolchain/bin:$PATH cmake -S src -B build -G Ninja -DPICO_SDK_PATH=$HOME/pico/pico-sdk -DPICO_BOARD=waveshare_rp2040_zero -DCMAKE_POLICY_VERSION_MINIMUM=3.5
ninja -C build proxy_souris_click proxy_souris_click_dev      # les .uf2 sont dans build/
```

### Outils Windows (le proxy est branché au PC ; la carte de contrôle reste sur le Mac)

- `tools/win_reflash.py <uf2> [--secs N] [--log f]` : reflashe le proxy (variante `_dev` : commande `B` sur le port série ; sinon `ctrl.sh g` en SSH sur le Mac : **sur une variante `click` d'avant le 2026-11-04 cela envoie un clic de 2 s au PC avant le BOOTSEL ; depuis le filtre par la molette, plus de clic sauf molette inclinée**), copie le .uf2 sur `RPI-RP2`, capture le journal série.
- `tools/win_mouse_watch.py [secondes] [--only c08b]` : évènements souris bruts par appareil (déplacements, changements de bouton, molette verticale et inclinaison horodatés).
- `tools/win_click_sink.py [secondes] [--send N] [--gap s] [--first s]` : fenêtre-cible au centre de l'écran qui absorbe les clics du proxy pendant un test (aucun clic sur le bureau), envoie N `k` par SSH et note les inclinaisons de molette vues.
