enum payload_event_type {
  EVT_BUTTON = 0x00,
  EVT_NFC = 0x01,
  EVT_BATTERY = 0x02,
  EVT_HEARTBEAT = 0x04,
  EVT_LOW_BATTERY = 0x05,
  EVT_COUNTER_SYNC = 0x06, /* rejoin: send current counter per button */
  EVT_FUTURE = 0xFF,
};



will be changed to

enum payload_event_type {
EVT_BUTTON        = 0x00,

EVT_NFC_IN        = 0x01, // ts + event_id + 4 byte UID
EVT_NFC_OUT       = 0x02,
EVT_NFC_VOTE      = 0x03,  // ts + button_id(2) + card_data(4)

EVT_HEARTBEAT     = 0x10,
EVT_LOW_BATTERY   = 0x11,
EVT_COUNTER_SYNC  = 0x12,
EVT_FUTURE        = 0xFF,
};


will be changed to:

/* LoRaWAN FPorts */
#define FPORT_BUTTON 10
#define FPORT_NFC 11

#define FPORT_HEARTBEAT 20 // heartbeat

#define FPORT_COUNTER_SYNC 30 // COUNTER SYNC UP on boot

#define FPORT_FUTURE 13 // 



periodically we also need to send LinkCheckReq to verify the network resiliency, if it doesnt get answer then we know there's an issue and we initate the lora rejoin loop/thread. 

