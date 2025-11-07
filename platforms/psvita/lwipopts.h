#ifndef __LWIPOPTS_H__
#define __LWIPOPTS_H__

/* ===== IPv6 Core Settings ===== */
#define LWIP_IPV6                       1
#define LWIP_IPV6_AUTOCONFIG            0  /* No router advertisements */
#define LWIP_IPV6_DHCP6                 0  /* No DHCPv6 */

/* ===== IPv4 (minimal support) ===== */
#define LWIP_IPV4                       1
#define LWIP_DHCP                       0

/* ===== Socket API ===== */
#define LWIP_SOCKET                     0  /* We don't use POSIX sockets directly */
#define LWIP_NETCONN                    0  /* No netconn API */

/* ===== UDP (for Husarnet base connection) ===== */
#define LWIP_UDP                        1
#define UDP_TTL                         255

/* ===== TCP (optional, used by control plane) ===== */
#define LWIP_TCP                        1
#define TCP_TTL                         255
#define LWIP_TCP_KEEPALIVE              1

/* ===== Raw IP (for custom protocol handling) ===== */
#define LWIP_RAW                        1

/* ===== Network Interface ===== */
#define LWIP_NETIF_LOOPBACK             0
#define LWIP_LOOPIF_MULTICAST           0
#define LWIP_IGMP                       0
#define LWIP_ICMP6                      1

/* ===== Memory & Buffers (tuned for embedded) ===== */
#define MEM_SIZE                        (64 * 1024)     /* 64KB heap */
#define MEMP_NUM_PBUF                   16
#define MEMP_NUM_UDP_PCB                4
#define MEMP_NUM_TCP_PCB                4
#define MEMP_NUM_TCP_PCB_LISTEN         2
#define PBUF_POOL_SIZE                  16
#define PBUF_POOL_BUFSIZE               1500

/* ===== Timeouts & Periodic Tasks ===== */
#define LWIP_TIMERS                     1
#define SYS_LIGHTWEIGHT_PROT            1

/* ===== Debugging (disable for production) ===== */
#define LWIP_DEBUG                      0
#define LWIP_STATS                      0
#define LWIP_STATS_DISPLAY              0

/* ===== Misc ===== */
#define LWIP_SINGLE_NETIF               1  /* Single interface (Husarnet TUN) */
#define LWIP_NETIF_TX_SINGLE_PBUF       0
#define LWIP_NETIF_HOSTNAME             1
#define LWIP_NETIF_STATUS_CALLBACK      1

#endif /* __LWIPOPTS_H__ */
