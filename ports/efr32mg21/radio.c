/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/*
 * IEEE 802.15.4 radio driver for the EFR32MG21 (Series 2), built on the
 * Silicon Labs RAIL library.  Adapted from the EFR32MG1P (Series 1) port in
 * the v1.29.0-efr32 branch; the Series 2 differences are:
 *   - the 2.4 GHz standard PHY config is provided by the weak symbol inside
 *     librail itself (rfhal_standard_phys.o), matching the working SONOFF
 *     Dongle Plus E sniffer reference on the same chip,
 *   - the PA mode is RAIL_TX_POWER_MODE_2P4GIG_MP (raw power level 90 = 10 dBm),
 *   - RAIL_ConfigCal uses RAIL_CAL_ALL_PENDING (Series 2 lazy calibration).
 *
 * All radio interrupt handlers (AGC/BUFC/FRC/MODEM/PROTIMER/RAC_SEQ/RAC_RSM/
 * SYNTH) are supplied by librail_efr32xg21 itself, so no ISRs are needed here.
 */

#include <stdio.h>
#include <string.h>

#include "py/obj.h"
#include "py/runtime.h"
#include "py/binary.h"
#include "py/mphal.h"

#include "em_device.h"
#include "em_core.h"
#include "em_cmu.h"
#include "em_emu.h"

#include "sl_status.h"

#include "radio.h"
#include "rail.h"
#include "protocol/ieee802154/rail_ieee802154.h"

#ifndef RADIO_CHANNEL
#define RADIO_CHANNEL 11
#endif

uint8_t radio_mac_address[8];
uint16_t radio_short_address = 0xFFFF; // default value
uint16_t radio_pan_id = 0xFFFF; // default value
static bool radio_promiscuous;
static volatile int radio_tx_pending;
static volatile RAIL_Events_t radio_tx_result;
static int radio_channel;

typedef enum {
    RADIO_UNINIT,
    RADIO_INITING,
    RADIO_IDLE,
    RADIO_TX,
    RADIO_RX,
    RADIO_CALIBRATION
} siliconlabs_modem_state_t;

static siliconlabs_modem_state_t radio_state = RADIO_UNINIT;

static volatile RAIL_Handle_t rail = NULL;
static volatile int radio_last_startrx_result;
static void rail_callback_events(RAIL_Handle_t rail, RAIL_Events_t events);

// RX-path diagnostics: how far down the receive chain does an incoming
// 802.15.4 frame get?  Preamble/sync detection proves the demodulator is
// locking; a frame_error without a packet_received means the CRC failed.
static volatile unsigned radio_diag_preamble;
static volatile unsigned radio_diag_sync1;
static volatile unsigned radio_diag_sync2;
static volatile unsigned radio_diag_rx_packet;
static volatile unsigned radio_diag_frame_error;
static volatile unsigned radio_diag_fifo_overflow;
static volatile unsigned radio_diag_rx_aborted;
static volatile unsigned radio_diag_addr_filtered;
static volatile unsigned radio_diag_cal_needed;
static volatile unsigned radio_diag_tx_completion;
static volatile int radio_diag_last_packet_status;
// Live incoming-packet diagnostics, captured from RAIL_GetRxIncomingPacketInfo()
// in callback context when the FRC has parsed a frame header (RX_FILTER_PASSED).
static volatile unsigned radio_diag_filter_passed;
static volatile unsigned radio_diag_incoming_bytes;
static volatile int radio_diag_incoming_status;
// Set when RAIL reports a pending calibration (RAIL_EVENT_CAL_NEEDED).  The
// calibration itself must run with the radio idle, so it is deferred to the
// next radio_channel_set() rather than executed in the interrupt handler.
static volatile bool radio_cal_pending;

// can't be const since the buffer is used for writes
static RAIL_Config_t rail_config = {
    .eventsCallback = rail_callback_events,
    .protocol = NULL,   // must be NULL for ieee802.15.4
    .scheduler = NULL,  // not multi-protocol
};

static const RAIL_DataConfig_t rail_data_config = {
    .txSource = TX_PACKET_DATA,
    .rxSource = RX_PACKET_DATA,
    .txMethod = PACKET_MODE,
    .rxMethod = PACKET_MODE,
};

// The 2.4 GHz OQPSK PHY is bound to RAIL_IEEE802154_Phy2p4GHz inside
// librail itself (rfhal_standard_phys.o weak symbol -> standard 250 kbps
// channel config).  Do NOT override it here: an earlier revision of this
// port bound the antenna-diversity variant channel config into the standard
// entry point, which enables the AGC antenna-diversity register set.  On
// this board (no external antenna-select GPIOs, antenna hard-wired to
// RF2G4_IO2) that register set was proven to break RX frame termination:
// headers passed the address filter (RX_FILTER_PASSED, length byte parsed
// correctly into the FIFO) but the FRC never ended a single frame — the RX
// FIFO filled to 512 bytes with multiple concatenated frames and no
// RX_PACKET_RECEIVED / RX_FRAME_ERROR / RX_FIFO_OVERFLOW events ever fired.
// The working SONOFF Dongle Plus E sniffer (same EFR32MG21 chip) uses the
// library default standard profile and receives fine.

