cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(FoloToy-AI-Passport)

# ESP-IDF 5.5.3 Security 2 leaves a freed SRP handle in the session on error,
# causing a double free when BLE disconnects. Compile the pinned, minimally
# patched upstream file in the original component until the IDF fix lands.
idf_component_get_property(protocomm_lib protocomm COMPONENT_LIB)
get_target_property(protocomm_sources ${protocomm_lib} SOURCES)
list(FILTER protocomm_sources EXCLUDE REGEX "(^|/)src/security/security2\\.c$")
# Pin the upstream SRP implementation before substituting our short-salt fix.
set(srp_upstream "$ENV{IDF_PATH}/components/protocomm/src/crypto/srp6a/esp_srp.c")
file(SHA256 "${srp_upstream}" srp_upstream_sha256)
if(NOT srp_upstream_sha256 STREQUAL "060268ed7d670d90304c0c4d4515edd89521e30645316f7fb33d3a5169f5aa63")
    message(FATAL_ERROR "Unsupported ESP-IDF SRP implementation; audit the short-salt fix before building")
endif()
set(srp_mpi_upstream "$ENV{IDF_PATH}/components/protocomm/src/crypto/srp6a/esp_srp_mpi.c")
file(SHA256 "${srp_mpi_upstream}" srp_mpi_sha256)
if(NOT srp_mpi_sha256 STREQUAL "22fe3eef0418d3064874a6582699fb26bf961167092b814cc3f9b5f06da5e70d")
    message(FATAL_ERROR "Unsupported ESP-IDF SRP MPI implementation; audit the error cleanup before building")
endif()
list(FILTER protocomm_sources EXCLUDE REGEX "(^|/)src/crypto/srp6a/esp_srp_mpi\\.c$")
list(FILTER protocomm_sources EXCLUDE REGEX "(^|/)src/crypto/srp6a/esp_srp\\.c$")
list(APPEND protocomm_sources "${CMAKE_CURRENT_LIST_DIR}/main/meeting_security2_idf553.c"
                             "${CMAKE_CURRENT_LIST_DIR}/main/meeting_srp_idf553.c"
                             "${CMAKE_CURRENT_LIST_DIR}/main/meeting_srp_mpi_idf553.c")
target_include_directories(${protocomm_lib} PRIVATE "$ENV{IDF_PATH}/components/protocomm/src/crypto/srp6a")
set_property(TARGET ${protocomm_lib} PROPERTY SOURCES "${protocomm_sources}")
