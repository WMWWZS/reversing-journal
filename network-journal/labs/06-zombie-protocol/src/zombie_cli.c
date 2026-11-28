/*
 * zombie_cli.c - ZOMBIE 靶场的客户端
 *
 * 编译：
 *   gcc -O0 -g -Wall -Wextra -o bin\zombie_cli_O0.exe  zombie_cli.c -lws2_32
 *   gcc -O2 -s  -Wall -Wextra -o bin\zombie_cli_O2s.exe zombie_cli.c -lws2_32
 *
 * 它做的事就是真实联机游戏客户端做的事：
 *   1. 连上去，发 HELLO
 *   2. 自己也维护一份「本地预测血量」predicted（先按 -5 算）
 *   3. 每打一下发 ATTACK，payload 里带上「我本地算我剩多少血」
 *   4. 收服务端的 HP_SYNC，和本地预测对比；不一致就打 RECONCILE
 *
 * 另外它是「TCP 是字节流」的活证据：
 *   每一次 recv() 都打 n=多少、前 32 字节 hex，
 *   所以跑 --recv-buf 8192 时，一张 16006 字节的 SNAPSHOT 会被切成好几次 recv。
 */
#include "zombie.h"

/* ------------------------------------------------------------------ */
/* 连接 + 接收缓冲                                                      */
/* ------------------------------------------------------------------ */
typedef struct {
    SOCKET  s;
    unsigned char *in;          /* 还没凑成完整帧的字节都堆在这 */
    size_t  inlen;
    size_t  incap;
    int     hex_bytes;          /* 每次 recv 打多少字节的 hex（默认 32） */
    int     recv_sz;            /* 一次 recv() 最多收多少字节（--recv-buf） */
    long    recv_calls;         /* 总共调了几次 recv() */
    long    frame_first_call;   /* 当前这一帧是从第几次 recv() 开始攒的 */
    unsigned char frame[ZB_MAX_FRAME];   /* 完整帧拷贝，打 hex / 解析用 */
} zb_conn;

typedef struct {
    float predicted;        /* 客户端本地预测血量 */
    float server_hp;        /* 最近一次 HP_SYNC 里服务端报的血量 */
    float max_hp;
    int   hp_sync;          /* 收到几次 HP_SYNC */
    int   snapshot_frames;
    long  snapshot_bytes;   /* 收到几张快照、共多少字节 */
    int   reconcile;        /* 预测和权威值不一致的次数 */
    int   bye;
    uint32_t last_sync_tick;
} zb_cli;

static zb_conn *conn_new(void)
{
    zb_conn *c = (zb_conn *)calloc(1, sizeof(zb_conn));
    if (c == NULL) {
        return NULL;
    }
    c->incap = 262144;      /* 够装下 16006 字节的快照帧，甚至多帧粘在一起 */
    c->in = (unsigned char *)malloc(c->incap);
    c->hex_bytes = 32;
    c->recv_sz = 8192;
    if (c->in == NULL) {
        free(c);
        return NULL;
    }
    return c;
}

static void conn_free(zb_conn *c)
{
    if (c == NULL) {
        return;
    }
    free(c->in);
    free(c);
}

/*
 * 读一块。返回 1=读到、0=超时、-1=对端关闭/出错。
 * 关键：这里 recv 到多少就是多少，不假设等于一帧。
 */
