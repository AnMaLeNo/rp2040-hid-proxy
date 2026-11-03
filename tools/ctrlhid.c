// Pilote la carte de controle (HID vendeur 2E8A:10C1) sans port serie.
// Compilation : clang -O2 -o tools/ctrlhid tools/ctrlhid.c -framework IOKit -framework CoreFoundation
// Usage :
//   ctrlhid <cmd>            envoie un octet de commande (P R k g r b B) par SET_REPORT
//   ctrlhid ?                lit l'etat par GET_REPORT
//   ctrlhid bench <n> [cmd]  n commandes espacees de 1 s ; imprime pour chacune l'heure epoch
//                            du debut du SET_REPORT et sa duree (aller-retour Mac -> carte)
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static double now_epoch(void) { struct timeval tv; gettimeofday(&tv, NULL); return tv.tv_sec + tv.tv_usec / 1e6; }

static IOHIDDeviceRef open_ctrl(void) {
  IOHIDManagerRef mgr = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
  int vid = 0x2E8A, pid = 0x10C1;
  CFNumberRef v = CFNumberCreate(NULL, kCFNumberIntType, &vid), p = CFNumberCreate(NULL, kCFNumberIntType, &pid);
  CFStringRef keys[] = { CFSTR(kIOHIDVendorIDKey), CFSTR(kIOHIDProductIDKey) };
  CFTypeRef vals[] = { v, p };
  CFDictionaryRef match = CFDictionaryCreate(NULL, (const void**) keys, vals, 2,
                                             &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  IOHIDManagerSetDeviceMatching(mgr, match);
  if (IOHIDManagerOpen(mgr, kIOHIDOptionsTypeNone) != kIOReturnSuccess) { fprintf(stderr, "IOHIDManagerOpen a echoue\n"); return NULL; }
  CFSetRef set = IOHIDManagerCopyDevices(mgr);
  if (!set || CFSetGetCount(set) == 0) { fprintf(stderr, "carte de controle HID introuvable\n"); return NULL; }
  IOHIDDeviceRef dev;
  CFSetGetValues(set, (const void**) &dev);
  if (IOHIDDeviceOpen(dev, kIOHIDOptionsTypeNone) != kIOReturnSuccess) { fprintf(stderr, "IOHIDDeviceOpen a echoue\n"); return NULL; }
  return dev;
}

static int send_cmd(IOHIDDeviceRef dev, char c, double* dur_us) {
  uint8_t rep[32] = { 0 };
  rep[0] = (uint8_t) c;
  struct timespec a, b;
  clock_gettime(CLOCK_MONOTONIC, &a);
  IOReturn r = IOHIDDeviceSetReport(dev, kIOHIDReportTypeOutput, 0, rep, sizeof rep);
  clock_gettime(CLOCK_MONOTONIC, &b);
  if (dur_us) *dur_us = (b.tv_sec - a.tv_sec) * 1e6 + (b.tv_nsec - a.tv_nsec) / 1e3;
  return r == kIOReturnSuccess ? 0 : -1;
}

int main(int argc, char** argv) {
  if (argc < 2) { fprintf(stderr, "usage: ctrlhid <cmd> | ? | bench <n> [cmd]\n"); return 2; }
  IOHIDDeviceRef dev = open_ctrl();
  if (!dev) return 1;
  if (strcmp(argv[1], "?") == 0) {
    uint8_t buf[32] = { 0 }; CFIndex len = sizeof buf;
    if (IOHIDDeviceGetReport(dev, kIOHIDReportTypeInput, 0, buf, &len) != kIOReturnSuccess) { fprintf(stderr, "GET_REPORT a echoue\n"); return 1; }
    printf("%.*s\n", (int) len, buf);
    return 0;
  }
  if (strcmp(argv[1], "bench") == 0) {
    int n = argc > 2 ? atoi(argv[2]) : 10;
    char c = argc > 3 ? argv[3][0] : 'k';
    for (int i = 0; i < n; i++) {
      double t = now_epoch(), d;
      if (send_cmd(dev, c, &d)) { printf("write %.6f echec\n", t); continue; }
      printf("write %.6f rt=%.0f us\n", t, d);
      fflush(stdout);
      usleep(1000000);
    }
    return 0;
  }
  double d;
  if (send_cmd(dev, argv[1][0], &d)) { fprintf(stderr, "SET_REPORT a echoue\n"); return 1; }
  printf("cmd %c ok (%.0f us)\n", argv[1][0], d);
  return 0;
}
