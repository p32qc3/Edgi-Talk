#include "voice_http.h"

#include "voice_config.h"

#include <rtthread.h>
#include <string.h>
#include <stdlib.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <lwip/inet.h>
#include <netdev.h>

#define VOICE_HTTP_HEADER_MAX 1024u
#define VOICE_HTTP_IO_CHUNK   2048u

typedef struct
{
    int socket;
    int status;
    size_t content_length;
    uint8_t initial[VOICE_HTTP_IO_CHUNK];
    size_t initial_bytes;
} HttpResponse;

static int ascii_equal_nocase(const char *left, const char *right,
                              size_t count)
{
    size_t i;
    for (i = 0u; i < count; i++)
    {
        char a = left[i];
        char b = right[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return 0;
    }
    return 1;
}

static int network_ready(void)
{
    struct netdev *device = netdev_get_by_name("w0");
    if (!device) device = netdev_get_by_name("wlan0");
    if (!device || !netdev_is_up(device) || !netdev_is_link_up(device))
        return 0;
    netdev_set_default(device);
    return 1;
}

static void socket_io_timeout(int socket)
{
    struct timeval value;
    value.tv_sec = VOICE_HTTP_TURN_TIMEOUT_MS / 1000u;
    value.tv_usec = (VOICE_HTTP_TURN_TIMEOUT_MS % 1000u) * 1000u;
    (void)lwip_setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                          &value, sizeof(value));
    value.tv_sec = VOICE_HTTP_CONNECT_TIMEOUT_MS / 1000u;
    value.tv_usec = (VOICE_HTTP_CONNECT_TIMEOUT_MS % 1000u) * 1000u;
    (void)lwip_setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO,
                          &value, sizeof(value));
}

static int connect_with_timeout(int socket, const struct sockaddr *address,
                                socklen_t address_size)
{
    fd_set write_set;
    fd_set error_set;
    struct timeval timeout;
    int error = 0;
    socklen_t error_size = sizeof(error);
    int result;
    (void)lwip_fcntl(socket, F_SETFL, O_NONBLOCK);
    result = lwip_connect(socket, address, address_size);
    if (result == 0)
    {
        (void)lwip_fcntl(socket, F_SETFL, 0);
        return 1;
    }
    error = rt_get_errno();
    if (error != EINPROGRESS && error != EWOULDBLOCK && error != EAGAIN)
    {
        (void)lwip_fcntl(socket, F_SETFL, 0);
        return 0;
    }
    FD_ZERO(&write_set);
    FD_ZERO(&error_set);
    FD_SET(socket, &write_set);
    FD_SET(socket, &error_set);
    timeout.tv_sec = VOICE_HTTP_CONNECT_TIMEOUT_MS / 1000u;
    timeout.tv_usec = (VOICE_HTTP_CONNECT_TIMEOUT_MS % 1000u) * 1000u;
    result = lwip_select(socket + 1, RT_NULL, &write_set, &error_set, &timeout);
    if (result <= 0 ||
        lwip_getsockopt(socket, SOL_SOCKET, SO_ERROR,
                        &error, &error_size) < 0 || error != 0)
    {
        (void)lwip_fcntl(socket, F_SETFL, 0);
        return 0;
    }
    (void)lwip_fcntl(socket, F_SETFL, 0);
    return 1;
}

static int open_socket(void)
{
    struct hostent *host;
    struct sockaddr_in address;
    int socket;
    if (!network_ready()) return VOICE_HTTP_OFFLINE;
    host = lwip_gethostbyname(VOICE_ECS_HOST);
    if (!host || !host->h_addr_list || !host->h_addr_list[0])
        return VOICE_HTTP_CONNECT;
    socket = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (socket < 0) return VOICE_HTTP_CONNECT;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(VOICE_ECS_PORT);
    memcpy(&address.sin_addr, host->h_addr_list[0], sizeof(address.sin_addr));
    if (!connect_with_timeout(socket, (struct sockaddr *)&address,
                              sizeof(address)))
    {
        lwip_close(socket);
        return VOICE_HTTP_CONNECT;
    }
    socket_io_timeout(socket);
    return socket;
}

