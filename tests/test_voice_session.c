#include <stdio.h>
#include <string.h>

#include "voice_session.h"

static VoiceEffect send_event(VoiceSession *session, VoiceEventType type,
                              unsigned now_ms, unsigned turn_id,
                              unsigned energy, unsigned has_audio,
                              VoiceAction action)
{
    VoiceEvent event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.now_ms = now_ms;
    event.turn_id = turn_id;
    event.energy = energy;
    event.has_audio = has_audio;
    event.action = action;
    return voice_session_dispatch(session, &event);
}

static int test_complete_turn(void)
{
    VoiceSession session;
    VoiceEffect effect;
    voice_session_init(&session);

    effect = send_event(&session, VOICE_EVENT_TAP, 100u, 0u, 0u, 0u,
                        VOICE_ACTION_NONE);
    if (session.state != VOICE_STATE_LISTENING ||
        effect.type != VOICE_EFFECT_RECORD_START || !effect.turn_id)
        return 0;

    effect = send_event(&session, VOICE_EVENT_TAP, 500u, 0u, 0u, 0u,
                        VOICE_ACTION_NONE);
    if (session.state != VOICE_STATE_THINKING ||
        effect.type != VOICE_EFFECT_RECORD_STOP)
        return 0;
    effect = send_event(&session, VOICE_EVENT_RECORD_STOPPED, 510u,
                        effect.turn_id, 0u, 0u, VOICE_ACTION_NONE);
    if (effect.type != VOICE_EFFECT_UPLOAD) return 0;

    effect = send_event(&session, VOICE_EVENT_UPLOAD_OK, 900u,
                        effect.turn_id, 0u, 1u, VOICE_ACTION_SHOW_STATUS);
    if (session.state != VOICE_STATE_SPEAKING ||
        effect.type != VOICE_EFFECT_PLAY_START)
        return 0;
    effect = send_event(&session, VOICE_EVENT_PLAY_DONE, 1200u,
                        effect.turn_id, 0u, 0u, VOICE_ACTION_NONE);
    return session.state == VOICE_STATE_IDLE &&
           effect.type == VOICE_EFFECT_EXECUTE_ACTION &&
           effect.action == VOICE_ACTION_SHOW_STATUS;
}

static int test_silence_and_hard_stop(void)
{
    VoiceSession session;
    VoiceEffect effect;
    voice_session_init(&session);
    (void)send_event(&session, VOICE_EVENT_TAP, 0u, 0u, 0u, 0u,
                     VOICE_ACTION_NONE);
    (void)send_event(&session, VOICE_EVENT_AUDIO_FRAME, 100u, 0u,
                     VOICE_SPEECH_ENERGY + 1u, 0u, VOICE_ACTION_NONE);
    effect = send_event(&session, VOICE_EVENT_AUDIO_FRAME, 1599u, 0u, 0u,
                        0u, VOICE_ACTION_NONE);
    if (effect.type != VOICE_EFFECT_NONE) return 0;
    effect = send_event(&session, VOICE_EVENT_AUDIO_FRAME, 1600u, 0u, 0u,
                        0u, VOICE_ACTION_NONE);
    if (effect.type != VOICE_EFFECT_RECORD_STOP) return 0;

    voice_session_init(&session);
    (void)send_event(&session, VOICE_EVENT_TAP, 10u, 0u, 0u, 0u,
                     VOICE_ACTION_NONE);
    effect = send_event(&session, VOICE_EVENT_TICK, 8009u, 0u, 0u, 0u,
                        VOICE_ACTION_NONE);
    if (effect.type != VOICE_EFFECT_NONE) return 0;
    effect = send_event(&session, VOICE_EVENT_TICK, 8010u, 0u, 0u, 0u,
                        VOICE_ACTION_NONE);
    return effect.type == VOICE_EFFECT_RECORD_STOP &&
           session.state == VOICE_STATE_THINKING;
}

static int test_timeout_cancel_wifi_and_duplicate(void)
{
    VoiceSession session;
    VoiceEffect effect;
    unsigned turn;
    voice_session_init(&session);
    effect = send_event(&session, VOICE_EVENT_TAP, 0u, 0u, 0u, 0u,
                        VOICE_ACTION_NONE);
    turn = effect.turn_id;
    effect = send_event(&session, VOICE_EVENT_TAP, 10u, turn, 0u, 0u,
                        VOICE_ACTION_NONE);
    (void)send_event(&session, VOICE_EVENT_RECORD_STOPPED, 20u, turn, 0u,
                     0u, VOICE_ACTION_NONE);
    effect = send_event(&session, VOICE_EVENT_TICK, 25019u, turn, 0u, 0u,
                        VOICE_ACTION_NONE);
    if (effect.type != VOICE_EFFECT_NONE) return 0;
    effect = send_event(&session, VOICE_EVENT_TICK, 25020u, turn, 0u, 0u,
                        VOICE_ACTION_NONE);
    if (session.state != VOICE_STATE_ERROR ||
        effect.type != VOICE_EFFECT_CANCEL) return 0;

    effect = send_event(&session, VOICE_EVENT_TAP, 26000u, 0u, 0u, 0u,
                        VOICE_ACTION_NONE);
    if (session.state != VOICE_STATE_LISTENING ||
        effect.type != VOICE_EFFECT_RECORD_START) return 0;
    effect = send_event(&session, VOICE_EVENT_WIFI_LOST, 26001u,
                        effect.turn_id, 0u, 0u, VOICE_ACTION_NONE);
    if (session.state != VOICE_STATE_ERROR ||
        effect.type != VOICE_EFFECT_CANCEL) return 0;
    effect = send_event(&session, VOICE_EVENT_CANCEL, 26002u, 0u, 0u, 0u,
                        VOICE_ACTION_NONE);
    if (session.state != VOICE_STATE_IDLE ||
        effect.type != VOICE_EFFECT_CANCEL) return 0;

    effect = send_event(&session, VOICE_EVENT_TAP, 27000u, 0u, 0u, 0u,
                        VOICE_ACTION_NONE);
    turn = effect.turn_id;
    (void)send_event(&session, VOICE_EVENT_TAP, 27010u, turn, 0u, 0u,
                     VOICE_ACTION_NONE);
    (void)send_event(&session, VOICE_EVENT_RECORD_STOPPED, 27020u, turn,
                     0u, 0u, VOICE_ACTION_NONE);
    effect = send_event(&session, VOICE_EVENT_UPLOAD_OK, 27100u, turn - 1u,
                        0u, 1u, VOICE_ACTION_HOME);
    return effect.type == VOICE_EFFECT_NONE &&
           session.state == VOICE_STATE_THINKING;
}

int main(void)
{
    if (!test_complete_turn()) { fprintf(stderr, "FAIL: complete voice turn\n"); return 1; }
    if (!test_silence_and_hard_stop()) { fprintf(stderr, "FAIL: voice stop rules\n"); return 1; }
    if (!test_timeout_cancel_wifi_and_duplicate()) { fprintf(stderr, "FAIL: voice recovery\n"); return 1; }
    puts("PASS: voice_session");
    return 0;
}
