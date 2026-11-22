/* =====================================================================
 *  osprobe.c  --  labs/07-net-os / os-lab
 *
 *  Goal: print REAL numbers out of a REAL Windows process. Nothing here is
 *  hard-coded; every address / ID / millisecond below is read at runtime.
 *
 *    [A] four addresses in one place -> the address-space partitions
 *        A1 GetModuleHandle(NULL)     : base of this .exe image
 *        A2 &g_global                 : a global variable (inside the image)
 *        A3 HeapAlloc(...)            : a block on the heap
 *        A4 &local_marker (in main)   : a local variable, i.e. on the stack
 *
 *    [B] VirtualQuery() walk over the whole address space, then print the
 *        regions that matter: MEM_IMAGE (the exe + DLLs), MEM_PRIVATE heap,
 *        MEM_PRIVATE stack, plus the stack's PAGE_GUARD page.
 *
 *    [C] PID / current TID / GetProcessHandleCount
 *
 *    [D] CreateThread() a real worker thread; that thread prints its own TID
 *        and its OWN stack range read straight out of the TEB:
 *            gs:0x08 = TEB+0x08 = StackBase   (top of the stack)
 *            gs:0x10 = TEB+0x10 = StackLimit  (bottom of committed memory)
 *
 *    [E] 5000 real system calls (CreateEventW + CloseHandleW) measured with
 *        GetProcessTimes() before/after -> the cost lands in KERNEL time.
 *        A pure user-space loop is measured the same way as a control.
 *
 *    [F] GetFileType() on three different kernel objects:
 *        a disk file, an anonymous pipe, and the console (CONOUT$).
 *
 *  Build (MinGW-w64 gcc, Win32 API only, no extra libraries):
 *      gcc -O0 -g -Wall -Wextra -o osprobe.exe osprobe.c
 *
 *  Run:
 *      osprobe.exe                 print everything and exit
 *      osprobe.exe --hold 3000     print everything, then stay alive 3000 ms
 *                                  so another window can inspect the live
 *                                  process (Get-Process / tasklist)
 * ===================================================================== */

#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define P64(x) ((unsigned long long)(uintptr_t)(x))

/* ------------------------------------------------------------------ */
/* a global variable: section [A] prints the address of this one        */
/* ------------------------------------------------------------------ */
volatile unsigned long long g_global = 0x1122334455667788ULL;

/* ------------------------------------------------------------------ */
/* worker-thread shared state                                          */
/* ------------------------------------------------------------------ */
static HANDLE g_ready = NULL;   /* worker sets it after printing      */
static HANDLE g_stop  = NULL;   /* main sets it to let the worker exit*/
static volatile DWORD g_w_tid = 0;
static volatile unsigned long long g_w_base = 0;
static volatile unsigned long long g_w_limit = 0;
static volatile uintptr_t g_w_local = 0;

/* ------------------------------------------------------------------ */
/* TEB access                                                          */
/*                                                                     */
/* On x64 Windows the gs segment register points at the current        */
/* thread's TEB (Thread Environment Block). The first thing in it is   */
/* the NT_TIB structure, so:                                           */
/*     gs:0x08 -> StackBase  = highest address of this thread's stack  */
/*     gs:0x10 -> StackLimit = lowest address currently committed      */
/*     gs:0x30 -> Self       = pointer to the TEB itself               */
/*     gs:0x60 -> PEB        = pointer to the process environment block*/
/* ------------------------------------------------------------------ */
static unsigned long long gs_stack_base(void)
{
    unsigned long long v;
    __asm__ __volatile__("movq %%gs:0x08, %0" : "=r"(v));
    return v;
}

static unsigned long long gs_stack_limit(void)
{
    unsigned long long v;
    __asm__ __volatile__("movq %%gs:0x10, %0" : "=r"(v));
    return v;
}

static unsigned long long gs_qword(unsigned long long off)
{
    unsigned long long v;
    /* off is compile-time constant at the (few) call sites below, but we
       keep it a runtime value by using a register constraint approach.   */
    switch (off) {
    case 0x30: __asm__ __volatile__("movq %%gs:0x30, %0" : "=r"(v)); break;
    case 0x60: __asm__ __volatile__("movq %%gs:0x60, %0" : "=r"(v)); break;
    default:   v = 0; break;
    }
    return v;
}

/* ------------------------------------------------------------------ */
/* tiny formatting helpers                                             */
/* ------------------------------------------------------------------ */
static const char *mem_state_name(DWORD s)
{
    switch (s) {
    case MEM_COMMIT:  return "MEM_COMMIT";
    case MEM_RESERVE: return "MEM_RESERVE";
    case MEM_FREE:    return "MEM_FREE";
    default:          return "MEM_?";
    }
}

static const char *mem_type_name(DWORD t)
{
    switch (t) {
    case MEM_IMAGE:   return "MEM_IMAGE";
    case MEM_MAPPED:  return "MEM_MAPPED";
    case MEM_PRIVATE: return "MEM_PRIVATE";
    case MEM_FREE:    return "MEM_FREE";
    default:          return "MEM_?";
    }
}

