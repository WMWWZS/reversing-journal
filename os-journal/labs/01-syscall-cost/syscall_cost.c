/*
 * Lab 01 - syscall & kernel-path cost  (Windows x64, MinGW-w64 gcc)
 *
 * 用 QueryPerformanceCounter 实测四类「内核路径」的开销：
 *   [1] memcpy 带宽          —— 用户态带宽基准，后面所有数字都拿它对照
 *   [2] 裸 syscall 往返      —— NtQuerySystemTime，对比纯用户态调用
 *   [3] 文件 I/O 每次调用     —— CreateFileW / ReadFile / WriteFile
 *   [4] 线程创建 + 上下文切换 —— CreateThread(+join) 与事件 ping-pong
 *   [5] 缺页成本             —— VirtualAlloc 提交 / 首次触碰 / 已触碰
 *
 * 两条工程纪律（否则测出来的数字全是假的）：
 *   1. 每个循环都把结果喂给 volatile 变量，-O2 不敢删；
 *   2. 每个点位重复 N 次，报 min 与 median，只用 min 说「下限」。
 *
 * 构建：powershell -ExecutionPolicy Bypass -File .\build.ps1
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static double g_ticks_per_us = 0.0;     /* QPC 每微秒的 tick 数 */
static volatile unsigned long long g_sink = 0;  /* 防优化用的汇聚变量 */

static double now_us(void) {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / g_ticks_per_us;
}

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a;
    double y = *(const double *)b;
    return (x > y) - (x < y);
}

/* 对样本排序后取最小值与中位数（单位：微秒） */
static void stats(double *s, int n, double *out_min, double *out_med) {
    qsort(s, (size_t)n, sizeof(double), cmp_double);
    *out_min = s[0];
    if (n & 1) {
        *out_med = s[n / 2];
    } else {
        *out_med = 0.5 * (s[n / 2 - 1] + s[n / 2]);
    }
}

static void rule(void) {
    puts("  ------------------------------------------------------------------");
}