static const RAIL_IEEE802154_Config_t ieee802154_config = {
    // Promiscuous mode: accept every 802.15.4 frame regardless of PAN ID or
    // destination address.  With promiscuousMode=false and the default
    // PAN ID / short address (0xFFFF), RAIL's hardware-accelerated address
    // filter silently drops every frame not addressed to this device (and does
    // NOT raise RAIL_EVENT_RX_ADDRESS_FILTERED, which only applies to the
    // generic RAIL_EnableAddressFilter() path).  That leaves the RX path stuck
    // after sync-word detection with no packet, no error, and no abort.
    .promiscuousMode = true,
    .isPanCoordinator = false,
    .framesMask = RAIL_IEEE802154_ACCEPT_STANDARD_FRAMES,
    .ackConfig = {
        // The working SONOFF Dongle Plus E sniffer (same EFR32MG21) runs with
        // auto-ack disabled; keep it off here too.  Auto-ack makes the RX
        // state machine stop to transmit an ACK mid-frame, which is another
        // variable in the frame-termination failure.
        .enable = false,
        .ackTimeout = 54 * 16,      // 54 symbols * 16 us/symbol = 864 usec
        .rxTransitions = {
            .success = RAIL_RF_STATE_RX, // go to Rx after sending the ACK
            .error = RAIL_RF_STATE_RX,   // ignored
        },
        .txTransitions = {
            .success = RAIL_RF_STATE_RX, // go to Rx for receiving the ACK
            .error = RAIL_RF_STATE_RX,   // ignored
        },
    },
    .timings = {
        .idleToRx = 100,
        .idleToTx = 100,
        .rxToTx = 192,              // 12 symbols * 16 us/symbol
        .txToRx = 192 - 10,         // slightly lower to get to RX in time
        .rxSearchTimeout = 0,       // not used
        .txToRxSearchTimeout = 0,   // not used
    },
    .addresses = NULL,              // set by explicit calls below
};

static const RAIL_TxPowerConfig_t paInit2p4 = {
    // The MG21 comes in 10 dBm ("010") and 20 dBm ("020") variants.  Only the
    // 020 has the high-power PA (HP, 20 dBm); the 010 bonds out only the MP PA
    // (10 dBm) and LP PA (0 dBm).  Selecting HP mode on a 10 dBm part drives a
    // PA that is not present, so the transmitter reports success but radiates
    // nothing.  MP mode + level 90 = 10 dBm works on both variants.
    .mode = RAIL_TX_POWER_MODE_2P4GIG_MP,
    .voltage = 1800,    // 1.8 V internal VREGVDD LDO supply
    .rampTime = 10,
};

static const RAIL_CsmaConfig_t csma_config =
    RAIL_CSMA_CONFIG_802_15_4_2003_2p4_GHz_OQPSK_CSMA;

/*
 * librail dependency stubs.
 *
 * These symbols are referenced by librail but are either supplied by plugins
 * this port does not use, or are only exercised by features (PTI, dBm-based
 * TX power) that this port never enables.
 */

// Normally provided by the pa-conversions plugin; only needed by
// RAIL_SetTxPowerDbm(), which this port replaces with raw RAIL_SetTxPower().
RAIL_TxPowerLevel_t RAIL_ConvertDbmToRaw(RAIL_Handle_t railHandle,
                                         RAIL_TxPowerMode_t mode,
                                         RAIL_TxPower_t power)
{
    (void)railHandle;
    (void)mode;
    (void)power;
    return 0; // raw power level 0
}

// PTI (Packet Trace Interface) is never configured by this port (no
// RAIL_ConfigPti call), but rfhal_pti.o still references the GPIO toggles.
// The generated sl_gpio driver is not part of this build, so stub them out.
sl_status_t sl_gpio_set_pin(const void *gpio)
{
    (void)gpio;
    return SL_STATUS_OK;
}

sl_status_t sl_gpio_clear_pin(const void *gpio)
{
    (void)gpio;
    return SL_STATUS_OK;
}

// RAIL_ConfigAntenna() pulls in rfhal_trustzone.o, which references the pin
// mode setter even when no external antenna-select GPIO is configured.  The
// generated sl_gpio driver is not part of this build, so stub it out.  It is
// never called here (ant0PinEn/ant1PinEn are false — we use the internal RF
// path via defaultPath).
sl_status_t sl_gpio_set_pin_mode(const void *gpio, int mode, bool output_value)
{
    (void)gpio;
    (void)mode;
    (void)output_value;
    return SL_STATUS_OK;
}

/*
 * Called when the radio has finished its asynchronous bring-up.
 */
static void rail_callback_rfready(RAIL_Handle_t rail)
{
    (void)rail;
    radio_state = RADIO_IDLE;
    radio_tx_pending = 0;
}

#define MAX_PKTS 4
static volatile unsigned rx_buffer_write;
static volatile unsigned rx_buffer_read;
static uint8_t rx_buffers[MAX_PKTS][MAC_PACKET_MAX_LENGTH];
static uint8_t radio_tx_buffer[MAC_PACKET_MAX_LENGTH];

#define FRAME_TYPE_ACK 0x02

/*
 * Send a pre-formed ACK message ASAP in the RX path, using the AutoAckFifo.
 * Needs to fill in the sequence number from the message, so this is not safe
 * to call from outside the RX interrupt.
 */
static int radio_tx_autoack(uint8_t seq)
{
    static uint8_t ack_buf[] = {
        0x05,           // length, including FCS
        FRAME_TYPE_ACK,
        0x00,           // FCF bits that we don't care about
        0x00,           // seq goes here
    };

    ack_buf[3] = seq;
    return RAIL_WriteAutoAckFifo(rail, ack_buf, 5);
}

