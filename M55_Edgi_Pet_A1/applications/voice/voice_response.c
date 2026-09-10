#include "voice_response.h"

#include <string.h>

typedef struct
{
    const char *bytes;
    size_t size;
    size_t at;
} JsonCursor;

static void skip_space(JsonCursor *cursor)
{
    while (cursor->at < cursor->size)
    {
        char c = cursor->bytes[cursor->at];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        cursor->at++;
    }
}

static int take(JsonCursor *cursor, char wanted)
{
    if (cursor->at >= cursor->size || cursor->bytes[cursor->at] != wanted)
        return 0;
    cursor->at++;
    return 1;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int append_byte(char *out, size_t capacity, size_t *length,
                       unsigned byte)
{
    if (*length + 1u >= capacity) return 0;
    out[(*length)++] = (char)byte;
    return 1;
}

static int append_codepoint(char *out, size_t capacity, size_t *length,
                            uint32_t codepoint)
{
    if ((!codepoint || codepoint < 0x20u) && codepoint != 0x0au) return 0;
    if (codepoint >= 0xd800u && codepoint <= 0xdfffu) return 0;
    if (codepoint <= 0x7fu)
        return append_byte(out, capacity, length, codepoint);
    if (codepoint <= 0x7ffu)
        return append_byte(out, capacity, length, 0xc0u | (codepoint >> 6)) &&
               append_byte(out, capacity, length, 0x80u | (codepoint & 0x3fu));
    if (codepoint <= 0xffffu)
        return append_byte(out, capacity, length, 0xe0u | (codepoint >> 12)) &&
               append_byte(out, capacity, length,
                           0x80u | ((codepoint >> 6) & 0x3fu)) &&
               append_byte(out, capacity, length, 0x80u | (codepoint & 0x3fu));
    if (codepoint <= 0x10ffffu)
        return append_byte(out, capacity, length, 0xf0u | (codepoint >> 18)) &&
               append_byte(out, capacity, length,
                           0x80u | ((codepoint >> 12) & 0x3fu)) &&
               append_byte(out, capacity, length,
                           0x80u | ((codepoint >> 6) & 0x3fu)) &&
               append_byte(out, capacity, length, 0x80u | (codepoint & 0x3fu));
    return 0;
}

static int take_hex4(JsonCursor *cursor, uint32_t *value)
{
    uint32_t result = 0u;
    unsigned i;
    for (i = 0u; i < 4u; i++)
    {
        int digit;
        if (cursor->at >= cursor->size) return 0;
        digit = hex_value(cursor->bytes[cursor->at++]);
        if (digit < 0) return 0;
        result = result * 16u + (uint32_t)digit;
    }
    *value = result;
    return 1;
}

static int take_unicode(JsonCursor *cursor, char *out, size_t capacity,
                        size_t *length)
{
    uint32_t first;
    uint32_t second;
    uint32_t codepoint;
    if (!take_hex4(cursor, &first)) return 0;
    if (first >= 0xd800u && first <= 0xdbffu)
    {
        if (!take(cursor, '\\') || !take(cursor, 'u') ||
            !take_hex4(cursor, &second) ||
            second < 0xdc00u || second > 0xdfffu)
            return 0;
        codepoint = 0x10000u + ((first - 0xd800u) << 10) +
                    second - 0xdc00u;
    }
    else
    {
        if (first >= 0xdc00u && first <= 0xdfffu) return 0;
        codepoint = first;
    }
    return append_codepoint(out, capacity, length, codepoint);
}

static int take_string(JsonCursor *cursor, char *out, size_t capacity)
{
    size_t length = 0u;
    if (!capacity || !take(cursor, '"')) return 0;
    while (cursor->at < cursor->size)
    {
        unsigned byte = (unsigned char)cursor->bytes[cursor->at++];
        if (byte == '"')
        {
            out[length] = '\0';
            return 1;
        }
        if (byte < 0x20u) return 0;
        if (byte != '\\')
        {
            if (!append_byte(out, capacity, &length, byte)) return 0;
            continue;
        }
        if (cursor->at >= cursor->size) return 0;
        byte = (unsigned char)cursor->bytes[cursor->at++];
        if (byte == '"' || byte == '\\' || byte == '/')
        {
            if (!append_byte(out, capacity, &length, byte)) return 0;
        }
        else if (byte == 'n')
        {
            if (!append_byte(out, capacity, &length, '\n')) return 0;
        }
        else if (byte == 'r')
        {
            if (!append_byte(out, capacity, &length, '\r')) return 0;
        }
        else if (byte == 't')
        {
            if (!append_byte(out, capacity, &length, '\t')) return 0;
        }
        else if (byte == 'u')
        {
            if (!take_unicode(cursor, out, capacity, &length)) return 0;
        }
        else return 0;
    }
    return 0;
}

static int take_uint(JsonCursor *cursor, uint32_t *value)
{
    uint32_t result = 0u;
    size_t digits = 0u;
    while (cursor->at < cursor->size)
    {
        unsigned digit;
        char c = cursor->bytes[cursor->at];
        if (c < '0' || c > '9') break;
        digit = (unsigned)(c - '0');
        if (result > (UINT32_MAX - digit) / 10u) return 0;
        result = result * 10u + digit;
        cursor->at++;
        digits++;
    }
    if (!digits) return 0;
    *value = result;
    return 1;
}

static VoiceAction action_from_text(const char *text)
{
    if (!strcmp(text, "show_status")) return VOICE_ACTION_SHOW_STATUS;
    if (!strcmp(text, "start_simon")) return VOICE_ACTION_START_SIMON;
    if (!strcmp(text, "home")) return VOICE_ACTION_HOME;
    return VOICE_ACTION_NONE;
}

static int valid_turn_id(const char *text)
{
    size_t i;
    size_t length = strlen(text);
    if (!length || length >= VOICE_TURN_ID_MAX) return 0;
    for (i = 0u; i < length; i++)
    {
        char c = text[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-')) return 0;
    }
    return 1;
}

static int valid_audio_id(const char *text)
{
    size_t i;
    if (strlen(text) != 32u) return 0;
    for (i = 0u; i < 32u; i++)
        if (!((text[i] >= '0' && text[i] <= '9') ||
              (text[i] >= 'a' && text[i] <= 'f'))) return 0;
    return 1;
}

int voice_response_parse(const char *json, size_t size, VoiceResponse *out)
{
    enum { HAVE_TURN = 1, HAVE_TRANSCRIPT = 2, HAVE_REPLY = 4,
           HAVE_ACTION = 8, HAVE_AUDIO = 16, HAVE_EXPIRES = 32 };
    JsonCursor cursor;
    VoiceResponse parsed;
    char key[24];
    char action[16];
    unsigned have = 0u;
    if (!json || !out || !size || size > VOICE_RESPONSE_JSON_MAX) return 0;
    memset(&parsed, 0, sizeof(parsed));
    cursor.bytes = json;
    cursor.size = size;
    cursor.at = 0u;
    skip_space(&cursor);
    if (!take(&cursor, '{')) return 0;
    skip_space(&cursor);
    while (cursor.at < cursor.size && cursor.bytes[cursor.at] != '}')
    {
        if (!take_string(&cursor, key, sizeof(key))) return 0;
        skip_space(&cursor);
        if (!take(&cursor, ':')) return 0;
        skip_space(&cursor);
        if (!strcmp(key, "turn_id"))
        {
            if ((have & HAVE_TURN) || !take_string(&cursor, parsed.turn_id,
                                                   sizeof(parsed.turn_id)) ||
                !valid_turn_id(parsed.turn_id)) return 0;
            have |= HAVE_TURN;
        }
        else if (!strcmp(key, "transcript"))
        {
            if ((have & HAVE_TRANSCRIPT) ||
                !take_string(&cursor, parsed.transcript,
                             sizeof(parsed.transcript))) return 0;
            have |= HAVE_TRANSCRIPT;
        }
        else if (!strcmp(key, "reply_text"))
        {
            if ((have & HAVE_REPLY) ||
                !take_string(&cursor, parsed.reply_text,
                             sizeof(parsed.reply_text))) return 0;
            have |= HAVE_REPLY;
        }
        else if (!strcmp(key, "action"))
        {
            if ((have & HAVE_ACTION) ||
                !take_string(&cursor, action, sizeof(action))) return 0;
            parsed.action = action_from_text(action);
            have |= HAVE_ACTION;
        }
        else if (!strcmp(key, "audio_id"))
        {
            if ((have & HAVE_AUDIO) ||
                !take_string(&cursor, parsed.audio_id,
                             sizeof(parsed.audio_id)) ||
                !valid_audio_id(parsed.audio_id)) return 0;
            have |= HAVE_AUDIO;
        }
        else if (!strcmp(key, "expires_in"))
        {
            if ((have & HAVE_EXPIRES) ||
                !take_uint(&cursor, &parsed.expires_in)) return 0;
            have |= HAVE_EXPIRES;
        }
        else return 0;
        skip_space(&cursor);
        if (cursor.at < cursor.size && cursor.bytes[cursor.at] == ',')
        {
            cursor.at++;
            skip_space(&cursor);
            if (cursor.at >= cursor.size || cursor.bytes[cursor.at] == '}')
                return 0;
            continue;
        }
        break;
    }
    if (!take(&cursor, '}')) return 0;
    skip_space(&cursor);
    if (cursor.at != cursor.size || have != 63u) return 0;
    *out = parsed;
    return 1;
}

int voice_response_audio_size_valid(size_t size)
{
    return size <= VOICE_AUDIO_MAX_BYTES;
}
