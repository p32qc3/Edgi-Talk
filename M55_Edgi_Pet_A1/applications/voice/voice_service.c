#include "voice_service.h"

#include "voice_config.h"
#include "voice_http.h"
#include "voice_shm_client.h"
#include "voice_wifi.h"

#include <rtthread.h>
#include <string.h>

#define VOICE_WORKER_STACK       12288u
#define VOICE_RECORD_CHUNK       2048u
#define VOICE_SHM_WAIT_MS        5000u
#define VOICE_PLAY_WAIT_MS       3000u

typedef struct
{
    VoiceSession session;
    VoiceShmClient shared;
    uint8_t *recording;
    size_t recording_bytes;
    uint32_t boot_id;
    uint8_t waiting_record_stop;
    volatile uint8_t toggle_requested;
    volatile uint8_t cancel_requested;
    VoiceView view;
    struct rt_mutex view_lock;
    uint32_t action_turn_id;
    VoiceAction action;
    volatile uint8_t action_ready;
} VoiceService;

static VoiceService s_service;

static uint32_t now_ms(void)
{
    return (uint32_t)((rt_tick_get() * 1000u) / RT_TICK_PER_SECOND);
}

static VoiceEvent event_make(VoiceEventType type, uint32_t turn_id)
{
    VoiceEvent event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.now_ms = now_ms();
    event.turn_id = turn_id;
    return event;
}

static VoiceViewState view_state_from_session(VoiceState state)
{
    switch (state)
    {
    case VOICE_STATE_LISTENING: return VOICE_VIEW_LISTENING;
    case VOICE_STATE_THINKING: return VOICE_VIEW_THINKING;
    case VOICE_STATE_SPEAKING: return VOICE_VIEW_SPEAKING;
    case VOICE_STATE_ERROR: return VOICE_VIEW_ERROR;
    default: return voice_wifi_is_online() ? VOICE_VIEW_READY :
                                             VOICE_VIEW_CONNECTING;
    }
}

static void view_update_state(void)
{
    rt_mutex_take(&s_service.view_lock, RT_WAITING_FOREVER);
    s_service.view.state = view_state_from_session(s_service.session.state);
    s_service.view.wifi_online = (uint8_t)voice_wifi_is_online();
    s_service.view.turn_id = s_service.session.active_turn_id;
    rt_mutex_release(&s_service.view_lock);
}

static void view_set_error(int error)
{
    rt_mutex_take(&s_service.view_lock, RT_WAITING_FOREVER);
    s_service.view.state = VOICE_VIEW_ERROR;
    s_service.view.error_code = error;
    s_service.view.wifi_online = (uint8_t)voice_wifi_is_online();
    rt_mutex_release(&s_service.view_lock);
}

static void view_set_response(const VoiceResponse *response)
{
    rt_mutex_take(&s_service.view_lock, RT_WAITING_FOREVER);
    rt_strncpy(s_service.view.transcript, response->transcript,
               sizeof(s_service.view.transcript) - 1u);
    rt_strncpy(s_service.view.reply_text, response->reply_text,
               sizeof(s_service.view.reply_text) - 1u);
    s_service.view.transcript[sizeof(s_service.view.transcript) - 1u] = '\0';
    s_service.view.reply_text[sizeof(s_service.view.reply_text) - 1u] = '\0';
    s_service.view.error_code = 0;
    rt_mutex_release(&s_service.view_lock);
}

static uint32_t pcm_energy(const uint8_t *bytes, size_t size)
{
    const int16_t *samples = (const int16_t *)bytes;
    size_t count = size / sizeof(*samples);
    uint64_t sum = 0u;
    size_t i;
    if (!count) return 0u;
    for (i = 0u; i < count; i++)
    {
        int32_t sample = samples[i];
        sum += (uint32_t)(sample < 0 ? -sample : sample);
    }
    return (uint32_t)(sum / count);
}

static int playback_sink(const uint8_t *data, size_t size, void *context)
{
    VoiceShmClient *shared = (VoiceShmClient *)context;
    size_t offset = 0u;
    uint32_t progress = now_ms();
    while (offset < size)
    {
        size_t piece = size - offset;
        size_t space = voice_shm_playback_space(shared);
        if (piece > space) piece = space;
        if (piece)
        {
            size_t written = voice_shm_write_playback(shared, data + offset,
                                                      piece);
            if (!written) return 0;
            offset += written;
            progress = now_ms();
        }
        else
        {
            if (voice_shm_playback_state(shared) == EDGI_VOICE_PLAY_ERROR ||
                (uint32_t)(now_ms() - progress) > VOICE_PLAY_WAIT_MS)
                return 0;
            rt_thread_mdelay(5);
        }
    }
    return 1;
}