static void process_packet(RAIL_Handle_t rail)
{
    RAIL_RxPacketInfo_t info;
    RAIL_RxPacketHandle_t handle =
        RAIL_GetRxPacketInfo(rail, RAIL_RX_PACKET_HANDLE_NEWEST, &info);

    radio_diag_last_packet_status = info.packetStatus;

    // not a valid receive? discard it.
    if (info.packetStatus != RAIL_RX_PACKET_READY_SUCCESS)
        goto done;
    // too long? discard it.
    if (info.packetBytes > MAC_PACKET_MAX_LENGTH)
        goto done;

    // check for an ack packet and turn off the auto ack for this rx
    uint8_t header[4];
    RAIL_PeekRxPacket(rail, handle, header, sizeof(header), 0);
    const uint8_t len = header[0];
    const uint8_t packet_type = header[1] & 0x03;
    const uint8_t ack_requested = header[1] & 0x20;
    if (len == 5 && packet_type == FRAME_TYPE_ACK) {
        // this is an ack, so don't send a reply
        RAIL_CancelAutoAck(rail);
    } else if (ack_requested && !radio_promiscuous) {
        // ACK requested and to us; the sequence number is the third byte in
        // the header (after the length byte and the two FCF bytes).
        radio_tx_autoack(header[3]);
    }

    {
        unsigned write_index = rx_buffer_write;
        uint8_t *rx_buffer = rx_buffers[write_index];
        if (info.packetBytes > MAC_PACKET_MAX_LENGTH - 2) {
            // should never happen?
            printf("rx too long %d\n", info.packetBytes);
        } else {
            RAIL_CopyRxPacket(rx_buffer, &info); // puts the length in byte 0
            if (write_index == MAX_PKTS - 1)
                rx_buffer_write = 0;
            else
                rx_buffer_write = write_index + 1;
        }
    }

done:
    RAIL_ReleaseRxPacket(rail, handle);
}

/*
 * Callback from the radio interrupt when there is an event.
 */
static void rail_callback_events(RAIL_Handle_t rail, RAIL_Events_t events)
{
    if (events & RAIL_EVENT_RSSI_AVERAGE_DONE) {
        (void)RAIL_GetAverageRssi(rail);
    }

    if (events & RAIL_EVENT_RX_PREAMBLE_DETECT) {
        radio_diag_preamble++;
        // Note: no RAIL_GetRssi() here.  Calling it in mid-frame event
        // context was another deviation from every working reference (the
        // SONOFF sniffer never queries RSSI from a callback), and RAIL
        // documents GetRssi as busy-waiting the AGC — harmless in theory,
        // but removed to keep the RX datapath untouched during a frame.
    }
    if (events & RAIL_EVENT_RX_SYNC1_DETECT) {
        radio_diag_sync1++;
    }
    if (events & RAIL_EVENT_RX_SYNC2_DETECT) {
        radio_diag_sync2++;
    }
    if (events & RAIL_EVENT_RX_PACKET_RECEIVED) {
        radio_diag_rx_packet++;
        process_packet(rail);
    }
    if (events & RAIL_EVENT_RX_FRAME_ERROR) {
        radio_diag_frame_error++;
    }
    if (events & RAIL_EVENT_RX_FIFO_OVERFLOW) {
        radio_diag_fifo_overflow++;
    }
    if (events & RAIL_EVENT_RX_PACKET_ABORTED) {
        radio_diag_rx_aborted++;
    }
    if (events & RAIL_EVENT_RX_ADDRESS_FILTERED) {
        radio_diag_addr_filtered++;
    }
    if (events & RAIL_EVENT_RX_FILTER_PASSED) {
        radio_diag_filter_passed++;
        RAIL_RxPacketInfo_t incoming;
        RAIL_GetRxIncomingPacketInfo(rail, &incoming);
        radio_diag_incoming_bytes = incoming.packetBytes;
        radio_diag_incoming_status = (int)incoming.packetStatus;
    }

    if (events & RAIL_EVENTS_TX_COMPLETION) {
        radio_diag_tx_completion++;
        radio_tx_result = events;
        radio_tx_pending = 0;
    }

    if (events & RAIL_EVENT_CAL_NEEDED) {
        radio_diag_cal_needed++;
        // Defer the calibration: RAIL_Calibrate() must run with the radio
        // idle (not from interrupt context, where the synth is mid-transition).
        // radio_channel_set() performs it before the next RAIL_StartRx().
        radio_cal_pending = true;
        radio_tx_pending = 0;
    }
}

static void radio_channel_set(unsigned channel)
{
    RAIL_Idle(rail, RAIL_IDLE_FORCE_SHUTDOWN_CLEAR_FLAGS, true);

    // If RAIL asked for a calibration (RAIL_EVENT_CAL_NEEDED), run it now that
    // the radio is idle and before we tune the next channel.
    if (radio_cal_pending) {
        radio_cal_pending = false;
        RAIL_Calibrate(rail, NULL, RAIL_CAL_ALL_PENDING);
    }

    radio_state = RADIO_RX;
    radio_channel = channel;
    radio_last_startrx_result = RAIL_StartRx(rail, channel, NULL);
}

