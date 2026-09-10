#include <stdio.h>
#include <string.h>

#include "edgi_voice_shm.h"

static int test_wraparound_keeps_byte_order(void)
{
    EdgiVoiceRing ring;
    unsigned char first[EDGI_VOICE_RING_BYTES - 8u];
    unsigned char prefix[EDGI_VOICE_RING_BYTES - 16u];
    unsigned char second[16];
    unsigned char output[24];
    unsigned i;

    edgi_voice_ring_reset(&ring, 1u);
    for (i = 0; i < sizeof(first); i++) first[i] = (unsigned char)(i & 0xffu);
    for (i = 0; i < sizeof(second); i++) second[i] = (unsigned char)(0xa0u + i);
    if (edgi_voice_ring_write(&ring, first, sizeof(first)) != sizeof(first)) return 0;
    if (edgi_voice_ring_read(&ring, prefix, sizeof(prefix)) != sizeof(prefix)) return 0;
    if (edgi_voice_ring_write(&ring, second, sizeof(second)) != sizeof(second)) return 0;
    if (edgi_voice_ring_read(&ring, output, sizeof(output)) != sizeof(output)) return 0;
    return memcmp(output, first + sizeof(prefix), 8u) == 0 &&
           memcmp(output + 8u, second, sizeof(second)) == 0;
}

static int test_full_ring_refuses_overwrite(void)
{
    EdgiVoiceRing ring;
    unsigned char input[EDGI_VOICE_RING_BYTES - 1u];
    unsigned char marker = 0x5a;
    unsigned char output;

    memset(input, 0x31, sizeof(input));
    edgi_voice_ring_reset(&ring, 2u);
    if (edgi_voice_ring_write(&ring, input, sizeof(input)) != sizeof(input)) return 0;
    if (edgi_voice_ring_write(&ring, &marker, 1u) != 0u) return 0;
    if (ring.overflow_count != 1u) return 0;
    if (edgi_voice_ring_read(&ring, &output, 1u) != 1u) return 0;
    return output == 0x31 && edgi_voice_ring_available(&ring) == sizeof(input) - 1u;
}

static int test_reset_clears_data_and_advances_generation(void)
{
    EdgiVoiceRing ring;
    unsigned char byte = 7u;

    edgi_voice_ring_reset(&ring, 8u);
    if (edgi_voice_ring_write(&ring, &byte, 1u) != 1u) return 0;
    edgi_voice_ring_reset(&ring, 9u);
    return ring.generation == 9u && ring.head == 0u && ring.tail == 0u &&
           ring.overflow_count == 0u && edgi_voice_ring_available(&ring) == 0u;
}

static int test_shared_header_validation(void)
{
    EdgiVoiceShm shared;
    memset(&shared, 0, sizeof(shared));
    if (edgi_voice_shm_valid(&shared)) return 0;
    shared.magic = EDGI_VOICE_SHM_MAGIC;
    shared.version = EDGI_VOICE_SHM_VERSION + 1u;
    if (edgi_voice_shm_valid(&shared)) return 0;
    shared.version = EDGI_VOICE_SHM_VERSION;
    shared.total_bytes = sizeof(shared);
    return edgi_voice_shm_valid(&shared);
}

static int test_command_sequence_is_consumed_once(void)
{
    EdgiVoiceShm shared;
    unsigned command = 0u;
    uint32_t seen = 0u;
    memset(&shared, 0, sizeof(shared));
    shared.command = EDGI_VOICE_CMD_RECORD_START;
    shared.command_seq = 4u;
    if (!edgi_voice_command_take(&shared, &seen, &command)) return 0;
    if (command != EDGI_VOICE_CMD_RECORD_START || seen != 4u) return 0;
    return !edgi_voice_command_take(&shared, &seen, &command);
}

static int test_corrupt_indices_are_rejected(void)
{
    EdgiVoiceRing ring;
    unsigned char byte = 9u;
    edgi_voice_ring_reset(&ring, 1u);
    ring.head = EDGI_VOICE_RING_BYTES;
    if (edgi_voice_ring_write(&ring, &byte, 1u) != 0u) return 0;
    edgi_voice_ring_reset(&ring, 1u);
    ring.tail = EDGI_VOICE_RING_BYTES;
    return edgi_voice_ring_read(&ring, &byte, 1u) == 0u;
}

int main(void)
{
    if (!test_wraparound_keeps_byte_order()) {
        fprintf(stderr, "FAIL: voice ring wraparound\n"); return 1;
    }
    if (!test_full_ring_refuses_overwrite()) {
        fprintf(stderr, "FAIL: voice ring overwrite guard\n"); return 1;
    }
    if (!test_reset_clears_data_and_advances_generation()) {
        fprintf(stderr, "FAIL: voice ring reset\n"); return 1;
    }
    if (!test_shared_header_validation()) {
        fprintf(stderr, "FAIL: voice shared header validation\n"); return 1;
    }
    if (!test_command_sequence_is_consumed_once()) {
        fprintf(stderr, "FAIL: voice command deduplication\n"); return 1;
    }
    if (!test_corrupt_indices_are_rejected()) {
        fprintf(stderr, "FAIL: corrupt voice ring indices\n"); return 1;
    }
    puts("PASS: voice_ring");
    return 0;
}
