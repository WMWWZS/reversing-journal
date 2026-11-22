/*
 * zombie_srv.c - ZOMBIE 靶场的服务端（权威方）
 *
 * 编译：
 *   gcc -O0 -g -Wall -Wextra -o bin\zombie_srv_O0.exe  zombie_srv.c -lws2_32
 *   gcc -O2 -s  -Wall -Wextra -o bin\zombie_srv_O2s.exe zombie_srv.c -lws2_32
 *
 * 两种模式（整节课的安全主线）：
 *   --authoritative（默认）  只信自己：hp = max(0, hp - damage)
 *                            claimed_hp 只拿来打日志做对比
 *   --trust-client           直接把客户端报的 claimed_hp 当权威血量
 *                            （现实中无数联机游戏的漏洞就长这样）
 *
 * 服务端只做一件事：一个连接、一帧一帧读、按协议回 HP_SYNC。
 * 所有收发都打十六进制，这样文档里引用的每个字节都能对上。
 */
#include "zombie.h"

typedef struct {
    uint32_t player_id;
    float    hp;
    float    max_hp;
    int      trust_client;     /* 1 = 直接采信客户端的 claimed_hp */
} zb_game;

/* 统计，最后一行 FINAL 里打出来，方便和客户端日志对账 */
typedef struct {
    int frames;
    int hello;
    int attack;
    int reject;
    int bye;
    int unknown;
} zb_stats;

static int srv_send(SOCKET s, const unsigned char *frame, int len)
{
    int err = zb_send_all(s, frame, len);
    if (err != 0) {
        zb_log("send() failed, WSA error %d", err);
    }
    return err;
}

/* HP_SYNC: 24 字节整帧 = 4 字节帧头 + 20 字节 payload */
static void srv_send_hp_sync(SOCKET s, const zb_game *g, uint32_t tick)
{
    unsigned char pl[ZB_PL_HP_SYNC];
    unsigned char fr[ZB_FR_HP_SYNC];
    char hex[256];

    memset(pl, 0, sizeof pl);
    zb_put_u32(pl + 0,  g->player_id);
    zb_put_f32(pl + 4,  g->hp);
    zb_put_f32(pl + 8,  g->max_hp);
    zb_put_u32(pl + 12, tick);
    pl[16] = (g->hp > 0.0f) ? 1 : 0;   /* alive */
    /* pl[17..19] = pad[3]，memset 已经清零 */

    zb_frame_hdr(fr, ZB_OP_HP_SYNC, ZB_PL_HP_SYNC);
    memcpy(fr + 4, pl, ZB_PL_HP_SYNC);

    zb_hex(hex, (int)sizeof hex, fr, ZB_FR_HP_SYNC, 32);
    zb_log("TX  %2dB %s HP_SYNC hp=%.2f max=%.2f tick=%u alive=%u",
           ZB_FR_HP_SYNC, hex, g->hp, g->max_hp, tick, (unsigned)pl[16]);

    srv_send(s, fr, ZB_FR_HP_SYNC);
}

/*
 * SNAPSHOT: 大地图快照，一张 16006 字节的巨帧。
 * 这里故意允许把一帧用多次 send() 拆开发（--send-chunk N），
 * 因为 TCP 是「字节流」不是「消息流」：发送端怎么切、接收端都不能假设。
 */
