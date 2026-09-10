#include "voice_session.h"

#include <string.h>

static VoiceEffect effect_make(VoiceEffectType type, uint32_t turn_id,
                               VoiceAction action)
{
    VoiceEffect effect;
    effect.type = type;
    effect.turn_id = turn_id;
    effect.action = action;
    return effect;
}

static int elapsed(uint32_t now_ms, uint32_t start_ms, uint32_t limit_ms)
{
    return (uint32_t)(now_ms - start_ms) >= limit_ms;
}

static int active_turn_matches(const VoiceSession *session,
                               const VoiceEvent *event)
{
    return event->turn_id == session->active_turn_id;
}

static VoiceEffect begin_recording(VoiceSession *session, uint32_t now_ms)
{
    session->next_turn_id++;
    if (!session->next_turn_id) session->next_turn_id++;
    session->active_turn_id = session->next_turn_id;
    session->state = VOICE_STATE_LISTENING;
    session->phase_started_ms = now_ms;
    session->last_speech_ms = now_ms;
    session->speech_seen = 0u;
    session->pending_action = VOICE_ACTION_NONE;
    return effect_make(VOICE_EFFECT_RECORD_START,
                       session->active_turn_id, VOICE_ACTION_NONE);
}

static VoiceEffect stop_recording(VoiceSession *session, uint32_t now_ms)
{
    session->state = VOICE_STATE_THINKING;
    session->phase_started_ms = now_ms;
    return effect_make(VOICE_EFFECT_RECORD_STOP,
                       session->active_turn_id, VOICE_ACTION_NONE);
}

void voice_session_init(VoiceSession *session)
{
    if (!session) return;
    memset(session, 0, sizeof(*session));
    session->state = VOICE_STATE_IDLE;
}

VoiceEffect voice_session_dispatch(VoiceSession *session,
                                   const VoiceEvent *event)
{
    VoiceAction action;
    if (!session || !event)
        return effect_make(VOICE_EFFECT_NONE, 0u, VOICE_ACTION_NONE);

    if (event->type == VOICE_EVENT_CANCEL)
    {
        uint32_t turn_id = session->active_turn_id;
        session->state = VOICE_STATE_IDLE;
        session->active_turn_id = 0u;
        session->pending_action = VOICE_ACTION_NONE;
        return effect_make(VOICE_EFFECT_CANCEL, turn_id, VOICE_ACTION_NONE);
    }

    if (event->type == VOICE_EVENT_WIFI_LOST &&
        session->state != VOICE_STATE_IDLE)
    {
        session->state = VOICE_STATE_ERROR;
        session->pending_action = VOICE_ACTION_NONE;
        return effect_make(VOICE_EFFECT_CANCEL, session->active_turn_id,
                           VOICE_ACTION_NONE);
    }

    if (event->type == VOICE_EVENT_TAP)
    {
        if (session->state == VOICE_STATE_IDLE ||
            session->state == VOICE_STATE_ERROR)
            return begin_recording(session, event->now_ms);
        if (session->state == VOICE_STATE_LISTENING)
            return stop_recording(session, event->now_ms);
        return effect_make(VOICE_EFFECT_NONE, session->active_turn_id,
                           VOICE_ACTION_NONE);
    }

    if (session->state == VOICE_STATE_LISTENING)
    {
        if (event->type == VOICE_EVENT_AUDIO_FRAME)
        {
            if (event->energy >= VOICE_SPEECH_ENERGY)
            {
                session->speech_seen = 1u;
                session->last_speech_ms = event->now_ms;
            }
            else if (session->speech_seen &&
                     elapsed(event->now_ms, session->last_speech_ms,
                             VOICE_SILENCE_STOP_MS))
                return stop_recording(session, event->now_ms);
        }
        if (event->type == VOICE_EVENT_TICK &&
            elapsed(event->now_ms, session->phase_started_ms,
                    VOICE_RECORD_MAX_MS))
            return stop_recording(session, event->now_ms);
        return effect_make(VOICE_EFFECT_NONE, session->active_turn_id,
                           VOICE_ACTION_NONE);
    }

    if (session->state == VOICE_STATE_THINKING)
    {
        if ((event->type == VOICE_EVENT_RECORD_STOPPED ||
             event->type == VOICE_EVENT_UPLOAD_OK ||
             event->type == VOICE_EVENT_UPLOAD_FAILED) &&
            !active_turn_matches(session, event))
            return effect_make(VOICE_EFFECT_NONE, session->active_turn_id,
                               VOICE_ACTION_NONE);

        if (event->type == VOICE_EVENT_RECORD_STOPPED)
        {
            session->phase_started_ms = event->now_ms;
            return effect_make(VOICE_EFFECT_UPLOAD, session->active_turn_id,
                               VOICE_ACTION_NONE);
        }
        if (event->type == VOICE_EVENT_UPLOAD_FAILED ||
            (event->type == VOICE_EVENT_TICK &&
             elapsed(event->now_ms, session->phase_started_ms,
                     VOICE_CLOUD_TIMEOUT_MS)))
        {
            session->state = VOICE_STATE_ERROR;
            session->pending_action = VOICE_ACTION_NONE;
            return effect_make(VOICE_EFFECT_CANCEL, session->active_turn_id,
                               VOICE_ACTION_NONE);
        }
        if (event->type == VOICE_EVENT_UPLOAD_OK)
        {
            action = event->action;
            if (action > VOICE_ACTION_HOME)
                action = VOICE_ACTION_NONE;
            session->pending_action = action;
            if (event->has_audio)
            {
                session->state = VOICE_STATE_SPEAKING;
                session->phase_started_ms = event->now_ms;
                return effect_make(VOICE_EFFECT_PLAY_START,
                                   session->active_turn_id,
                                   VOICE_ACTION_NONE);
            }
            session->state = VOICE_STATE_IDLE;
            session->active_turn_id = 0u;
            session->pending_action = VOICE_ACTION_NONE;
            return effect_make(action == VOICE_ACTION_NONE ?
                               VOICE_EFFECT_NONE :
                               VOICE_EFFECT_EXECUTE_ACTION,
                               event->turn_id, action);
        }
        return effect_make(VOICE_EFFECT_NONE, session->active_turn_id,
                           VOICE_ACTION_NONE);
    }

    if (session->state == VOICE_STATE_SPEAKING &&
        event->type == VOICE_EVENT_PLAY_FAILED &&
        active_turn_matches(session, event))
    {
        session->state = VOICE_STATE_ERROR;
        session->pending_action = VOICE_ACTION_NONE;
        return effect_make(VOICE_EFFECT_CANCEL, session->active_turn_id,
                           VOICE_ACTION_NONE);
    }

    if (session->state == VOICE_STATE_SPEAKING &&
        event->type == VOICE_EVENT_PLAY_DONE &&
        active_turn_matches(session, event))
    {
        uint32_t turn_id = session->active_turn_id;
        action = session->pending_action;
        session->state = VOICE_STATE_IDLE;
        session->active_turn_id = 0u;
        session->pending_action = VOICE_ACTION_NONE;
        return effect_make(action == VOICE_ACTION_NONE ?
                           VOICE_EFFECT_NONE : VOICE_EFFECT_EXECUTE_ACTION,
                           turn_id, action);
    }

    return effect_make(VOICE_EFFECT_NONE, session->active_turn_id,
                       VOICE_ACTION_NONE);
}
