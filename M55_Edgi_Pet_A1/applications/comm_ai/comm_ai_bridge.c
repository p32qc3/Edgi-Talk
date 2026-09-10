/**
 * @file comm_ai_bridge.c
 * @brief Bridge the A-side pet application with B-side UART and offline AI.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>

#include "pet_app.h"
#include "comm_ai_bridge.h"
#include "comm_link.h"
#include "ai_client.h"
#include "ai_action.h"
#include "ai_offline.h"
#include "pet_service_status.h"

#ifndef EDGI_M55_UART_CONSOLE
#define EDGI_M55_UART_CONSOLE 1
#endif

#if !EDGI_M55_UART_CONSOLE
#include "drv_uart.h"
#endif

#define COMM_UART_NAME "uart5"
#define COMM_BAUDRATE 115200
#define COMM_THREAD_STACK_SIZE 2048
#define COMM_THREAD_PRIORITY 10
#define AI_COMMAND_MAX 32
#define AI_QUEUE_DEPTH 4

typedef struct {
    char text[AI_COMMAND_MAX];
} AiDemoCommand;

static rt_device_t uart_dev = RT_NULL;
static rt_thread_t comm_thread = RT_NULL;
static CommLink comm_link;
static u8 comm_session_counter = 1;
static struct rt_messagequeue ai_queue;
static rt_uint8_t ai_queue_pool[AI_QUEUE_DEPTH * sizeof(AiDemoCommand)];
static u8 ai_queue_ready;

static rt_err_t uart_rx_callback(rt_device_t dev, rt_size_t size)
{
    (void)dev;
    (void)size;
    return RT_EOK;
}

static u8 comm_event_handler(void *context, const CommEvent *event)
{
    (void)context;
    switch (event->type) {
    case COMM_EVENT_LINK_READY:
        rt_kprintf("[B] 51 link ready, session=%lu\n",
            (unsigned long)event->session);
        pet_app_report_link(event->session, PET_GAME_CAP_SIMON);
        pet_service_status_set_51_state(PET_51_LINK);
        break;
    case COMM_EVENT_LINK_LOST:
        rt_kprintf("[B] 51 link lost\n");
        pet_app_report_link(0, 0);
        pet_service_status_set_51_state(PET_51_WAIT);
        break;
    case COMM_EVENT_BUTTON:
        rt_kprintf("[B] Button %u from 51\n", event->key_id);
        break;
    case COMM_EVENT_GAME_RESULT:
    {
        int result;
        rt_kprintf("[B] Game result: id=%lu result=%u input=%u score=%u reaction=%lu\n",
            (unsigned long)event->game_id, event->game_result,
            event->had_input, event->score, (unsigned long)event->reaction_ms);
        result = pet_app_report_game_result(event->session, event->game_id,
            event->game_result, event->had_input, event->score,
            event->reaction_ms);
        if (result == RT_EOK) return COMM_ACK_OK;
        return result == -RT_EINVAL ? COMM_ACK_REJECTED : COMM_ACK_BUSY;
    }
    case COMM_EVENT_TX_ACKED:
        if (event->command == CMD_GAME_START) {
            rt_kprintf("[B] Game start acknowledged: id=%lu\n",
                (unsigned long)event->game_id);
            pet_app_report_game_started(event->session, event->game_id, 1);
        } else if (event->command == CMD_GAME_CANCEL) {
            rt_kprintf("[B] Game cancel acknowledged: id=%lu\n",
                (unsigned long)event->game_id);
            pet_app_report_game_cancelled(event->session, event->game_id);
        }
        break;
    case COMM_EVENT_TX_FAILED:
        rt_kprintf("[B] TX failed: cmd=%u (retry exhausted)\n",
            event->command);
        if (event->command == CMD_GAME_START)
            pet_app_report_game_started(event->session, event->game_id, 0);
        break;
    case COMM_EVENT_TX_REJECTED:
        rt_kprintf("[B] TX rejected: cmd=%u status=%u\n",
            event->command, event->status);
        if (event->command == CMD_GAME_START)
            pet_app_report_game_started(event->session, event->game_id, 0);
        break;
    }
    return COMM_ACK_OK;
}

static void write_frame(const ProtoFrame *frame)
{
    u8 wire[PROTO_WIRE_MAX];
    u8 length = proto_encode(frame, wire, sizeof(wire));
    if (length) rt_device_write(uart_dev, 0, wire, length);
}

static void comm_thread_entry(void *parameter)
{
    ProtoFrame tx_frame, reply;
    PetSnapshot snapshot;
    PetGameCommand game_command;
    u8 rx_byte;
    u8 have_game_command = 0;
    u32 now_ms;

    (void)parameter;
    rt_thread_mdelay(2000);
    now_ms = rt_tick_get_millisecond();
    comm_link_init(&comm_link, comm_session_counter++, now_ms);
    rt_kprintf("[B] Comm thread started on %s @ %d baud\n",
        COMM_UART_NAME, COMM_BAUDRATE);
    while (1) {
        now_ms = rt_tick_get_millisecond();
        while (rt_device_read(uart_dev, 0, &rx_byte, 1) == 1) {
            if (comm_link_feed_byte(&comm_link, rx_byte, now_ms,
                comm_event_handler, RT_NULL, &reply))
                write_frame(&reply);
        }
        if (comm_link_tick(&comm_link, now_ms, comm_event_handler,
            RT_NULL, &tx_frame))
            write_frame(&tx_frame);
        if (!comm_link_is_ready(&comm_link))
            have_game_command = 0;
        if (comm_link_is_ready(&comm_link) &&
            pet_app_get_snapshot(&snapshot) == RT_EOK)
            comm_link_set_state(&comm_link, snapshot.revision, snapshot.base,
                snapshot.display, 1000);
        if (comm_link_is_ready(&comm_link) && !have_game_command &&
            pet_app_take_game_command(&game_command) == RT_EOK)
            have_game_command = 1;
        if (comm_link_is_ready(&comm_link) && have_game_command) {
            if (game_command.action == PET_GAME_SEND_START) {
                if (comm_link_start_game(&comm_link, game_command.game_id,
                    game_command.game_type, game_command.seed))
                    have_game_command = 0;
            } else if (game_command.action == PET_GAME_SEND_CANCEL) {
                if (comm_link_cancel_game(&comm_link, game_command.game_id))
                    have_game_command = 0;
            } else {
                have_game_command = 0;
            }
        }
        rt_thread_mdelay(20);
    }
}

static void ai_demo_thread_entry(void *parameter)
{
    AiClient client;
    AiEvent event;
    AiActionDecision decision;
    AiDemoCommand command;
    char json[AI_RESPONSE_JSON_MAX];
    u32 request_id = 0;
    int json_size;
    int accepted;

    (void)parameter;
    ai_client_init(&client);
    rt_thread_mdelay(3000);
    rt_kprintf("[B] AI demo thread started (offline mode)\n");
    pet_service_status_set_ai_state(PET_AI_DEMO);
    while (1) {
        if (rt_mq_recv(&ai_queue, &command, sizeof(command),
            RT_WAITING_FOREVER) != (rt_ssize_t)sizeof(command))
            continue;
        pet_service_status_set_ai_state(PET_AI_BUSY);
        if (++request_id == 0) request_id = 1;
        accepted = ai_client_start(&client, request_id,
            rt_tick_get_millisecond());
        json_size = accepted ? ai_offline_respond(command.text, json,
            sizeof(json)) : 0;
        if (accepted)
            ai_response_complete(&client, request_id,
                json_size ? json : RT_NULL, (u32)json_size,
                rt_tick_get_millisecond());
        accepted = ai_client_take_event(&client, &event) &&
            ai_action_decide(&event, &decision);
        if (accepted && decision.start_simon)
            accepted = pet_app_request_game(PET_GAME_SIMON) == RT_EOK;
        else if (accepted && (!strcmp(command.text, "查看状态") ||
                 !strcmp(command.text, "状态") || !strcmp(command.text, "status")))
            accepted = pet_app_navigate(PET_PAGE_STATUS) == RT_EOK;
        rt_kprintf("[B] AI command '%s': %s\n", command.text,
            accepted ? "accepted" : "rejected");
        pet_service_status_set_ai_state(accepted ? PET_AI_DEMO : PET_AI_ERROR);
        if (!accepted) {
            rt_thread_mdelay(1000);
            pet_service_status_set_ai_state(PET_AI_DEMO);
        }
    }
}

int comm_ai_demo_submit(const char *command)
{
    AiDemoCommand item;
    size_t size;
    if (!ai_queue_ready || !command) return -RT_ERROR;
    size = strlen(command);
    if (!size || size >= sizeof(item.text)) return -RT_EINVAL;
    memset(&item, 0, sizeof(item));
    memcpy(item.text, command, size);
    return rt_mq_send(&ai_queue, &item, sizeof(item));
}

int comm_ai_bridge_init(void)
{
    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;
    rt_thread_t ai_thread;
    rt_err_t result;

    pet_service_status_init();
    pet_service_status_set_version("AB1");
    pet_service_status_set_ai_state(PET_AI_DEMO);
    pet_service_status_set_51_state(PET_51_WAIT);
#if !EDGI_M55_UART_CONSOLE
    /* Board init deliberately leaves uart2 to CM33. Register CM55's uart5 now. */
    rt_hw_uart_init();