void radio_init(void)
{
    // do not re-init
    if (radio_state != RADIO_UNINIT)
        return;

    // The MG21 (Series 2 config 1) has no DC-DC converter: the device header
    // sets _SILICON_LABS_DCDC_FEATURE = _SILICON_LABS_DCDC_FEATURE_NOTUSED.
    // Its 2.4 GHz PA is fed from the internal 1.8 V VREGVDD LDO, so no EMU
    // DCDC init is needed here (unlike the Series 1 sniffer-tradfri reference,
    // which is a different part that does have a DC-DC).

    // The radio reference is the HFXO (38.4 MHz crystal).  Enable it before
    // RAIL_Init(); Series 2 leaves oscillator bring-up to the application.
    CMU_HFXOInit_TypeDef hfxoInit = CMU_HFXOINIT_DEFAULT;
    CMU_HFXOInit(&hfxoInit);
    CMU_OscillatorEnable(cmuOsc_HFXO, true, true);

    {
        RAIL_Version_t version;
        RAIL_GetVersion(&version, true);
        printf("rail=%d.%d.%d build %d flags %d (%08x)%s\n",
            version.major,
            version.minor,
            version.rev,
            version.build,
            version.flags,
            (unsigned int)version.hash,
            version.multiprotocol ? " multiprotocol" : ""
        );
    }

    rail = RAIL_Init(&rail_config, rail_callback_rfready);

    // Select the internal 2.4 GHz RF path.  The EFR32MG21 (Series 2 config 1)
    // bonds out BOTH internal RF paths: pin 13 = RF2G4_IO1 and pin 12 =
    // RF2G4_IO2.  This breakout board connects its antenna to RF2G4_IO2 =
    // RAIL_ANTENNA_1.  The working SONOFF Dongle Plus E sniffer (same chip,
    // antenna on the same path) also uses RAIL_ANTENNA_1.  The path can still
    // be changed at runtime with radio.antenna().
    {
        RAIL_AntennaConfig_t antennaConfig = { 0 };
        antennaConfig.defaultPath = RAIL_ANTENNA_1;
        printf("cfgant=%d\n", (int)RAIL_ConfigAntenna(rail, &antennaConfig));
    }

    printf("cfgdata=%d\n", (int)RAIL_ConfigData(rail, &rail_data_config));
    // Match the working Series 1 sniffer-tradfri reference and the OpenThread
    // efr32 radio: RAIL_ConfigCal() runs BEFORE the PHY/channel config and
    // before RAIL_IEEE802154_Init().  It only arms the lazy calibration mask;
    // the actual calibration values are computed when the radio later requests
    // RAIL_EVENT_CAL_NEEDED (deferred to radio_channel_set()).
    printf("cfgcal=%d\n", (int)RAIL_ConfigCal(rail, RAIL_CAL_ALL));
    // Apply the PHY / modem / FCD / CRC register set via
    // RAIL_IEEE802154_Config2p4GHzRadio(), then initialize the 802.15.4
    // hardware acceleration with RAIL_IEEE802154_Init().
    printf("ieee_cfg2p4=%d\n", (int)RAIL_IEEE802154_Config2p4GHzRadio(rail));
    printf("ieee_init=%d\n", (int)RAIL_IEEE802154_Init(rail, &ieee802154_config));
    // Match the official SDK init (sl_rail_util_init.c.jinja) and the working
    // SONOFF sniffer: always end up back in RX after a TX or RX operation
    // completes, so the radio keeps listening for the next frame.
    {
        RAIL_StateTransitions_t transitions = {
            .success = RAIL_RF_STATE_RX,
            .error = RAIL_RF_STATE_RX,
        };
        printf("settxtr=%d\n", (int)RAIL_SetTxTransitions(rail, &transitions));
        printf("setrxtr=%d\n", (int)RAIL_SetRxTransitions(rail, &transitions));
    }
    RAIL_ConfigEvents(rail, RAIL_EVENTS_ALL, 0
        | RAIL_EVENT_RSSI_AVERAGE_DONE
        | RAIL_EVENT_RX_PACKET_RECEIVED
        | RAIL_EVENT_RX_PREAMBLE_DETECT
        | RAIL_EVENT_RX_SYNC1_DETECT
        | RAIL_EVENT_RX_SYNC2_DETECT
        | RAIL_EVENT_RX_FRAME_ERROR
        | RAIL_EVENT_RX_FIFO_OVERFLOW
        | RAIL_EVENT_RX_PACKET_ABORTED
        | RAIL_EVENT_RX_ADDRESS_FILTERED
        | RAIL_EVENT_RX_FILTER_PASSED
        | RAIL_EVENTS_TX_COMPLETION
        | RAIL_EVENTS_TXACK_COMPLETION
        | RAIL_EVENT_CAL_NEEDED
    );

    RAIL_ConfigTxPower(rail, &paInit2p4);
    RAIL_SetTxPower(rail, 90); // raw MP max = 10 dBm (works on both 010 and 020)

    // use the device unique id as the MAC (EU64)
    memcpy(&radio_mac_address[0], (const void *)&DEVINFO->EUI64L, 4);
    memcpy(&radio_mac_address[4], (const void *)&DEVINFO->EUI64H, 4);
    RAIL_IEEE802154_SetLongAddress(rail, radio_mac_address, 0);

    // set the short address to something other than 0
    RAIL_IEEE802154_SetShortAddress(rail, radio_short_address, 0);
    RAIL_IEEE802154_SetPanId(rail, radio_pan_id, 0);

    // always use the same tx buffer
    RAIL_SetTxFifo(rail, radio_tx_buffer, 0, sizeof(radio_tx_buffer));

    // unpause auto-ack
    RAIL_PauseRxAutoAck(rail, false);

    // cache the current promiscuous mode
    radio_promiscuous = ieee802154_config.promiscuousMode;

    // Do not force a manual RAIL_Calibrate() here.  RAIL_ConfigCal(RAIL_CAL_ALL)
    // above arms lazy calibration: RAIL requests it via RAIL_EVENT_CAL_NEEDED
    // (handled in rail_callback_events -> radio_cal_pending) and it is applied
    // in the next radio_channel_set().  Forcing TEMP_VCO|RX_IRCAL up front was
    // observed to leave the RX demodulator producing garbage "sync" locks with
    // an unstable frequency offset, so the FRC parsed a bogus header but never
    // completed a packet.  This matches sniffer-tradfri (Series 1) and the
    // OpenThread efr32 radio, neither of which force a manual calibrate.
    radio_channel_set(RADIO_CHANNEL);
}

