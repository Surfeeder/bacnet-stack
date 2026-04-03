/**************************************************************************
 *
 * Copyright (C) 2012 Steve Karg
 *
 * SPDX-License-Identifier: GPL-2.0-or-later WITH GCC-exception-2.0
 *
 *********************************************************************/

/*
 * BACnet/IP datalink using the lwIP raw API.
 *
 * Adapted from ports/lwip/bip.c for ESP32 (ESP-IDF + FreeRTOS).
 * The only ESP32-specific additions are LOCK_TCPIP_CORE() /
 * UNLOCK_TCPIP_CORE() guards around every call into the lwIP raw API that
 * may originate from a task other than the lwIP task (e.g. the BACnet
 * application task calling bip_init() or bip_send_mpdu()).
 *
 * Reception is callback-driven: bip_server_callback() is invoked by the
 * lwIP task and directly calls npdu_handler(), which is the same pattern
 * used in ports/lwip.
 */

#include <stdint.h> /* for standard integer types uint8_t etc. */
#include <stdbool.h> /* for the standard bool type. */
#include "bacnet/bacdef.h"
#include "bacnet/bacdcode.h"
#include "bacnet/bacint.h"
#include "bacnet/datalink/bip.h"
#include "bacnet/datalink/bvlc.h"
#include "bacnet/basic/services.h"
#include "bacnet/basic/bbmd/h_bbmd.h"
#include "../bacport.h" /* custom per port */

/** @file bip.c  Configuration and Operations for BACnet/IP */

/* port to use - stored in network byte order */
static bool BIP_Port_Changed;
/* IP Address */
static BACNET_IP_ADDRESS BIP_Address;
/* Broadcast Address */
static BACNET_IP_ADDRESS BIP_Broadcast_Address;
/* lwIP UDP control block */
static struct udp_pcb *Server_upcb;
/* track packets for diagnostics */
struct bacnet_stats {
    uint32_t xmit; /* Transmitted packets. */
    uint32_t recv; /* Received packets. */
    uint32_t drop; /* Dropped packets. */
};
struct bacnet_stats BIP_Stats;
#define BIP_STATS_INC(x) ++BIP_Stats.x

/**
 * @brief Get the BACnet/IP transmit stats
 * @return number of BACnet/IP transmitted packets
 */
uint32_t bip_stats_xmit(void)
{
    return BIP_Stats.xmit;
}

/**
 * @brief Get the BACnet/IP received stats
 * @return number of BACnet/IP received packets
 */
uint32_t bip_stats_recv(void)
{
    return BIP_Stats.recv;
}

/**
 * @brief Get the BACnet/IP drop stats
 * @return number of BACnet/IP drops
 */
uint32_t bip_stats_drop(void)
{
    return BIP_Stats.drop;
}

/**
 * @brief Set the BACnet/IP address
 * @param addr - network IPv4 address
 * @return true if the address is set
 */
bool bip_set_addr(const BACNET_IP_ADDRESS *addr)
{
    return bvlc_address_copy(&BIP_Address, addr);
}

/**
 * @brief Get the BACnet/IP address
 * @param network IPv4 address
 * @return true if the address is set
 */
bool bip_get_addr(BACNET_IP_ADDRESS *addr)
{
    return bvlc_address_copy(addr, &BIP_Address);
}

/**
 * @brief Set the BACnet/IP broadcast address
 * @param network IPv4 broadcast address
 * @return true if the broadcast address is retrieved
 */
bool bip_set_broadcast_addr(const BACNET_IP_ADDRESS *addr)
{
    return bvlc_address_copy(&BIP_Broadcast_Address, addr);
}

/**
 * @brief Get the BACnet/IP broadcast address
 * @param network IPv4 broadcast address
 * @return true if the broadcast address is set
 */
bool bip_get_broadcast_addr(BACNET_IP_ADDRESS *addr)
{
    return bvlc_address_copy(addr, &BIP_Broadcast_Address);
}

/**
 * @brief Set the BACnet IPv4 UDP port number
 * @param port - IPv4 UDP port number - in host byte order
 */
void bip_set_port(uint16_t port)
{
    if (BIP_Address.port != htons(port)) {
        BIP_Port_Changed = true;
        BIP_Address.port = htons(port);
    }
}

/**
 * @brief Determine if the BACnet IPv4 UDP port number changed
 * @return true of the BACnet IPv4 UDP port number changed
 */
bool bip_port_changed(void)
{
    return BIP_Port_Changed;
}

