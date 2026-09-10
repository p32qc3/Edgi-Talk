#include "edgi_voice_shm.h"

static int ring_indices_valid(const EdgiVoiceRing *ring)
{
    return ring && ring->head < EDGI_VOICE_RING_BYTES &&
           ring->tail < EDGI_VOICE_RING_BYTES;
}


void edgi_voice_ring_reset(EdgiVoiceRing *ring, uint32_t generation)
{
    if (!ring) return;
    ring->head = 0u;
    ring->tail = 0u;
    ring->generation = generation;
    ring->overflow_count = 0u;
}


size_t edgi_voice_ring_available(const EdgiVoiceRing *ring)
{
    uint32_t head;
    uint32_t tail;
    if (!ring) return 0u;
    head = ring->head;
    tail = ring->tail;
    if (head >= EDGI_VOICE_RING_BYTES || tail >= EDGI_VOICE_RING_BYTES) return 0u;
    if (head >= tail) return (size_t)(head - tail);
    return (size_t)(EDGI_VOICE_RING_BYTES - tail + head);
}


size_t edgi_voice_ring_space(const EdgiVoiceRing *ring)
{
    return EDGI_VOICE_RING_BYTES - 1u - edgi_voice_ring_available(ring);
}


size_t edgi_voice_ring_write(EdgiVoiceRing *ring, const void *data, size_t size)
{
    const uint8_t *input = (const uint8_t *)data;
    uint32_t head;
    size_t i;
    if (!ring_indices_valid(ring) || (!data && size)) return 0u;
    if (size > edgi_voice_ring_space(ring))
    {
        ring->overflow_count++;
        return 0u;
    }
    head = ring->head;
    for (i = 0; i < size; i++)
    {
        ring->data[head] = input[i];
        head++;
        if (head == EDGI_VOICE_RING_BYTES) head = 0u;
    }
    ring->head = head;
    return size;
}


size_t edgi_voice_ring_read(EdgiVoiceRing *ring, void *data, size_t capacity)
{
    uint8_t *output = (uint8_t *)data;
    uint32_t tail;
    size_t available;
    size_t count;
    size_t i;
    if (!ring_indices_valid(ring) || (!data && capacity)) return 0u;
    available = edgi_voice_ring_available(ring);
    count = available < capacity ? available : capacity;
    tail = ring->tail;
    for (i = 0; i < count; i++)
    {
        output[i] = ring->data[tail];
        tail++;
        if (tail == EDGI_VOICE_RING_BYTES) tail = 0u;
    }
    ring->tail = tail;
    return count;
}


int edgi_voice_shm_valid(const EdgiVoiceShm *shared)
{
    return shared &&
           shared->magic == EDGI_VOICE_SHM_MAGIC &&
           shared->version == EDGI_VOICE_SHM_VERSION &&
           shared->total_bytes == (uint32_t)sizeof(*shared);
}


int edgi_voice_command_take(const EdgiVoiceShm *shared,
                            uint32_t *last_sequence,
                            unsigned *command)
{
    uint32_t sequence;
    if (!shared || !last_sequence || !command) return 0;
    sequence = shared->command_seq;
    if (!sequence || sequence == *last_sequence) return 0;
    *command = (unsigned)shared->command;
    *last_sequence = sequence;
    return 1;
}
