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

# Find libcurl and OpenSSL
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)

# Configure LwIP for PS Vita
set(LWIP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lwip")
set(LWIP_CONTRIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lwip-contrib")

# Add lwipopts.h directory BEFORE LwIP to ensure it finds our custom configuration
include_directories(BEFORE ${CMAKE_CURRENT_SOURCE_DIR})  # for lwipopts.h

# Add VitaSDK headers
include_directories(BEFORE ${VITASDK}/arm-vita-eabi/include)

# LwIP core sources
set(LWIP_CORE_SRC
    ${LWIP_DIR}/src/core/init.c
    ${LWIP_DIR}/src/core/def.c
    ${LWIP_DIR}/src/core/dns.c
    ${LWIP_DIR}/src/core/inet_chksum.c
    ${LWIP_DIR}/src/core/ip.c
    ${LWIP_DIR}/src/core/ipv4/icmp.c
    ${LWIP_DIR}/src/core/ipv4/igmp.c
    ${LWIP_DIR}/src/core/ipv4/ip4.c
    ${LWIP_DIR}/src/core/ipv4/ip4_addr.c
    ${LWIP_DIR}/src/core/ipv4/ip4_frag.c
    ${LWIP_DIR}/src/core/mem.c
    ${LWIP_DIR}/src/core/memp.c
    ${LWIP_DIR}/src/core/netif.c
    ${LWIP_DIR}/src/core/pbuf.c
    ${LWIP_DIR}/src/core/raw.c
    ${LWIP_DIR}/src/core/stats.c
    ${LWIP_DIR}/src/core/sys.c
    ${LWIP_DIR}/src/core/tcp.c
    ${LWIP_DIR}/src/core/tcp_in.c
    ${LWIP_DIR}/src/core/tcp_out.c
    ${LWIP_DIR}/src/core/timeouts.c
    ${LWIP_DIR}/src/core/udp.c
    ${LWIP_DIR}/src/core/ipv6/dhcp6.c
    ${LWIP_DIR}/src/core/ipv6/ethip6.c
    ${LWIP_DIR}/src/core/ipv6/icmp6.c
    ${LWIP_DIR}/src/core/ipv6/inet6.c
    ${LWIP_DIR}/src/core/ipv6/ip6.c
    ${LWIP_DIR}/src/core/ipv6/ip6_addr.c
    ${LWIP_DIR}/src/core/ipv6/ip6_frag.c
    ${LWIP_DIR}/src/core/ipv6/mld6.c
    ${LWIP_DIR}/src/core/ipv6/nd6.c
)

# LwIP netif sources (Ethernet interface and ARP)
set(LWIP_NETIF_SRC
    ${LWIP_DIR}/src/netif/ethernet.c
    ${LWIP_DIR}/src/core/ipv4/etharp.c
    ${LWIP_DIR}/src/netif/bridgeif.c
    ${LWIP_DIR}/src/netif/bridgeif_fdb.c
    ${LWIP_DIR}/src/netif/slipif.c
)

# LwIP API layer
set(LWIP_API_SRC
    ${LWIP_DIR}/src/api/api_lib.c
    ${LWIP_DIR}/src/api/api_msg.c
    ${LWIP_DIR}/src/api/err.c
    ${LWIP_DIR}/src/api/if_api.c
    ${LWIP_DIR}/src/api/netbuf.c
    ${LWIP_DIR}/src/api/netdb.c
    ${LWIP_DIR}/src/api/netifapi.c
    ${LWIP_DIR}/src/api/sockets.c
    ${LWIP_DIR}/src/api/tcpip.c
)

# Add LwIP as library
add_library(lwip STATIC ${LWIP_CORE_SRC} ${LWIP_NETIF_SRC} ${LWIP_API_SRC})

# Include paths for LwIP
include_directories(${LWIP_DIR}/src/include)
include_directories(${LWIP_CONTRIB_DIR}/ports/vita/port/include)

# LwIP PS Vita port sources
# NOTE: Excluding unistd.c because it calls lwip_read/write which are not needed for our use case
set(LWIP_PORT_SRC
    ${LWIP_CONTRIB_DIR}/ports/vita/port/sys_arch.c
    ${LWIP_CONTRIB_DIR}/ports/vita/port/strings.c
)

# Add port sources to LwIP library
target_sources(lwip PRIVATE ${LWIP_PORT_SRC})

# Include Husarnet core
include(../../core/husarnet.cmake)

# PS Vita specific sources
file(GLOB psvita_SRC "*.cpp" "*.c")

# Add PS Vita port sources for LwIP (sys_arch, socket wrapper, threading)
# Note: Select only the essential files for our use case
# NOTE: Do NOT include Debug.c here - we use the local debug.c instead
# NOTE: Do NOT include unistd.c here - it calls lwip_close/read/write which are not available
set(psvita_lwip_SRC 
    "${LWIP_CONTRIB_DIR}/ports/vita/port/sys_arch.c"
    "${LWIP_CONTRIB_DIR}/ports/vita/port/pthread.c"
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
    ${CURL_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    SceLibKernel_stub
    SceDisplay_stub
    SceCtrl_stub
    SceNetCtl_stub
    SceNet_stub
    SceHttp_stub
    SceSsl_stub
    SceCommonDialog_stub
    SceAppUtil_stub
    SceGxm_stub
    SceSysmodule_stub
    SceIofilemgr_stub
    SceKernelThreadMgr_stub
    SceVshBridge_stub
    SceSysmem_stub
    c
    m
    z
)

include_directories(${CMAKE_SOURCE_DIR}/core)
include_directories(${CMAKE_SOURCE_DIR}/core/husarnet/ports/psvita)

add_definitions(-DPSVITA_PLATFORM)
add_definitions(-D__PSVITA__)

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