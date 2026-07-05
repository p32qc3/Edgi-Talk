/*
 * Minimal HTTP alarm report command for FinSH/MSH on M55.
 * Usage:
 *   alarm_post
 *   alarm_post "temperature_high" 2
 */

#include <rtthread.h>
#ifdef RT_USING_FINSH
#include <finsh.h>
#endif
#include <string.h>
#include <stdlib.h>
#include "alarm_http.h"

#if defined(__has_include)
#if __has_include(<lwip/sockets.h>) && __has_include(<lwip/netdb.h>)
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <lwip/inet.h>
#define EDGI_ALARM_HTTP_HAS_LWIP 1
#endif
#endif

#if defined(RT_USING_NETDEV) && EDGI_ALARM_HTTP_HAS_LWIP
#include <netdev.h>
#endif

#ifndef EDGI_ALARM_HTTP_HAS_LWIP
#define EDGI_ALARM_HTTP_HAS_LWIP 0
#endif

/* UTF-8：与 M33 alarm_type 0..4 → background/fire/knock/baby/boiling 一致。
 * 每条含「告警」，便于钉钉机器人关键词过滤；文案与用户约定一致。 */
static const char *alarm_code_to_zh(const char *code)
{
    if (code == RT_NULL)
        return "告警：未知类型";

    if (rt_strcmp(code, "background") == 0)
        return "告警：背景环境音";
    if (rt_strcmp(code, "fire") == 0)
        return "火灾告警";
    if (rt_strcmp(code, "knock") == 0)
        return "告警：敲门／敲击";
    if (rt_strcmp(code, "baby") == 0)
        return "告警：婴儿哭泣";
    if (rt_strcmp(code, "boiling") == 0)
        return "告警：烧水煮沸";
    if (rt_strcmp(code, "alarm_test") == 0)
        return "告警：手动测试";

    return "告警：未知类型";
}

/*
 * Public release: point these values to your own ECS deployment.
 * They can also be supplied by the project build configuration.
 */
#ifndef ALARM_HTTP_HOST
#define ALARM_HTTP_HOST      "YOUR_ECS_IP_OR_DOMAIN"
#endif
#ifndef ALARM_HTTP_PORT
#define ALARM_HTTP_PORT      80
#endif
#ifndef ALARM_HTTP_PATH
#define ALARM_HTTP_PATH      "/api/alarm/report"
#endif
#ifndef ALARM_DEVICE_ID
#define ALARM_DEVICE_ID      "m55-dev-001"
#endif
#ifndef ALARM_API_KEY
#define ALARM_API_KEY        "CHANGE_ME"
#endif

#if EDGI_ALARM_HTTP_HAS_LWIP
/* Avoid flooding serial when network is down (same log at most once per interval). */
#ifndef ALARM_HTTP_ERR_LOG_GAP_MS
#define ALARM_HTTP_ERR_LOG_GAP_MS 10000U
#endif

/* WiFi 不稳时短暂失败：自动重试（不含业务逻辑队列，避免过度设计） */
#ifndef EDGI_ALARM_HTTP_RETRY_COUNT
#define EDGI_ALARM_HTTP_RETRY_COUNT 3
#endif
#ifndef EDGI_ALARM_HTTP_RETRY_GAP_MS
#define EDGI_ALARM_HTTP_RETRY_GAP_MS 400U
#endif
#ifndef EDGI_ALARM_HTTP_TIMEOUT_MS
#define EDGI_ALARM_HTTP_TIMEOUT_MS 5000U
#endif
/* 1：串口打印本次 POST 总耗时与尝试次数，便于测「采声→HTTP→钉钉」链路 */
#ifndef EDGI_ALARM_HTTP_TRACE_LATENCY
#define EDGI_ALARM_HTTP_TRACE_LATENCY 0
#endif

static int alarm_http_prepare_netdev(void)
{
#if defined(RT_USING_NETDEV) && EDGI_ALARM_HTTP_HAS_LWIP
    struct netdev *netdev = netdev_get_by_name("w0");

    if (netdev == RT_NULL)
        netdev = netdev_get_by_name("wlan0");

    if (netdev == RT_NULL)
    {
        rt_kprintf("alarm_post: WiFi netdev not found (run wifi_start + wifi join first)\n");
        return -1;
    }

    if (!netdev_is_up(netdev) || !netdev_is_link_up(netdev))
    {
        rt_kprintf("alarm_post: WiFi not ready on %s (check wifi status/ifconfig)\n", netdev->name);
        return -1;
    }

    /* 有 IP 即可尝试 TCP；INTERNET_UP 依赖 link.rt-thread.org 探测，热点下常迟迟不置位 */
    if (!netdev_is_internet_up(netdev))
    {
        rt_kprintf("alarm_post: %s no INTERNET_UP yet, try POST anyway\n", netdev->name);
    }

    netdev_set_default(netdev);
#endif
    return 0;
}