/* ---------------------------------------------------------------- [1] */
static void bench_memcpy(void) {
    static const size_t sizes[4] = {1u << 20, 4u << 20, 16u << 20, 64u << 20};
    static const char *names[4] = {"1 MiB", "4 MiB", "16 MiB", "64 MiB"};
    static const int iters[4] = {400, 200, 60, 20};

    puts("[1] memcpy bandwidth (dst <- src)");
    printf("  %-8s %7s %12s %12s %10s\n", "size", "iters", "best ms", "median ms", "GB/s");
    rule();

    for (int k = 0; k < 4; k++) {
        size_t sz = sizes[k];
        int n = iters[k];
        unsigned char *src = (unsigned char *)VirtualAlloc(NULL, sz, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        unsigned char *dst = (unsigned char *)VirtualAlloc(NULL, sz, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        double *s;

        if (src == NULL || dst == NULL) {
            puts("  VirtualAlloc failed");
            return;
        }
        memset(src, 0xA5, sz);
        memcpy(dst, src, sz);   /* warm up：先把页面都摸热，别把缺页算进带宽 */

        s = (double *)malloc(sizeof(double) * (size_t)n);
        if (s == NULL) {
            return;
        }
        for (int i = 0; i < n; i++) {
            double t0 = now_us();
            memcpy(dst, src, sz);
            s[i] = now_us() - t0;
            /* 读回 dst 里几个位置：memcpy 的结果必须真的存在，循环才删不掉 */
            g_sink += dst[(size_t)(i * 7919u) % sz];
        }

        {
            double mn, md;
            stats(s, n, &mn, &md);
            printf("  %-8s %7d %12.3f %12.3f %10.2f\n",
                   names[k], n, mn / 1000.0, md / 1000.0,
                   ((double)sz / (mn * 1e-6)) / 1e9);
        }

        free(s);
        VirtualFree(src, 0, MEM_RELEASE);
        VirtualFree(dst, 0, MEM_RELEASE);
    }
}

/* ---------------------------------------------------------------- [2] */
/*
 * 这一节故意选三个「看起来都像系统调用」的函数，用字节签名拆穿它们：
 *
 *   ntdll!NtQuerySystemTime        可能是 jmp rel32 —— 在 Windows 11 上它被
 *                                  转发到用户态实现，两次调用只要 2 ns，根本
 *                                  没进内核。名字带 Nt 不代表它是 syscall。
 *   ntdll!NtQuerySystemInformation 标准 stub：mov r10,rcx / mov eax,imm /
 *                                  test [7FFE0308] / jne / syscall / ret，
 *                                  这才是真正跨内核边界的成本。
 *   kernel32!GetSystemTimeAsFileTime  用户态 jmp 跳板，读 KUSER_SHARED_DATA，
 *                                  用来当「纯用户态」的对照。
 *
 * 测量方法：QPC 只有 100 ns 分辨率，单次调用可能不到一个 tick，所以必须
 * 「批量计时再除」—— 一次密集调用 2000 次，再除以 2000。
 */
static const char *stub_kind(const unsigned char *b) {
    if (b[0] == 0xE9) {
        return "jmp rel32 -> NOT a syscall";
    }
    if (b[0] == 0x48 && b[1] == 0xFF && b[2] == 0x25) {
        return "jmp [rip+..] thunk, user-mode";
    }
    for (int i = 0; i + 1 < 20; i++) {
        if (b[i] == 0x0F && b[i + 1] == 0x05) {
            return "contains 0f 05 -> real syscall";
        }
    }
    return "(unclassified)";
}

static void print_stub(const char *name, const void *fn) {
    unsigned char b[24];
    memcpy(b, fn, sizeof(b));
    printf("  %-30s %02x %02x %02x %02x %02x %02x   %s\n",
           name, b[0], b[1], b[2], b[3], b[4], b[5], stub_kind(b));
}

static void bench_raw_syscall(void) {
    typedef LONG (NTAPI *PFN_NTST)(PLARGE_INTEGER);
    typedef LONG (NTAPI *PFN_NTSI)(ULONG, PVOID, ULONG, PULONG);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    PFN_NTST NtQuerySystemTime;
    PFN_NTSI NtQuerySystemInformation;
    const int batch = 2000;    /* 每个计时样本里调用多少次 */
    const int samples = 100;   /* 每个 repeat 采多少个样本 */
    const int reps = 5;
    double k_best = 1e30, k_med = 0.0;      /* NtQuerySystemInformation */
    double t_best = 1e30, t_med = 0.0;      /* NtQuerySystemTime */
    double u_best = 1e30, u_med = 0.0;      /* GetSystemTimeAsFileTime */
    double *s;
    unsigned char sysinfo[64];
    ULONG ret = 0;
    LARGE_INTEGER li;
    FILETIME ft;

    puts("[2] how expensive is crossing the kernel boundary?");
    printf("  %-30s %-13s %s\n", "function", "first 6 bytes", "classification");
    rule();

    if (ntdll == NULL) {
        puts("  ntdll.dll not found");
        return;
    }
    NtQuerySystemTime = (PFN_NTST)(void *)GetProcAddress(ntdll, "NtQuerySystemTime");
    NtQuerySystemInformation = (PFN_NTSI)(void *)GetProcAddress(ntdll, "NtQuerySystemInformation");
    if (NtQuerySystemTime == NULL || NtQuerySystemInformation == NULL) {
        puts("  ntdll exports missing");
        return;
    }

    print_stub("NtQuerySystemTime", (const void *)NtQuerySystemTime);
    print_stub("NtQuerySystemInformation", (const void *)NtQuerySystemInformation);
    print_stub("GetSystemTimeAsFileTime", (const void *)GetSystemTimeAsFileTime);

    printf("\n  batched timing: %d calls/sample, %d samples, %d repeats\n", batch, samples, reps);
    printf("  %-30s %9s %14s %12s\n", "operation", "batches", "best ns/call", "median ns");
    rule();

    s = (double *)malloc(sizeof(double) * (size_t)samples);
    if (s == NULL) {
        return;
    }

    for (int r = 0; r < reps; r++) {
        /* --- 真 syscall --- */
        for (int j = 0; j < samples; j++) {
            double t0 = now_us();
            for (int i = 0; i < batch; i++) {
                NtQuerySystemInformation(0 /* SystemBasicInformation */,
                                         sysinfo, (ULONG)sizeof(sysinfo), &ret);
            }
            s[j] = (now_us() - t0) * 1000.0 / (double)batch;
            g_sink += sysinfo[0] + ret;
        }
        {
            double mn, md;
            stats(s, samples, &mn, &md);
            if (mn < k_best) {
                k_best = mn;
            }
            k_med = md;
        }

        /* --- 名字像 syscall，其实被转发了 --- */
        for (int j = 0; j < samples; j++) {
            double t0 = now_us();
            for (int i = 0; i < batch; i++) {
                NtQuerySystemTime(&li);
            }
            s[j] = (now_us() - t0) * 1000.0 / (double)batch;
            g_sink += (unsigned long long)li.QuadPart;
        }
        {
            double mn, md;
            stats(s, samples, &mn, &md);
            if (mn < t_best) {
                t_best = mn;
            }
            t_med = md;
        }

        /* --- 纯用户态对照 --- */
        for (int j = 0; j < samples; j++) {
            double t0 = now_us();
            for (int i = 0; i < batch; i++) {
                GetSystemTimeAsFileTime(&ft);
            }
            s[j] = (now_us() - t0) * 1000.0 / (double)batch;
            g_sink += ft.dwLowDateTime;
        }
        {
            double mn, md;
            stats(s, samples, &mn, &md);
            if (mn < u_best) {
                u_best = mn;
            }
            u_med = md;
        }
    }
    free(s);

    printf("  %-30s %9d %14.1f %12.1f\n", "NtQuerySystemInformation", samples, k_best, k_med);
    printf("  %-30s %9d %14.1f %12.1f\n", "NtQuerySystemTime", samples, t_best, t_med);
    printf("  %-30s %9d %14.1f %12.1f\n", "GetSystemTimeAsFileTime", samples, u_best, u_med);
    printf("  -> real syscall is about %.0f x the user-mode call (%.0f ns vs %.0f ns)\n",
           u_best > 0.0 ? k_best / u_best : 0.0, k_best, u_best);
    puts("  note: NtQuerySystemTime shows ~2 ns, so it never enters the kernel on this");
    puts("        build. 'Nt' prefix is not proof of a syscall; check the stub bytes.");
}

/* ---------------------------------------------------------------- [3] */
static void make_temp_path(wchar_t *path, size_t cch) {
    wchar_t tmp[MAX_PATH];
    DWORD len = GetTempPathW(MAX_PATH, tmp);

    if (len > 0 && len < MAX_PATH) {
        wcsncpy(path, tmp, cch - 1);
        path[cch - 1] = L'\0';
        wcsncat(path, L"lab01_syscall_cost.bin", cch - wcslen(path) - 1);
    } else {
        wcsncpy(path, L"lab01_syscall_cost.bin", cch - 1);
        path[cch - 1] = L'\0';
    }
}

static void bench_file_io(void) {
    static const size_t sizes[3] = {4u << 10, 64u << 10, 1u << 20};
    static const char *names[3] = {"4 KiB", "64 KiB", "1 MiB"};
    static const int iters[3] = {2000, 500, 64};
    wchar_t path[MAX_PATH];
    HANDLE h;
    DWORD written = 0;
    double *s;

    make_temp_path(path, MAX_PATH);

    puts("[3] file I/O per call (page cache enabled, size matters)");
    printf("  %-22s %-8s %7s %12s %12s %10s\n",
           "operation", "size", "iters", "best us", "median us", "MB/s");
    rule();

    for (int k = 0; k < 3; k++) {
        size_t sz = sizes[k];
        int n = iters[k];
        unsigned char *buf = (unsigned char *)VirtualAlloc(NULL, sz, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (buf == NULL) {
            return;
        }
        memset(buf, (int)(0x30 + k), sz);
        s = (double *)malloc(sizeof(double) * (size_t)n);
        if (s == NULL) {
            VirtualFree(buf, 0, MEM_RELEASE);
            return;
        }

        /* --- WriteFile --- */
        h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_TEMPORARY, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            printf("  CreateFileW(write) failed: %lu\n", GetLastError());
            free(s);
            VirtualFree(buf, 0, MEM_RELEASE);
            continue;
        }
        for (int i = 0; i < n; i++) {
            double t0 = now_us();
            BOOL ok = WriteFile(h, buf, (DWORD)sz, &written, NULL);
            s[i] = now_us() - t0;
            g_sink += (unsigned long long)written + (ok ? 1u : 0u);
        }
        {
            double mn, md;
            stats(s, n, &mn, &md);
            printf("  %-22s %-8s %7d %12.3f %12.3f %10.1f\n", "WriteFile", names[k], n, mn, md,
                   ((double)sz / (mn * 1e-6)) / 1e6);
        }
        CloseHandle(h);

        /* --- ReadFile --- */
        h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            printf("  CreateFileW(read) failed: %lu\n", GetLastError());
            free(s);
            VirtualFree(buf, 0, MEM_RELEASE);
            continue;
        }
        for (int i = 0; i < n; i++) {
            double t0 = now_us();
            DWORD got = 0;
            BOOL ok = ReadFile(h, buf, (DWORD)sz, &got, NULL);
            s[i] = now_us() - t0;
            g_sink += (unsigned long long)got + (ok ? 1u : 0u);
        }
        {
            double mn, md;
            stats(s, n, &mn, &md);
            printf("  %-22s %-8s %7d %12.3f %12.3f %10.1f\n", "ReadFile", names[k], n, mn, md,
                   ((double)sz / (mn * 1e-6)) / 1e6);
        }
        CloseHandle(h);

        free(s);
        VirtualFree(buf, 0, MEM_RELEASE);
    }

    /* --- open + close --- */
    {
        const int n = 2000;
        s = (double *)malloc(sizeof(double) * (size_t)n);
        if (s != NULL) {
            for (int i = 0; i < n; i++) {
                double t0 = now_us();
                h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                NULL, OPEN_EXISTING, 0, NULL);
                if (h != INVALID_HANDLE_VALUE) {
                    CloseHandle(h);
                    g_sink += 1;
                }
                s[i] = now_us() - t0;
            }
            {
                double mn, md;
                stats(s, n, &mn, &md);
                printf("  %-22s %-8s %7d %12.3f %12.3f %10s\n",
                       "CreateFileW+CloseHandle", "-", n, mn, md, "-");
            }
            free(s);
        }
        DeleteFileW(path);
    }

    putchar('\n');
    puts("  note: the file is already in the page cache, so this is 'syscall + memcpy',");
    puts("        not disk latency. A cold first read is an order of magnitude slower.");
}

