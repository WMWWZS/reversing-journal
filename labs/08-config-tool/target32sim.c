/* target32sim.c -- 在 64 位进程里模拟"32 位游戏的布局"
 *
 * 为什么要它：AssaultCube 是 32 位程序，指针是 4 字节。我们没有 32 位编译器，
 * 但可以造一个等价结构来验证工具：
 *
 *   模块里放一个 uint32_t 的槽位（4 字节）          <- 相当于 32 位游戏的静态指针
 *   World、Entity 全部用 VirtualAlloc 分配到 4GB 以下  <- 32 位进程的地址空间就这么大
 *   World.units 用 uint32_t 存                         <- 4 字节指针
 *
 * 工具用 ptr_size=4 去读，就能走通；用 8 会读错——这正是要验证的差异。
 *
 * 编译：gcc -fexec-charset=GBK -g -O0 -no-pie -Wall -o target32sim.exe target32sim.c
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <conio.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t id;
    float    pos[3];
    float    hp;
    uint8_t  team;
    uint8_t  _pad[3];
    char     name[16];
} Entity32;

typedef struct {
    uint32_t units;     /* 4 字节指针：指向下面的实体数组 */
    int32_t  count;
} World32;

/* 模块里的静态槽位：4 字节，存着 World 的 32 位地址 */
static unsigned long long g_pad[2];
static uint32_t g_world32;

/* 在 4GB 以下拿一块内存。32 位进程的地址空间上限就是 4GB，
 * 所以"低位分配"是模拟 32 位布局的关键。 */
static void *alloc_low(size_t size)
{
    for (uintptr_t base = 0x10000000; base < 0x70000000ULL; base += 0x01000000) {
        void *p = VirtualAlloc((void *)base, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (p)
            return p;
    }
    return NULL;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);   /* 输出不缓冲：重定向到文件时也能立刻看到 */
    World32 *w = (World32 *)alloc_low(sizeof(World32));
    Entity32 *u = (Entity32 *)alloc_low(sizeof(Entity32) * 4);
    if (!w || !u) {
        printf("低位分配失败（这台机器的 4GB 以下被占满了）\n");
        return 1;
    }

    w->units = (uint32_t)(uintptr_t)u;   /* 截断成 4 字节存进去 */
    w->count = 4;

    for (int i = 0; i < 4; i++) {
        u[i].id   = (uint32_t)(i + 1);
        u[i].hp   = 100.0f;
        u[i].team = (uint8_t)(i % 2);
        snprintf(u[i].name, sizeof(u[i].name), "bot-%d", i + 1);
    }
    g_world32 = (uint32_t)(uintptr_t)w;

    unsigned char *base = (unsigned char *)GetModuleHandleA(NULL);

    printf("=== 模拟 32 位布局的靶子 ===\n");
    printf("pid              = %lu\n", (unsigned long)GetCurrentProcessId());
    printf("模块基址         = %p   (真实 64 位进程)\n", (void *)base);
    printf("g_world32 槽位   = %p   模块内偏移 0x%zX  （4 字节）\n",
           (void *)&g_world32, (size_t)((unsigned char *)&g_world32 - base));
    printf("World 地址       = 0x%08X   （在 4GB 以下）\n", g_world32);
    printf("前面那块占位     = %p   （故意挤一下，把槽位推后）\n", (void *)g_pad);
    printf("units 地址       = 0x%08X   （4 字节指针）\n", w->units);
    printf("Entity 步长      = 0x%zX，hp 偏移 = 0x%zX\n",
           sizeof(Entity32), offsetof(Entity32, hp));
    printf("\n按 ESC 退出。hp 每 100ms -2.5\n");

    int frame = 0;
    for (;;) {
        if (_kbhit()) {
            int c = _getch();
            if (c == 27)
                break;
        }
        Entity32 *list = (Entity32 *)(uintptr_t)w->units;
        for (int i = 0; i < w->count; i++) {
            list[i].hp -= 2.5f;
            if (list[i].hp < 0.0f)
                list[i].hp = 0.0f;
        }
        if (++frame % 10 == 0) {
            printf("frame %-4d  [0].hp = %6.1f\n", frame, list[0].hp);
        }
        Sleep(100);
    }

    VirtualFree(u, 0, MEM_RELEASE);
    VirtualFree(w, 0, MEM_RELEASE);
    return 0;
}