static void alarm_http_log_net_err(const char *msg)
{
    static rt_tick_t s_last;
    rt_tick_t now = rt_tick_get();
    if ((now - s_last) < rt_tick_from_millisecond(ALARM_HTTP_ERR_LOG_GAP_MS))
        return;
    s_last = now;
    rt_kprintf("%s", msg);
}

static void alarm_http_set_timeout(int sock)
{
    struct timeval timeout;

    timeout.tv_sec = EDGI_ALARM_HTTP_TIMEOUT_MS / 1000U;
    timeout.tv_usec = (EDGI_ALARM_HTTP_TIMEOUT_MS % 1000U) * 1000U;

    (void)lwip_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
                          &timeout, sizeof(timeout));
    (void)lwip_setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO,
                          &timeout, sizeof(timeout));
}

static int alarm_http_response_ok(const char *resp)
{
    if (resp == RT_NULL)
        return 0;

    return (rt_strncmp(resp, "HTTP/1.1 2", 10) == 0 ||
            rt_strncmp(resp, "HTTP/1.0 2", 10) == 0);
}

static int alarm_http_connect_timeout(int sock,
                                      const struct sockaddr *addr,
                                      socklen_t addr_len)
{
    int ret;
    int err = 0;
    socklen_t err_len = sizeof(err);
    fd_set write_set;
    fd_set err_set;
    struct timeval timeout;

    (void)lwip_fcntl(sock, F_SETFL, O_NONBLOCK);
    ret = lwip_connect(sock, addr, addr_len);
    if (ret == 0)
    {
        (void)lwip_fcntl(sock, F_SETFL, 0);
        return 0;
    }

    err = rt_get_errno();
    if (err != EINPROGRESS && err != EWOULDBLOCK && err != EAGAIN)
    {
        (void)lwip_fcntl(sock, F_SETFL, 0);
        return -1;
    }

    FD_ZERO(&write_set);
    FD_ZERO(&err_set);
    FD_SET(sock, &write_set);
    FD_SET(sock, &err_set);
    timeout.tv_sec = EDGI_ALARM_HTTP_TIMEOUT_MS / 1000U;
    timeout.tv_usec = (EDGI_ALARM_HTTP_TIMEOUT_MS % 1000U) * 1000U;

    ret = lwip_select(sock + 1, RT_NULL, &write_set, &err_set, &timeout);
    if (ret <= 0)
    {
        (void)lwip_fcntl(sock, F_SETFL, 0);
        return -1;
    }

    if (lwip_getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &err_len) < 0 || err != 0)
    {
        (void)lwip_fcntl(sock, F_SETFL, 0);
        return -1;
    }

    (void)lwip_fcntl(sock, F_SETFL, 0);
    return 0;
}

