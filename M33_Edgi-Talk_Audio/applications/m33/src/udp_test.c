/*
 * Minimal UDP receive test command for FinSH/MSH.
 *
 * Purpose: verify that the WiFi/LWIP network stack is fully working end-to-end,
 * without relying on RT-Thread network examples (udpserver/udpclient).
 */

#include <rtthread.h>
#include <finsh.h>

#if defined(RT_USING_LWIP) && defined(__has_include)
#if __has_include(<lwip/api.h>) && __has_include(<lwip/netbuf.h>)
#include <lwip/api.h>
#include <lwip/init.h>
#include <lwip/ip_addr.h>
#include <lwip/netbuf.h>
#define EDGI_UDP_TEST_HAS_LWIP 1
#endif
#endif

#ifndef EDGI_UDP_TEST_HAS_LWIP
#define EDGI_UDP_TEST_HAS_LWIP 0
#endif

#if EDGI_UDP_TEST_HAS_LWIP
static void udp_recv_once(int argc, char **argv)
{
    int port = 0;
    u32_t timeout_ms = 10000;

    if (argc < 2 || argc > 3)
    {
        rt_kprintf("Usage: udp_recv_once <port> [timeout_ms]\n");
        return;
    }

    port = atoi(argv[1]);
    if (argc == 3)
    {
        timeout_ms = atoi(argv[2]);
    }

    struct netconn *conn = netconn_new(NETCONN_UDP);
    if (conn == RT_NULL)
    {
        rt_kprintf("udp_recv_once: netconn_new failed\n");
        return;
    }

    err_t err = netconn_bind(conn, IP_ADDR_ANY, (u16_t)port);
    if (err != ERR_OK)
    {
        rt_kprintf("udp_recv_once: netconn_bind failed (port=%d, err=%d)\n", port, (int)err);
        netconn_delete(conn);
        return;
    }

    netconn_set_recvtimeout(conn, timeout_ms);
    rt_kprintf("udp_recv_once: waiting on UDP port %d, timeout %u ms ...\n", port, timeout_ms);

    struct netbuf *buf = netconn_recv(conn);
    if (buf == RT_NULL)
    {
        rt_kprintf("udp_recv_once: timeout (no packet)\n");
        netconn_delete(conn);
        return;
    }

    void *data = RT_NULL;
    u16_t len = 0;
    netbuf_data(buf, &data, &len);

    /* For print safety, truncate to buffer size and add NUL terminator. */
    char print_buf[256];
    u16_t copy_len = (len < (sizeof(print_buf) - 1)) ? len : (sizeof(print_buf) - 1);
    rt_memcpy(print_buf, data, copy_len);
    print_buf[copy_len] = '\0';

    rt_kprintf("udp_recv_once: recv %u bytes, from=%s:%u, data='%s'\n",
               (unsigned)len,
               ipaddr_ntoa(netbuf_fromaddr(buf)),
               (unsigned)netbuf_fromport(buf),
               print_buf);

    netbuf_delete(buf);
    netconn_delete(conn);
}
#else
static void udp_recv_once(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);
    rt_kprintf("udp_recv_once: lwip headers not found, skip network test build.\n");
}
#endif

MSH_CMD_EXPORT_ALIAS(udp_recv_once, udp_recv_once, recv one UDP packet and print);