/**
 * @brief Get the BACnet IPv4 UDP port number
 * @return IPv4 UDP port number - in host byte order
 */
uint16_t bip_get_port(void)
{
    return ntohs(BIP_Address.port);
}

/**
 * @brief Convert the BACnet IPv4 address
 * @param address - IPv4 address from LwIP
 * @param mac - IP address from BACnet/IP
 */
static void bip_mac_to_addr(ip4_addr_t *address, const uint8_t *mac)
{
    if (mac && address) {
        address->addr = ((u32_t)((((uint32_t)mac[0]) << 24) & 0xff000000));
        address->addr |= ((u32_t)((((uint32_t)mac[1]) << 16) & 0x00ff0000));
        address->addr |= ((u32_t)((((uint32_t)mac[2]) << 8) & 0x0000ff00));
        address->addr |= ((u32_t)(((uint32_t)mac[3]) & 0x000000ff));
    }
}

/**
 * @brief Convert from a BACnet/IP address
 * @param baddr - BACnet/IP address
 * @param address - IPv4 address from LwIP
 * @param port - IPv4 UDP port number
 */
static int bip_decode_bip_address(
    const BACNET_IP_ADDRESS *baddr, ip_addr_t *address, uint16_t *port)
{
    int len = 0;

    if (baddr && address && port) {
        address->type = IPADDR_TYPE_V4;
        bip_mac_to_addr(&address->u_addr.ip4, &baddr->address[0]);
        *port = baddr->port;
        len = 6;
    }

    return len;
}

/**
 * @brief Convert the BACnet IPv4 address
 * @param mac - IP address from BACnet/IP
 * @param address - IPv4 address from LwIP
 */
static void bip_addr_to_mac(uint8_t *mac, const ip4_addr_t *address)
{
    if (mac && address) {
        mac[0] = (uint8_t)(address->addr >> 24);
        mac[1] = (uint8_t)(address->addr >> 16);
        mac[2] = (uint8_t)(address->addr >> 8);
        mac[3] = (uint8_t)(address->addr);
    }
}

/**
 * @brief Convert to a BACnet/IP address
 * @param baddr - BACnet/IP address
 * @param address - IPv4 address from LwIP
 * @param port - IPv4 UDP port number
 */
static int bip_encode_bip_address(
    BACNET_IP_ADDRESS *baddr, const ip_addr_t *address, uint16_t port)
{
    int len = 0;

    if (baddr && address) {
        if (address->type == IPADDR_TYPE_V4) {
            bip_addr_to_mac(&baddr->address[0], &address->u_addr.ip4);
            baddr->port = port;
            len = 6;
        }
    }

    return len;
}

/** Function to send a packet out the BACnet/IP socket (Annex J).
 * @ingroup DLBIP
 *
 * @param dest [in] Destination address and port
 * @param mtu [in] packet data
 * @param mtu_len [in] packet length
 * @return number of bytes sent, or 0 on failure.
 *
 * @note LOCK_TCPIP_CORE() / UNLOCK_TCPIP_CORE() are recursive on ESP-IDF so
 *       this is safe to call from both the lwIP task (inside the receive
 *       callback) and from any other FreeRTOS task.
 */
int bip_send_mpdu(
    const BACNET_IP_ADDRESS *dest, const uint8_t *mtu, uint16_t mtu_len)
{
    struct pbuf *pkt = NULL;
    ip_addr_t dst_ip;
    uint16_t port = 0;
    err_t status = ERR_OK;

    LOCK_TCPIP_CORE();
    pkt = pbuf_alloc(PBUF_TRANSPORT, mtu_len, PBUF_POOL);
    if (pkt == NULL) {
        UNLOCK_TCPIP_CORE();
        return 0;
    }
    bip_decode_bip_address(dest, &dst_ip, &port);
    pbuf_take(pkt, mtu, mtu_len);
    status = udp_sendto(Server_upcb, pkt, &dst_ip, port);
    pbuf_free(pkt);
    UNLOCK_TCPIP_CORE();

    if (status == ERR_OK) {
        BIP_STATS_INC(xmit);
    } else {
        mtu_len = 0;
    }

    return mtu_len;
}

/** Send the Original Broadcast or Unicast messages
 *
 * @param dest [in] Destination address (may encode an IP address and port #).
 * @param npdu_data [in] The NPDU header (Network) information (not used).
 * @param pdu [in] Buffer of data to be sent - may be null (why?).
 * @param pdu_len [in] Number of bytes in the pdu buffer.
 *
 * @return number of bytes sent
 */
