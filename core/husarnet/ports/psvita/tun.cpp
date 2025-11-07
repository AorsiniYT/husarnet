// Copyright (c) 2025 Husarnet sp. z o.o.
// Authors: listed in project_root/README.md
// License: specified in project_root/LICENSE.txt

// Husarnet TUN adapter implementation for PS Vita using LwIP
// Simplified version using raw socket interface only

#include "husarnet/ports/psvita/tun.h"

#include "husarnet/ports/port.h"
#include "husarnet/logging.h"
#include "husarnet/util.h"

#include "lwip/err.h"
#include "lwip/ip.h"
#include "lwip/ip6.h"
#include "lwip/ip6_addr.h"
#include "lwip/netif.h"
#include "lwip/sys.h"
#include "lwip/pbuf.h"

#include <cstring>

// --- LwIP netif layer ---

static struct netif* husarnet_netif = NULL;
static Tun* g_tun_instance = NULL;

extern "C" {
err_t husarnet_netif_init(struct netif* netif)
{
  netif->name[0] = 'h';
  netif->name[1] = 'n';

  // Set MAC address (fake)
  netif->hwaddr_len = 6;
  netif->hwaddr[0] = 0xFC;
  netif->hwaddr[1] = 0x94;
  netif->hwaddr[2] = 0x00;
  netif->hwaddr[3] = 0x00;
  netif->hwaddr[4] = 0x00;
  netif->hwaddr[5] = 0x01;

  // Create IPv6 link-local address
  netif_create_ip6_linklocal_address(netif, 1);

  // Set MTU
  netif->mtu = 1500;

  // Set packet output function
  netif->output_ip6 = NULL;  // Will be set later

  // Checksum offloading
  NETIF_SET_CHECKSUM_CTRL(netif, NETIF_CHECKSUM_ENABLE_ALL);

  LOG_INFO("Husarnet netif initialized");

  return ERR_OK;
}

err_t husarnet_netif_output(struct netif* netif, struct pbuf* p, const ip6_addr_t* ipaddr)
{
  if (!g_tun_instance) {
    return ERR_IF;
  }

  if (p->next != NULL) {
    LOG_ERROR("Packet chain not supported");
    return ERR_ARG;
  }

  // Queue packet for transmission to lower layer
  if (sys_mbox_trypost((sys_mbox_t*)g_tun_instance->tunTapMsgQueue, p) != ERR_OK) {
    LOG_ERROR("Failed to queue outgoing packet");
    return ERR_WOULDBLOCK;
  }

  pbuf_ref(p);
  return ERR_OK;
}

// Hook for IPv6 routing to Husarnet interface
extern "C" struct netif* lwip_hook_ip6_route(const ip6_addr_t* src, const ip6_addr_t* dest)
{
  if (husarnet_netif && PP_NTOHL(dest->addr[0]) >> 16 == 0xFC94) {
    return husarnet_netif;
  }
  return NULL;
}
}

// --- Tun class implementation ---

Tun::Tun(ip6_addr_t ipAddr, size_t queueSize) : ipAddr(ipAddr), netif(nullptr), conn(nullptr), tunTapMsgQueue(nullptr)
{
  g_tun_instance = this;

  // Create message queue for packet processing
  tunTapMsgQueue = (void*)malloc(sizeof(sys_mbox_t));
  if (sys_mbox_new((sys_mbox_t*)tunTapMsgQueue, queueSize) != ERR_OK) {
    LOG_ERROR("Failed to create message queue");
    abort();
  }

  // Create netif structure
  static struct netif s_husarnet_netif;
  husarnet_netif = &s_husarnet_netif;
  
  // Add network interface with dummy IPv4
  ip4_addr_t ipaddr4, netmask4, gw4;
  IP4_ADDR(&ipaddr4, 0, 0, 0, 0);
  IP4_ADDR(&netmask4, 0, 0, 0, 0);
  IP4_ADDR(&gw4, 0, 0, 0, 0);
  
  if (netif_add(husarnet_netif, &ipaddr4, &netmask4, &gw4, NULL, husarnet_netif_init, netif_input) == NULL) {
    LOG_ERROR("Failed to create netif");
    abort();
  }
  
  this->netif = husarnet_netif;

  // Set as default interface
  netif_set_default(husarnet_netif);
  netif_set_up(husarnet_netif);

  // Add IPv6 address
  if (netif_add_ip6_address(husarnet_netif, &this->ipAddr, NULL) < 0) {
    LOG_ERROR("Failed to add IPv6 address");
    abort();
  }

  // Set output function now that netif is created
  husarnet_netif->output_ip6 = husarnet_netif_output;

  LOG_INFO("Husarnet Tun initialized with IPv6");
}

Tun::~Tun()
{
  this->close();
}

void Tun::onLowerLayerData(HusarnetAddress source, string_view data)
{
  if (!husarnet_netif || data.size() < 40) {
    LOG_ERROR("Invalid packet or interface");
    return;
  }

  // Create pbuf to inject packet into LwIP
  struct pbuf* p = pbuf_alloc(PBUF_RAW, data.size(), PBUF_POOL);
  if (!p) {
    LOG_ERROR("Failed to allocate pbuf");
    return;
  }

  memcpy(p->payload, data.data(), data.size());

  // Inject into LwIP stack
  if (husarnet_netif->input(p, husarnet_netif) != ERR_OK) {
    LOG_ERROR("Failed to input packet to LwIP");
    pbuf_free(p);
  }
}

void Tun::processQueuedPackets()
{
  void* p = NULL;
  
  if (!tunTapMsgQueue) {
    return;
  }

  // Process all queued pbuf packets
  while (sys_mbox_tryfetch((sys_mbox_t*)tunTapMsgQueue, &p) == ERR_OK) {
    struct pbuf* pbuf_p = (struct pbuf*)p;
    
    if (pbuf_p) {
      // Send to lower layer
      string_view packet((char*)pbuf_p->payload, pbuf_p->len);
      sendToLowerLayer(IpAddress(), packet);
      pbuf_free(pbuf_p);
    }
  }
}

void Tun::close()
{
  if (husarnet_netif) {
    netif_remove(husarnet_netif);
    husarnet_netif = NULL;
  }

  if (tunTapMsgQueue) {
    sys_mbox_free((sys_mbox_t*)tunTapMsgQueue);
    free(tunTapMsgQueue);
    tunTapMsgQueue = NULL;
  }

  g_tun_instance = NULL;
}

IpAddress Tun::getIp()
{
  uint8_t binary[16];
  for (int i = 0; i < 4; i++) {
    uint32_t word = ipAddr.addr[i];
    binary[i*4] = (word >> 24) & 0xFF;
    binary[i*4+1] = (word >> 16) & 0xFF;
    binary[i*4+2] = (word >> 8) & 0xFF;
    binary[i*4+3] = word & 0xFF;
  }
  return IpAddress::fromBinary((const char*)binary);
}

ip6_addr_t Tun::getIp6Addr()
{
  return ipAddr;
}