static int send_all(int socket, const void *bytes, size_t size)
{
    const uint8_t *cursor = (const uint8_t *)bytes;
    while (size)
    {
        int sent = lwip_send(socket, cursor,
                             size > 16384u ? 16384u : size, 0);
        if (sent <= 0) return 0;
        cursor += (size_t)sent;
        size -= (size_t)sent;
    }
    return 1;
}

static const char *find_header_end(const char *bytes, size_t size)
{
    size_t i;
    for (i = 3u; i < size; i++)
        if (bytes[i - 3u] == '\r' && bytes[i - 2u] == '\n' &&
            bytes[i - 1u] == '\r' && bytes[i] == '\n')
            return bytes + i + 1u;
    return 0;
}

static int parse_content_length(const char *headers, size_t size,
                                size_t *content_length)
{
    static const char name[] = "content-length:";
    size_t i;
    for (i = 0u; i + sizeof(name) - 1u < size; i++)
    {
        const char *value;
        char *end;
        unsigned long parsed;
        if (i && !(headers[i - 1u] == '\n')) continue;
        if (!ascii_equal_nocase(headers + i, name, sizeof(name) - 1u))
            continue;
        value = headers + i + sizeof(name) - 1u;
        while (value < headers + size && (*value == ' ' || *value == '\t'))
            value++;
        parsed = strtoul(value, &end, 10);
        if (end == value || parsed > (unsigned long)SIZE_MAX) return 0;
        *content_length = (size_t)parsed;
        return 1;
    }
    return 0;
}

static int receive_headers(int socket, HttpResponse *response)
{
    char headers[VOICE_HTTP_HEADER_MAX];
    size_t used = 0u;
    const char *body;
    while (used < sizeof(headers))
    {
        int count = lwip_recv(socket, headers + used, sizeof(headers) - used, 0);
        if (count <= 0) return VOICE_HTTP_PROTOCOL;
        used += (size_t)count;
        body = find_header_end(headers, used);
        if (body)
        {
            size_t header_bytes = (size_t)(body - headers);
            size_t initial = used - header_bytes;
            if (used < 12u || memcmp(headers, "HTTP/1.", 7u) != 0)
                return VOICE_HTTP_PROTOCOL;
            response->status = atoi(headers + 9u);
            if (!parse_content_length(headers, header_bytes,
                                      &response->content_length))
                return VOICE_HTTP_PROTOCOL;
            if (initial > sizeof(response->initial))
                return VOICE_HTTP_PROTOCOL;
            memcpy(response->initial, body, initial);
            response->initial_bytes = initial;
            response->socket = socket;
            return VOICE_HTTP_OK;
        }
    }
    return VOICE_HTTP_PROTOCOL;
}

static int receive_fixed(HttpResponse *response, uint8_t *output,
                         size_t capacity)
{
    size_t received = response->initial_bytes;
    if (response->content_length > capacity ||
        received > response->content_length) return VOICE_HTTP_TOO_LARGE;
    memcpy(output, response->initial, received);
    while (received < response->content_length)
    {
        int count = lwip_recv(response->socket, output + received,
                              response->content_length - received, 0);
        if (count <= 0) return VOICE_HTTP_PROTOCOL;
        received += (size_t)count;
    }
    return VOICE_HTTP_OK;
}

static int post_once(const uint8_t *pcm, size_t pcm_bytes,
                     const char *turn_id, VoiceResponse *response)
{
    int socket = open_socket();
    char header[VOICE_HTTP_HEADER_MAX];
    uint8_t body[VOICE_RESPONSE_JSON_MAX + 1u];
    HttpResponse http;
    int length;
    int result;
    if (socket < 0) return socket;
    length = rt_snprintf(header, sizeof(header),
        "POST /v1/voice/turn HTTP/1.1\r\n"
        "Host: %s\r\nConnection: close\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Content-Length: %u\r\n"
        "X-Device-ID: %s\r\nX-Device-Token: %s\r\n"
        "X-Turn-ID: %s\r\nX-Sample-Rate: 16000\r\n\r\n",
        VOICE_ECS_HOST, (unsigned)pcm_bytes, VOICE_DEVICE_ID,
        VOICE_DEVICE_TOKEN, turn_id);
    if (length <= 0 || (size_t)length >= sizeof(header) ||
        !send_all(socket, header, (size_t)length) ||
        !send_all(socket, pcm, pcm_bytes))
    {
        lwip_close(socket);
        return VOICE_HTTP_CONNECT;
    }
    memset(&http, 0, sizeof(http));
    result = receive_headers(socket, &http);
    if (result == VOICE_HTTP_OK)
        result = receive_fixed(&http, body, VOICE_RESPONSE_JSON_MAX);
    lwip_close(socket);
    if (result != VOICE_HTTP_OK) return result;
    if (http.status != 200) return VOICE_HTTP_REMOTE;
    body[http.content_length] = '\0';
    if (!voice_response_parse((const char *)body, http.content_length,
                              response)) return VOICE_HTTP_PROTOCOL;
    return VOICE_HTTP_OK;
}

