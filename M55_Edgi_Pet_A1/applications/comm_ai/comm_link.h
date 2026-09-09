#ifndef COMM_LINK_H
#define COMM_LINK_H

#include "../common/messages.h"
#include "../common/reliable.h"

#define COMM_HELLO_INTERVAL_MS 500UL
#define COMM_HEARTBEAT_INTERVAL_MS 500UL
#define COMM_LINK_TIMEOUT_MS 3000UL

enum CommAckStatus {
    COMM_ACK_OK=0,
    COMM_ACK_BUSY,
    COMM_ACK_REJECTED,
    COMM_ACK_INTERNAL_ERROR
};

enum CommEventType {
    COMM_EVENT_LINK_READY=1,
    COMM_EVENT_LINK_LOST,
    COMM_EVENT_BUTTON,
    COMM_EVENT_GAME_RESULT,
    COMM_EVENT_TX_ACKED,
    COMM_EVENT_TX_FAILED,
    COMM_EVENT_TX_REJECTED
};

typedef struct {
    u8 type;
    u8 command;
    u8 status;
    u8 key_id;
    u8 game_result;
    u8 had_input;
    u16 score;
    u32 session;
    u32 game_id;
    u32 reaction_ms;
} CommEvent;

typedef u8 (*CommEventHandler)(void *context, const CommEvent *event);

typedef struct {
    ProtoParser parser;
    ReliableTx tx;
    ReliableRx rx;
    u32 session;
    u32 last_rx_ms;
    u32 last_tx_ms;
    u32 last_hello_ms;
    u8 ready;
    u8 hello_sent;
    u8 state_cached;
    u8 state_dirty;
    u8 state_payload[8];
    u32 tx_game_id;
} CommLink;

int comm_link_init(CommLink *link, u32 session, u32 now);
int comm_link_is_ready(const CommLink *link);

/* Returns 1 when out contains one frame to send, otherwise zero. */
int comm_link_tick(CommLink *link, u32 now, CommEventHandler handler,
    void *context, ProtoFrame *out);

/* Returns 1 when reply contains an ACK to send, otherwise zero. */
int comm_link_receive(CommLink *link, const ProtoFrame *frame, u32 now,
    CommEventHandler handler, void *context, ProtoFrame *reply);

/* Same contract as comm_link_receive, but accepts one UART byte. */
int comm_link_feed_byte(CommLink *link, u8 byte, u32 now,
    CommEventHandler handler, void *context, ProtoFrame *reply);

/* Keeps only the latest state and automatically resends it after reconnect. */
int comm_link_set_state(CommLink *link, u32 revision, u8 base,
    u8 display, u16 lease_ms);

/* Game commands require an established link and an idle reliable sender. */
int comm_link_start_game(CommLink *link, u32 game_id, u8 game_type,
    u32 seed);
int comm_link_cancel_game(CommLink *link, u32 game_id);

#endif
