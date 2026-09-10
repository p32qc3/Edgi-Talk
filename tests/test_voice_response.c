#include <stdio.h>
#include <string.h>

#include "voice_response.h"

static int test_valid_response(void)
{
    static const char json[] =
        "{\"turn_id\":\"00000001-00000002\",\"transcript\":\"查看状态\","
        "\"reply_text\":\"小团子今天很开心。\",\"action\":\"show_status\","
        "\"audio_id\":\"0123456789abcdef0123456789abcdef\",\"expires_in\":60}";
    VoiceResponse response;
    return voice_response_parse(json, sizeof(json) - 1u, &response) &&
           strcmp(response.turn_id, "00000001-00000002") == 0 &&
           strcmp(response.transcript, "查看状态") == 0 &&
           strcmp(response.reply_text, "小团子今天很开心。") == 0 &&
           response.action == VOICE_ACTION_SHOW_STATUS &&
           strcmp(response.audio_id, "0123456789abcdef0123456789abcdef") == 0;
}

static int test_unknown_action_becomes_none(void)
{
    static const char json[] =
        "{\"turn_id\":\"00000001-00000003\",\"transcript\":\"你好\","
        "\"reply_text\":\"你好呀\",\"action\":\"erase\","
        "\"audio_id\":\"abcdef0123456789abcdef0123456789\",\"expires_in\":60}";
    VoiceResponse response;
    return voice_response_parse(json, sizeof(json) - 1u, &response) &&
           response.action == VOICE_ACTION_NONE;
}

static int test_rejects_bad_shape_and_limits(void)
{
    static const char extra[] =
        "{\"turn_id\":\"00000001-00000003\",\"transcript\":\"你好\","
        "\"reply_text\":\"你好呀\",\"action\":\"none\","
        "\"audio_id\":\"abcdef0123456789abcdef0123456789\","
        "\"expires_in\":60,\"secret\":\"x\"}";
    static const char bad_audio[] =
        "{\"turn_id\":\"00000001-00000003\",\"transcript\":\"你好\","
        "\"reply_text\":\"你好呀\",\"action\":\"none\","
        "\"audio_id\":\"xyz\",\"expires_in\":60}";
    VoiceResponse response;
    char too_large[VOICE_RESPONSE_JSON_MAX + 1u];
    memset(too_large, 'x', sizeof(too_large));
    return !voice_response_parse(extra, sizeof(extra) - 1u, &response) &&
           !voice_response_parse(bad_audio, sizeof(bad_audio) - 1u, &response) &&
           !voice_response_parse(too_large, sizeof(too_large), &response) &&
           voice_response_audio_size_valid(VOICE_AUDIO_MAX_BYTES) &&
           !voice_response_audio_size_valid(VOICE_AUDIO_MAX_BYTES + 1u);
}

int main(void)
{
    if (!test_valid_response()) { fprintf(stderr, "FAIL: valid voice response\n"); return 1; }
    if (!test_unknown_action_becomes_none()) { fprintf(stderr, "FAIL: safe voice action\n"); return 1; }
    if (!test_rejects_bad_shape_and_limits()) { fprintf(stderr, "FAIL: voice response limits\n"); return 1; }
    puts("PASS: voice_response");
    return 0;
}
