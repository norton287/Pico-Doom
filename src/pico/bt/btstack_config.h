/*
 * BTstack configuration for the Bluetooth gamepad build (Pico 2 W), used via Bluepad32.
 * Based on the settings Bluepad32's Pico W example uses (themselves derived from
 * pico-examples' pico_w/bt/config/btstack_config.h), trimmed to what HID gamepads need.
 */
#ifndef DOOM_BTSTACK_CONFIG_H
#define DOOM_BTSTACK_CONFIG_H

#define ENABLE_LOG_ERROR
// required by the SDK's always-built hci_dump_embedded_stdout.c (dumping itself stays off)
#define ENABLE_PRINTF_HEXDUMP

// BLE (Xbox Series, some 8BitDo modes) as well as Classic (most 8BitDo modes, PS4/5, Switch)
#define ENABLE_GATT_CLIENT_PAIRING
#define ENABLE_L2CAP_LE_CREDIT_BASED_FLOW_CONTROL_MODE
#define ENABLE_LE_CENTRAL
#define ENABLE_LE_DATA_LENGTH_EXTENSION
#define ENABLE_LE_PERIPHERAL
#define ENABLE_LE_PRIVACY_ADDRESS_RESOLUTION
#define ENABLE_LE_SECURE_CONNECTIONS
#define ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE
#define ENABLE_CROSS_TRANSPORT_KEY_DERIVATION

#define HCI_OUTGOING_PRE_BUFFER_SIZE 4
#define HCI_ACL_PAYLOAD_SIZE (1691 + 4)
#define HCI_ACL_CHUNK_SIZE_ALIGNMENT 4

// static memory pools (no malloc - Doom's allocator must not be re-entered from the Bluetooth interrupt)
#define MAX_NR_GATT_CLIENTS 2
#define MAX_NR_HCI_CONNECTIONS 2
#define MAX_NR_HID_HOST_CONNECTIONS 2
#define MAX_NR_HIDS_HOSTS 2
#define MAX_NR_L2CAP_CHANNELS 6
#define MAX_NR_L2CAP_SERVICES 4
#define MAX_NR_SERVICE_RECORD_ITEMS 2
#define MAX_NR_SM_LOOKUP_ENTRIES 3
#define MAX_NR_WHITELIST_ENTRIES 4
#define MAX_NR_LE_DEVICE_DB_ENTRIES 4
#define MAX_NR_BTSTACK_LINK_KEY_DB_MEMORY_ENTRIES 2
#define MAX_NR_AVDTP_CONNECTIONS 0
#define MAX_NR_AVDTP_STREAM_ENDPOINTS 0
#define MAX_NR_AVRCP_CONNECTIONS 0
#define MAX_NR_BNEP_CHANNELS 0
#define MAX_NR_BNEP_SERVICES 0
#define MAX_NR_HFP_CONNECTIONS 0
#define MAX_NR_RFCOMM_CHANNELS 0
#define MAX_NR_RFCOMM_MULTIPLEXERS 0
#define MAX_NR_RFCOMM_SERVICES 0

// limit ACL buffers and enable host flow control to avoid overrunning the cyw43 shared bus
#define MAX_NR_CONTROLLER_ACL_BUFFERS 3
#define MAX_NR_CONTROLLER_SCO_PACKETS 3
#define ENABLE_HCI_CONTROLLER_TO_HOST_FLOW_CONTROL
#define HCI_HOST_ACL_PACKET_LEN 1024
#define HCI_HOST_ACL_PACKET_NUM 3
#define HCI_HOST_SCO_PACKET_LEN 120
#define HCI_HOST_SCO_PACKET_NUM 3

// pairing keys are kept in flash (TLV), so a paired controller reconnects by itself
#define NVM_NUM_DEVICE_DB_ENTRIES 4
#define NVM_NUM_LINK_KEYS 4
#define MAX_ATT_DB_SIZE 512

#define HAVE_EMBEDDED_TIME_MS
#define HAVE_ASSERT
#define HCI_RESET_RESEND_TIMEOUT_MS 1000
#define ENABLE_SOFTWARE_AES128
#define ENABLE_MICRO_ECC_FOR_LE_SECURE_CONNECTIONS

#endif
