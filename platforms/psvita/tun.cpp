// Copyright (c) 2025 Husarnet sp. z o.o.
// Authors: listed in project_root/README.md
// License: specified in project_root/LICENSE.txt

// Husarnet TUN adapter implementation for PS Vita using LwIP
//
// This implementation provides a virtual IPv6 network interface on PS Vita
// by using LwIP (embedded TCP/IP stack). Unlike ESP32, PS Vita doesn't have
// ESP-NETIF, so we work directly with LwIP's netif API.
//
// The design mirrors ESP32's approach:
// 1. LwIP netif initialized with IPv6 address (fc94::/16)
// 2. lwip_hook_ip6_route catches fc94::/16 traffic and routes to Husarnet
// 3. Incoming packets from Husarnet are injected via raw netconn API
// 4. Outgoing packets from LwIP are queued and sent to Husarnet lower layer

#include "husarnet/ports/psvita/tun.h"

#include "husarnet/ports/port.h"
#include "husarnet/logging.h"
#include "husarnet/util.h"

#include "lwip/api.h"
#include "lwip/err.h"
#include "lwip/ip.h"
#include "lwip/ip6.h"
#include "lwip/ip6_addr.h"
#include "lwip/netdb.h"
#include "lwip/netif.h"
#include "lwip/sys.h"

// --- LwIP layer ---

extern "C" {
err_t husarnet_netif_init(struct netif* netif);
err_t husarnet_netif_output(struct netif* netif, struct pbuf* p, const ip6_addr_t* ipaddr);
}

static struct netif* husarnet_netif = NULL;
static Tun* g_tun_instance = NULL;

err_t husarnet_netif_init(struct netif* netif)
{
  netif->name[0] = 'h';
  netif->name[1] = 'n';

  // Set MAC address (fake, just for initialization)
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
  netif->output_ip6 = husarnet_netif_output;

  // Checksum offloading
  NETIF_SET_CHECKSUM_CTRL(netif, NETIF_CHECKSUM_ENABLE_ALL);

  LOG_INFO("Husarnet TUN interface initialized");

  return ERR_OK;
}

err_t husarnet_netif_output(struct netif* netif, struct pbuf* p, const ip6_addr_t* ipaddr)
{
  if (p->next != NULL) {
    LOG_ERROR("Packet chain is not supported");
    return ERR_ARG;
  }

  // Queue packet to be processed by Husarnet lower layer
  if (g_tun_instance && g_tun_instance->tunTapMsgQueue) {
    if (sys_mbox_trypost((sys_mbox_t*)g_tun_instance->tunTapMsgQueue, p) != ERR_OK) {
      LOG_ERROR("Failed to send packet to Husarnet stack");
      return ERR_WOULDBLOCK;
    }

    // Increase packet's refcounter to prevent premature free
    pbuf_ref(p);
    return ERR_OK;
  }

  return ERR_IF;
}

// External LwIP hook to route packets to the Husarnet interface
extern "C" struct netif* lwip_hook_ip6_route(const ip6_addr_t* src, const ip6_addr_t* dest)
{
  // Check if destination is Husarnet address (fc94::/16)
  if (husarnet_netif && PP_NTOHL(dest->addr[0]) >> 16 == 0xFC94) {
    return husarnet_netif;
  }

  return NULL;
}

// --- Tun layer ---

Tun::Tun(ip6_addr_t ipAddr, size_t queueSize) : ipAddr(ipAddr), conn(NULL)
{
  g_tun_instance = this;

  // Create message queue for pbuf packets using LwIP sys_arch
  // This will be managed by our threading layer from lwip-contrib
  tunTapMsgQueue = (void*)malloc(sizeof(sys_mbox_t));
  if (sys_mbox_new((sys_mbox_t*)tunTapMsgQueue, queueSize) != ERR_OK) {
    LOG_ERROR("Failed to create message queue");
    abort();
  }

  // Create and setup Husarnet network interface
  husarnet_netif = netif_add(NULL, NULL, NULL, NULL, NULL, husarnet_netif_init, ip_input);
  if (husarnet_netif == NULL) {
    LOG_ERROR("Failed to create Husarnet netif");
    abort();
  }

  // Set as default (primary) interface
  netif_set_default(husarnet_netif);
  netif_set_up(husarnet_netif);

  // Add IPv6 address
  if (netif_add_ip6_address(husarnet_netif, &this->ipAddr, NULL) < 0) {
    LOG_ERROR("Failed to add IPv6 address");
    abort();
  }

  LOG_INFO("Added IPv6 address to Husarnet interface");

  // Create raw netconn for sending packets with custom headers
  this->conn = netconn_new(NETCONN_RAW_IPV6);
  if (this->conn == NULL) {
    LOG_ERROR("Failed to create netconn");
    abort();
  }

  // Bind to our Husarnet address
  ip_addr_t ipSrc = IPADDR6_INIT(0, 0, 0, 0);
  memcpy(ipSrc.u_addr.ip6.addr, this->getIp6Addr().addr, 16);
  
  if (netconn_bind(this->conn, &ipSrc, 0) != ERR_OK) {
    LOG_ERROR("Failed to bind netconn");
    abort();
  }

  // Set raw mode with IP headers included
  this->conn->pcb.raw->flags = RAW_FLAGS_HDRINCL;

  LOG_INFO("Husarnet Tun initialized with IPv6 stack");
}

Tun::~Tun()
{
  close();
}

void Tun::onLowerLayerData(HusarnetAddress source, string_view data)
{
  // Input packet should contain IPv6 header (at least 40 bytes)
  if (data.size() < 40) {
    LOG_ERROR("Packet too short");
    return;
  }

  // Create netbuf to wrap the packet data
  struct netbuf* buf = netbuf_new();
  if (buf == NULL) {
    LOG_ERROR("Failed to allocate netbuf");
    return;
  }

  // Reference the data (don't copy)
  if (netbuf_ref(buf, data.data(), data.size()) != ERR_OK) {
    LOG_ERROR("Failed to reference packet data in netbuf");
    netbuf_delete(buf);
    return;
  }

  // Extract destination address from IPv6 header (at offset 24)
  ip_addr_t ipDest = IPADDR6_INIT(0, 0, 0, 0);
  memcpy(ipDest.u_addr.ip6.addr, data.data() + 24, 16);

  // Send packet through raw netconn (which injects it into LwIP stack)
  if (netconn_sendto(this->conn, buf, &ipDest, 0) != ERR_OK) {
    LOG_ERROR("Failed to send packet to LwIP stack");
    netbuf_delete(buf);
    return;
  }

  // netbuf_delete will be called by netconn internals
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
      // Send packet to Husarnet lower layer
      string_view packet((char*)pbuf_p->payload, pbuf_p->len);
      sendToLowerLayer(IpAddress(), packet);

      pbuf_free(pbuf_p);
    }
  }
}

void Tun::close()
{
  if (this->conn != NULL) {
    netconn_delete(this->conn);
    this->conn = NULL;
  }

  if (husarnet_netif != NULL) {
    netif_remove(husarnet_netif);
    husarnet_netif = NULL;
  }

  if (tunTapMsgQueue != NULL) {
    sys_mbox_free((sys_mbox_t*)tunTapMsgQueue);
    free(tunTapMsgQueue);
    tunTapMsgQueue = NULL;
  }

  g_tun_instance = NULL;
}

ip6_addr_t Tun::getIp6Addr()
{
  return ipAddr;
}