#endif
    if (rt_mq_init(&ai_queue, "ai_cmd", ai_queue_pool,
        sizeof(AiDemoCommand), sizeof(ai_queue_pool), RT_IPC_FLAG_FIFO) != RT_EOK)
        return -RT_ERROR;
    ai_queue_ready = 1;
    uart_dev = rt_device_find(COMM_UART_NAME);
    if (!uart_dev) {
        rt_kprintf("[B] ERROR: Cannot find %s\n", COMM_UART_NAME);
        return -RT_ERROR;
    }
    config.baud_rate = COMM_BAUDRATE;
    result = rt_device_control(uart_dev, RT_DEVICE_CTRL_CONFIG, &config);
    if (result != RT_EOK) return result;
    result = rt_device_open(uart_dev,
        RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_INT_RX);
    if (result != RT_EOK) return result;
    rt_device_set_rx_indicate(uart_dev, uart_rx_callback);
    rt_kprintf("[B] %s opened @ %d baud\n", COMM_UART_NAME, COMM_BAUDRATE);

    comm_thread = rt_thread_create("comm_51", comm_thread_entry, RT_NULL,
        COMM_THREAD_STACK_SIZE, COMM_THREAD_PRIORITY, 20);
    if (!comm_thread) return -RT_ENOMEM;
    rt_thread_startup(comm_thread);
    ai_thread = rt_thread_create("ai_demo", ai_demo_thread_entry, RT_NULL,
        1536, COMM_THREAD_PRIORITY + 1, 20);
    if (!ai_thread) return -RT_ENOMEM;
    rt_thread_startup(ai_thread);
    rt_kprintf("[B] Comm & AI bridge initialized\n");
    return RT_EOK;
}
INIT_APP_EXPORT(comm_ai_bridge_init);

#if defined(RT_USING_FINSH) && defined(FINSH_USING_MSH)
#include <finsh.h>
static int cmd_ai_demo(int argc, char **argv)
{
    if (argc != 2) {
        rt_kprintf("usage: ai_demo simon|status|开始游戏|查看状态\n");
        return -RT_EINVAL;
    }
    return comm_ai_demo_submit(argv[1]);
}
MSH_CMD_EXPORT_ALIAS(cmd_ai_demo, ai_demo, Submit one allow-listed offline AI command);
#endif
