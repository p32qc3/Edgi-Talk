#include "../include/edgi_voice_m33.h"
#include "../include/edgi_audio_capture.h"
#include "../include/edgi_shm_cache.h"
#include "edgi_voice_shm.h"

#include <rtthread.h>

#define EDGI_VOICE_THREAD_STACK 2048u
#define EDGI_VOICE_THREAD_PRIORITY 9u
#define EDGI_VOICE_PLAY_CHUNK 2048u

static rt_thread_t s_voice_thread;
static volatile rt_bool_t s_recording;
static uint32_t s_command_sequence;
static uint32_t s_generation;
static rt_bool_t s_play_end_requested;

static void flush_control(EdgiVoiceShm *shared)
{
    edgi_shm_cache_flush(shared, 64);
}

static void invalidate_control(EdgiVoiceShm *shared)
{
    edgi_shm_cache_invalidate(shared, 64);
}

static void flush_ring_write(EdgiVoiceRing *ring, uint32_t old_head, size_t size)
{
    size_t first = size;
    if (first > EDGI_VOICE_RING_BYTES - old_head)
        first = EDGI_VOICE_RING_BYTES - old_head;
    edgi_shm_cache_flush((void *)&ring->data[old_head], (int)first);
    if (size > first)
        edgi_shm_cache_flush((void *)&ring->data[0], (int)(size - first));
    edgi_shm_cache_flush((void *)ring, 32);
}

static size_t downlink_read(EdgiVoiceShm *shared, uint8_t *output, size_t capacity)
{
    EdgiVoiceRing *ring = &shared->downlink;
    uint32_t old_tail;
    size_t available;
    size_t count;
    size_t first;
    edgi_shm_cache_invalidate((void *)ring, 32);
    old_tail = ring->tail;
    available = edgi_voice_ring_available(ring);
    count = available < capacity ? available : capacity;
    if (!count) return 0u;
    first = count;
    if (first > EDGI_VOICE_RING_BYTES - old_tail)
        first = EDGI_VOICE_RING_BYTES - old_tail;
    edgi_shm_cache_invalidate((void *)&ring->data[old_tail], (int)first);
    if (count > first)
        edgi_shm_cache_invalidate((void *)&ring->data[0], (int)(count - first));
    count = edgi_voice_ring_read(ring, output, count);
    edgi_shm_cache_flush((void *)ring, 32);
    return count;
}

static void playback_finish(EdgiVoiceShm *shared, uint32_t state)
{
    edgi_audio_playback_stop();
    edgi_audio_capture_set_paused(RT_FALSE);
    shared->playback_state = state;
    s_play_end_requested = RT_FALSE;
    flush_control(shared);
}

static void handle_command(EdgiVoiceShm *shared, unsigned command)
{
    switch (command)
    {
    case EDGI_VOICE_CMD_RECORD_START:
        s_generation++;
        edgi_voice_ring_reset(&shared->uplink, s_generation);
        edgi_shm_cache_flush((void *)&shared->uplink, 32);
        shared->record_bytes = 0u;
        shared->error_code = 0u;
        shared->record_state = EDGI_VOICE_RECORDING;
        s_recording = RT_TRUE;
        break;
    case EDGI_VOICE_CMD_RECORD_STOP:
        s_recording = RT_FALSE;
        shared->record_state = EDGI_VOICE_RECORD_STOPPED;
        break;
    case EDGI_VOICE_CMD_PLAY_START:
        s_recording = RT_FALSE;
        edgi_audio_capture_set_paused(RT_TRUE);
        shared->playback_bytes = 0u;
        shared->error_code = 0u;
        if (edgi_audio_playback_start() == RT_EOK)
        {
            shared->playback_state = EDGI_VOICE_PLAYING;
            s_play_end_requested = RT_FALSE;
        }
        else
        {
            shared->error_code = 1u;
            playback_finish(shared, EDGI_VOICE_PLAY_ERROR);
        }
        break;
    case EDGI_VOICE_CMD_PLAY_STOP:
        s_play_end_requested = RT_TRUE;
        break;
    case EDGI_VOICE_CMD_RESET:
        s_recording = RT_FALSE;
        edgi_voice_ring_reset(&shared->uplink, ++s_generation);
        edgi_voice_ring_reset(&shared->downlink, s_generation);
        edgi_shm_cache_flush((void *)&shared->uplink, 32);
        edgi_shm_cache_flush((void *)&shared->downlink, 32);
        shared->record_state = EDGI_VOICE_RECORD_IDLE;
        playback_finish(shared, EDGI_VOICE_PLAY_IDLE);
        break;
    default:
        shared->error_code = 2u;
        break;
    }
    shared->command_ack_seq = s_command_sequence;
    flush_control(shared);
}