static int wait_play_state(unsigned wanted, uint32_t timeout_ms)
{
    uint32_t started = now_ms();
    while ((uint32_t)(now_ms() - started) < timeout_ms)
    {
        unsigned state = voice_shm_playback_state(&s_service.shared);
        if (state == wanted) return 1;
        if (state == EDGI_VOICE_PLAY_ERROR) return 0;
        rt_thread_mdelay(5);
    }
    return 0;
}

static VoiceEffect dispatch(const VoiceEvent *event)
{
    VoiceEffect effect = voice_session_dispatch(&s_service.session, event);
    view_update_state();
    return effect;
}

static void handle_effect(VoiceEffect effect)
{
    VoiceResponse response;
    VoiceEvent event;
    char turn_id[24];
    int result;
    size_t audio_bytes;

    switch (effect.type)
    {
    case VOICE_EFFECT_RECORD_START:
        s_service.recording_bytes = 0u;
        s_service.waiting_record_stop = 0u;
        if (!voice_shm_send_command(&s_service.shared,
                                    EDGI_VOICE_CMD_RECORD_START))
            view_set_error(-10);
        break;
    case VOICE_EFFECT_RECORD_STOP:
        if (voice_shm_send_command(&s_service.shared,
                                   EDGI_VOICE_CMD_RECORD_STOP))
            s_service.waiting_record_stop = 1u;
        else
            view_set_error(-11);
        break;
    case VOICE_EFFECT_UPLOAD:
        if (!voice_wifi_is_online())
        {
            event = event_make(VOICE_EVENT_UPLOAD_FAILED, effect.turn_id);
            handle_effect(dispatch(&event));
            view_set_error(VOICE_HTTP_OFFLINE);
            break;
        }
        rt_snprintf(turn_id, sizeof(turn_id), "%08x-%08x",
                    (unsigned)s_service.boot_id, (unsigned)effect.turn_id);
        memset(&response, 0, sizeof(response));
        result = voice_http_post_turn(s_service.recording,
                                      s_service.recording_bytes,
                                      turn_id, &response);
        if (result != VOICE_HTTP_OK || strcmp(response.turn_id, turn_id))
        {
            event = event_make(VOICE_EVENT_UPLOAD_FAILED, effect.turn_id);
            handle_effect(dispatch(&event));
            view_set_error(result ? result : VOICE_HTTP_PROTOCOL);
            break;
        }
        view_set_response(&response);
        event = event_make(VOICE_EVENT_UPLOAD_OK, effect.turn_id);
        event.action = response.action;
        event.has_audio = 1u;
        effect = dispatch(&event);
        if (effect.type != VOICE_EFFECT_PLAY_START) break;
        if (!voice_shm_send_command(&s_service.shared,
                                    EDGI_VOICE_CMD_PLAY_START) ||
            !wait_play_state(EDGI_VOICE_PLAYING, 1000u))
        {
            event = event_make(VOICE_EVENT_PLAY_FAILED, effect.turn_id);
            handle_effect(dispatch(&event));
            view_set_error(-12);
            break;
        }
        audio_bytes = 0u;
        result = voice_http_get_audio(response.audio_id, playback_sink,
                                      &s_service.shared, &audio_bytes);
        (void)voice_shm_send_command(&s_service.shared,
                                     EDGI_VOICE_CMD_PLAY_STOP);
        if (result != VOICE_HTTP_OK || !audio_bytes ||
            !wait_play_state(EDGI_VOICE_PLAY_DRAINED, VOICE_PLAY_WAIT_MS))
        {
            event = event_make(VOICE_EVENT_PLAY_FAILED, effect.turn_id);
            handle_effect(dispatch(&event));
            view_set_error(result ? result : -13);
            break;
        }
        event = event_make(VOICE_EVENT_PLAY_DONE, effect.turn_id);
        handle_effect(dispatch(&event));
        break;
    case VOICE_EFFECT_CANCEL:
        s_service.waiting_record_stop = 0u;
        (void)voice_shm_send_command(&s_service.shared, EDGI_VOICE_CMD_RESET);
        break;
    case VOICE_EFFECT_EXECUTE_ACTION:
        if (effect.action != VOICE_ACTION_NONE)
        {
            s_service.action_turn_id = effect.turn_id;
            s_service.action = effect.action;
            s_service.action_ready = 1u;
        }
        break;
    default:
        break;
    }
}

static int wait_for_shared_audio(void)
{
    uint32_t started = now_ms();
    while ((uint32_t)(now_ms() - started) < VOICE_SHM_WAIT_MS)
    {
        if (voice_shm_client_init(&s_service.shared)) return 1;
        rt_thread_mdelay(100);
    }
    return 0;
}