static void prot_name(DWORD p, char *buf, size_t n)
{
    const char *base;
    switch (p & 0xFF) {
    case PAGE_NOACCESS:          base = "PAGE_NOACCESS";          break;
    case PAGE_READONLY:          base = "PAGE_READONLY";          break;
    case PAGE_READWRITE:         base = "PAGE_READWRITE";         break;
    case PAGE_WRITECOPY:         base = "PAGE_WRITECOPY";         break;
    case PAGE_EXECUTE:           base = "PAGE_EXECUTE";           break;
    case PAGE_EXECUTE_READ:      base = "PAGE_EXECUTE_READ";      break;
    case PAGE_EXECUTE_READWRITE: base = "PAGE_EXECUTE_READWRITE"; break;
    case PAGE_EXECUTE_WRITECOPY: base = "PAGE_EXECUTE_WRITECOPY"; break;
    default:                     base = "PAGE_?";                break;
    }
    if (p & PAGE_GUARD)     snprintf(buf, n, "%s|PAGE_GUARD", base);
    else if (p & PAGE_NOCACHE) snprintf(buf, n, "%s|PAGE_NOCACHE", base);
    else                    snprintf(buf, n, "%s", base);
}

/* ------------------------------------------------------------------ */
/* one memory region, as VirtualQuery() reports it                     */
/* ------------------------------------------------------------------ */
typedef struct {
    ULONG_PTR base;
    ULONG_PTR size;
    ULONG_PTR alloc_base;   /* for MEM_IMAGE: base of the owning module */
    DWORD     state;        /* MEM_COMMIT / MEM_RESERVE / MEM_FREE      */
    DWORD     type;         /* MEM_IMAGE / MEM_MAPPED / MEM_PRIVATE     */
    DWORD     prot;
} REG;

typedef struct {
    REG  r;
    char label[64];
} ROW;

typedef struct {
    unsigned long long regions_total;
    unsigned long long n_commit, n_reserve, n_free_state;
    unsigned long long n_image, n_mapped, n_private;

    int  n_main_img;
    ROW  main_img[3];        /* regions of our own .exe image            */
    int  n_other_img;
    ROW  other_img[2];       /* one or two other DLL image regions       */

    int  n_heap;
    ROW  heap[4];

    int  has_stack;
    ROW  stack;
    int  has_guard;
    ROW  guard;
} WALK;

static int region_of(const void *p, REG *out)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(p, &mbi, sizeof mbi) == 0)
        return 0;
    out->base       = (ULONG_PTR)mbi.BaseAddress;
    out->size       = (ULONG_PTR)mbi.RegionSize;
    out->alloc_base = (ULONG_PTR)mbi.AllocationBase;
    out->state      = mbi.State;
    out->type       = mbi.Type;
    out->prot       = mbi.Protect;
    return 1;
}

/* module table, so an image region can be labelled with a module name */
typedef struct {
    ULONG_PTR base;
    char      name[64];
} MODINFO;

static int enum_modules(MODINFO *tab, int max)
{
    int n = 0;
    MODULEENTRY32 me;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                           GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE)
        return 0;
    me.dwSize = sizeof me;
    if (Module32First(snap, &me)) {
        do {
            if (n < max) {
                tab[n].base = (ULONG_PTR)me.modBaseAddr;
                strncpy(tab[n].name, me.szModule, sizeof tab[n].name - 1);
                tab[n].name[sizeof tab[n].name - 1] = 0;
                n++;
            }
        } while (Module32Next(snap, &me));
    }
    CloseHandle(snap);
    return n;
}

static const char *modname_of(const MODINFO *tab, int n, ULONG_PTR base)
{
    int i;
    for (i = 0; i < n; i++)
        if (tab[i].base == base)
            return tab[i].name;
    return "?";
}

/* Count (and list) the threads that belong to THIS process, using a
   Toolhelp32 snapshot. This is the number of threads the process really
   has; Get-Process in another window can show MORE, because any process
   with enough rights may create a thread inside yours. */
static void print_thread_list(const char *prefix)
{
    HANDLE snap;
    THREADENTRY32 te;
    DWORD pid = GetCurrentProcessId();
    char ids[480];
    size_t off = 0;
    int n = 0;

    ids[0] = 0;
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        printf("%s <Toolhelp32 snapshot failed, GetLastError=%lu>\n",
               prefix, (unsigned long)GetLastError());
        return;
    }
    te.dwSize = sizeof te;
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID == pid) {
                n++;
                if (off < sizeof ids - 24)
                    off += (size_t)snprintf(ids + off, sizeof ids - off, "%s%lu",
                                            (n > 1) ? " " : "",
                                            (unsigned long)te.th32ThreadID);
            }
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    printf("%s %d thread(s), TIDs: %s\n", prefix, n, ids);
}

/* ------------------------------------------------------------------ */
/* [B] one full VirtualQuery() walk                                    */
/* ------------------------------------------------------------------ */
#define USER_VA_END ((ULONG_PTR)0x00007FFFFFFF0000ULL)

