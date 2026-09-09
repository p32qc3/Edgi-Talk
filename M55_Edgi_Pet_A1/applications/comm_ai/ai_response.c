#include <string.h>
#include "ai_response.h"

typedef struct {
    const char *bytes;
    u16 size;
    u16 at;
} AiJsonCursor;

static void ai_json_skip_space(AiJsonCursor *cursor)
{
    char c;

    while (cursor->at<cursor->size)
    {
        c=cursor->bytes[cursor->at];
        if (c!=' ' && c!='\t' && c!='\r' && c!='\n')
            break;
        cursor->at++;
    }
}

static int ai_json_take(AiJsonCursor *cursor, char wanted)
{
    if (cursor->at>=cursor->size || cursor->bytes[cursor->at]!=wanted)
        return 0;
    cursor->at++;
    return 1;
}

static int ai_hex_value(char c)
{
    if (c>='0' && c<='9')
        return c-'0';
    if (c>='a' && c<='f')
        return c-'a'+10;
    if (c>='A' && c<='F')
        return c-'A'+10;
    return -1;
}

static int ai_json_hex4(AiJsonCursor *cursor, u32 *value)
{
    u32 result;
    int digit;
    unsigned i;

    result=0;
    for (i=0;i<4;i++)
    {
        if (cursor->at>=cursor->size)
            return 0;
        digit=ai_hex_value(cursor->bytes[cursor->at++]);
        if (digit<0)
            return 0;
        result=result*16UL+(u32)digit;
    }
    *value=result;
    return 1;
}

static int ai_text_append(char *out, u16 capacity, u16 *length, u8 byte)
{
    if ((u16)(*length+1)>=capacity)
        return 0;
    out[(*length)++]=(char)byte;
    return 1;
}

static int ai_text_append_codepoint(char *out, u16 capacity,
    u16 *length, u32 codepoint)
{
    if ((!codepoint || codepoint<0x20UL) && codepoint!=0x0aUL)
        return 0;
    if (codepoint>=0xd800UL && codepoint<=0xdfffUL)
        return 0;
    if (codepoint<=0x7fUL)
        return ai_text_append(out,capacity,length,(u8)codepoint);
    if (codepoint<=0x7ffUL)
    {
        return ai_text_append(out,capacity,length,
                   (u8)(0xc0UL|(codepoint>>6))) &&
               ai_text_append(out,capacity,length,
                   (u8)(0x80UL|(codepoint&0x3fUL)));
    }
    if (codepoint<=0xffffUL)
    {
        return ai_text_append(out,capacity,length,
                   (u8)(0xe0UL|(codepoint>>12))) &&
               ai_text_append(out,capacity,length,
                   (u8)(0x80UL|((codepoint>>6)&0x3fUL))) &&
               ai_text_append(out,capacity,length,
                   (u8)(0x80UL|(codepoint&0x3fUL)));
    }
    if (codepoint<=0x10ffffUL)
    {
        return ai_text_append(out,capacity,length,
                   (u8)(0xf0UL|(codepoint>>18))) &&
               ai_text_append(out,capacity,length,
                   (u8)(0x80UL|((codepoint>>12)&0x3fUL))) &&
               ai_text_append(out,capacity,length,
                   (u8)(0x80UL|((codepoint>>6)&0x3fUL))) &&
               ai_text_append(out,capacity,length,
                   (u8)(0x80UL|(codepoint&0x3fUL)));
    }
    return 0;
}

static int ai_text_valid_utf8(const char *text, u16 size)
{
    u16 i;
    u8 a;
    u8 b;

    i=0;
    while (i<size)
    {
        a=(u8)text[i++];
        if (a<=0x7f)
            continue;
        if (a>=0xc2 && a<=0xdf)
        {
            if (i>=size || ((u8)text[i]&0xc0)!=0x80)
                return 0;
            i++;
            continue;
        }
        if (a>=0xe0 && a<=0xef)
        {
            if ((u16)(i+1)>=size)
                return 0;
            b=(u8)text[i];
            if ((a==0xe0 && b<0xa0) || (a==0xed && b>=0xa0) ||
                (b&0xc0)!=0x80 || ((u8)text[i+1]&0xc0)!=0x80)
                return 0;
            i=(u16)(i+2);
            continue;
        }
        if (a>=0xf0 && a<=0xf4)
        {
            if ((u16)(i+2)>=size)
                return 0;
            b=(u8)text[i];
            if ((a==0xf0 && b<0x90) || (a==0xf4 && b>=0x90) ||
                (b&0xc0)!=0x80 || ((u8)text[i+1]&0xc0)!=0x80 ||
                ((u8)text[i+2]&0xc0)!=0x80)
                return 0;
            i=(u16)(i+3);
            continue;
        }
        return 0;
    }
    return 1;
}

