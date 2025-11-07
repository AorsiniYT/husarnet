#pragma once

#include "husarnet/ports/port.h"
#include "husarnet/layer_interfaces.h"

#include <lwip/netif.h>
#include <lwip/ip6.h>
#include <lwip/pbuf.h>
#include <vector>

class Tun : public UpperLayer {
public:
    Tun();
    ~Tun();
    void onLowerLayerData(HusarnetAddress source, string_view data) override;
    IpAddress getIp();
    
    // Called by LwIP netif output callback to queue outgoing packets
    void queueOutgoingPacket(struct pbuf* p);
    
private:
    ip6_addr_t ipAddr;                          // IPv6 address
    struct netif* netif;                        // LwIP network interface
    struct netconn* conn;                       // Raw netconn for packet handling
    void* tunTapMsgQueue;                       // Message queue for packet processing
    std::vector<std::vector<uint8_t>> outgoingPackets;  // Queue of outgoing packets
};