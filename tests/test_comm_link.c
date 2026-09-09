#include <stdio.h>
#include <string.h>

#include "comm_link.h"

static unsigned g_event_count;
static CommEvent g_event;

static u8 capture_event(void *context, const CommEvent *event)
{
    (void)context;
    g_event_count++;
    g_event = *event;
    return COMM_ACK_OK;
}

static int make_ready(CommLink *link, u32 session)
{
    ProtoFrame ready;
    ProtoFrame reply;

    memset(&ready, 0, sizeof(ready));
    ready.command = CMD_READY;
    ready.session = session;
    return comm_link_receive(link, &ready, 1, capture_event, NULL, &reply) == 0 &&
           comm_link_is_ready(link);
}

static int test_game_start_ack_reports_identity_once(void)
{
    CommLink link;
    ProtoFrame outgoing;
    ProtoFrame ack;
    const u32 session = 0x10203040UL;
    const u32 game_id = 0x55667788UL;

    memset(&g_event, 0, sizeof(g_event));
    g_event_count = 0;
    if (!comm_link_init(&link, session, 0) || !make_ready(&link, session))
        return 0;
    g_event_count = 0;

    if (!comm_link_start_game(&link, game_id, 1, 0x12345678UL) ||
        comm_link_tick(&link, 2, capture_event, NULL, &outgoing) != 1)
        return 0;

    memset(&ack, 0, sizeof(ack));
    ack.command = CMD_ACK;
    ack.length = 2;
    ack.sequence = outgoing.sequence;
    ack.session = session;
    ack.payload[0] = CMD_GAME_START;
    ack.payload[1] = COMM_ACK_OK;

    if (comm_link_receive(&link, &ack, 3, capture_event, NULL, &outgoing) != 0)
        return 0;
    if (g_event_count != 1 || g_event.type != COMM_EVENT_TX_ACKED ||
        g_event.command != CMD_GAME_START || g_event.status != COMM_ACK_OK ||
        g_event.session != session || g_event.game_id != game_id)
        return 0;

    comm_link_receive(&link, &ack, 4, capture_event, NULL, &outgoing);
    return g_event_count == 1;
}

int main(void)
{
    if (!test_game_start_ack_reports_identity_once()) {
        fprintf(stderr, "FAIL: acknowledged game start must be reported once with session and game id\n");
        return 1;
    }
    puts("PASS: comm_link");
    return 0;
}
