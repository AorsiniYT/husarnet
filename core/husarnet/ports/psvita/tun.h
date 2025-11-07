#pragma once

#include "husarnet/ports/port.h"
#include "husarnet/layer_interfaces.h"

#include <lwip/netif.h>
#include <lwip/ip6.h>
#include <lwip/pbuf.h>
#include <lwip/api.h>
#include <vector>

class Tun : public UpperLayer {
public:
    Tun(ip6_addr_t ipAddr, size_t queueSize = 16);
    ~Tun();
    void onLowerLayerData(HusarnetAddress source, string_view data) override;
    IpAddress getIp();
    
    // Process queued packets and send to lower layer
    void processQueuedPackets();
    
    // Close and clean up resources
    void close();
    
    // Get IPv6 address
    ip6_addr_t getIp6Addr();
    
    // Public for LwIP callbacks
    void* tunTapMsgQueue;                       // Message queue for packets
    
private:
    ip6_addr_t ipAddr;                          // IPv6 address
    struct netif* netif;                        // LwIP network interface
    struct netconn* conn;                       // Raw netconn (unused, kept for compatibility)
};