int bip_send_pdu(
    BACNET_ADDRESS *dest, /* destination address */
    BACNET_NPDU_DATA *npdu_data, /* network information */
    uint8_t *pdu, /* any data to be sent - may be null */
    unsigned pdu_len)
{
    return bvlc_send_pdu(dest, npdu_data, pdu, pdu_len);
}

/** lwIP BACnet service callback — called by the lwIP task on packet arrival.
 *
 * @param arg [in] optional argument from service
 * @param upcb [in] UDP control block
 * @param pkt [in] UDP packet - PBUF
 * @param addr [in] UDP source address
 * @param port [in] UDP port number
 */
void bip_server_callback(
    void *arg,
    struct udp_pcb *upcb,
    struct pbuf *pkt,
    const ip_addr_t *addr,
    u16_t port)
{
    uint16_t npdu_offset = 0;
    BACNET_ADDRESS src = { 0 }; /* address where message came from */
    BACNET_IP_ADDRESS saddr;
    uint8_t *npdu = (uint8_t *)pkt->payload;
    uint16_t npdu_len = pkt->tot_len;

    (void)arg;
    (void)upcb;

    bip_encode_bip_address(&saddr, addr, port);
    npdu_offset = bvlc_handler(&saddr, &src, npdu, npdu_len);
    if (npdu_offset > 0) {
        BIP_STATS_INC(recv);
        npdu_len -= npdu_offset;
        npdu_handler(&src, &npdu[npdu_offset], npdu_len);
    } else {
        BIP_STATS_INC(drop);
    }
    pbuf_free(pkt);
}

/**
 * @brief Get the BACnet/IP unicast address in full BACnet address format
 * @param my_address - pointer to the #BACNET_ADDRESS to fill
 */
void bip_get_my_address(BACNET_ADDRESS *my_address)
{
    int i = 0;

    if (my_address) {
        my_address->mac_len = 6;
        memcpy(&my_address->mac[0], &BIP_Address.address, 4);
        memcpy(&my_address->mac[4], &BIP_Address.port, 2);
        my_address->net = 0; /* local only, no routing */
        my_address->len = 0; /* no SLEN */
        for (i = 0; i < MAX_MAC_LEN; i++) {
            /* no SADR */
            my_address->adr[i] = 0;
        }
    }
}

/**
 * @brief Get the BACnet/IP broadcast address in full BACnet address format
 * @param dest - pointer to the #BACNET_ADDRESS to fill
 */
void bip_get_broadcast_address(BACNET_ADDRESS *dest)
{
    int i = 0; /* counter */

    if (dest) {
        dest->mac_len = 6;
        memcpy(&dest->mac[0], &BIP_Broadcast_Address.address, 4);
        memcpy(&dest->mac[4], &BIP_Address.port, 2);
        dest->net = BACNET_BROADCAST_NETWORK;
        dest->len = 0; /* no SLEN */
        for (i = 0; i < MAX_MAC_LEN; i++) {
            /* no SADR */
            dest->adr[i] = 0;
        }
    }
}

/** Initialize the BACnet/IP services at the given interface.
 * @ingroup DLBIP
 *
 * Call this only after bip_set_addr(), bip_set_broadcast_addr(), and
 * bip_set_port() have been populated (i.e. after WiFi gets an IP).
 *
 * @param ifname [in] unused on ESP32
 * @return true if the UDP socket is successfully opened.
 */
bool bip_init(char *ifname)
{
    (void)ifname;

    LOCK_TCPIP_CORE();
    Server_upcb = udp_new();
    if (Server_upcb == NULL) {
        UNLOCK_TCPIP_CORE();
        /* Increase MEMP_NUM_UDP_PCB in lwipopts.h if this fires */
        return false;
    }
    /* Bind to any local address on the BACnet/IP port */
    udp_bind(Server_upcb, IP_ADDR_ANY, BIP_Address.port);
    /* Register the receive callback */
    udp_recv(Server_upcb, bip_server_callback, NULL);
    UNLOCK_TCPIP_CORE();

    return true;
}

/**
 * @brief Tear down the BACnet/IP UDP control block.
 */
void bip_cleanup(void)
{
    if (Server_upcb) {
        LOCK_TCPIP_CORE();
        udp_remove(Server_upcb);
        UNLOCK_TCPIP_CORE();
        Server_upcb = NULL;
    }
}