/* ---------------------------------------------------------------- [4] */
static DWORD WINAPI noop_thread(LPVOID arg) {
    (void)arg;
    return 0;
}

static void bench_thread_create(void) {
    const int n = 300;
    double *s = (double *)malloc(sizeof(double) * (size_t)n);

    puts("[4a] CreateThread + wait + CloseHandle");
    printf("  %-30s %7s %12s %12s\n", "operation", "iters", "best us", "median us");
    rule();

    if (s == NULL) {
        return;
    }
    for (int i = 0; i < n; i++) {
        double t0 = now_us();
        HANDLE h = CreateThread(NULL, 0, noop_thread, NULL, 0, NULL);
        if (h != NULL) {
            WaitForSingleObject(h, INFINITE);
            CloseHandle(h);
            g_sink += 1;
        }
        s[i] = now_us() - t0;
    }
    {
        double mn, md;
        stats(s, n, &mn, &md);
        printf("  %-30s %7d %12.3f %12.3f\n", "create+join thread", n, mn, md);
    }
    free(s);
}

static HANDLE g_ev_ping = NULL;
static HANDLE g_ev_pong = NULL;

static DWORD WINAPI pong_proc(LPVOID arg) {
    int rounds = (int)(INT_PTR)arg;
    for (int i = 0; i < rounds; i++) {
        if (WaitForSingleObject(g_ev_ping, INFINITE) != WAIT_OBJECT_0) {
            return 1;
        }
        SetEvent(g_ev_pong);
    }
    return 0;
}