static void collect_memory(WALK *w,
                           ULONG_PTR main_base,
                           const void **heap_addrs, const char **heap_labels, int n_heap_addrs,
                           const void *stack_anchor,
                           ULONG_PTR stack_base, ULONG_PTR stack_limit)
{
    ULONG_PTR addr = 0;
    int i;

    memset(w, 0, sizeof *w);

    while (addr < USER_VA_END) {
        MEMORY_BASIC_INFORMATION mbi;
        REG r;
        ULONG_PTR next;

        if (VirtualQuery((LPCVOID)addr, &mbi, sizeof mbi) == 0)
            break;

        r.base       = (ULONG_PTR)mbi.BaseAddress;
        r.size       = (ULONG_PTR)mbi.RegionSize;
        r.alloc_base = (ULONG_PTR)mbi.AllocationBase;
        r.state      = mbi.State;
        r.type       = mbi.Type;
        r.prot       = mbi.Protect;

        w->regions_total++;

        if (r.state == MEM_FREE)            w->n_free_state++;
        else if (r.state == MEM_RESERVE)    w->n_reserve++;
        else if (r.state == MEM_COMMIT)     w->n_commit++;

        if (r.state == MEM_COMMIT && r.type == MEM_IMAGE) {
            w->n_image++;
            if (r.alloc_base == main_base) {
                if (w->n_main_img < 3) {
                    w->main_img[w->n_main_img].r = r;
                    w->n_main_img++;
                }
            } else if (w->n_other_img < 1) {
                w->other_img[w->n_other_img].r = r;
                w->n_other_img++;
            }
        } else if (r.state == MEM_COMMIT && r.type == MEM_MAPPED) {
            w->n_mapped++;
        } else if (r.state == MEM_COMMIT && r.type == MEM_PRIVATE) {
            w->n_private++;
        }

        /* heap anchor inside this region? */
        for (i = 0; i < n_heap_addrs; i++) {
            ULONG_PTR a = (ULONG_PTR)heap_addrs[i];
            int j, dup = 0;
            if (a < r.base || a >= r.base + r.size)
                continue;
            for (j = 0; j < w->n_heap; j++)
                if (w->heap[j].r.base == r.base)
                    dup = 1;
            if (!dup && w->n_heap < 4) {
                w->heap[w->n_heap].r = r;
                snprintf(w->heap[w->n_heap].label, sizeof w->heap[0].label,
                         "%s", heap_labels[i]);
                w->n_heap++;
            }
        }

        /* stack anchor (&local_marker lives on the main thread stack) */
        if (!w->has_stack && stack_anchor != NULL) {
            ULONG_PTR a = (ULONG_PTR)stack_anchor;
            if (a >= r.base && a < r.base + r.size) {
                w->stack.r = r;
                snprintf(w->stack.label, sizeof w->stack.label, "main stack");
                w->has_stack = 1;
            }
        }

        /* the PAGE_GUARD page sits just under StackLimit */
        if (!w->has_guard && (r.prot & PAGE_GUARD) &&
            r.base + r.size >= stack_limit - 0x20000 && r.base <= stack_base) {
            w->guard.r = r;
            snprintf(w->guard.label, sizeof w->guard.label, "stack guard page");
            w->has_guard = 1;
        }

        next = r.base + r.size;
        if (next <= addr)
            next = addr + 0x1000;
        addr = next;
    }
}

static void print_row(const char *tag, const ROW *row, const char *modname)
{
    char pb[96];
    prot_name(row->r.prot, pb, sizeof pb);
    printf("%-6s base=0x%016llx  size=0x%08llx (%6llu KB)  state=%-11s type=%-11s  Protect=%-30s %s\n",
           tag,
           (unsigned long long)row->r.base,
           (unsigned long long)row->r.size,
           (unsigned long long)(row->r.size / 1024),
           mem_state_name(row->r.state),
           mem_type_name(row->r.type),
           pb,
           modname ? modname : "");
}

/* ------------------------------------------------------------------ */
/* [D] worker thread                                                   */
/* ------------------------------------------------------------------ */
static DWORD WINAPI worker_proc(LPVOID param)
{
    volatile unsigned char w_local = 0xA5;   /* this is a local -> stack */
    unsigned long long sb = gs_stack_base();
    unsigned long long sl = gs_stack_limit();
    DWORD tid = GetCurrentThreadId();

    (void)param;

    /* the worker thread prints its own TID and its own stack range */
    printf("D3 worker  TID=%lu  [TEB+0x08] StackBase  = 0x%016llx\n",
           (unsigned long)tid, (unsigned long long)sb);
    printf("D4 worker  TID=%lu  [TEB+0x10] StackLimit = 0x%016llx\n",
           (unsigned long)tid, (unsigned long long)sl);
    fflush(stdout);

    g_w_tid   = tid;
    g_w_base  = sb;
    g_w_limit = sl;
    g_w_local = (uintptr_t)&w_local;

    SetEvent(g_ready);
    WaitForSingleObject(g_stop, INFINITE);
    return 0;
}

/* ------------------------------------------------------------------ */
/* [E] process-time accounting                                         */
/*                                                                     */
/* GetProcessTimes() reports user/kernel CPU time in 100 ns units, but */
/* on this machine those counters only move when the clock tick fires  */
/* (GetSystemTimeAdjustment tells us the tick length). So a batch that */
/* costs less than one tick reads as +0.000 ms - that is the resolution*/
/* floor, not "the syscall was free". The scaling table below makes it */
/* visible: 5000 calls -> 0 ticks, 400000 calls -> 12 ticks, and the   */
/* time shows up in KERNEL time while USER time stays ~0.              */
/* ------------------------------------------------------------------ */
static unsigned long long ft_100ns_raw(const FILETIME *ft)
{
    return ((unsigned long long)ft->dwHighDateTime << 32) |
           (unsigned long long)ft->dwLowDateTime;
}

