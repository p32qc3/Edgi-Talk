#ifndef VOICE_SHM_CLIENT_H
#define VOICE_SHM_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#include "edgi_voice_shm.h"

typedef struct
{
    EdgiVoiceShm *shared;
    uint32_t command_sequence;
} VoiceShmClient;

int voice_shm_client_init(VoiceShmClient *client);
int voice_shm_send_command(VoiceShmClient *client, unsigned command);
unsigned voice_shm_record_state(VoiceShmClient *client);
unsigned voice_shm_playback_state(VoiceShmClient *client);
size_t voice_shm_read_recording(VoiceShmClient *client, void *data,
                                size_t capacity);
size_t voice_shm_write_playback(VoiceShmClient *client, const void *data,
                                size_t size);
size_t voice_shm_playback_space(VoiceShmClient *client);

#endif /* VOICE_SHM_CLIENT_H */