static void bench_context_switch(void) {
    const int rounds = 2000;
    const int reps = 5;
    double best = 1e30;

    puts("[4b] context switch round trip (event ping-pong, 2 threads)");
    printf("  %-30s %7s %12s %12s\n", "metric", "rounds", "best us", "median us");
    rule();

    for (int r = 0; r < reps; r++) {
        HANDLE th;
        double t0, t1, total;

        g_ev_ping = CreateEventW(NULL, FALSE, FALSE, NULL);   /* auto-reset */
        g_ev_pong = CreateEventW(NULL, FALSE, FALSE, NULL);
        if (g_ev_ping == NULL || g_ev_pong == NULL) {
            puts("  CreateEvent failed");
            return;
        }
        th = CreateThread(NULL, 0, pong_proc, (LPVOID)(INT_PTR)rounds, 0, NULL);
        if (th == NULL) {
            puts("  CreateThread failed");
            return;
        }

        t0 = now_us();
        for (int i = 0; i < rounds; i++) {
            SetEvent(g_ev_ping);
            WaitForSingleObject(g_ev_pong, INFINITE);
        }
        t1 = now_us();
        total = t1 - t0;

        WaitForSingleObject(th, INFINITE);
        CloseHandle(th);
        CloseHandle(g_ev_ping);
        CloseHandle(g_ev_pong);

        if (total < best) {
            best = total;
        }
    }

    printf("  %-30s %7d %12.3f %12s\n", "round trip (2 switches)", rounds, best / rounds, "-");
    printf("  %-30s %7d %12.3f %12s\n", "per switch (round trip / 2)", rounds, best / rounds / 2.0, "-");
    puts("  note: one round trip = 2 x SetEvent + 2 x Wait + 2 x thread switch;");
    puts("        dividing by 2 attributes the cost to one switch, it is not a pure");
    puts("        'switch instruction' measurement.");
}