int voice_http_post_turn(const uint8_t *pcm, size_t pcm_bytes,
                         const char *turn_id, VoiceResponse *response)
{
    unsigned attempt;
    rt_tick_t started;
    int result = VOICE_HTTP_CONNECT;
    if (!pcm || pcm_bytes < 640u ||
        pcm_bytes > VOICE_RECORD_BUFFER_BYTES || !turn_id || !response)
        return VOICE_HTTP_PROTOCOL;
    started = rt_tick_get();
    for (attempt = 0u; attempt <= VOICE_HTTP_RETRY_COUNT; attempt++)
    {
        result = post_once(pcm, pcm_bytes, turn_id, response);
        if (result == VOICE_HTTP_OK || result == VOICE_HTTP_REMOTE ||
            result == VOICE_HTTP_OFFLINE) return result;
        if ((uint32_t)((rt_tick_get() - started) * 1000u / RT_TICK_PER_SECOND)
            >= VOICE_HTTP_TURN_TIMEOUT_MS) return VOICE_HTTP_TIMEOUT;
    }
    return result;
}

int voice_http_get_audio(const char *audio_id, VoiceHttpAudioSink sink,
                         void *context, size_t *received_bytes)
{
    int socket;
    char header[VOICE_HTTP_HEADER_MAX];
    HttpResponse http;
    uint8_t chunk[VOICE_HTTP_IO_CHUNK];
    size_t received = 0u;
    int length;
    int result;
    if (!audio_id || !sink) return VOICE_HTTP_PROTOCOL;
    socket = open_socket();
    if (socket < 0) return socket;
    length = rt_snprintf(header, sizeof(header),
        "GET /v1/voice/audio/%s HTTP/1.1\r\n"
        "Host: %s\r\nConnection: close\r\n"
        "X-Device-ID: %s\r\nX-Device-Token: %s\r\n\r\n",
        audio_id, VOICE_ECS_HOST, VOICE_DEVICE_ID, VOICE_DEVICE_TOKEN);
    if (length <= 0 || (size_t)length >= sizeof(header) ||
        !send_all(socket, header, (size_t)length))
    {
        lwip_close(socket);
        return VOICE_HTTP_CONNECT;
    }
    memset(&http, 0, sizeof(http));
    result = receive_headers(socket, &http);
    if (result != VOICE_HTTP_OK || http.status != 200)
    {
        lwip_close(socket);
        return result == VOICE_HTTP_OK ? VOICE_HTTP_REMOTE : result;
    }
    if (!voice_response_audio_size_valid(http.content_length))
    {
        lwip_close(socket);
        return VOICE_HTTP_TOO_LARGE;
    }
    if (http.initial_bytes)
    {
        if (!sink(http.initial, http.initial_bytes, context))
        {
            lwip_close(socket);
            return VOICE_HTTP_PROTOCOL;
        }
        received = http.initial_bytes;
    }
    while (received < http.content_length)
    {
        size_t wanted = http.content_length - received;
        int count;
        if (wanted > sizeof(chunk)) wanted = sizeof(chunk);
        count = lwip_recv(socket, chunk, wanted, 0);
        if (count <= 0 || !sink(chunk, (size_t)count, context))
        {
            lwip_close(socket);
            return VOICE_HTTP_PROTOCOL;
        }
        received += (size_t)count;
    }
    lwip_close(socket);
    if (received_bytes) *received_bytes = received;
    return VOICE_HTTP_OK;
}
