#ifndef EDGI_VOICE_SHM_H
#define EDGI_VOICE_SHM_H

#include <stddef.h>
#include <stdint.h>

#define EDGI_VOICE_SHM_ADDR       0x261C1000u
#define EDGI_VOICE_SHM_REGION_SIZE 0x00010000u
#define EDGI_VOICE_SHM_MAGIC      0x564F4943u
#define EDGI_VOICE_SHM_VERSION    1u
#define EDGI_VOICE_RING_BYTES     30720u

enum EdgiVoiceCommand
{
    EDGI_VOICE_CMD_NONE = 0u,
    EDGI_VOICE_CMD_RECORD_START = 1u,
    EDGI_VOICE_CMD_RECORD_STOP = 2u,
    EDGI_VOICE_CMD_PLAY_START = 3u,
    EDGI_VOICE_CMD_PLAY_STOP = 4u,
    EDGI_VOICE_CMD_RESET = 5u
};

enum EdgiVoiceRecordState
{
    EDGI_VOICE_RECORD_IDLE = 0u,
    EDGI_VOICE_RECORDING = 1u,
    EDGI_VOICE_RECORD_STOPPED = 2u,
    EDGI_VOICE_RECORD_ERROR = 3u
};

enum EdgiVoicePlaybackState
{
    EDGI_VOICE_PLAY_IDLE = 0u,
    EDGI_VOICE_PLAYING = 1u,
    EDGI_VOICE_PLAY_DRAINED = 2u,
    EDGI_VOICE_PLAY_ERROR = 3u
};

typedef struct
{
    volatile uint32_t head;
    volatile uint32_t tail;
    volatile uint32_t generation;
    volatile uint32_t overflow_count;
    uint32_t _metadata_padding[4];
    volatile uint8_t data[EDGI_VOICE_RING_BYTES];
} EdgiVoiceRing;

typedef struct
{
    volatile uint32_t magic;
    volatile uint32_t version;
    volatile uint32_t total_bytes;
    volatile uint32_t flags;
    volatile uint32_t command_seq;
    volatile uint32_t command_ack_seq;
    volatile uint32_t command;
    volatile uint32_t record_state;
    volatile uint32_t playback_state;
    volatile uint32_t error_code;
    volatile uint32_t record_bytes;
    volatile uint32_t playback_bytes;
    volatile uint32_t heartbeat;
    uint32_t _header_padding[3];
    EdgiVoiceRing uplink;
    EdgiVoiceRing downlink;
} EdgiVoiceShm;

typedef char edgi_voice_shm_must_fit[
    (sizeof(EdgiVoiceShm) <= EDGI_VOICE_SHM_REGION_SIZE) ? 1 : -1];
typedef char edgi_voice_ring_must_be_cache_aligned[
    ((sizeof(EdgiVoiceRing) & 31u) == 0u) ? 1 : -1];

void edgi_voice_ring_reset(EdgiVoiceRing *ring, uint32_t generation);
size_t edgi_voice_ring_available(const EdgiVoiceRing *ring);
size_t edgi_voice_ring_space(const EdgiVoiceRing *ring);
size_t edgi_voice_ring_write(EdgiVoiceRing *ring, const void *data, size_t size);
size_t edgi_voice_ring_read(EdgiVoiceRing *ring, void *data, size_t capacity);
int edgi_voice_shm_valid(const EdgiVoiceShm *shared);
int edgi_voice_command_take(const EdgiVoiceShm *shared,
                            uint32_t *last_sequence,
                            unsigned *command);

static inline EdgiVoiceShm *edgi_voice_shm_get(void)
{
    return (EdgiVoiceShm *)(uintptr_t)EDGI_VOICE_SHM_ADDR;
}

#endif /* EDGI_VOICE_SHM_H */