static int conn_read(zb_conn *c, int timeout_ms)
{
    unsigned char tmp[65536];
    char hex[256];
    int n, r;

    r = zb_wait_readable(c->s, timeout_ms);
    if (r == 0) {
        return 0;
    }
    if (r < 0) {
        zb_log("select() failed, WSA error %d", WSAGetLastError());
        return -1;
    }

    n = recv(c->s, (char *)tmp, c->recv_sz, 0);
    if (n <= 0) {
        zb_log("RX recv() = %d (WSA error %d) -> peer closed", n,
               (n < 0) ? WSAGetLastError() : 0);
        return -1;
    }

    c->recv_calls++;
    if (c->inlen == 0) {
        c->frame_first_call = c->recv_calls;    /* 这一帧从这次 recv 开始 */
    }
    if (c->inlen + (size_t)n > c->incap) {
        zb_log("ERROR receive buffer full (%d + %d > %d), dropping it",
               (int)c->inlen, n, (int)c->incap);
        c->inlen = 0;
        c->frame_first_call = c->recv_calls;
    }
    memcpy(c->in + c->inlen, tmp, (size_t)n);
    c->inlen += (size_t)n;

    zb_hex(hex, (int)sizeof hex, tmp, n, c->hex_bytes);
    zb_log("RX recv n=%5d  %s", n, hex);
    return 1;
}

/*
 * 从缓冲里切一帧出来。
 * 返回 1=切到一帧（*opcode / *payload / *payload_len 有效）、0=还不够、-1=流坏了。
 * recv_used 顺便告诉调用者：这一帧是几次 recv() 拼出来的。
 */
