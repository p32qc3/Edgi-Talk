#include "comm_link.h"
#include <string.h>

static u8 emit_event(CommEventHandler handler, void *context, u8 type,
    u8 command, u8 status, u32 session, u32 game_id) {
    CommEvent event;
    memset(&event,0,sizeof(event));
    event.type=type;
    event.command=command;
    event.status=status;
    event.session=session;
    event.game_id=game_id;
    if (!handler) return COMM_ACK_INTERNAL_ERROR;
    return handler(context,&event);
}

static void make_control(CommLink *link, u8 command, ProtoFrame *out) {
    memset(out,0,sizeof(*out));
    out->command=command;
    out->session=link->session;
}

static int current_state_is_pending(const CommLink *link) {
    return link->state_cached && link->tx.pending.command==CMD_STATE &&
        link->tx.pending.length==8 &&
        !memcmp(link->state_payload,link->tx.pending.payload,8);
}

static void reset_connection(CommLink *link) {
    link->ready=0;
    link->hello_sent=0;
    if (link->state_cached) link->state_dirty=1;
    tx_init(&link->tx,link->session);
    rx_init(&link->rx,link->session);
    link->tx_game_id=0;
}

int comm_link_init(CommLink *link, u32 session, u32 now) {
    if (!link || !session) return 0;
    memset(link,0,sizeof(*link));
    link->session=session;
    link->last_rx_ms=now;
    link->last_tx_ms=now;
    proto_init(&link->parser);
    tx_init(&link->tx,session);
    rx_init(&link->rx,session);
    return 1;
}

int comm_link_is_ready(const CommLink *link) {
    return link && link->ready;
}

int comm_link_tick(CommLink *link, u32 now, CommEventHandler handler,
    void *context, ProtoFrame *out) {
    int result;
    u8 failed_command;
    int failed_current_state;
    if (!link || !out || !link->session) return 0;
    if (link->ready && (u32)(now-link->last_rx_ms)>COMM_LINK_TIMEOUT_MS) {
        reset_connection(link);
        emit_event(handler,context,COMM_EVENT_LINK_LOST,0,0,
            link->session,0);
    }
    if (!link->ready) {
        if (!link->hello_sent ||
            (u32)(now-link->last_hello_ms)>=COMM_HELLO_INTERVAL_MS) {
            make_control(link,CMD_HELLO,out);
            link->hello_sent=1;
            link->last_hello_ms=now;
            link->last_tx_ms=now;
            return 1;
        }
        return 0;
    }
    result=tx_poll(&link->tx,now,out);
    if (result==1) {
        link->last_tx_ms=now;
        return 1;
    }
    if (result<0) {
        failed_command=link->tx.pending.command;
        failed_current_state=current_state_is_pending(link);
        if (failed_current_state) link->state_dirty=0;
        emit_event(handler,context,COMM_EVENT_TX_FAILED,failed_command,
            link->tx.status,link->session,link->tx_game_id);
        link->tx_game_id=0;
        return 0;
    }
    if (!link->tx.busy && link->state_dirty) {
        if (tx_begin(&link->tx,CMD_STATE,link->state_payload,8) &&
            tx_poll(&link->tx,now,out)==1) {
            link->last_tx_ms=now;
            return 1;
        }
    }
    if ((u32)(now-link->last_tx_ms)>=COMM_HEARTBEAT_INTERVAL_MS) {
        make_control(link,CMD_HEARTBEAT,out);
        link->last_tx_ms=now;
        return 1;
    }
    return 0;
}

static u8 deliver_business(const ProtoFrame *frame,
    CommEventHandler handler, void *context) {
    CommEvent event;
    u8 status;
    memset(&event,0,sizeof(event));
    event.command=frame->command;
    event.session=frame->session;
    if (frame->command==CMD_BUTTON) {
        event.type=COMM_EVENT_BUTTON;
        event.key_id=frame->payload[0];
    } else {
        event.type=COMM_EVENT_GAME_RESULT;
        event.game_id=get_u32(frame->payload);
        event.game_result=frame->payload[4];
        event.had_input=frame->payload[5];
        event.score=get_u16(frame->payload+6);
        event.reaction_ms=get_u32(frame->payload+8);
    }
    if (!handler) return COMM_ACK_INTERNAL_ERROR;
    status=handler(context,&event);
    return status<=COMM_ACK_INTERNAL_ERROR ? status : COMM_ACK_INTERNAL_ERROR;
}

