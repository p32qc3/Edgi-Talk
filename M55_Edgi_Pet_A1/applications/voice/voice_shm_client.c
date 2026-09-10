#include "voice_shm_client.h"

#include <string.h>
#include "cy_device_headers.h"

#define VOICE_CACHE_LINE 32u

static void cache_invalidate(void *address, size_t size)
{
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    uintptr_t value;
    uintptr_t start;
    uintptr_t end;
    if (!address || !size) return;
    value = (uintptr_t)address;
    start = value & ~(uintptr_t)(VOICE_CACHE_LINE - 1u);
    end = (value + size + VOICE_CACHE_LINE - 1u) &
          ~(uintptr_t)(VOICE_CACHE_LINE - 1u);
    SCB_InvalidateDCache_by_Addr((void *)start, (int32_t)(end - start));
    __DSB();
    __ISB();
#else
    (void)address;
    (void)size;
#endif
}

static void cache_flush(void *address, size_t size)
{
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    uintptr_t value;
    uintptr_t start;
    uintptr_t end;
    if (!address || !size) return;
    value = (uintptr_t)address;
    start = value & ~(uintptr_t)(VOICE_CACHE_LINE - 1u);
    end = (value + size + VOICE_CACHE_LINE - 1u) &
          ~(uintptr_t)(VOICE_CACHE_LINE - 1u);
    SCB_CleanDCache_by_Addr((void *)start, (int32_t)(end - start));
    __DSB();
    __ISB();
#else
    (void)address;
    (void)size;
#endif
}

static size_t ring_read_cached(EdgiVoiceRing *ring, void *data,
                               size_t capacity)
{
    uint32_t tail;
    size_t count;
    size_t first;
    cache_invalidate(ring, 32u);
    if (ring->head >= EDGI_VOICE_RING_BYTES ||
        ring->tail >= EDGI_VOICE_RING_BYTES) return 0u;
    tail = ring->tail;
    count = edgi_voice_ring_available(ring);
    if (count > capacity) count = capacity;
    if (!count) return 0u;
    first = count;
    if (first > EDGI_VOICE_RING_BYTES - tail)
        first = EDGI_VOICE_RING_BYTES - tail;
    cache_invalidate((void *)&ring->data[tail], first);
    if (count > first) cache_invalidate((void *)&ring->data[0], count - first);
    count = edgi_voice_ring_read(ring, data, count);
    cache_flush(ring, 32u);
    return count;
}

static size_t ring_write_cached(EdgiVoiceRing *ring, const void *data,
                                size_t size)
{
    uint32_t head;
    size_t first;
    cache_invalidate(ring, 32u);
    if (ring->head >= EDGI_VOICE_RING_BYTES ||
        ring->tail >= EDGI_VOICE_RING_BYTES) return 0u;
    head = ring->head;
    if (edgi_voice_ring_write(ring, data, size) != size) return 0u;
    first = size;
    if (first > EDGI_VOICE_RING_BYTES - head)
        first = EDGI_VOICE_RING_BYTES - head;
    cache_flush((void *)&ring->data[head], first);
    if (size > first) cache_flush((void *)&ring->data[0], size - first);
    cache_flush(ring, 32u);
    return size;
}

int voice_shm_client_init(VoiceShmClient *client)
{
    EdgiVoiceShm *shared;
    if (!client) return 0;
    memset(client, 0, sizeof(*client));
    shared = edgi_voice_shm_get();
    cache_invalidate(shared, 64u);
    if (!edgi_voice_shm_valid(shared)) return 0;
    client->shared = shared;
    client->command_sequence = shared->command_seq;
    return 1;
}

int voice_shm_send_command(VoiceShmClient *client, unsigned command)
{
    EdgiVoiceShm *shared;
    if (!client || !client->shared || command > EDGI_VOICE_CMD_RESET)
        return 0;
    shared = client->shared;
    cache_invalidate(shared, 32u);
    client->command_sequence++;
    if (!client->command_sequence) client->command_sequence++;
    shared->command = command;
    shared->command_seq = client->command_sequence;
    cache_flush(shared, 32u);
    return 1;
}

unsigned voice_shm_record_state(VoiceShmClient *client)
{
    if (!client || !client->shared) return EDGI_VOICE_RECORD_ERROR;
    cache_invalidate((uint8_t *)client->shared + 32u, 32u);
    return (unsigned)client->shared->record_state;
}

unsigned voice_shm_playback_state(VoiceShmClient *client)
{
    if (!client || !client->shared) return EDGI_VOICE_PLAY_ERROR;
    cache_invalidate((uint8_t *)client->shared + 32u, 32u);
    return (unsigned)client->shared->playback_state;
}

size_t voice_shm_read_recording(VoiceShmClient *client, void *data,
                                size_t capacity)
{
    if (!client || !client->shared || !data || !capacity) return 0u;
    return ring_read_cached(&client->shared->uplink, data, capacity);
}

size_t voice_shm_write_playback(VoiceShmClient *client, const void *data,
                                size_t size)
{
    if (!client || !client->shared || !data || !size) return 0u;
    return ring_write_cached(&client->shared->downlink, data, size);
}

size_t voice_shm_playback_space(VoiceShmClient *client)
{
    if (!client || !client->shared) return 0u;
    cache_invalidate(&client->shared->downlink, 32u);
    return edgi_voice_ring_space(&client->shared->downlink);
}