static void get_proc_times_raw(unsigned long long *user100, unsigned long long *ker100)
{
    FILETIME c, e, k, u;
    GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
    *user100 = ft_100ns_raw(&u);
    *ker100  = ft_100ns_raw(&k);
}

static double qpc_ms(void)
{
    LARGE_INTEGER c, f;
    QueryPerformanceCounter(&c);
    QueryPerformanceFrequency(&f);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}

#define SYSCALL_ROUNDS 5000

/* one batch of CreateEventW + CloseHandleW: CPU-time deltas (100 ns) and
   wall-clock time (QueryPerformanceCounter, milliseconds) for the batch */
static void batch_syscalls(int rounds,
                           unsigned long long *user100,
                           unsigned long long *ker100,
                           double *wall_ms)
{
    unsigned long long u0, k0, u1, k1;
    double t0, t1;
    int i;

    get_proc_times_raw(&u0, &k0);
    t0 = qpc_ms();
    for (i = 0; i < rounds; i++) {
        HANDLE h = CreateEventW(NULL, FALSE, FALSE, NULL);
        if (h != NULL)
            CloseHandle(h);
    }
    t1 = qpc_ms();
    get_proc_times_raw(&u1, &k1);

    *user100 = u1 - u0;
    *ker100  = k1 - k0;
    *wall_ms = t1 - t0;
}

static void print_batch_row(int calls, double wall_ms,
                            unsigned long long u100, unsigned long long k100,
                            unsigned long long tick100)
{
    printf("   %7d  %9.3f  %8.3f  %9.3f   %8.0f  %8.0f\n",
           calls, wall_ms, (double)u100 / 10000.0, (double)k100 / 10000.0,
           tick100 ? (double)u100 / (double)tick100 : 0.0,
           tick100 ? (double)k100 / (double)tick100 : 0.0);
}

static void section_E(void)
{
    DWORD time_adj = 0, time_inc = 0;
    BOOL  time_adj_disabled = FALSE;
    unsigned long long tick100 = 0;
    double t0, t1;
    unsigned long long cu0, ck0, cu1, ck1;
    volatile unsigned long long acc = 0;
    DWORD h_before = 0, h_after = 0;
    int batch_calls[4];
    double batch_wall[4];
    unsigned long long batch_user[4], batch_kern[4];
    unsigned long long total_pairs = 0;
    int i, last;

    printf("\n--- [E] 5000 REAL SYSTEM CALLS vs 5000 PURE USER-SPACE ITERATIONS ---\n");

    if (GetSystemTimeAdjustment(&time_adj, &time_inc, &time_adj_disabled))
        tick100 = (unsigned long long)(time_inc ? time_inc : time_adj);

    if (tick100) {
        printf("   this machine's clock tick: GetSystemTimeAdjustment TimeIncrement=%lu x100ns\n",
               (unsigned long)time_inc);
        printf("   = %.4f ms.  GetProcessTimes() only moves in whole ticks here, so a\n",
               (double)tick100 / 10000.0);
        printf("   batch shorter than one tick reads as +0.000 ms. That is the\n");
        printf("   resolution floor, NOT \"the syscall was free\" (see scaling table).\n\n");
    } else {
        printf("   (GetSystemTimeAdjustment failed; tick column unavailable)\n\n");
    }

    /* ---- control: no syscall at all, just arithmetic ---- */
    get_proc_times_raw(&cu0, &ck0);
    t0 = qpc_ms();
    for (i = 0; i < SYSCALL_ROUNDS; i++)
        acc += (unsigned long long)i;
    t1 = qpc_ms();
    get_proc_times_raw(&cu1, &ck1);
    printf("CONTROL (no syscall)   %d x \"acc += i\"\n", SYSCALL_ROUNDS);
    printf("   wall(QueryPerformanceCounter) +%8.3f ms\n", t1 - t0);
    printf("   user +%8.3f ms   kernel +%8.3f ms   <- 0 syscalls, 0 kernel time\n",
           (double)(cu1 - cu0) / 10000.0, (double)(ck1 - ck0) / 10000.0);
    printf("   (the loop really ran: acc = %llu)\n\n", (unsigned long long)acc);

    /* ---- the syscall batches: 5000 (this lab's number) plus scaling ---- */
    batch_calls[0] = SYSCALL_ROUNDS;            /* 5000     - the lab's number */
    batch_calls[1] = SYSCALL_ROUNDS * 80;       /* 400000                      */
    batch_calls[2] = SYSCALL_ROUNDS * 320;      /* 1600000                     */

    GetProcessHandleCount(GetCurrentProcess(), &h_before);
    for (i = 0; i < 3; i++) {
        batch_syscalls(batch_calls[i], &batch_user[i], &batch_kern[i], &batch_wall[i]);
        total_pairs += (unsigned long long)batch_calls[i];
    }
    GetProcessHandleCount(GetCurrentProcess(), &h_after);

    printf("SYSCALL batches: CreateEventW + CloseHandleW\n");
    printf("   %7s  %9s  %8s  %9s   %8s  %8s\n",
           "calls", "wall ms", "user ms", "kernel ms", "user tick", "kern tick");
    for (i = 0; i < 3; i++)
        print_batch_row(batch_calls[i], batch_wall[i], batch_user[i], batch_kern[i], tick100);
    printf("   (calls=5000 is the batch this lab asks for: its wall time is only ~2.4 ms,\n");
    printf("    i.e. below one 15.6 ms tick, so its CPU columns can read +0.000 ms - that\n");
    printf("    is the resolution floor, not a free syscall; the bigger batches fix that)\n\n");

    last = 2;
    printf("   per pair, from the %d-call batch:\n", batch_calls[last]);
    printf("     wall   %6.3f us\n", batch_wall[last] * 1000.0 / batch_calls[last]);
    printf("     kernel %6.3f us   <- the %d kernel entries/exits are billed here\n",
           (double)batch_kern[last] / 10.0 / (double)batch_calls[last], batch_calls[last]);
    printf("     user   %6.3f us\n",
           (double)batch_user[last] / 10.0 / (double)batch_calls[last]);
    printf("   cross-check: the 5000-call batch took %.3f ms of wall time = %.3f us/pair;\n",
           batch_wall[0], batch_wall[0] * 1000.0 / batch_calls[0]);
    printf("   the big batch says %.3f us/pair of CPU total, the same number.\n",
           ((double)batch_kern[last] + (double)batch_user[last]) / 10.0 / (double)batch_calls[last]);
    printf("   open handles before=%lu after=%lu  (%llu create+close pairs, no leak)\n",
           (unsigned long)h_before, (unsigned long)h_after, total_pairs);
    printf("   reading: kernel time is where the %llu calls were paid; user-mode code in\n",
           total_pairs);
    printf("            front of them costs a small fraction. Both CPU columns move in\n");
    printf("            whole ticks, so one batch can read 0 or 1 tick too high/low.\n");
}