static int ai_json_unicode_escape(AiJsonCursor *cursor, char *out,
    u16 capacity, u16 *length)
{
    u32 first;
    u32 second;
    u32 codepoint;

    if (!ai_json_hex4(cursor,&first))
        return 0;
    if (first>=0xd800UL && first<=0xdbffUL)
    {
        if (!ai_json_take(cursor,'\\') || !ai_json_take(cursor,'u') ||
            !ai_json_hex4(cursor,&second) ||
            second<0xdc00UL || second>0xdfffUL)
            return 0;
        codepoint=0x10000UL+((first-0xd800UL)<<10)+(second-0xdc00UL);
    }
    else
    {
        if (first>=0xdc00UL && first<=0xdfffUL)
            return 0;
        codepoint=first;
    }
    return ai_text_append_codepoint(out,capacity,length,codepoint);
}

static int ai_json_string(AiJsonCursor *cursor, char *out, u16 capacity)
{
    u16 length;
    u8 byte;
    char escaped;

    if (!capacity || !ai_json_take(cursor,'\"'))
        return 0;
    length=0;
    while (cursor->at<cursor->size)
    {
        byte=(u8)cursor->bytes[cursor->at++];
        if (byte=='\"')
        {
            out[length]='\0';
            return ai_text_valid_utf8(out,length);
        }
        if (byte<0x20)
            return 0;
        if (byte!='\\')
        {
            if (!ai_text_append(out,capacity,&length,byte))
                return 0;
            continue;
        }
        if (cursor->at>=cursor->size)
            return 0;
        escaped=cursor->bytes[cursor->at++];
        if (escaped=='\"' || escaped=='\\' || escaped=='/')
        {
            if (!ai_text_append(out,capacity,&length,(u8)escaped))
                return 0;
        }
        else if (escaped=='n')
        {
            if (!ai_text_append(out,capacity,&length,(u8)'\n'))
                return 0;
        }
        else if (escaped=='u')
        {
            if (!ai_json_unicode_escape(cursor,out,capacity,&length))
                return 0;
        }
        else
            return 0;
    }
    return 0;
}

static int ai_action_from_text(const char *text, u8 *action)
{
    if (!strcmp(text,"text"))
        *action=AI_ACTION_TEXT;
    else if (!strcmp(text,"celebrate"))
        *action=AI_ACTION_CELEBRATE;
    else if (!strcmp(text,"simon"))
        *action=AI_ACTION_START_GAME;
    else
        return 0;
    return 1;
}

int ai_response_parse(const char *json, u32 size, AiParsedResponse *out)
{
    AiJsonCursor cursor;
    AiParsedResponse parsed;
    char key[16];
    char value[AI_REPLY_MAX];
    u8 have_text;
    u8 have_action;

    if (!json || !out || !size || size>AI_RESPONSE_JSON_MAX)
        return 0;
    memset(&parsed,0,sizeof(parsed));
    cursor.bytes=json;
    cursor.size=(u16)size;
    cursor.at=0;
    have_text=0;
    have_action=0;
    ai_json_skip_space(&cursor);
    if (!ai_json_take(&cursor,'{'))
        return 0;
    ai_json_skip_space(&cursor);
    while (cursor.at<cursor.size && cursor.bytes[cursor.at]!='}')
    {
        if (!ai_json_string(&cursor,key,sizeof(key)))
            return 0;
        ai_json_skip_space(&cursor);
        if (!ai_json_take(&cursor,':'))
            return 0;
        ai_json_skip_space(&cursor);
        if (!ai_json_string(&cursor,value,sizeof(value)))
            return 0;
        if (!strcmp(key,"text"))
        {
            if (have_text || !value[0])
                return 0;
            strcpy(parsed.text,value);
            have_text=1;
        }
        else if (!strcmp(key,"action"))
        {
            if (have_action || !ai_action_from_text(value,&parsed.action))
                return 0;
            have_action=1;
        }
        else
            return 0;
        ai_json_skip_space(&cursor);
        if (cursor.at<cursor.size && cursor.bytes[cursor.at]==',')
        {
            cursor.at++;
            ai_json_skip_space(&cursor);
            if (cursor.at>=cursor.size || cursor.bytes[cursor.at]=='}')
                return 0;
            continue;
        }
        break;
    }
    if (!ai_json_take(&cursor,'}'))
        return 0;
    ai_json_skip_space(&cursor);
    if (cursor.at!=cursor.size || !have_text || !have_action)
        return 0;
    *out=parsed;
    return 1;
}

int ai_response_complete(AiClient *client, u32 request_id,
    const char *json, u32 size, u32 now_ms)
{
    AiParsedResponse response;

    if (!client)
        return 0;
    if (!ai_response_parse(json,size,&response))
        return ai_client_complete(client,request_id,0,AI_ACTION_TEXT,now_ms);
    return ai_client_complete(client,request_id,response.text,
        response.action,now_ms);
}
