#ifndef _radio_h_
#define _radio_h_

#include <stddef.h>
#include <stdint.h>

/* 802.15.4 maximum size of a single packet including the PHY (length) byte is
 * 128 bytes, but we keep a smaller bound for the RX ring buffers. */
#define MAC_PACKET_MAX_LENGTH   128

/* This driver prepends the length byte (copied in by RAIL_CopyRxPacket); the
 * RSSI/LQI offsets below are reserved for a future richer frame header. */
#define MAC_PACKET_OFFSET_RSSI  0
#define MAC_PACKET_OFFSET_LQI   1
#define MAC_PACKET_INFO_LENGTH  2

extern void radio_init(void);

extern uint8_t radio_mac_address[8];

extern int radio_tx_buffer_send(const void *buf, size_t len);

#endif