/* 单次 TCP+POST；成功返回 0 */
static int alarm_http_post_once(const char *alarm_code, int level)
{
    int ret = -1;
    int sock = -1;
    struct hostent *host = RT_NULL;
    struct sockaddr_in server_addr;
    char payload[384];
    char request[896];
    char resp[256];

    rt_memset(&server_addr, 0, sizeof(server_addr));
    rt_memset(payload, 0, sizeof(payload));
    rt_memset(request, 0, sizeof(request));
    rt_memset(resp, 0, sizeof(resp));

    host = lwip_gethostbyname(ALARM_HTTP_HOST);
    if (host == RT_NULL || host->h_addr_list == RT_NULL || host->h_addr_list[0] == RT_NULL)
    {
        static char s_dns_buf[80];
        rt_snprintf(s_dns_buf, sizeof(s_dns_buf), "alarm_post: DNS failed for host: %s\n", ALARM_HTTP_HOST);
        alarm_http_log_net_err(s_dns_buf);
        return -1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(ALARM_HTTP_PORT);
    rt_memcpy(&server_addr.sin_addr, host->h_addr_list[0], sizeof(server_addr.sin_addr));

    sock = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        alarm_http_log_net_err("alarm_post: socket create failed\n");
        return -1;
    }
    alarm_http_set_timeout(sock);

    if (alarm_http_connect_timeout(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        static char s_conn_buf[112];
        rt_snprintf(s_conn_buf, sizeof(s_conn_buf),
                    "alarm_post: connect failed host=%s port=%d errno=%d timeout=%ums\n",
                    ALARM_HTTP_HOST, ALARM_HTTP_PORT, (int)rt_get_errno(),
                    (unsigned)EDGI_ALARM_HTTP_TIMEOUT_MS);
        /* 连接失败不打速率限制，否则三次重试往往只显示一条，只剩 report failed */
        rt_kprintf("%s", s_conn_buf);
        goto __exit;
    }

    rt_snprintf(payload,
                sizeof(payload),
                "{\"device_id\":\"%s\",\"alarm_code\":\"%s\",\"alarm_msg\":\"%s\",\"level\":%d}",
                ALARM_DEVICE_ID,
                alarm_code,
                alarm_code_to_zh(alarm_code),
                level);

    rt_snprintf(request,
                sizeof(request),
                "POST %s HTTP/1.1\r\n"
                "Host: %s:%d\r\n"
                "Content-Type: application/json\r\n"
                "X-Api-Key: %s\r\n"
                "Connection: close\r\n"
                "Content-Length: %d\r\n"
                "\r\n"
                "%s",
                ALARM_HTTP_PATH,
                ALARM_HTTP_HOST,
                ALARM_HTTP_PORT,
                ALARM_API_KEY,
                (int)rt_strlen(payload),
                payload);

    if (lwip_send(sock, request, rt_strlen(request), 0) < 0)
    {
        rt_kprintf("alarm_post: send failed\n");
        goto __exit;
    }

    /* Read the first chunk of HTTP response for quick verification. */
    ret = lwip_recv(sock, resp, sizeof(resp) - 1, 0);
    if (ret > 0)
    {
        resp[ret] = '\0';
        rt_kprintf("alarm_post: server response:\n%s\n", resp);
        ret = alarm_http_response_ok(resp) ? 0 : -1;
    }
    else
    {
        rt_kprintf("alarm_post: no valid response from server (timeout=%ums)\n",
                   (unsigned)EDGI_ALARM_HTTP_TIMEOUT_MS);
        ret = -1;
    }

__exit:
    if (sock >= 0)
    {
        lwip_close(sock);
    }
    return ret;
}

int edgi_alarm_http_post(const char *alarm_code, int level)
{
    int ret = -1;
    int attempt;
#if EDGI_ALARM_HTTP_TRACE_LATENCY
    rt_tick_t t0 = rt_tick_get();
#endif

    if (alarm_http_prepare_netdev() != 0)
        return -1;

    for (attempt = 0; attempt < (int)EDGI_ALARM_HTTP_RETRY_COUNT; attempt++)
    {
        if (attempt > 0)
            rt_thread_mdelay(EDGI_ALARM_HTTP_RETRY_GAP_MS);
        ret = alarm_http_post_once(alarm_code, level);
        if (ret == 0)
            break;
    }

#if EDGI_ALARM_HTTP_TRACE_LATENCY
    {
        rt_tick_t dt = rt_tick_get() - t0;
        unsigned ms = (unsigned)((uint32_t)dt * 1000U / (uint32_t)RT_TICK_PER_SECOND);
        rt_kprintf("alarm_post: latency=%ums attempts=%d ret=%d\n", ms, attempt + 1, ret);
    }
#endif

    return ret;
}

#ifdef RT_USING_FINSH
static void alarm_post_cmd(int argc, char **argv)
{
    const char *alarm_code = "alarm_test";
    int level = 1;

    if (argc >= 2)
    {
        alarm_code = argv[1];
    }
    if (argc >= 3)
    {
        level = atoi(argv[2]);
    }
    if (argc > 3)
    {
        rt_kprintf("Usage: alarm_post [alarm_code] [level]\n");
        return;
    }

    if (edgi_alarm_http_post(alarm_code, level) != 0)
    {
        rt_kprintf("alarm_post: report failed\n");
    }
    else
    {
        rt_kprintf("alarm_post: report ok\n");
    }
}
#endif
#else
int edgi_alarm_http_post(const char *alarm_code, int level)
{
    (void)alarm_code;
    (void)level;
    return -1;
}

#ifdef RT_USING_FINSH
static void alarm_post_cmd(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);
    rt_kprintf("alarm_post: lwip headers missing in this build.\n");
}
#endif
#endif

#ifdef RT_USING_FINSH
MSH_CMD_EXPORT_ALIAS(alarm_post_cmd, alarm_post, report alarm to cloud by HTTP POST);
#endif
