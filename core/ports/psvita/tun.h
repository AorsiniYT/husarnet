#pragma once

#include "husarnet/ports/port.h"
#include "husarnet/layer_interfaces.h"

#include <lwip/netif.h>
#include <lwip/ip6.h>
#include <lwip/pbuf.h>
#include <vector>

class Tun : public UpperLayer {
public:
    Tun(ip6_addr_t ipAddr = {}, size_t queueSize = 16);
    ~Tun();
    void onLowerLayerData(DeviceId source, string_view data) override;
    IpAddress getIp();
    ip6_addr_t getIp6Addr() { return ipAddr; }
    
    // Called by LwIP netif output callback to queue outgoing packets
    void queueOutgoingPacket(struct pbuf* p);
    
    // Process queued packets (for PS Vita)
    void processQueuedPackets();
    
    void* tunTapMsgQueue;                       // Message queue for packet processing
    
private:
    ip6_addr_t ipAddr;                          // IPv6 address
    struct netif* netif;                        // LwIP network interface
    struct netconn* conn;                       // Raw netconn for packet handling
    std::vector<std::vector<uint8_t>> outgoingPackets;  // Queue of outgoing packets
};