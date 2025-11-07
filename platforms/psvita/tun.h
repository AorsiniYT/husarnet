#pragma once

#include "husarnet/ports/port.h"
#include "husarnet/husarnet.h"
#include "husarnet/layer_interfaces.h"
#include "husarnet/ipaddress.h"

#include "lwip/netif.h"
#include "lwip/ip6_addr.h"
#include "lwip/raw.h"

class Tun : public UpperLayer {
public:
    // Constructor with IPv6 address and queue size for pbuf packets
    Tun(ip6_addr_t ipAddr, size_t queueSize = 16);
    ~Tun();
    void onLowerLayerData(HusarnetAddress source, string_view data) override;
    void processQueuedPackets();
    ip6_addr_t getIp6Addr();
    
    // Queue handle for pbuf packets
    void* tunTapMsgQueue;
    
private:
    void close();
    ip6_addr_t ipAddr;
    struct netconn* conn;
};