int comm_link_receive(CommLink *link, const ProtoFrame *frame, u32 now,
    CommEventHandler handler, void *context, ProtoFrame *reply) {
    int received;
    u8 status;
    u8 pending_command;
    int pending_current_state;
    if (!link || !frame || !reply || !message_valid(frame) ||
        frame->session!=link->session) return 0;
    if (frame->command==CMD_READY) {
        link->last_rx_ms=now;
        if (!link->ready) {
            link->ready=1;
            link->last_tx_ms=now;
            tx_init(&link->tx,link->session);
            rx_init(&link->rx,link->session);
            emit_event(handler,context,COMM_EVENT_LINK_READY,0,0,
                link->session,0);
        }
        return 0;
    }
    if (!link->ready) return 0;
    if (frame->command==CMD_HEARTBEAT) {
        link->last_rx_ms=now;
        return 0;
    }
    if (frame->command==CMD_ACK) {
        u32 pending_game_id=link->tx_game_id;
        pending_command=link->tx.pending.command;
        pending_current_state=current_state_is_pending(link);
        if (!tx_ack(&link->tx,frame)) return 0;
        link->last_rx_ms=now;
        if (pending_current_state) link->state_dirty=0;
        if (link->tx.status!=COMM_ACK_OK) {
            emit_event(handler,context,COMM_EVENT_TX_REJECTED,
                pending_command,link->tx.status,link->session,
                pending_game_id);
        } else {
            emit_event(handler,context,COMM_EVENT_TX_ACKED,
                pending_command,link->tx.status,link->session,
                pending_game_id);
        }
        link->tx_game_id=0;
        return 0;
    }
    if (frame->command!=CMD_BUTTON && frame->command!=CMD_GAME_RESULT)
        return 0;
    received=rx_check(&link->rx,frame);
    if (!received) return 0;
    link->last_rx_ms=now;
    if (received==2) {
        make_ack(frame,link->rx.status,reply);
        return 1;
    }
    status=deliver_business(frame,handler,context);
    rx_commit(&link->rx,frame,status);
    make_ack(frame,status,reply);
    return 1;
}

int comm_link_feed_byte(CommLink *link, u8 byte, u32 now,
    CommEventHandler handler, void *context, ProtoFrame *reply) {
    ProtoFrame frame;
    int result;
    if (!link) return 0;
    result=proto_feed(&link->parser,byte,now,&frame);
    if (result!=1) return 0;
    return comm_link_receive(link,&frame,now,handler,context,reply);
}

int comm_link_set_state(CommLink *link, u32 revision, u8 base,
    u8 display, u16 lease_ms) {
    ProtoFrame frame;
    if (!link) return 0;
    memset(&frame,0,sizeof(frame));
    frame.command=CMD_STATE;
    frame.length=8;
    frame.session=link->session;
    put_u32(frame.payload,revision);
    frame.payload[4]=base;
    frame.payload[5]=display;
    put_u16(frame.payload+6,lease_ms);
    if (!message_valid(&frame)) return 0;
    if (link->state_cached &&
        !memcmp(link->state_payload,frame.payload,8)) return 1;
    memcpy(link->state_payload,frame.payload,8);
    link->state_cached=1;
    link->state_dirty=1;
    return 1;
}

int comm_link_start_game(CommLink *link, u32 game_id, u8 game_type,
    u32 seed) {
    u8 payload[9];
    ProtoFrame frame;
    if (!link || !link->ready) return 0;
    memset(&frame,0,sizeof(frame));
    frame.command=CMD_GAME_START;
    frame.length=9;
    frame.session=link->session;
    put_u32(payload,game_id);
    payload[4]=game_type;
    put_u32(payload+5,seed);
    memcpy(frame.payload,payload,9);
    if (!message_valid(&frame)) return 0;
    if (!tx_begin(&link->tx,CMD_GAME_START,payload,9)) return 0;
    link->tx_game_id=game_id;
    return 1;
}

int comm_link_cancel_game(CommLink *link, u32 game_id) {
    u8 payload[4];
    if (!link || !link->ready || !game_id) return 0;
    put_u32(payload,game_id);
    if (!tx_begin(&link->tx,CMD_GAME_CANCEL,payload,4)) return 0;
    link->tx_game_id=game_id;
    return 1;
}