static void worker_entry(void *parameter)
{
    uint8_t audio[VOICE_RECORD_CHUNK];
    (void)parameter;
    if (!wait_for_shared_audio())
    {
        view_set_error(-20);
        return;
    }
    (void)voice_wifi_start();
    while (1)
    {
        VoiceEvent event;
        VoiceEffect effect;
        size_t bytes;

        if (s_service.cancel_requested)
        {
            s_service.cancel_requested = 0u;
            event = event_make(VOICE_EVENT_CANCEL,
                               s_service.session.active_turn_id);
            handle_effect(dispatch(&event));
        }
        if (s_service.toggle_requested)
        {
            s_service.toggle_requested = 0u;
            event = event_make(VOICE_EVENT_TAP,
                               s_service.session.active_turn_id);
            handle_effect(dispatch(&event));
        }

        if (s_service.session.state == VOICE_STATE_LISTENING)
        {
            bytes = voice_shm_read_recording(&s_service.shared, audio,
                                             sizeof(audio));
            if (bytes)
            {
                if (s_service.recording_bytes + bytes <=
                    VOICE_RECORD_BUFFER_BYTES)
                {
                    memcpy(s_service.recording + s_service.recording_bytes,
                           audio, bytes);
                    s_service.recording_bytes += bytes;
                    event = event_make(VOICE_EVENT_AUDIO_FRAME,
                                       s_service.session.active_turn_id);
                    event.energy = pcm_energy(audio, bytes);
                    handle_effect(dispatch(&event));
                }
                else
                {
                    event = event_make(VOICE_EVENT_TICK,
                                       s_service.session.active_turn_id);
                    event.now_ms = s_service.session.phase_started_ms +
                                   VOICE_RECORD_MAX_MS;
                    handle_effect(dispatch(&event));
                }
            }
        }

        if (s_service.waiting_record_stop &&
            voice_shm_record_state(&s_service.shared) ==
            EDGI_VOICE_RECORD_STOPPED)
        {
            s_service.waiting_record_stop = 0u;
            event = event_make(VOICE_EVENT_RECORD_STOPPED,
                               s_service.session.active_turn_id);
            handle_effect(dispatch(&event));
        }

        event = event_make(VOICE_EVENT_TICK,
                           s_service.session.active_turn_id);
        effect = dispatch(&event);
        handle_effect(effect);
        view_update_state();
        rt_thread_mdelay(10);
    }
}

void voice_service_toggle(void)
{
    if (s_service.view.configured) s_service.toggle_requested = 1u;
}

void voice_service_cancel(void)
{
    if (s_service.view.configured) s_service.cancel_requested = 1u;
}

void voice_service_get_view(VoiceView *view)
{
    if (!view) return;
    rt_mutex_take(&s_service.view_lock, RT_WAITING_FOREVER);
    *view = s_service.view;
    rt_mutex_release(&s_service.view_lock);
}

int voice_service_take_action(uint32_t *turn_id, VoiceAction *action)
{
    if (!turn_id || !action || !s_service.action_ready) return 0;
    *turn_id = s_service.action_turn_id;
    *action = s_service.action;
    s_service.action_ready = 0u;
    return 1;
}

static int voice_service_init(void)
{
    rt_thread_t worker;
    memset(&s_service, 0, sizeof(s_service));
    rt_mutex_init(&s_service.view_lock, "voicevw", RT_IPC_FLAG_PRIO);
    voice_session_init(&s_service.session);
    s_service.view.configured = (uint8_t)voice_config_is_ready();
    s_service.view.state = s_service.view.configured ?
                           VOICE_VIEW_CONNECTING : VOICE_VIEW_UNCONFIGURED;
    if (!s_service.view.configured)
    {
        rt_kprintf("[voice] config not set; copy voice_private_config.h.example\n");
        return RT_EOK;
    }
    s_service.recording = (uint8_t *)rt_malloc(VOICE_RECORD_BUFFER_BYTES);
    if (!s_service.recording)
    {
        s_service.view.state = VOICE_VIEW_ERROR;
        s_service.view.error_code = -30;
        rt_kprintf("[voice] recording buffer allocation failed\n");
        return -RT_ENOMEM;
    }
    s_service.boot_id = (uint32_t)rt_tick_get() ^ 0x564f4943u;
    worker = rt_thread_create("voice", worker_entry, RT_NULL,
                              VOICE_WORKER_STACK, 18, 10);
    if (!worker)
    {
        rt_free(s_service.recording);
        s_service.recording = RT_NULL;
        s_service.view.state = VOICE_VIEW_ERROR;
        s_service.view.error_code = -31;
        return -RT_ENOMEM;
    }
    rt_thread_startup(worker);
    rt_kprintf("[voice] standalone cloud voice service ready\n");
    return RT_EOK;
}
INIT_APP_EXPORT(voice_service_init);