static void srv_send_snapshot(SOCKET s, int count, int send_chunk)
{
    int payload_len = 2 + ZB_PL_ENTRY * count;
    int frame_len = 4 + payload_len;
    unsigned char *pl = (unsigned char *)malloc((size_t)payload_len);
    unsigned char *fr = (unsigned char *)malloc((size_t)frame_len);
    char hex[256];
    int i;

    if (pl == NULL || fr == NULL) {
        zb_log("ERROR out of memory for SNAPSHOT count=%d", count);
        free(pl);
        free(fr);
        return;
    }

    zb_put_u16(pl + 0, (uint16_t)count);
    for (i = 0; i < count; i++) {
        unsigned char *e = pl + 2 + ZB_PL_ENTRY * i;
        zb_put_u32(e + 0, (uint32_t)i);                    /* id */
        zb_put_f32(e + 4, (float)i * 1.5f);                /* x  */
        zb_put_f32(e + 8, (float)i * 0.5f);                /* y  */
        zb_put_f32(e + 12, 100.0f - (float)(i % 100));     /* hp */
    }

    zb_frame_hdr(fr, ZB_OP_SNAPSHOT, payload_len);
    memcpy(fr + 4, pl, (size_t)payload_len);
    free(pl);

    zb_hex(hex, (int)sizeof hex, fr, frame_len, 32);

    if (send_chunk <= 0) {
        zb_log("TX  %5dB %s SNAPSHOT count=%d payload=%d (one send() call, frame=%.2f KB)",
               frame_len, hex, count, payload_len, (double)frame_len / 1024.0);
        srv_send(s, fr, frame_len);
    } else {
        int parts = (frame_len + send_chunk - 1) / send_chunk;
        int off = 0;
        int part = 0;

        zb_log("TX  %5dB %s SNAPSHOT count=%d payload=%d (split into %d send() calls of <=%d bytes)",
               frame_len, hex, count, payload_len, parts, send_chunk);

        while (off < frame_len) {
            int n = frame_len - off;
            int err;
            if (n > send_chunk) {
                n = send_chunk;
            }
            err = zb_send_all(s, fr + off, n);
            part++;
            /* 每次 send() 单独一行：这次切了多少字节、切在帧里的哪个 offset、
             * 这次写的头 8 个字节长什么样 —— 一眼能看出「一帧被拆成了几段」 */
            {
                char shex[64];
                int m = (n < 8) ? n : 8;
                zb_hex(shex, (int)sizeof shex, fr + off, m, m);
                zb_log("TX    send() #%d/%d  %5d bytes  offset=%5d  first8=%s%s",
                       part, parts, n, off, shex, (n > m) ? " ..." : "");
            }
            if (err != 0) {
                zb_log("send() failed mid-snapshot, WSA error %d", err);
                break;
            }
            off += n;
        }
    }
    free(fr);
}