static void voice_thread_entry(void *parameter)
{
    EdgiVoiceShm *shared = edgi_voice_shm_get();
    uint8_t playback[EDGI_VOICE_PLAY_CHUNK];
    unsigned command;
    (void)parameter;
    while (1)
    {
        size_t bytes;
        invalidate_control(shared);
        if (edgi_voice_command_take(shared, &s_command_sequence, &command))
            handle_command(shared, command);
        if (shared->playback_state == EDGI_VOICE_PLAYING)
        {
            bytes = downlink_read(shared, playback, sizeof(playback));
            if (bytes)
            {
                rt_ssize_t written = edgi_audio_playback_write(playback, bytes);
                if (written != (rt_ssize_t)bytes)
                {
                    shared->error_code = 3u;
                    playback_finish(shared, EDGI_VOICE_PLAY_ERROR);
                }
                else
                {
                    shared->playback_bytes += (uint32_t)bytes;
                    flush_control(shared);
                }
            }
            else if (s_play_end_requested)
                playback_finish(shared, EDGI_VOICE_PLAY_DRAINED);
        }
        shared->heartbeat++;
        flush_control(shared);
        rt_thread_mdelay(5);
    }
}

rt_err_t edgi_voice_m33_init(void)
{
    EdgiVoiceShm *shared = edgi_voice_shm_get();
    rt_memset(shared, 0, sizeof(*shared));
    edgi_voice_ring_reset(&shared->uplink, 1u);
    edgi_voice_ring_reset(&shared->downlink, 1u);
    s_generation = 1u;
    shared->version = EDGI_VOICE_SHM_VERSION;
    shared->total_bytes = (uint32_t)sizeof(*shared);
    shared->record_state = EDGI_VOICE_RECORD_IDLE;
    shared->playback_state = EDGI_VOICE_PLAY_IDLE;
    shared->magic = EDGI_VOICE_SHM_MAGIC;
    edgi_shm_cache_flush(shared, (int)sizeof(*shared));

    s_voice_thread = rt_thread_create("voice33", voice_thread_entry, RT_NULL,
        EDGI_VOICE_THREAD_STACK, EDGI_VOICE_THREAD_PRIORITY, 10);
    if (!s_voice_thread) return -RT_ENOMEM;
    rt_thread_startup(s_voice_thread);
    rt_kprintf("[voice] M33 audio bridge ready @0x%08X\n", EDGI_VOICE_SHM_ADDR);
    return RT_EOK;
}

rt_bool_t edgi_voice_m33_is_recording(void)
{
    return s_recording;
}

rt_err_t edgi_voice_m33_publish_pcm(const int16_t *pcm, rt_size_t samples)
{
    EdgiVoiceShm *shared = edgi_voice_shm_get();
    EdgiVoiceRing *ring = &shared->uplink;
    size_t bytes = samples * sizeof(*pcm);
    uint32_t old_head;
    if (!s_recording || !pcm || !samples) return -RT_EINVAL;
    edgi_shm_cache_invalidate((void *)ring, 32);
    old_head = ring->head;
    if (edgi_voice_ring_write(ring, pcm, bytes) != bytes)
    {
        s_recording = RT_FALSE;
        shared->record_state = EDGI_VOICE_RECORD_ERROR;
        shared->error_code = 4u;
        flush_control(shared);
        return -RT_EFULL;
    }
    flush_ring_write(ring, old_head, bytes);
    shared->record_bytes += (uint32_t)bytes;
    flush_control(shared);
    return RT_EOK;
}
