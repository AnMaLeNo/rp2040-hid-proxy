cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)
project(proxy_souris C CXX ASM)
pico_sdk_init()

set(PICO_PIO_USB_DIR "${CMAKE_CURRENT_LIST_DIR}/../Pico-PIO-USB")
add_subdirectory(${PICO_PIO_USB_DIR} pico_pio_usb)

foreach(variant proxy_souris proxy_souris_dmdp proxy_souris_debug proxy_souris_dmdp_debug proxy_souris_dev proxy_souris_dev_late proxy_souris_dev_plainfirst proxy_souris_dev_hostlog)
  add_executable(${variant} main.c
    ${PICO_TINYUSB_PATH}/src/portable/raspberrypi/pio_usb/dcd_pio_usb.c
    ${PICO_TINYUSB_PATH}/src/portable/raspberrypi/pio_usb/hcd_pio_usb.c)
  target_include_directories(${variant} PRIVATE ${CMAKE_CURRENT_LIST_DIR})
  target_compile_definitions(${variant} PRIVATE PIO_USB_USE_TINYUSB)
  target_compile_options(${variant} PRIVATE -Wall -Wextra)
  target_link_options(${variant} PRIVATE -Xlinker --print-memory-usage)
  target_link_libraries(${variant} PRIVATE pico_stdlib pico_multicore pico_pio_usb tinyusb_device tinyusb_host)
  pico_add_extra_outputs(${variant})
endforeach()
# variante D+/D- echanges (D- sur GP0, D+ sur GP1) pour un cablage inverse
target_compile_definitions(proxy_souris_dmdp PRIVATE SWAP_DPDM)
# variante diagnostic : port serie + journal, la souris n'est pas exposee au PC
target_compile_definitions(proxy_souris_debug PRIVATE DEBUG_CDC)
target_compile_definitions(proxy_souris_dmdp_debug PRIVATE SWAP_DPDM DEBUG_CDC)
# variante developpement : souris clonee + port serie ajoute (journal, reflash par 'B'), non transparente
target_compile_definitions(proxy_souris_dev PRIVATE DEV_CDC)
target_compile_definitions(proxy_souris_dev_late PRIVATE DEV_CDC DEV_LATE)
target_compile_definitions(proxy_souris_dev_plainfirst PRIVATE DEV_CDC DEV_LATE DEV_PLAIN_FIRST)
target_compile_definitions(proxy_souris_dev_hostlog PRIVATE DEV_CDC DEV_HOST_LOG)