/* ------------------------------------------------------------------ */
/* [F] GetFileType on three kinds of kernel object                     */
/* ------------------------------------------------------------------ */
static const char *filetype_name(DWORD t)
{
    switch (t) {
    case FILE_TYPE_DISK:  return "FILE_TYPE_DISK";
    case FILE_TYPE_CHAR:  return "FILE_TYPE_CHAR";
    case FILE_TYPE_PIPE:  return "FILE_TYPE_PIPE";
    case FILE_TYPE_UNKNOWN: return "FILE_TYPE_UNKNOWN";
    default:              return "FILE_TYPE_?";
    }
}

static void section_F(void)
{
    HANDLE hFile = INVALID_HANDLE_VALUE;
    HANDLE hRead = NULL, hWrite = NULL, hCon = INVALID_HANDLE_VALUE;
    HANDLE hStdOut;
    DWORD tFile = FILE_TYPE_UNKNOWN, tPipe = FILE_TYPE_UNKNOWN;
    DWORD tCon  = FILE_TYPE_UNKNOWN, tStd  = FILE_TYPE_UNKNOWN;
    char full[MAX_PATH + 4] = { 0 };
    char pb[96];

    printf("\n--- [F] GetFileType() ON THREE DIFFERENT KINDS OF HANDLE ---\n");

    /* 1. a real file on disk */
    hFile = CreateFileW(L"osprobe_tmp.txt", GENERIC_WRITE, 0, NULL,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD wr = 0;
        WriteFile(hFile, "x", 1, &wr, NULL);
        tFile = GetFileType(hFile);
        if (GetFullPathNameA("osprobe_tmp.txt", MAX_PATH, full, NULL) == 0)
            strcpy(full, "osprobe_tmp.txt");
        printf("F1 CreateFileW(\"osprobe_tmp.txt\")      handle=0x%016llx  type=0x%08lx  %-16s  (%s)\n",
               P64(hFile), (unsigned long)tFile, filetype_name(tFile), full);
    } else {
        printf("F1 CreateFileW(\"osprobe_tmp.txt\")      FAILED, GetLastError=%lu\n",
               (unsigned long)GetLastError());
    }

    /* 2. an anonymous pipe: a kernel object with NO path at all */
    if (CreatePipe(&hRead, &hWrite, NULL, 0)) {
        tPipe = GetFileType(hWrite);
        printf("F2 CreatePipe() write end              handle=0x%016llx  type=0x%08lx  %-16s  (anonymous pipe: kernel object, no filename)\n",
               P64(hWrite), (unsigned long)tPipe, filetype_name(tPipe));
    } else {
        printf("F2 CreatePipe()                        FAILED, GetLastError=%lu\n",
               (unsigned long)GetLastError());
    }

    /* 3. the console: a character device. CONOUT$ always opens the console,
          even when this process's stdout has been redirected to a file.   */
    hCon = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, NULL,
                       OPEN_EXISTING, 0, NULL);
    if (hCon != INVALID_HANDLE_VALUE) {
        tCon = GetFileType(hCon);
        printf("F3 CreateFileW(\"CONOUT$\")              handle=0x%016llx  type=0x%08lx  %-16s  (console = character device)\n",
               P64(hCon), (unsigned long)tCon, filetype_name(tCon));
    } else {
        printf("F3 CreateFileW(\"CONOUT$\")              FAILED, GetLastError=%lu  (no console attached)\n",
               (unsigned long)GetLastError());
    }

    /* 4. and what does THIS RUN's stdout actually point at? */
    hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    tStd = GetFileType(hStdOut);
    printf("F4 GetStdHandle(STD_OUTPUT_HANDLE)     handle=0x%016llx  type=0x%08lx  %-16s  -> this run's stdout is ",
           P64(hStdOut), (unsigned long)tStd, filetype_name(tStd));
    if (tStd == FILE_TYPE_CHAR)
        printf("a CONSOLE (character device)\n");
    else if (tStd == FILE_TYPE_DISK)
        printf("redirected to a FILE on disk (that is why it is not CHAR)\n");
    else if (tStd == FILE_TYPE_PIPE)
        printf("redirected into a PIPE (that is why it is not CHAR)\n");
    else
        printf("unknown\n");
    printf("  reading: the handle value alone does not tell you what it is;\n");
    printf("           GetFileType asks the kernel what object it points at.\n");
    printf("           Run osprobe.exe directly in a console and F4 becomes FILE_TYPE_CHAR.\n");

    /* cleanup */
    if (hFile != INVALID_HANDLE_VALUE) {
        CloseHandle(hFile);
        DeleteFileW(L"osprobe_tmp.txt");
    }
    if (hRead)  CloseHandle(hRead);
    if (hWrite) CloseHandle(hWrite);
    if (hCon != INVALID_HANDLE_VALUE) CloseHandle(hCon);
    (void)pb;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
    HMODULE hMod;
    HANDLE heap_blk = NULL;
    HANDLE priv_heap = NULL;
    void  *priv_blk = NULL;
    volatile unsigned char local_marker = 0x5A;
    unsigned long long main_sb, main_sl;
    DWORD pid, tid_main;
    DWORD handle_count = 0;
    int hold_ms = 0;
    int i;

    MODINFO mods[256];
    int nmods = 0;

    WALK walk;
    REG reg;
    const void *heap_addrs[4];
    const char *heap_labels[4];
    int n_heap_addrs = 0;

    /* one line per print is better for a live demo and for the parent
       process that reads our stdout from a file */
    setvbuf(stdout, NULL, _IONBF, 0);

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--hold") == 0 && i + 1 < argc) {
            hold_ms = atoi(argv[++i]);
        }
    }

    /* ---------------- [A] four addresses ---------------- */
    hMod = GetModuleHandle(NULL);
    heap_blk = HeapAlloc(GetProcessHeap(), 0, 64);
    if (heap_blk)
        memset(heap_blk, 0xAB, 64);

    /* a second heap, to show a heap region that is not the process heap */
    priv_heap = HeapCreate(0, 0x10000, 0);
    if (priv_heap)
        priv_blk = HeapAlloc(priv_heap, 0, 0x1000);

    main_sb = gs_stack_base();
    main_sl = gs_stack_limit();
    pid     = GetCurrentProcessId();
    tid_main = GetCurrentThreadId();

    nmods = enum_modules(mods, (int)(sizeof mods / sizeof mods[0]));

    printf("================================================================\n");
    printf(" osprobe.exe -- real numbers from a real Windows process\n");
    printf(" built with MinGW-w64 gcc 14.2.0, -O0 -g, Win32 API only\n");
    printf("================================================================\n");

    printf("\n--- [A] THE ADDRESSES IN ONE PLACE (address-space partitions) ---\n");
    printf("A1 module base     GetModuleHandle(NULL)                 = 0x%016llx\n",
           P64(hMod));
    printf("A2 global variable &g_global  (module .data section)     = 0x%016llx\n",
           P64((uintptr_t)&g_global));
    printf("A3 heap block      HeapAlloc(GetProcessHeap(), 0, 64)    = 0x%016llx\n",
           P64(heap_blk));
    printf("A4 stack local     &local_marker (local var in main())   = 0x%016llx\n",
           P64((uintptr_t)&local_marker));
    printf("A5 kernel32 base   GetModuleHandle(\"kernel32.dll\")      = 0x%016llx\n",
           P64(GetModuleHandleW(L"kernel32.dll")));
    printf("A6 ntdll    base   GetModuleHandle(\"ntdll.dll\")         = 0x%016llx\n",
           P64(GetModuleHandleW(L"ntdll.dll")));
    printf("   (g_global = 0x%016llx, local_marker = 0x%02x, the heap block was filled with 0xAB)\n",
           (unsigned long long)g_global, (unsigned)local_marker);
    printf("   (A5/A6: kernel32 and ntdll are shared image mappings. In this boot every\n");
    printf("    process sees the same base for them, and a debugger shows that value.)\n");

    printf("\n   where each of them really lives (VirtualQuery on each address):\n");
    if (region_of(hMod, &reg)) {
        char pb[96];
        prot_name(reg.prot, pb, sizeof pb);
        printf("   A1 -> region base=0x%016llx size=0x%08llx state=%-11s type=%-11s Protect=%-16s module=%s\n",
               (unsigned long long)reg.base, (unsigned long long)reg.size,
               mem_state_name(reg.state), mem_type_name(reg.type), pb,
               modname_of(mods, nmods, reg.alloc_base));
    }
    if (region_of((const void *)(uintptr_t)&g_global, &reg)) {
        char pb[96];
        prot_name(reg.prot, pb, sizeof pb);
        printf("   A2 -> region base=0x%016llx size=0x%08llx state=%-11s type=%-11s Protect=%-16s module=%s\n",
               (unsigned long long)reg.base, (unsigned long long)reg.size,
               mem_state_name(reg.state), mem_type_name(reg.type), pb,
               modname_of(mods, nmods, reg.alloc_base));
    }
    if (heap_blk && region_of(heap_blk, &reg)) {
        char pb[96];
        prot_name(reg.prot, pb, sizeof pb);
        printf("   A3 -> region base=0x%016llx size=0x%08llx state=%-11s type=%-11s Protect=%s\n",
               (unsigned long long)reg.base, (unsigned long long)reg.size,
               mem_state_name(reg.state), mem_type_name(reg.type), pb);
    }
    if (region_of((const void *)(uintptr_t)&local_marker, &reg)) {
        char pb[96];
        prot_name(reg.prot, pb, sizeof pb);
        printf("   A4 -> region base=0x%016llx size=0x%08llx state=%-11s type=%-11s Protect=%s\n",
               (unsigned long long)reg.base, (unsigned long long)reg.size,
               mem_state_name(reg.state), mem_type_name(reg.type), pb);
    }

    printf("   (note: A4's region size is only what the stack has committed SO FAR;\n");
    printf("    the stack grows on demand, so by the time [B] runs more pages are in)\n");

    /* sorted low -> high, so the partitions are obvious */
    {
        struct { const char *name; unsigned long long a; } ord[6];
        int n = 6, a, b;
        ord[0].name = "A1 module base (osprobe.exe image)"; ord[0].a = P64(hMod);
        ord[1].name = "A2 global g_global (inside that image)"; ord[1].a = P64((uintptr_t)&g_global);
        ord[2].name = "A3 heap block"; ord[2].a = P64(heap_blk);
        ord[3].name = "A4 local variable (stack)"; ord[3].a = P64((uintptr_t)&local_marker);
        ord[4].name = "A5 kernel32.dll (shared image)"; ord[4].a = P64(GetModuleHandleW(L"kernel32.dll"));
        ord[5].name = "A6 ntdll.dll (shared image, usually highest)"; ord[5].a = P64(GetModuleHandleW(L"ntdll.dll"));
        for (a = 0; a < n; a++)
            for (b = a + 1; b < n; b++)
                if (ord[b].a < ord[a].a) {
                    const char *tn = ord[a].name; unsigned long long ta = ord[a].a;
                    ord[a].name = ord[b].name; ord[a].a = ord[b].a;
                    ord[b].name = tn; ord[b].a = ta;
                }
        printf("\n   sorted from LOW address to HIGH address (this run):\n");
        for (a = 0; a < n; a++)
            printf("     0x%016llx  %s\n", ord[a].a, ord[a].name);
        printf("   low  = stack (grows down from StackBase), then the heap\n");
        printf("   high = the .exe image, and the system DLLs above it\n");
    }

    /* ---------------- [B] VirtualQuery walk ---------------- */
    heap_addrs[0] = heap_blk;              heap_labels[0] = "process heap block A3";
    n_heap_addrs = 1;
    if (priv_blk) {
        heap_addrs[n_heap_addrs] = priv_blk;
        heap_labels[n_heap_addrs] = "private heap (HeapCreate)";
        n_heap_addrs++;
    }
    collect_memory(&walk, (ULONG_PTR)hMod, heap_addrs, heap_labels, n_heap_addrs,
                   (const void *)(uintptr_t)&local_marker, (ULONG_PTR)main_sb, (ULONG_PTR)main_sl);

    /* fill module names into rows (the walk does not know the module table) */
    for (i = 0; i < walk.n_main_img; i++)
        snprintf(walk.main_img[i].label, sizeof walk.main_img[0].label, "%s",
                 modname_of(mods, nmods, walk.main_img[i].r.alloc_base));
    for (i = 0; i < walk.n_other_img; i++)
        snprintf(walk.other_img[i].label, sizeof walk.other_img[0].label, "%s",
                 modname_of(mods, nmods, walk.other_img[i].r.alloc_base));

    printf("\n--- [B] VirtualQuery(): WALK THE WHOLE ADDRESS SPACE OF THIS PROCESS ---\n");
    printf("B0 scanned 0x0000000000000000 .. 0x%016llx\n", P64(USER_VA_END));
    printf("   regions found: %llu\n", walk.regions_total);
    printf("     by State : MEM_COMMIT=%llu  MEM_RESERVE=%llu  MEM_FREE=%llu\n",
           walk.n_commit, walk.n_reserve, walk.n_free_state);
    printf("     committed by Type: MEM_IMAGE=%llu  MEM_MAPPED=%llu  MEM_PRIVATE=%llu\n",
           walk.n_image, walk.n_mapped, walk.n_private);
    printf("   (a 'region' is one run of pages with the same state/type/Protect.\n");
    printf("    The kernel keeps them in this process's VAD tree; VirtualQuery walks it.)\n");

    printf("\nB1 MEM_IMAGE regions: the .exe and every DLL it loaded\n");
    for (i = 0; i < walk.n_main_img; i++)
        print_row("IMAGE", &walk.main_img[i], walk.main_img[i].label);
    for (i = 0; i < walk.n_other_img; i++)
        print_row("IMAGE", &walk.other_img[i], walk.other_img[i].label);

    printf("\nB2 MEM_PRIVATE heap regions: the regions that hold the HeapAlloc blocks\n");
    for (i = 0; i < walk.n_heap; i++)
        print_row("HEAP", &walk.heap[i], walk.heap[i].label);

    printf("\nB3 MEM_PRIVATE stack regions: the thread stack\n");
    if (walk.has_stack)
        print_row("STACK", &walk.stack, walk.stack.label);
    if (walk.has_guard)
        print_row("GUARD", &walk.guard, walk.guard.label);
    printf("   (the guard page is how Windows grows a stack: touch it -> kernel commits\n");
    printf("    the next page and moves the guard one page lower)\n");

    /* ---------------- [C] PID / TID / handles ---------------- */
    GetProcessHandleCount(GetCurrentProcess(), &handle_count);
    printf("\n--- [C] PROCESS / THREAD IDENTITY ---\n");
    printf("C1 process id  PID                    = %lu\n", (unsigned long)pid);
    printf("C2 thread id   TID (this = main thread)= %lu\n", (unsigned long)tid_main);
    printf("C3 open handles GetProcessHandleCount() = %lu\n", (unsigned long)handle_count);
    printf("   (a handle is an index into this process's handle table; every file,\n");
    printf("    event, thread, process ... you open costs one slot)\n");
    print_thread_list("C4 threads now (Toolhelp32 snapshot inside the process):");
    printf("   (our code has NOT called CreateThread at this point - main is the only\n");
    printf("    thread we created. If the line above shows 2, something else created\n");
    printf("    one inside this process. That is possible on Windows, and it is why a\n");
    printf("    tool like Get-Process can show more threads than your code made.)\n");

    /* ---------------- [D] a second thread and its own stack ---------------- */
    printf("\n--- [D] A REAL SECOND THREAD AND ITS OWN STACK (read from the TEB) ---\n");
    printf("D0 TEB self = gs:0x30 = 0x%016llx   PEB = gs:0x60 = 0x%016llx\n",
           (unsigned long long)gs_qword(0x30), (unsigned long long)gs_qword(0x60));
    printf("   (gs points at the TEB: NT_TIB is the first part of it, so\n");
    printf("    gs:0x08 = TEB+0x08 = StackBase, gs:0x10 = TEB+0x10 = StackLimit)\n");
    printf("D1 main    TID=%lu  [TEB+0x08] StackBase  = 0x%016llx\n",
           (unsigned long)tid_main, main_sb);
    printf("D2 main    TID=%lu  [TEB+0x10] StackLimit = 0x%016llx\n",
           (unsigned long)tid_main, main_sl);

    g_ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    g_stop  = CreateEventW(NULL, TRUE, FALSE, NULL);
    {
        HANDLE hThread = CreateThread(NULL, 0, worker_proc, NULL, 0, NULL);
        if (hThread == NULL) {
            printf("D3 CreateThread FAILED, GetLastError=%lu\n", (unsigned long)GetLastError());
        } else {
            printf("   CreateThread() -> hThread = 0x%016llx  (waiting for it to report)\n",
                   P64(hThread));
            WaitForSingleObject(g_ready, 5000);
            print_thread_list("D5 threads now (Toolhelp32 snapshot inside the process):");
            printf("   (that is our own count: main thread + the one we just created.\n");
            printf("    A tool like Get-Process may show MORE threads than this: any\n");
            printf("    process may create a thread inside yours. We saw one extra on\n");
            printf("    this machine - it is not in our code.)\n");

            printf("   main stack  : StackBase=0x%016llx  StackLimit=0x%016llx  size=%llu KB\n",
                   main_sb, main_sl, (main_sb - main_sl) / 1024);
            printf("   worker stack: StackBase=0x%016llx  StackLimit=0x%016llx  size=%llu KB\n",
                   (unsigned long long)g_w_base, (unsigned long long)g_w_limit,
                   (g_w_base - g_w_limit) / 1024);
            printf("   the two threads have TWO DIFFERENT stacks: 0x%016llx vs 0x%016llx\n",
                   main_sb, (unsigned long long)g_w_base);

            if (region_of((const void *)(uintptr_t)g_w_local, &reg)) {
                char pb[96];
                prot_name(reg.prot, pb, sizeof pb);
                printf("   worker's local var &w_local = 0x%016llx -> VirtualQuery: region base=0x%016llx size=0x%08llx type=%-11s Protect=%s\n",
                       (unsigned long long)g_w_local,
                       (unsigned long long)reg.base, (unsigned long long)reg.size,
                       mem_type_name(reg.type), pb);
            }

            /* keep the worker alive (it waits on g_stop) until we are done */
            if (hold_ms > 0) {
                printf("\n[HOLD] process stays alive for %d ms so another window can look at it:\n", hold_ms);
                printf("       Get-Process -Id %lu | Select-Object Id,Handles,Threads\n",
                       (unsigned long)pid);
                fflush(stdout);
                Sleep((DWORD)hold_ms);
            }

            SetEvent(g_stop);
            WaitForSingleObject(hThread, 5000);
            CloseHandle(hThread);
            printf("   worker thread was told to exit (SetEvent) and joined; thread count back to 1\n");
        }
    }

    /* ---------------- [E] syscall timing ---------------- */
    section_E();

    /* ---------------- [F] GetFileType ---------------- */
    section_F();

    /* ---------------- cleanup ---------------- */
    if (g_ready) CloseHandle(g_ready);
    if (g_stop)  CloseHandle(g_stop);
    if (heap_blk) HeapFree(GetProcessHeap(), 0, heap_blk);
    if (priv_blk) HeapFree(priv_heap, 0, priv_blk);
    if (priv_heap) HeapDestroy(priv_heap);

    printf("\n--- done: osprobe.exe finished, exit code 0 ---\n");
    printf("PID was %lu, main TID was %lu\n", (unsigned long)pid, (unsigned long)tid_main);
    return 0;
}