/* ---------------------------------------------------------------- [5] */
static void bench_page_fault(void) {
    const size_t page = 4096;
    const size_t pages = 16384;                 /* 64 MiB */
    const size_t size = page * pages;
    unsigned char *a;
    unsigned char *c;
    double t0, t1;
    double first_touch, resident, commit_per_page, touch_after_commit;

    puts("[5] VirtualAlloc: commit / first touch / already touched");
    printf("  %-36s %10s %12s\n", "operation", "pages", "ns/page");
    rule();

    /* A: 一次提交 64 MiB，但一页都没碰过 —— 每页都会触发一次缺页 */
    a = (unsigned char *)VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (a == NULL) {
        puts("  VirtualAlloc(64 MiB) failed");
        return;
    }
    t0 = now_us();
    for (size_t i = 0; i < pages; i++) {
        a[i * page] = (unsigned char)(i * 31u);
    }
    t1 = now_us();
    first_touch = (t1 - t0) * 1000.0 / (double)pages;   /* ns/page */

    /* B: 同样的区域，第二次写 —— 页面已驻留，不再缺页 */
    t0 = now_us();
    for (size_t i = 0; i < pages; i++) {
        a[i * page]++;
    }
    t1 = now_us();
    resident = (t1 - t0) * 1000.0 / (double)pages;

    /* C: 只 RESERVE，然后逐页 MEM_COMMIT —— 提交本身的开销 */
    c = (unsigned char *)VirtualAlloc(NULL, size, MEM_RESERVE, PAGE_READWRITE);
    if (c == NULL) {
        VirtualFree(a, 0, MEM_RELEASE);
        puts("  VirtualAlloc(reserve) failed");
        return;
    }
    t0 = now_us();
    for (size_t i = 0; i < pages; i++) {
        if (VirtualAlloc(c + i * page, page, MEM_COMMIT, PAGE_READWRITE) == NULL) {
            break;
        }
    }
    t1 = now_us();
    commit_per_page = (t1 - t0) * 1000.0 / (double)pages;

    /* D: 逐页提交后首次触碰 —— 证实「提交」不等于「驻留」 */
    t0 = now_us();
    for (size_t i = 0; i < pages; i++) {
        c[i * page] = 1;
    }
    t1 = now_us();
    touch_after_commit = (t1 - t0) * 1000.0 / (double)pages;

    printf("  %-36s %10zu %12.1f\n", "commit all + first touch (fault)", pages, first_touch);
    printf("  %-36s %10zu %12.1f\n", "second touch (already resident)", pages, resident);
    printf("  %-36s %10zu %12.1f\n", "MEM_COMMIT per page (reserve first)", pages, commit_per_page);
    printf("  %-36s %10zu %12.1f\n", "first touch after per-page commit", pages, touch_after_commit);

    printf("  -> page faults are about %.0f%% of first-touch cost, i.e. %.0f ns / 4 KiB\n",
           first_touch > 0.0 ? (first_touch - resident) * 100.0 / first_touch : 0.0,
           first_touch - resident);

    VirtualFree(a, 0, MEM_RELEASE);
    VirtualFree(c, 0, MEM_RELEASE);
}

/* ---------------------------------------------------------------- main */
int main(void) {
    LARGE_INTEGER freq;
    SYSTEM_INFO si;

    QueryPerformanceFrequency(&freq);
    g_ticks_per_us = (double)freq.QuadPart / 1e6;
    GetSystemInfo(&si);

    puts("======================================================================");
    puts(" Lab 01 - syscall & kernel path cost");
    printf(" QPC frequency      : %.0f Hz\n", (double)freq.QuadPart);
    printf(" page size          : %lu bytes\n", (unsigned long)si.dwPageSize);
    printf(" logical processors : %lu\n", (unsigned long)si.dwNumberOfProcessors);
    printf(" resolution floor   : %.0f ns (1 QPC tick)\n", 1e3 / g_ticks_per_us);
    puts("======================================================================");
    putchar('\n');

    bench_memcpy();
    putchar('\n');
    bench_raw_syscall();
    putchar('\n');
    bench_file_io();
    bench_thread_create();
    putchar('\n');
    bench_context_switch();
    putchar('\n');
    bench_page_fault();

    putchar('\n');
    printf("checksum = %llu  (all loops feed it, so nothing was optimised away)\n", g_sink);
    return 0;
}