int main(int argc, char **argv)
{
    int   port = ZB_DEF_SRV_PORT;
    int   snapshot_count = 0;
    const char *bind_ip = "127.0.0.1";
    int   send_chunk = 0;
    int   trust = 0;
    float max_hp = ZB_MAX_HP_DEFAULT;
    int   i;
    WSADATA wsa;
    SOCKET ls, cs = INVALID_SOCKET;
    SOCKADDR_IN sa, ca;
    int ca_len = (int)sizeof ca;
    unsigned char *inbuf;
    size_t inlen = 0;
    size_t incap = 131072;
    zb_game g;
    zb_stats st;
    int done = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--trust-client") == 0) {
            trust = 1;
        } else if (strcmp(argv[i], "--authoritative") == 0) {
            trust = 0;
        } else if (strcmp(argv[i], "--bind") == 0 && i + 1 < argc) {
            bind_ip = argv[++i];
        } else if (strcmp(argv[i], "--max-hp") == 0 && i + 1 < argc) {
            max_hp = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--snapshot-count") == 0 && i + 1 < argc) {
            snapshot_count = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--send-chunk") == 0 && i + 1 < argc) {
            send_chunk = atoi(argv[++i]);
        } else {
            printf("usage: zombie_srv.exe [--port N] [--bind IP] "
                   "[--authoritative|--trust-client]\n"
                   "                     [--max-hp F] [--snapshot-count N] [--send-chunk N]\n");
            return 1;
        }
    }

    if (snapshot_count < 0 || snapshot_count > ZB_MAX_SNAPSHOT) {
        printf("error: --snapshot-count must be 0..%d\n", ZB_MAX_SNAPSHOT);
        return 1;
    }
    if (send_chunk < 0) {
        printf("error: --send-chunk must be >= 0\n");
        return 1;
    }

    g_zb_t0 = zb_now_s();

    memset(&g, 0, sizeof g);
    g.player_id = 0;
    g.hp = max_hp;
    g.max_hp = max_hp;
    g.trust_client = trust;

    memset(&st, 0, sizeof st);

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        zb_log("WSAStartup failed, error %d", WSAGetLastError());
        return 2;
    }

    inbuf = (unsigned char *)malloc(incap);
    if (inbuf == NULL) {
        zb_log("out of memory");
        WSACleanup();
        return 2;
    }

    ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls == INVALID_SOCKET) {
        zb_log("socket() failed, WSA error %d", WSAGetLastError());
        WSACleanup();
        return 2;
    }

    {
        BOOL yes = TRUE;
        /* SO_REUSEADDR: 刚退出/重启服务端时能立刻重新 bind 同一个端口 */
        setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof yes);
    }

    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((u_short)port);
    if (inet_pton(AF_INET, bind_ip, &sa.sin_addr) != 1) {
        zb_log("inet_pton(%s) failed", bind_ip);
        closesocket(ls);
        WSACleanup();
        return 2;
    }

    if (bind(ls, (SOCKADDR *)&sa, sizeof sa) == SOCKET_ERROR) {
        zb_log("bind %s:%d failed, WSA error %d", bind_ip, port, WSAGetLastError());
        closesocket(ls);
        WSACleanup();
        return 2;
    }
    if (listen(ls, 4) == SOCKET_ERROR) {
        zb_log("listen() failed, WSA error %d", WSAGetLastError());
        closesocket(ls);
        WSACleanup();
        return 2;
    }

    zb_log("zombie_srv pid=%lu  mode=%s  max_hp=%.2f  snapshot_count=%d  send_chunk=%d",
           (unsigned long)GetCurrentProcessId(),
           trust ? "trust-client" : "authoritative",
           (double)g.max_hp, snapshot_count, send_chunk);
    zb_log("listen %s:%d ok, waiting for client (Ctrl+C to stop)", bind_ip, port);

    cs = accept(ls, (SOCKADDR *)&ca, &ca_len);
    if (cs == INVALID_SOCKET) {
        zb_log("accept() failed, WSA error %d", WSAGetLastError());
        closesocket(ls);
        WSACleanup();
        return 2;
    }
    zb_log("accept ok: peer=%s:%u  (this peer is the tap, or the client if you bypass the tap)",
           inet_ntoa(ca.sin_addr), (unsigned)ntohs(ca.sin_port));

    while (!done) {
        int n;
        int keep = 1;

        /* 阻塞读一块字节。注意：读到的可能是半帧，也可能是多帧，下面按 total_len 拆 */
        n = recv(cs, (char *)inbuf + inlen, (int)(incap - inlen), 0);
        if (n <= 0) {
            zb_log("RX recv() = %d (WSA error %d) -> peer closed", n,
                   (n < 0) ? WSAGetLastError() : 0);
            break;
        }
        zb_log("RX recv n=%d", n);
        inlen += (size_t)n;

        while (keep) {
            uint16_t total;
            size_t need;
            int opcode, payload_len;
            unsigned char *pl;
            char hex[256];

            if (inlen < 4) {
                break;                 /* 连帧头都不够，继续 recv */
            }
            total = zb_get_u16(inbuf);
            if (total < 2) {
                zb_log("BAD frame: total_len=%u < 2, drop the whole buffer", (unsigned)total);
                inlen = 0;
                break;
            }
            need = 2 + (size_t)total;
            if (inlen < need) {
                break;                 /* 半帧，继续 recv */
            }

            opcode = inbuf[2];
            payload_len = (int)total - 2;
            pl = inbuf + 4;

            st.frames++;
            zb_hex(hex, (int)sizeof hex, inbuf, (int)need, 32);

            switch (opcode) {
            case ZB_OP_HELLO: {
                uint32_t cid, ver;
                if (payload_len >= ZB_PL_HELLO) {
                    cid = zb_get_u32(pl + 0);
                    ver = zb_get_u32(pl + 4);
                } else {
                    cid = 0;
                    ver = 0;
                    zb_log("WARN HELLO payload_len=%d < 8", payload_len);
                }
                st.hello++;
                zb_log("RX  %2dB %s HELLO client_id=%u version=%u",
                       (int)need, hex, cid, ver);
                g.player_id = cid;
                if (ver != ZB_PROTO_VERSION) {
                    zb_log("WARN version=%u != %u, keep going anyway", ver,
                           (unsigned)ZB_PROTO_VERSION);
                }
                if (snapshot_count > 0) {
                    /* HELLO 之后立刻发一张大地图快照：客户端必须自己处理“半帧” */
                    srv_send_snapshot(cs, snapshot_count, send_chunk);
                }
                break;
            }
            case ZB_OP_ATTACK: {
                float damage, claimed;
                uint32_t seq;
                int valid;
                float before = g.hp;

                if (payload_len < ZB_PL_ATTACK) {
                    zb_log("WARN ATTACK payload_len=%d, too short, ignore", payload_len);
                    break;
                }
                damage = zb_get_f32(pl + 0);
                seq = zb_get_u32(pl + 4);
                claimed = zb_get_f32(pl + 8);
                st.attack++;

                zb_log("RX  %2dB %s ATTACK seq=%u dmg=%.2f claimed=%.2f",
                       (int)need, hex, seq, (double)damage, (double)claimed);

                /* 服务端的第一道防线：伤害必须 0 < dmg <= 25，否则整条请求丢掉 */
                valid = (damage > 0.0f) && (damage <= ZB_MAX_DAMAGE);
                if (damage != damage) {         /* NaN */
                    valid = 0;
                }

                if (!valid) {
                    st.reject++;
                    zb_log("REJECT seq=%u dmg=%.2f out of range (0 < dmg <= 25); hp stays %.2f",
                           seq, (double)damage, (double)g.hp);
                } else if (g.trust_client) {
                    /* 漏洞演示：客户端说什么就是什么 */
                    float v = claimed;
                    if (v < 0.0f) {
                        v = 0.0f;
                    }
                    if (v > g.max_hp) {
                        v = g.max_hp;
                    }
                    g.hp = v;
                    zb_log("HP   %.2f -> %.2f  (trust-client: took claimed_hp=%.2f as truth, diff=%.2f)",
                           (double)before, (double)g.hp, (double)claimed,
                           (double)(claimed - before));
                } else {
                    /* 权威模式：自己算，claimed 只用来打日志 */
                    g.hp = before - damage;
                    if (g.hp < 0.0f) {
                        g.hp = 0.0f;
                    }
                    zb_log("HP   %.2f -> %.2f  (authoritative: claimed says %.2f, diff=%.2f)",
                           (double)before, (double)g.hp, (double)claimed,
                           (double)(claimed - g.hp));
                }

                srv_send_hp_sync(cs, &g, seq);
                break;
            }
            case ZB_OP_BYE:
                st.bye++;
                zb_log("RX  %2dB %s BYE", (int)need, hex);
                done = 1;
                keep = 0;
                break;
            default:
                st.unknown++;
                zb_log("RX  %2dB %s ??? opcode=0x%02x (unknown, ignored)",
                       (int)need, hex, (unsigned)opcode);
                break;
            }

            memmove(inbuf, inbuf + need, inlen - need);
            inlen -= need;
        }
    }

    if (inlen > 0) {
        zb_log("WARN %d trailing byte(s) left in the receive buffer (partial frame at exit)",
               (int)inlen);
    }

    zb_log("FINAL hp=%.2f max_hp=%.2f alive=%d  frames=%d hello=%d attack=%d reject=%d bye=%d unknown=%d",
           (double)g.hp, (double)g.max_hp, (g.hp > 0.0f) ? 1 : 0,
           st.frames, st.hello, st.attack, st.reject, st.bye, st.unknown);
    zb_log("server exit");

    closesocket(cs);
    closesocket(ls);
    WSACleanup();
    free(inbuf);
    return 0;
}
