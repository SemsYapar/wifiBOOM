cmake_minimum_required(VERSION 3.16)

# Only ESP32 is supported by this patched archive
if(DEFINED IDF_TARGET AND NOT IDF_TARGET STREQUAL "esp32")
    message(FATAL_ERROR
        "wifiBOOM custom libnet80211.a supports only ESP32"
    )
endif()

set(ORIGINAL_LIB
    "$ENV{IDF_PATH}/components/esp_wifi/lib/esp32/libnet80211.a"
)

set(BACKUP_LIB
    "$ENV{IDF_PATH}/components/esp_wifi/lib/esp32/libnet80211.a.original"
)

set(CUSTOM_LIB
    "${CMAKE_CURRENT_SOURCE_DIR}/main/libnet80211.a"
)

if(NOT EXISTS "${ORIGINAL_LIB}")
    message(FATAL_ERROR "Original libnet80211.a not found")
endif()

if(NOT EXISTS "${CUSTOM_LIB}")
    message(FATAL_ERROR "Custom libnet80211.a not found")
endif()

# Backup the original library only once
if(NOT EXISTS "${BACKUP_LIB}")
    file(COPY_FILE
        "${ORIGINAL_LIB}"
        "${BACKUP_LIB}"
        RESULT backup_result
    )

    if(NOT backup_result STREQUAL "0")
        message(FATAL_ERROR
            "Failed to back up libnet80211.a: ${backup_result}"
        )
    endif()

    message(STATUS "Original libnet80211.a backed up")
endif()

# Replace the library with wifiBOOM's version
file(COPY_FILE
    "${CUSTOM_LIB}"
    "${ORIGINAL_LIB}"
    ONLY_IF_DIFFERENT
    RESULT copy_result
)

if(NOT copy_result STREQUAL "0")
    message(FATAL_ERROR
        "Failed to install custom libnet80211.a: ${copy_result}"
    )
endif()

message(STATUS "wifiBOOM custom libnet80211.a installed")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(wifiBOOM-main)
