/*
 * zombie.h - the frozen ZOMBIE protocol (see labs/06-zombie-protocol/README.md)
 *
 * 这一个头文件 = 协议的全部。客户端和服务端都只 include 它，两边不会各写一套。
 *
 * 帧格式（真的字节，全部小端 little-endian）：
 *
 *   偏移  大小  字段        说明
 *   0     2    total_len   = 1(opcode) + 1(flags) + payload_len
 *   2     1    opcode      0x01 HELLO / 0x02 ATTACK / 0x03 HP_SYNC /
 *                          0x04 SNAPSHOT / 0x05 BYE
 *   3     1    flags       保留，填 0
 *   4     N    payload     每种 opcode 的 payload 见下面
 *
 *   所以「整帧占几个字节」= 2 + total_len = 4 + payload_len。
 *
 * 真实的字节数（后面日志里会反复出现这几个数字）：
 *   HELLO     payload  8  -> total_len 10 -> 整帧 12 字节
 *   ATTACK    payload 12  -> total_len 14 -> 整帧 16 字节
 *   HP_SYNC   payload 20  -> total_len 22 -> 整帧 24 字节
 *   SNAPSHOT  payload 2 + 16*count        -> 整帧 6 + 16*count（count=1000 -> 16006）
 *   BYE       payload  0  -> total_len  2 -> 整帧  4 字节
 */
#ifndef ZOMBIE_H
#define ZOMBIE_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ------------------------------------------------------------------ */
/* opcode                                                              */
/* ------------------------------------------------------------------ */
#define ZB_OP_HELLO     0x01
#define ZB_OP_ATTACK    0x02
#define ZB_OP_HP_SYNC   0x03
#define ZB_OP_SNAPSHOT  0x04
#define ZB_OP_BYE       0x05

/* ------------------------------------------------------------------ */
/* payload 长度（字节）                                                  */
/* ------------------------------------------------------------------ */
#define ZB_PL_HELLO      8
#define ZB_PL_ATTACK    12
#define ZB_PL_HP_SYNC   20
#define ZB_PL_ENTRY     16          /* SNAPSHOT 每个实体: u32 id, f32 x, f32 y, f32 hp */

/* ------------------------------------------------------------------ */
/* 整帧长度（字节）= 4 + payload_len                                     */
/* ------------------------------------------------------------------ */
#define ZB_FR_HELLO     (4 + ZB_PL_HELLO)    /* 12 */
#define ZB_FR_ATTACK    (4 + ZB_PL_ATTACK)   /* 16 */
#define ZB_FR_HP_SYNC   (4 + ZB_PL_HP_SYNC)  /* 24 */
#define ZB_FR_BYE       (4)                  /*  4 */

#define ZB_MAX_TOTAL_LEN 65535
#define ZB_MAX_FRAME     (2 + ZB_MAX_TOTAL_LEN)   /* 65537: 一帧最多这么大 */

/* ------------------------------------------------------------------ */
/* 业务默认值                                                            */
/* ------------------------------------------------------------------ */
/* 端口：服务端 47001，tap 监听 47000 转发到 47001，客户端默认连 47000。
 * 为什么不是 4000/4001：实测这台机器上 127.0.0.1:4001 被 QQ.exe 占着，
 * 服务端 bind 会拿到 WSA error 10013（详见 NOTES.md 的排查记录）。 */
#define ZB_MAX_HP_DEFAULT   100.0f
#define ZB_MAX_DAMAGE       25.0f      /* 服务端只接受 0 < damage <= 25 */
#define ZB_PROTO_VERSION    1
#define ZB_DEF_SRV_PORT     47001
#define ZB_DEF_TAP_PORT     47000
#define ZB_MAX_SNAPSHOT     4095       /* 4 + 16*count <= 65535 -> count <= 4095 */

/* ------------------------------------------------------------------ */
/* 小端读写：手写而不是直接 cast，这样每一字节在源码里都看得见            */
/* ------------------------------------------------------------------ */
static void zb_put_u16(unsigned char *p, uint16_t v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

static void zb_put_u32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

static void zb_put_f32(unsigned char *p, float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);          /* 取 float 的 4 个 IEEE-754 位，再按小端写 */
    zb_put_u32(p, u);
}

static uint16_t zb_get_u16(const unsigned char *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t zb_get_u32(const unsigned char *p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static float zb_get_f32(const unsigned char *p)
{
    uint32_t u = zb_get_u32(p);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static inline const char *zb_op_name(int op)
{
    switch (op) {
    case ZB_OP_HELLO:    return "HELLO";
    case ZB_OP_ATTACK:   return "ATTACK";
    case ZB_OP_HP_SYNC:  return "HP_SYNC";
    case ZB_OP_SNAPSHOT: return "SNAPSHOT";
    case ZB_OP_BYE:      return "BYE";
    default:             return "UNKNOWN";
    }
}

/* 写 4 字节帧头，返回 4（payload 由调用者接着往 buf+4 写） */
static int zb_frame_hdr(unsigned char *buf, int opcode, int payload_len)
{
    zb_put_u16(buf, (uint16_t)(2 + payload_len));
    buf[2] = (unsigned char)opcode;
    buf[3] = 0x00;                 /* flags: 保留，必须 0 */
    return 4;
}

/* ------------------------------------------------------------------ */
/* 小工具                                                                */
/* ------------------------------------------------------------------ */

/* 把前 max_bytes 个字节打成 "02 00 01 00 ..." 这种十六进制字符串 */
static void zb_hex(char *out, int out_cap, const unsigned char *p, int len, int max_bytes)
{
    int used = 0;
    int n = (len < max_bytes) ? len : max_bytes;
    int i;

    out[0] = '\0';
    for (i = 0; i < n; i++) {
        int w = snprintf(out + used, (size_t)(out_cap - used), "%02x ", (unsigned)p[i]);
        if (w <= 0 || w >= out_cap - used) {
            break;
        }
        used += w;
    }
    if (len > n && used + 4 < out_cap) {
        snprintf(out + used, (size_t)(out_cap - used), "...");
    }
}

static inline float zb_absf(float v)
{
    return (v < 0.0f) ? -v : v;
}

/* 单调时钟（QueryPerformanceCounter）：只用来算“从进程启动到现在过了多少秒” */
static double zb_now_s(void)
{
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}

/* 每个进程里的日志：统一前缀 [+  1.234s] */
static double g_zb_t0 = 0.0;

static void zb_log(const char *fmt, ...)
{
    char line[2048];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);

    printf("[+%7.3fs] %s\n", zb_now_s() - g_zb_t0, line);
    fflush(stdout);        /* 输出被重定向到文件时，不 flush 会攒在缓冲区里 */
}

/* 一次 send 不保证把 len 字节全发出去，必须循环到发完 */
static int zb_send_all(SOCKET s, const unsigned char *buf, int len)
{
    int off = 0;

    while (off < len) {
        int n = send(s, (const char *)buf + off, len - off, 0);
        if (n <= 0) {
            return (n == 0) ? WSAECONNRESET : WSAGetLastError();
        }
        off += n;
    }
    return 0;
}

/* select 等“可读”，返回 >0 可读、0 超时、<0 出错。timeout_ms=0 就是非阻塞探查 */
static inline int zb_wait_readable(SOCKET s, int timeout_ms)
{
    fd_set fds;
    struct timeval tv;

    FD_ZERO(&fds);
    FD_SET(s, &fds);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    return select(0, &fds, NULL, NULL, &tv);
}

#endif /* ZOMBIE_H */
