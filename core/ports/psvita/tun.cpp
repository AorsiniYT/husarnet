// PS Vita TUN adapter implementation using LwIP
// Provides IPv6 virtual network interface for Husarnet

#include "husarnet/ports/psvita/tun.h"
#include "husarnet/ports/port.h"
#include "husarnet/logging.h"
#include "husarnet/util.h"

#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <cstring>
#include <cstdint>
#include <arpa/inet.h>
#include <lwip/netif.h>
#include <lwip/ip6.h>
#include <lwip/pbuf.h>
#include <lwip/api.h>
#include <lwip/raw.h>

Tun::Tun(ip6_addr_t ipAddr, size_t queueSize) : netif(nullptr), conn(nullptr), tunTapMsgQueue(nullptr) {
    LOG_DEBUG("Initializing Tun interface for PS Vita (stub)");
    
    // Clear outgoing packet queue
    outgoingPackets.clear();
    
    // Initialize IPv6 address (Husarnet local address)
    this->ipAddr = ipAddr;
    if (ip6_addr_isany_val(ipAddr)) {
        std::memset(&this->ipAddr, 0, sizeof(this->ipAddr));
        
        // Set fc94::1
        this->ipAddr.addr[0] = htonl(0xfc940000U);
        this->ipAddr.addr[1] = htonl(0x00000000U);
        this->ipAddr.addr[2] = htonl(0x00000000U);
        this->ipAddr.addr[3] = htonl(0x00000001U);
    }
    
    // Stub: no netconn for PS Vita
    LOG_DEBUG("Tun initialized with IPv6: fc94::1 (stub)");
}

Tun::~Tun() {
    // Clean up queued packets
    outgoingPackets.clear();
    LOG_DEBUG("Tun destroyed");
}

void Tun::onLowerLayerData(DeviceId source, string_view data) {
    // Inject received packet into LwIP stack
    // This would normally create a pbuf and call netif_input()
    // For now, log the reception
    LOG_DEBUG("Received %zu bytes from Husarnet lower layer", data.size());
}

IpAddress Tun::getIp() {
    // Return the IPv6 address used for this interface
    // Convert LwIP ip6_addr_t to Husarnet IpAddress
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

void Tun::queueOutgoingPacket(struct pbuf* p) {
    // Queue packet for transmission to lower layer
    if (!p) return;
    
    // Get the total length from pbuf
    uint32_t total_len = p->tot_len;
    if (total_len > 65536) {
        LOG_WARNING("Packet too large: %u bytes", total_len);
        return;
    }
    
    // For now, just queue the size
    // Full packet handling would copy pbuf data
    LOG_DEBUG("Queued outgoing packet: %u bytes", total_len);
    outgoingPackets.push_back(std::vector<uint8_t>(total_len, 0));
}

void Tun::processQueuedPackets() {
    // Process queued outgoing packets
    // This would send packets to the lower layer
    LOG_DEBUG("Processing %zu queued packets", outgoingPackets.size());
    outgoingPackets.clear();  // Clear for now
}