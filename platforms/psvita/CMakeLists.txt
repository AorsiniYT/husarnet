cmake_minimum_required(VERSION 3.16)

# PS Vita port for Husarnet
# Assumes VitaSDK is installed and VITASDK environment variable is set

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(husarnet_psvita)

# Flag so shared CMake logic can tweak configuration for PS Vita builds
set(PSVITA_PLATFORM ON CACHE BOOL "Enable PS Vita specific build options")

# This line adds Vita helper macros, must go after project definition
include("${VITASDK}/share/vita.cmake" REQUIRED)

# Configure LwIP for PS Vita
set(LWIP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lwip")
set(LWIP_CONTRIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lwip-contrib")

# Add lwipopts.h directory BEFORE LwIP to ensure it finds our custom configuration
include_directories(BEFORE ${CMAKE_CURRENT_SOURCE_DIR})  # for lwipopts.h

# Add LwIP as subdirectory (core stack)
add_subdirectory(${LWIP_DIR} ${CMAKE_CURRENT_BINARY_DIR}/lwip)

# Include paths for LwIP
include_directories(${LWIP_DIR}/src/include)
include_directories(${LWIP_CONTRIB_DIR}/ports/vita/port/include)

# Include Husarnet core
include(../../core/husarnet.cmake)

# PS Vita specific sources
file(GLOB psvita_SRC "*.cpp" "*.c")

# Add PS Vita port sources for LwIP (sys_arch, socket wrapper, threading)
# Note: Select only the essential files for our use case
set(psvita_lwip_SRC 
    "${LWIP_CONTRIB_DIR}/ports/vita/port/sys_arch.c"
    "${LWIP_CONTRIB_DIR}/ports/vita/port/pthread.c"
    "${LWIP_CONTRIB_DIR}/ports/vita/port/unistd.c"
    "${LWIP_CONTRIB_DIR}/ports/vita/port/errno.c"
    "${LWIP_CONTRIB_DIR}/ports/vita/port/strings.c"
)

add_executable(husarnet-psvita ${psvita_SRC} ${psvita_lwip_SRC})
target_link_libraries(husarnet-psvita husarnet_core lwip)

# Disable strict compiler flags for PS Vita to handle incompatibilities with VitaSDK C++ library
target_compile_options(husarnet-psvita PRIVATE 
    -UDEBUG -U_DEBUG 
    -Wno-return-type 
    -Wno-error=return-type 
    -Wno-error=unused-variable
    -Wno-error
)

# Add compilation flags to suppress nlohmann_json errors
target_compile_options(husarnet-psvita PRIVATE -Wno-error=unused-variable)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu11")
# VitaSDK libraries (adjust as needed)
target_link_libraries(husarnet-psvita
    SceLibKernel_stub
    SceDisplay_stub
    SceCtrl_stub
    SceNetCtl_stub
    SceCommonDialog_stub
    SceAppUtil_stub
    SceGxm_stub
    SceSysmodule_stub
    SceIofilemgr_stub
)

include_directories(${CMAKE_SOURCE_DIR}/core)
include_directories(${CMAKE_SOURCE_DIR}/core/husarnet/ports/psvita)

add_definitions(-DPSVITA_PLATFORM)
add_definitions(-D__PSVITA__)

# Read join.txt if exists
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/join.txt")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/join.txt" JOIN_DATA)
    string(REGEX MATCH "id:([^/]+)/(.+)" _ ${JOIN_DATA})
    set(NETWORK_ID ${CMAKE_MATCH_1})
    set(SECRET ${CMAKE_MATCH_2})
    string(REGEX MATCH "name:([^\n]+)" _ ${JOIN_DATA})
    set(HOSTNAME ${CMAKE_MATCH_1})
    add_definitions(-DHUSARNET_NETWORK_ID="${NETWORK_ID}")
    add_definitions(-DHUSARNET_SECRET="${SECRET}")
    add_definitions(-DHUSARNET_HOSTNAME="${HOSTNAME}")
endif()

# Ensure PSVITA_PLATFORM is available as compile definition for all targets
add_compile_definitions(PSVITA_PLATFORM=1)

set(BUILD_HTTP_CONTROL_API ON)

## Configuration options for this app
# Display name (under bubble in LiveArea)
set(VITA_APP_NAME "Husarnet PS Vita")
# Unique ID must be exactly 9 characters.
set(VITA_TITLEID  "HUSARNPSV")
# Optional version string to show in LiveArea's more info screen
set(VITA_VERSION  "01.00")

## Create Vita files
vita_create_self(husarnet-psvita.self husarnet-psvita)
vita_create_vpk(husarnet-psvita.vpk ${VITA_TITLEID} husarnet-psvita.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
)