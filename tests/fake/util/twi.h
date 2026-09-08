#define TW_STATUS (TWSR & 0xf8)
#define TW_START 0x08
#define TW_REP_START 0x10
#define TW_MT_SLA_ACK 0x18
#define TW_MT_DATA_ACK 0x28
#define TW_MR_SLA_ACK 0x40
#define TW_MR_DATA_ACK 0x50
#define TW_MR_DATA_NACK 0x58
