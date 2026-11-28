/* target.c -- lab09 的靶子：血量每 100ms 减少 2.5
 *
 * 这一节的靶子必须让"减血"变成一条容易辨认的指令，所以：
 *   - 用 -O0 编译（不优化，结构清晰）
 *   - hp 是 float，减的是常量 2.5f
 *   - 循环写成独立函数 tick()，不让编译器内联进 main
 *
 * 编译：gcc -fexec-charset=GBK -g -O0 -no-pie -fno-inline -Wall -o target.exe target.c
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <conio.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct {
    uint32_t id;
    float    pos[3];
    float    hp;
    uint8_t  team;
    uint8_t  _pad[3];
    char     name[16];
} Entity;

typedef struct {
    Entity *units;
    int     count;
    int     _pad;
} World;

static World *g_world;

/* 不让内联：这样"减血"这件事在机器码里是一段独立、可辨认的代码 */
__attribute__((noinline)) static void tick(void)
{
    for (int i = 0; i < g_world->count; i++)
        g_world->units[i].hp -= 2.5f;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    World *w = (World *)calloc(1, sizeof(World));
    Entity *u = (Entity *)calloc(4, sizeof(Entity));
    if (!w || !u) {
        printf("分配失败\n");
        return 1;
    }

    w->units = u;
    w->count = 1;
    u[0].id = 1;
    u[0].hp = 100.0f;
    snprintf(u[0].name, sizeof(u[0].name), "bot-1");
    g_world = w;

    printf("=== lab09 靶子 ===\n");
    printf("pid        = %lu\n", (unsigned long)GetCurrentProcessId());
    printf("&g_world   = %p   （模块内偏移 0x%zX）\n",
           (void *)&g_world,
           (size_t)((unsigned char *)&g_world - (unsigned char *)GetModuleHandleA(NULL)));
    printf("hp 地址    = %p（units + 0x%zX）\n", (void *)&u[0].hp, offsetof(Entity, hp));
    printf("tick 函数  = %p（这个地址下面是减血指令）\n", (void *)tick);
    printf("hp 每 100ms -2.5，按 ESC 退出\n\n");

    int frame = 0;
    for (;;) {
        if (_kbhit()) {
            int c = _getch();
            if (c == 27)
                break;
        }
        tick();
        if (++frame % 10 == 0)
            printf("frame %-4d  hp = %7.1f\n", frame, g_world->units[0].hp);
        Sleep(100);
    }

    free(g_world->units);
    free(g_world);
    return 0;
}