STATIC mp_obj_t py_radio_init(void)
{
    radio_init();
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(radio_init_obj, py_radio_init);

/*
 * Return a freshly-allocated bytes object with the oldest received packet,
 * or None if the queue is empty.  The packet excludes the length byte and the
 * trailing 2-byte FCS.
 */
STATIC mp_obj_t radio_rxbytes_get(void)
{
    if (radio_state == RADIO_UNINIT)
        radio_init();

    if (rx_buffer_write == rx_buffer_read)
        return mp_const_none;

    unsigned read_index = rx_buffer_read;
    const uint8_t *rx_buffer = rx_buffers[read_index];
    const uint8_t len = rx_buffer[0];

    // advance the read pointer before returning so the slot can be reused
    if (read_index == MAX_PKTS - 1)
        rx_buffer_read = 0;
    else
        rx_buffer_read = read_index + 1;

    size_t n;
    if (len > MAC_PACKET_MAX_LENGTH - 2)
        n = MAC_PACKET_MAX_LENGTH - 2;
    else if (len < 2)
        n = 0;
    else
        n = len - 2;

    return mp_obj_new_bytes(rx_buffer + 1, n);
}
MP_DEFINE_CONST_FUN_OBJ_0(radio_rxbytes_obj, radio_rxbytes_get);

// returns 0 if ok, non-zero if not
int radio_tx_buffer_send(const void *buf, size_t len)
{
    // radio tx length including the 2 byte FCS at the end
    uint8_t tx_len = 2 + len;
    RAIL_WriteTxFifo(rail, &tx_len, 1, true);
    RAIL_WriteTxFifo(rail, buf, len, false);

    radio_tx_pending = 1;
    radio_tx_result = 0;
    radio_state = RADIO_TX;

    RAIL_TxOptions_t txOpt = RAIL_TX_OPTIONS_DEFAULT;

    // if this is not a multipurpose frame (0x5) and the FCF has ack
    // requested, then tell the radio to stay online to wait for the ACK.
    if (len >= 2) {
        const uint8_t *tx_payload = (const uint8_t *)buf;
        const uint8_t fcf = tx_payload[1];
        if ((fcf & 0x07) != 0x05 && (fcf & 0x20) != 0x00) {
            txOpt |= RAIL_TX_OPTION_WAIT_FOR_ACK;
        }
    }

    // start the transmit, we hope!
    int rc = RAIL_StartCcaCsmaTx(rail, radio_channel, txOpt, &csma_config, NULL);

    // if it failed to start, unset the pending flag
    if (rc != 0)
        radio_tx_pending = 0;

    return rc;
}

/*
 * Send a byte buffer to the radio.
 */
STATIC mp_obj_t radio_txbytes(mp_obj_t buf_obj)
{
    if (radio_state == RADIO_UNINIT)
        radio_init();

    mp_buffer_info_t buf;
    mp_get_buffer_raise(buf_obj, &buf, MP_BUFFER_READ);
    const size_t len = buf.len;

    if (len > MAC_PACKET_MAX_LENGTH - 2)
        mp_raise_ValueError("tx length too long");

    int rc = radio_tx_buffer_send(buf.buf, len);
    if (rc != 0)
        mp_raise_ValueError("tx failed");

    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(radio_txbytes_obj, radio_txbytes);

/*
 * Return a short string describing the result of the most recent TX: "sent",
 * "channel busy" (CCA failed), "channel clear", "aborted", "blocked",
 * "underflow", or "none" if no TX has completed yet.
 */
STATIC mp_obj_t radio_tx_status(void)
{
    const char *s = "none";
    RAIL_Events_t r = radio_tx_result;
    if (r & RAIL_EVENT_TX_PACKET_SENT) {
        s = "sent";
    } else if (r & RAIL_EVENT_TX_CHANNEL_BUSY) {
        s = "channel busy";
    } else if (r & RAIL_EVENT_TX_CHANNEL_CLEAR) {
        s = "channel clear";
    } else if (r & RAIL_EVENT_TX_ABORTED) {
        s = "aborted";
    } else if (r & RAIL_EVENT_TX_BLOCKED) {
        s = "blocked";
    } else if (r & RAIL_EVENT_TX_UNDERFLOW) {
        s = "underflow";
    }
    return mp_obj_new_str(s, strlen(s));
}
MP_DEFINE_CONST_FUN_OBJ_0(radio_tx_status_obj, radio_tx_status);

STATIC mp_obj_t radio_mac(void)
{
    if (radio_state == RADIO_UNINIT)
        radio_init();

    return mp_obj_new_bytes(radio_mac_address, sizeof(radio_mac_address));
}
MP_DEFINE_CONST_FUN_OBJ_0(radio_mac_obj, radio_mac);

STATIC mp_obj_t mp_radio_promiscuous(size_t n_args, const mp_obj_t *args)
{
    if (radio_state == RADIO_UNINIT)
        radio_init();

    if (n_args == 1) {
        radio_promiscuous = mp_obj_is_true(args[0]);
        RAIL_IEEE802154_SetPromiscuousMode(rail, radio_promiscuous);
    }

    return mp_obj_new_bool(radio_promiscuous);
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(radio_promiscuous_obj, 0, 1, mp_radio_promiscuous);

STATIC mp_obj_t mp_radio_short_address(size_t n_args, const mp_obj_t *args)
{
    if (radio_state == RADIO_UNINIT)
        radio_init();

    if (n_args == 1) {
        radio_short_address = mp_obj_get_int(args[0]);
        RAIL_IEEE802154_SetShortAddress(rail, radio_short_address, 0);
    }

    if (radio_short_address == 0xFFFF)
        return mp_const_none;

    return MP_OBJ_NEW_SMALL_INT(radio_short_address);
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(radio_short_address_obj, 0, 1, mp_radio_short_address);

STATIC mp_obj_t mp_radio_pan_id(size_t n_args, const mp_obj_t *args)
{
    if (radio_state == RADIO_UNINIT)
        radio_init();

    if (n_args == 1) {
        radio_pan_id = mp_obj_get_int(args[0]);
        RAIL_IEEE802154_SetPanId(rail, radio_pan_id, 0);
    }

    if (radio_pan_id == 0xFFFF)
        return mp_const_none;

    return MP_OBJ_NEW_SMALL_INT(radio_pan_id);
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(radio_pan_id_obj, 0, 1, mp_radio_pan_id);

STATIC mp_obj_t mp_radio_channel(mp_obj_t channel_obj)
{
    if (radio_state == RADIO_UNINIT)
        radio_init();

    unsigned channel = mp_obj_get_int(channel_obj);
    radio_channel_set(channel);

    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(radio_channel_obj, mp_radio_channel);

/*
 * Return the current RSSI in quarter-dBm (RAIL's native unit), or None if the
 * receiver could not produce a value.  Only valid while the radio is in RX.
 */
STATIC mp_obj_t radio_rssi_get(void)
{
    if (radio_state == RADIO_UNINIT)
        radio_init();

    int16_t rssi = RAIL_GetRssi(rail, true);
    if (rssi == RAIL_RSSI_INVALID)
        return mp_const_none;
    return mp_obj_new_int(rssi);
}
MP_DEFINE_CONST_FUN_OBJ_0(radio_rssi_obj, radio_rssi_get);

/*
 * Return the raw RAIL radio-state bitmask (RAIL_RF_STATE_*).
 */
STATIC mp_obj_t radio_rfstate_get(void)
{
    if (radio_state == RADIO_UNINIT)
        return mp_obj_new_int(0);
    return mp_obj_new_int(RAIL_GetRadioState(rail));
}
MP_DEFINE_CONST_FUN_OBJ_0(radio_rfstate_obj, radio_rfstate_get);

/*
 * Return the status of the most recent RAIL_StartRx() call (0 = success).
 */
STATIC mp_obj_t radio_startrx_get(void)
{
    return mp_obj_new_int(radio_last_startrx_result);
}
MP_DEFINE_CONST_FUN_OBJ_0(radio_startrx_obj, radio_startrx_get);

/*
 * Return the number of bytes currently available in the RX FIFO
 * (diagnostic).  Non-zero means the receiver has begun filling the FIFO with
 * an in-progress (or stuck) packet; 0 means the FRC is not receiving data.
 */
STATIC mp_obj_t radio_fifo_get(void)
{
    if (radio_state == RADIO_UNINIT)
        return mp_obj_new_int(0);
    return mp_obj_new_int((int)RAIL_GetRxFifoBytesAvailable(rail));
}
MP_DEFINE_CONST_FUN_OBJ_0(radio_fifo_obj, radio_fifo_get);

/*
 * Return live incoming-packet diagnostics captured from the callback:
 * (filter_passed_count, incoming_packet_bytes, incoming_packet_status).
 * filter_passed_count > 0 means the FRC has parsed a frame header at least
 * once; incoming_packet_bytes is how many bytes the FRC thought the current
 * (or last) in-progress packet has received.
 */
STATIC mp_obj_t radio_incoming_get(void)
{
    mp_obj_t tuple[3] = {
        mp_obj_new_int((int)radio_diag_filter_passed),
        mp_obj_new_int((int)radio_diag_incoming_bytes),
        mp_obj_new_int((int)radio_diag_incoming_status),
    };
    return mp_obj_new_tuple(3, tuple);
}
MP_DEFINE_CONST_FUN_OBJ_0(radio_incoming_obj, radio_incoming_get);

/*
 * Return (packetBytes, packetStatus, fifoAvail, bytes) for the newest
 * (possibly still-receiving) RX packet, or None if there is none.  This
 * reveals exactly what the demodulator is producing: how many bytes RAIL
 * thinks the packet has, its status (RECEIVING vs READY), how many bytes are
 * actually in the RX FIFO, and the raw bytes (byte 0 = 802.15.4 length byte,
 * bytes 1..2 = FCF).  A packet stuck at status RECEIVING with packetBytes
 * frozen well below the length byte proves the demodulator loses lock
 * mid-frame.
 */
STATIC mp_obj_t radio_peek_get(void)
{
    RAIL_RxPacketInfo_t info;
    RAIL_RxPacketHandle_t handle =
        RAIL_GetRxPacketInfo(rail, RAIL_RX_PACKET_HANDLE_NEWEST, &info);
    if (handle == RAIL_RX_PACKET_HANDLE_INVALID || info.packetBytes == 0)
        return mp_const_none;

    uint8_t buf[160];
    uint16_t n = info.packetBytes;
    if (n > sizeof(buf))
        n = sizeof(buf);
    uint16_t got = RAIL_PeekRxPacket(rail, handle, buf, n, 0);

    mp_obj_t items[4] = {
        mp_obj_new_int(info.packetBytes),
        mp_obj_new_int((int)info.packetStatus),
        mp_obj_new_int((int)RAIL_GetRxFifoBytesAvailable(rail)),
        mp_obj_new_bytes(buf, got),
    };
    return mp_obj_new_tuple(4, items);
}
MP_DEFINE_CONST_FUN_OBJ_0(radio_peek_obj, radio_peek_get);

/*
 * Return a tuple of RX-path diagnostic counters:
 *   (preamble, sync1, sync2, rx_packet, frame_error, fifo_overflow,
 *    rx_aborted, addr_filtered, cal_needed, tx_completion, last_packet_status)
 */
STATIC mp_obj_t radio_stats_get(void)
{
    mp_obj_t items[11] = {
        mp_obj_new_int(radio_diag_preamble),
        mp_obj_new_int(radio_diag_sync1),
        mp_obj_new_int(radio_diag_sync2),
        mp_obj_new_int(radio_diag_rx_packet),
        mp_obj_new_int(radio_diag_frame_error),
        mp_obj_new_int(radio_diag_fifo_overflow),
        mp_obj_new_int(radio_diag_rx_aborted),
        mp_obj_new_int(radio_diag_addr_filtered),
        mp_obj_new_int(radio_diag_cal_needed),
        mp_obj_new_int(radio_diag_tx_completion),
        mp_obj_new_int(radio_diag_last_packet_status),
    };
    return mp_obj_new_tuple(11, items);
}
MP_DEFINE_CONST_FUN_OBJ_0(radio_stats_obj, radio_stats_get);

/*
 * Return the HFXO_STATUS register (diagnostic): bit HFXO_STATUS_RDY (0x100)
 * is set when the 38.4 MHz crystal has locked.  0 means the radio reference
 * is dead, which would explain a deaf receiver.
 */
STATIC mp_obj_t radio_hfxo_get(void)
{
    return mp_obj_new_int(HFXO0->STATUS);
}
MP_DEFINE_CONST_FUN_OBJ_0(radio_hfxo_obj, radio_hfxo_get);

/*
 * Return the crystal CTUNE value RAIL is using (diagnostic).  A value far
 * outside the ~0x80..0x180 range for a 38.4 MHz crystal hints at a wrong
 * HFXO load-capacitance configuration.
 */
STATIC mp_obj_t radio_tune_get(void)
{
    return mp_obj_new_int(RAIL_GetTune(rail));
}
MP_DEFINE_CONST_FUN_OBJ_0(radio_tune_obj, radio_tune_get);

/*
 * Return the measured RX frequency offset (diagnostic).  Only valid after a
 * sync word has been detected; returns RAIL_FREQUENCY_OFFSET_INVALID (0x7FFF)
 * otherwise.  A large non-zero value means the synth is tuning off-frequency.
 */
STATIC mp_obj_t radio_freq_offset_get(void)
{
    return mp_obj_new_int(RAIL_GetRxFreqOffset(rail));
}
MP_DEFINE_CONST_FUN_OBJ_0(radio_freq_offset_obj, radio_freq_offset_get);

/*
 * Set the nominal radio frequency offset (signed synth ticks).  Used
 * diagnostically to sweep the receiver frequency and find where the sync word
 * decodes.  Idles the radio, applies the offset, and re-enters RX.
 */
STATIC mp_obj_t mp_radio_set_freq_offset(mp_obj_t offset_obj)
{
    if (radio_state == RADIO_UNINIT)
        radio_init();

    int16_t offset = (int16_t)mp_obj_get_int(offset_obj);

    RAIL_Idle(rail, RAIL_IDLE_FORCE_SHUTDOWN_CLEAR_FLAGS, true);
    int rc = (int)RAIL_SetFreqOffset(rail, offset);
    radio_channel_set(radio_channel);

    return mp_obj_new_int(rc);
}
MP_DEFINE_CONST_FUN_OBJ_1(radio_set_freq_offset_obj, mp_radio_set_freq_offset);

/*
 * Return the pending-calibration mask (RAIL_CalMask_t).  Non-zero bits are
 * calibrations RAIL is still waiting for.  RAIL_CAL_ONETIME_IRCAL (0x110000)
 * pending means the image-rejection calibration has not been applied.
 */
STATIC mp_obj_t radio_pending_cal_get(void)
{
    return mp_obj_new_int((int)RAIL_GetPendingCal(rail));
}
MP_DEFINE_CONST_FUN_OBJ_0(radio_pending_cal_obj, radio_pending_cal_get);

/*
 * Explicitly run the image-rejection calibration for the current RF path and
 * return the status (0 = success).  This is the "long" IR calibration that must
 * run once per path; RAIL caches the result.
 */
STATIC mp_obj_t mp_radio_ircal(size_t n_args, const mp_obj_t *args)
{
    if (radio_state == RADIO_UNINIT)
        radio_init();

    RAIL_AntennaSel_t path = RAIL_ANTENNA_AUTO;
    if (n_args == 1)
        path = (RAIL_AntennaSel_t)mp_obj_get_int(args[0]);

    RAIL_Idle(rail, RAIL_IDLE_FORCE_SHUTDOWN_CLEAR_FLAGS, true);
    RAIL_IrCalValues_t ir = { 0 };
    int rc = (int)RAIL_CalibrateIrAlt(rail, &ir, path);
    radio_channel_set(radio_channel);
    return mp_obj_new_int(rc);
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(radio_ircal_obj, 0, 1, mp_radio_ircal);

/*
 * Return the current/default internal RF path (RAIL_AntennaSel_t: 0 = path 0,
 * 1 = path 1, 255 = auto).
 */
STATIC mp_obj_t radio_rf_path_get(void)
{
    RAIL_AntennaSel_t rfPath = RAIL_ANTENNA_AUTO;
    RAIL_Status_t rc = RAIL_GetRfPath(rail, &rfPath);
    if (rc != RAIL_STATUS_NO_ERROR)
        return mp_const_none;
    return mp_obj_new_int((int)rfPath);
}
MP_DEFINE_CONST_FUN_OBJ_0(radio_rf_path_obj, radio_rf_path_get);

/*
 * Select the internal RF path (0 = path 0, 1 = path 1, 255 = auto) at
 * runtime, so the correct bonded port can be found empirically.  The radio is
 * idled, re-armed with the new path, and put back into RX on the current
 * channel.  Returns the RAIL_ConfigAntenna status code (0 = success).
 */
STATIC mp_obj_t mp_radio_antenna(mp_obj_t path_obj)
{
    if (radio_state == RADIO_UNINIT)
        radio_init();

    uint8_t path = (uint8_t)mp_obj_get_int(path_obj);

    RAIL_Idle(rail, RAIL_IDLE_FORCE_SHUTDOWN_CLEAR_FLAGS, true);
    RAIL_AntennaConfig_t antennaConfig = { 0 };
    antennaConfig.defaultPath = path;
    int rc = (int)RAIL_ConfigAntenna(rail, &antennaConfig);

    radio_channel_set(radio_channel);

    return mp_obj_new_int(rc);
}
MP_DEFINE_CONST_FUN_OBJ_1(radio_antenna_obj, mp_radio_antenna);

STATIC const mp_rom_map_elem_t radio_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_radio) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&radio_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_promiscuous), MP_ROM_PTR(&radio_promiscuous_obj) },
    { MP_ROM_QSTR(MP_QSTR_address), MP_ROM_PTR(&radio_short_address_obj) },
    { MP_ROM_QSTR(MP_QSTR_pan), MP_ROM_PTR(&radio_pan_id_obj) },
    { MP_ROM_QSTR(MP_QSTR_channel), MP_ROM_PTR(&radio_channel_obj) },
    { MP_ROM_QSTR(MP_QSTR_mac), MP_ROM_PTR(&radio_mac_obj) },
    { MP_ROM_QSTR(MP_QSTR_rx), MP_ROM_PTR(&radio_rxbytes_obj) },
    { MP_ROM_QSTR(MP_QSTR_tx), MP_ROM_PTR(&radio_txbytes_obj) },
    { MP_ROM_QSTR(MP_QSTR_tx_status), MP_ROM_PTR(&radio_tx_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_rssi), MP_ROM_PTR(&radio_rssi_obj) },
    { MP_ROM_QSTR(MP_QSTR_state), MP_ROM_PTR(&radio_rfstate_obj) },
    { MP_ROM_QSTR(MP_QSTR_startrx), MP_ROM_PTR(&radio_startrx_obj) },
    { MP_ROM_QSTR(MP_QSTR_fifo), MP_ROM_PTR(&radio_fifo_obj) },
    { MP_ROM_QSTR(MP_QSTR_peek), MP_ROM_PTR(&radio_peek_obj) },
    { MP_ROM_QSTR(MP_QSTR_incoming), MP_ROM_PTR(&radio_incoming_obj) },
    { MP_ROM_QSTR(MP_QSTR_stats), MP_ROM_PTR(&radio_stats_obj) },
    { MP_ROM_QSTR(MP_QSTR_hfxo), MP_ROM_PTR(&radio_hfxo_obj) },
    { MP_ROM_QSTR(MP_QSTR_tune), MP_ROM_PTR(&radio_tune_obj) },
    { MP_ROM_QSTR(MP_QSTR_freq_offset), MP_ROM_PTR(&radio_freq_offset_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_freq_offset), MP_ROM_PTR(&radio_set_freq_offset_obj) },
    { MP_ROM_QSTR(MP_QSTR_pending_cal), MP_ROM_PTR(&radio_pending_cal_obj) },
    { MP_ROM_QSTR(MP_QSTR_ircal), MP_ROM_PTR(&radio_ircal_obj) },
    { MP_ROM_QSTR(MP_QSTR_rf_path), MP_ROM_PTR(&radio_rf_path_obj) },
    { MP_ROM_QSTR(MP_QSTR_antenna), MP_ROM_PTR(&radio_antenna_obj) },
};

STATIC MP_DEFINE_CONST_DICT(mp_module_radio_globals, radio_globals_table);

const mp_obj_module_t mp_module_radio = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_radio_globals,
};

MP_REGISTER_MODULE(MP_QSTR_radio, mp_module_radio);