static int conn_next_frame(zb_conn *c, int *opcode,
                           const unsigned char **payload, int *payload_len,
                           int *recv_used)
{
    uint16_t total;
    size_t need;

    if (c->inlen < 4) {
        return 0;
    }
    total = zb_get_u16(c->in);
    if (total < 2) {
        zb_log("BAD frame: total_len=%u < 2, dropping the receive buffer", (unsigned)total);
        c->inlen = 0;
        return -1;
    }
    need = 2 + (size_t)total;          /* 整帧 = 2 字节 total_len + total_len 字节 */
    if (c->inlen < need) {
        return 0;                      /* 半帧 */
    }

    memcpy(c->frame, c->in, need);
    *opcode = (int)c->frame[2];
    *payload = (const unsigned char *)(c->frame + 4);
    *payload_len = (int)total - 2;
    *recv_used = (int)(c->recv_calls - c->frame_first_call + 1);

    memmove(c->in, c->in + need, c->inlen - need);
    c->inlen -= need;
    if (c->inlen > 0) {
        c->frame_first_call = c->recv_calls;   /* 剩下的字节属于下一帧 */
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* 发一个小帧（客户端只发 HELLO / ATTACK / BYE，都 <= 16 字节）          */
/* ------------------------------------------------------------------ */
static int cli_send(SOCKET s, int opcode, const unsigned char *pl, int plen, const char *desc)
{
    unsigned char fr[64];
    char hex[256];
    int len = 4 + plen;
    int err;

    if (len > (int)sizeof fr) {
        zb_log("BUG: client frame too big (%d bytes)", len);
        return -1;
    }
    zb_frame_hdr(fr, opcode, plen);
    if (plen > 0) {
        memcpy(fr + 4, pl, (size_t)plen);
    }
    zb_hex(hex, (int)sizeof hex, fr, len, 32);
    zb_log("TX  %2dB %s %s", len, hex, desc);

    err = zb_send_all(s, fr, len);
    if (err != 0) {
        zb_log("send() failed, WSA error %d", err);
    }
    return err;
}

/* ------------------------------------------------------------------ */
/* 处理一个收到的帧                                                      */
/* ------------------------------------------------------------------ */
static void cli_dispatch(zb_conn *c, int opcode, const unsigned char *pl,
                         int plen, int recv_used, zb_cli *st)
{
    char hex[256];
    int framelen = 4 + plen;

    zb_hex(hex, (int)sizeof hex, c->frame, framelen, 32);

    switch (opcode) {
    case ZB_OP_HP_SYNC: {
        uint32_t pid, tick;
        float hp, mhp;
        unsigned alive;
        float delta;

        if (plen < ZB_PL_HP_SYNC) {
            zb_log("RX  %5dB %s HP_SYNC (payload %d < 20, ignored)",
                   framelen, hex, plen);
            break;
        }
        pid  = zb_get_u32(pl + 0);
        hp   = zb_get_f32(pl + 4);
        mhp  = zb_get_f32(pl + 8);
        tick = zb_get_u32(pl + 12);
        alive = (unsigned)pl[16];

        st->hp_sync++;
        st->server_hp = hp;
        st->max_hp = mhp;
        st->last_sync_tick = tick;

        zb_log("RX  %5dB %s HP_SYNC player=%u hp=%.2f max=%.2f tick=%u alive=%u recv_chunks=%d",
               framelen, hex, pid, (double)hp, (double)mhp, tick, alive, recv_used);

        delta = hp - st->predicted;
        if (zb_absf(delta) <= 0.01f) {
            zb_log("      HP check tick=%u  my_prediction=%.2f  server=%.2f  OK",
                   tick, (double)st->predicted, (double)hp);
        } else {
            st->reconcile++;
            zb_log("      RECONCILE tick=%u  my_prediction=%.2f  server=%.2f  delta=%+.2f  "
                   "(server is authoritative, my prediction is wrong)",
                   tick, (double)st->predicted, (double)hp, (double)delta);
        }
        break;
    }
    case ZB_OP_SNAPSHOT: {
        int count = 0;
        int expect;

        if (plen >= 2) {
            count = (int)zb_get_u16(pl + 0);
        }
        expect = 2 + ZB_PL_ENTRY * count;
        st->snapshot_frames++;
        st->snapshot_bytes += framelen;

        zb_log("RX  %5dB %s SNAPSHOT count=%d payload=%d (payload should be %d) recv_chunks=%d",
               framelen, hex, count, plen, expect, recv_used);

        if (count > 0 && plen >= 2 + ZB_PL_ENTRY) {
            const unsigned char *e0 = pl + 2;
            const unsigned char *e1 = pl + 2 + (size_t)ZB_PL_ENTRY * (size_t)(count - 1);
            zb_log("      SNAPSHOT first={id=%u x=%.2f y=%.2f hp=%.2f} "
                   "last={id=%u x=%.2f y=%.2f hp=%.2f}",
                   zb_get_u32(e0 + 0), (double)zb_get_f32(e0 + 4),
                   (double)zb_get_f32(e0 + 8), (double)zb_get_f32(e0 + 12),
                   zb_get_u32(e1 + 0), (double)zb_get_f32(e1 + 4),
                   (double)zb_get_f32(e1 + 8), (double)zb_get_f32(e1 + 12));
        }
        break;
    }
    case ZB_OP_BYE:
        st->bye = 1;
        zb_log("RX  %5dB %s BYE", framelen, hex);
        break;
    default:
        zb_log("RX  %5dB %s opcode=0x%02x (unexpected from server, ignored)",
               framelen, hex, (unsigned)opcode);
        break;
    }
}

/*
 * 一直读到「安静」为止（idle_ms 内没有新字节，而且手里没有半帧）。
 * 用来收 HELLO 之后服务端主动发的东西（比如 SNAPSHOT）。
 */
static void conn_drain(zb_conn *c, zb_cli *st, int idle_ms)
{
    double hard_deadline = zb_now_s() + 10.0;

    for (;;) {
        int opcode, plen, used;
        const unsigned char *pl;
        int r;

        r = conn_next_frame(c, &opcode, &pl, &plen, &used);
        if (r > 0) {
            cli_dispatch(c, opcode, pl, plen, used, st);
            continue;
        }
        if (r < 0) {
            return;
        }
        if (zb_now_s() > hard_deadline) {
            zb_log("WARN drain: hard cap 10s reached, %d byte(s) still pending",
                   (int)c->inlen);
            return;
        }
        r = conn_read(c, idle_ms);
        if (r < 0) {
            return;
        }
        if (r == 0 && c->inlen == 0) {
            return;             /* 安静了，而且没有半帧 —— 收完了 */
        }
        /* r == 0 但 inlen > 0：手里有半帧，继续等（大帧就是这样收完的） */
    }
}

/* 等 tick==seq 的那条 HP_SYNC */
static int wait_hp_sync(zb_conn *c, zb_cli *st, uint32_t seq, int timeout_ms)
{
    double deadline = zb_now_s() + (double)timeout_ms / 1000.0;

    for (;;) {
        int opcode, plen, used;
        const unsigned char *pl;
        int r;
        double left;

        r = conn_next_frame(c, &opcode, &pl, &plen, &used);
        if (r > 0) {
            cli_dispatch(c, opcode, pl, plen, used, st);
            if (opcode == ZB_OP_HP_SYNC && st->last_sync_tick == seq) {
                return 1;
            }
            continue;
        }
        if (r < 0) {
            return 0;
        }
        left = deadline - zb_now_s();
        if (left <= 0.0) {
            return 0;
        }
        r = conn_read(c, (int)(left * 1000.0));
        if (r < 0) {
            return 0;
        }
    }
}

int main(int argc, char **argv)
{
    const char *host = "127.0.0.1";
    int   port = ZB_DEF_TAP_PORT;
    int   attacks = 5;
    float dmg = 5.0f;
    float max_hp = ZB_MAX_HP_DEFAULT;
    int   recv_buf = 8192;
    int   hex_bytes = 32;
    int   join_drain = 400;
    uint32_t client_id = 1;
    int   i;
    WSADATA wsa;
    SOCKET s = INVALID_SOCKET;
    SOCKADDR_IN sa, loc;
    int loc_len = (int)sizeof loc;
    zb_conn *c;
    zb_cli st;
    unsigned char pl[ZB_PL_ATTACK];
    char desc[160];
    uint32_t seq;
    int rc = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            host = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--attacks") == 0 && i + 1 < argc) {
            attacks = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--dmg") == 0 && i + 1 < argc) {
            dmg = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--max-hp") == 0 && i + 1 < argc) {
            max_hp = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--client-id") == 0 && i + 1 < argc) {
            client_id = (uint32_t)strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--join-drain-ms") == 0 && i + 1 < argc) {
            join_drain = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--hex-bytes") == 0 && i + 1 < argc) {
            hex_bytes = atoi(argv[++i]);        /* 每块打多少字节 hex，默认 32 */
        } else if (strcmp(argv[i], "--recv-buf") == 0 && i + 1 < argc) {
            recv_buf = atoi(argv[++i]);         /* 一次 recv() 的缓冲大小 */
        } else {
            printf("usage: zombie_cli.exe [--host 127.0.0.1] [--port 47000] [--attacks 5]\n"
                   "                     [--dmg 5.0] [--max-hp 100] [--client-id 1]\n"
                   "                     [--join-drain-ms 400] [--recv-buf 8192]\n");
            return 1;
        }
    }

    if (attacks < 0) {
        attacks = 0;
    }

    g_zb_t0 = zb_now_s();
    memset(&st, 0, sizeof st);
    st.predicted = max_hp;
    st.max_hp = max_hp;
    st.server_hp = max_hp;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        zb_log("WSAStartup failed, error %d", WSAGetLastError());
        return 2;
    }

    c = conn_new();
    if (c == NULL) {
        zb_log("out of memory");
        WSACleanup();
        return 2;
    }
    c->hex_bytes = hex_bytes;
    if (recv_buf < 1) {
        recv_buf = 1;
    }
    if (recv_buf > 65536) {
        recv_buf = 65536;
    }
    c->recv_sz = recv_buf;
    zb_log("zombie_cli pid=%lu recv_buf=%d hex_bytes=%d attacks=%d dmg=%.2f max_hp=%.2f",
           (unsigned long)GetCurrentProcessId(), c->recv_sz, c->hex_bytes,
           attacks, (double)dmg, (double)max_hp);

    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        zb_log("socket() failed, WSA error %d", WSAGetLastError());
        conn_free(c);
        WSACleanup();
        return 2;
    }

    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((u_short)port);
    if (inet_pton(AF_INET, host, &sa.sin_addr) != 1) {
        zb_log("inet_pton(%s) failed", host);
        closesocket(s);
        conn_free(c);
        WSACleanup();
        return 2;
    }

    if (connect(s, (SOCKADDR *)&sa, sizeof sa) == SOCKET_ERROR) {
        zb_log("connect %s:%d FAILED, WSA error %d", host, port, WSAGetLastError());
        closesocket(s);
        conn_free(c);
        WSACleanup();
        return 2;
    }
    c->s = s;

    if (getsockname(s, (SOCKADDR *)&loc, &loc_len) == 0) {
        zb_log("zombie_cli pid=%lu connect %s:%d ok  local=127.0.0.1:%u  (this local port is "
               "the one you see in netstat)",
               (unsigned long)GetCurrentProcessId(), host, port,
               (unsigned)ntohs(loc.sin_port));
    } else {
        zb_log("zombie_cli pid=%lu connect %s:%d ok (getsockname failed)",
               (unsigned long)GetCurrentProcessId(), host, port);
    }

    /* ---------- 1) HELLO: 12 字节 ---------- */
    zb_put_u32(pl + 0, client_id);
    zb_put_u32(pl + 4, (uint32_t)ZB_PROTO_VERSION);
    snprintf(desc, sizeof desc, "HELLO client_id=%u version=%u",
             client_id, (unsigned)ZB_PROTO_VERSION);
    if (cli_send(s, ZB_OP_HELLO, pl, ZB_PL_HELLO, desc) != 0) {
        rc = 2;
        goto cleanup;
    }

    /* 服务端如果在 HELLO 后发了 SNAPSHOT，就在这里收掉 */
    conn_drain(c, &st, join_drain);

    /* ---------- 2) 攻击 attacks 次 ---------- */
    for (seq = 1; seq <= (uint32_t)attacks; seq++) {
        st.predicted -= dmg;
        if (st.predicted < 0.0f) {
            st.predicted = 0.0f;
        }
        if (st.predicted > max_hp) {
            st.predicted = max_hp;
        }

        zb_put_f32(pl + 0, dmg);
        zb_put_u32(pl + 4, seq);
        zb_put_f32(pl + 8, st.predicted);

        snprintf(desc, sizeof desc, "ATTACK dmg=%.2f seq=%u claimed=%.2f",
                 (double)dmg, seq, (double)st.predicted);
        if (cli_send(s, ZB_OP_ATTACK, pl, ZB_PL_ATTACK, desc) != 0) {
            rc = 2;
            goto cleanup;
        }

        /* 每帧后 Sleep(200)，别让 TCP 把一堆帧合成一坨（否则 tap 日志里看不出“一帧一次 recv”） */
        Sleep(200);

        if (!wait_hp_sync(c, &st, seq, 3000)) {
            zb_log("WARN no HP_SYNC for tick=%u within 3000 ms", seq);
        }
    }

    /* ---------- 3) BYE: 4 字节 ---------- */
    if (cli_send(s, ZB_OP_BYE, NULL, 0, "BYE") != 0) {
        rc = 2;
        goto cleanup;
    }
    conn_drain(c, &st, 300);

cleanup:
    if (st.hp_sync > 0 || attacks > 0) {
        int all_ok = (attacks > 0)
                  && (st.hp_sync == attacks)
                  && (zb_absf(st.predicted - st.server_hp) <= 0.01f);
        zb_log("SUMMARY attacks=%d hp_sync=%d predicted=%.2f server_hp=%.2f match=%s",
               attacks, st.hp_sync, (double)st.predicted, (double)st.server_hp,
               all_ok ? "YES" : "NO");
        zb_log("SUMMARY snapshot_frames=%d snapshot_bytes=%ld reconcile=%d recv_calls=%ld "
               "bytes_left_in_buffer=%d",
               st.snapshot_frames, st.snapshot_bytes, st.reconcile, c->recv_calls,
               (int)c->inlen);
        if (!all_ok) {
            rc = 3;
        }
    }
    zb_log("client exit rc=%d", rc);

    closesocket(s);
    conn_free(c);
    WSACleanup();
    return rc;
}
