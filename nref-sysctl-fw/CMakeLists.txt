cmake_minimum_required(VERSION 3.13)

include(lib/pico_sdk_import.cmake)

set(SHARED_FW_INC "${CMAKE_CURRENT_LIST_DIR}/../pocket-reform-shared-fw/inc")
set(SHARED_FW_SRC "${CMAKE_CURRENT_LIST_DIR}/../pocket-reform-shared-fw/src")
set(SCRIPTS "${CMAKE_CURRENT_LIST_DIR}/../scripts")

set(PROJECT sysctl)
project(${PROJECT})

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

add_executable(${PROJECT}
	src/sysctl.c
	src/fusb302b.c
	src/bq25792.c
	src/bq76922.c
	src/spi_com.c
	src/uart_com.c
	src/pd_com.c
	src/cli.c
	src/cli_usb.c
	src/machine_next.c
	src/next_charger.c
	src/next_command.c
	src/next_gpio.c
	src/next_mux.c
	src/next_pack.c
	src/next_rail.c
	src/next_led.c
	src/forward_uart.c
	src/usb_descriptors.c
	"${CMAKE_CURRENT_BINARY_DIR}/inc-generated/ds20-descriptor.h"
	"${SHARED_FW_SRC}/reset_interface.c"
	"${SHARED_FW_SRC}/reform_stdio_usb.c"
)

target_include_directories(${PROJECT} PRIVATE
	inc
	"${CMAKE_CURRENT_BINARY_DIR}/inc-generated"
	"${SHARED_FW_INC}"
)

target_link_libraries(${PROJECT}
	pico_stdlib
	hardware_i2c
	hardware_spi
	hardware_pwm
	pico_stdio
	pico_time
	pico_unique_id
	tinyusb_device
)

pico_enable_stdio_uart(${PROJECT} 0)
pico_add_extra_outputs(${PROJECT})

#target_compile_definitions(${PROJECT} PUBLIC PICO_RP2040_USB_DEVICE_ENUMERATION_FIX=1)
target_compile_options(${PROJECT} PRIVATE -Wall -Wextra -Werror)

if (DEFINED ENV{MNTRE_FIRMWARE_VERSION} AND (NOT MNTRE_FIRMWARE_VERSION))
	set(MNTRE_FIRMWARE_VERSION $ENV{MNTRE_FIRMWARE_VERSION})
    set(FWCAB_DEVELOPER_NAME "MNT Research GmbH")
    set(FWCAB_RELEASE_SOURCE_URL "https://source.mnt.re/reform/reform-next/-/tags/${MNTRE_FIRMWARE_VERSION}")
    set(FWCAB_FIRMWARE_DESCRIPTION "<p>MNT Research Reform Next System Controller Firmware Update (Release Build)</p>")
    set(FWCAB_RELEASE_DESCRIPTION_FILE "${CMAKE_CURRENT_LIST_DIR}/description-release.html")
    message("Using MNTRE_FIRMWARE_VERSION from environment ('${MNTRE_FIRMWARE_VERSION}')")
endif ()
if (NOT MNTRE_FIRMWARE_VERSION)
	string(TIMESTAMP TODAY "%m%d")
	set(MNTRE_FIRMWARE_VERSION "${TODAY}")
    set(FWCAB_DEVELOPER_NAME "J. Random Hacker")
    if (DEFINED ENV{CI_COMMIT_BRANCH} AND (NOT MNTRE_FIRMWARE_BRANCH))
      set(MNTRE_FIRMWARE_BRANCH $ENV{CI_COMMIT_BRANCH})
    else()
      set(MNTRE_FIRMWARE_BRANCH "FIXME")
    endif()
    set(FWCAB_RELEASE_SOURCE_URL "https://source.mnt.re/reform/reform-next/branch/${MNTRE_FIRMWARE_BRANCH}")
    set(FWCAB_FIRMWARE_DESCRIPTION "<p>Development build for testing</p>")
    set(FWCAB_RELEASE_DESCRIPTION_FILE "${CMAKE_CURRENT_LIST_DIR}/description-debug.html")
    message("Using MNTRE_FIRMWARE_VERSION ${MNTRE_FIRMWARE_VERSION} for debug build")
    message("Using FWCAB_RELEASE_SOURCE_URL ${FWCAB_RELEASE_SOURCE_URL} for debug build")
endif ()

target_compile_definitions(${PROJECT} PRIVATE -DMNTRE_FIRMWARE_VERSION="${MNTRE_FIRMWARE_VERSION}")

pico_set_program_version(${PROJECT} ${MNTRE_FIRMWARE_VERSION})
set(FWCAB_RELEASE_VERSION ${MNTRE_FIRMWARE_VERSION})
set(FWCAB_FIRMWARE_ID "com.mntre.reform-next.sysctl10.firmware")
set(FWCAB_FIRMWARE_NAME "Reform Next System Controller 1.0 Firmware")
set(FWCAB_SUMMARY "MNT Reform Next System Controller")
set(FWCAB_CATEGORY "X-EmbeddedController")
set(FWCAB_FIRMWARE_LICENSE "GPL-3.0-or-later")
set(FWCAB_RELEASE_VERSION_FORMAT "number")
set(FWCAB_DEVICE_GUID "df0c9fc8-88d6-5100-ba66-0f5c436fc522") # GUID(USB\VID_1209&PID_6D07)
set(FWCAB_DEVICE_INTEGRITY "unsigned")
set(FWCAB_UPDATE_PROTOCOL "com.microsoft.uf2")
set(FWCAB_HOMEPAGE "https://mntre.com")

add_custom_command(
	TARGET ${PROJECT}
	POST_BUILD
    VERBATIM
    COMMAND "${SCRIPTS}/firmware_packager.py" --bin "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT}.uf2" --out "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT}.cab" --firmware-id "${FWCAB_FIRMWARE_ID}" --firmware-name "${FWCAB_FIRMWARE_NAME}" --firmware-description "${FWCAB_FIRMWARE_DESCRIPTION}" --firmware-license "${FWCAB_FIRMWARE_LICENSE}" --device-guid "${FWCAB_DEVICE_GUID}" --device-integrity "${FWCAB_DEVICE_INTEGRITY}" --homepage "${FWCAB_HOMEPAGE}" --firmware-summary "${FWCAB_SUMMARY}" --category "${FWCAB_CATEGORY}" --version-format "${FWCAB_RELEASE_VERSION_FORMAT}" --release-version "${FWCAB_RELEASE_VERSION}" --release-description-file "${FWCAB_RELEASE_DESCRIPTION_FILE}" --release-source-url "${FWCAB_RELEASE_SOURCE_URL}" --developer-name "${FWCAB_DEVELOPER_NAME}" --update-protocol "${FWCAB_UPDATE_PROTOCOL}"
)

configure_file("ds20.quirk.tpl" "ds20.quirk" @ONLY)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/inc-generated/ds20-descriptor.h"
	COMMAND "${SCRIPTS}/generate-ds20.py" --output "${CMAKE_CURRENT_BINARY_DIR}/inc-generated/ds20-descriptor.h" "${CMAKE_CURRENT_BINARY_DIR}/ds20.quirk"
	DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/ds20.quirk"
)
