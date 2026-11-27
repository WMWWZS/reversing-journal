/* target.c -- lab 04 的活靶子：数据搬到堆上，地址每次启动都不一样
 *
 * 和 lab 03 的区别，只有一个但是致命的：
 *   lab03 的 g_entities 是**全局数组**，地址 = 模块基址 + 固定偏移，写死也不会错。
 *   lab04 的实体在 **malloc 出来的堆**上，每次启动地址都不同。
 *
 * 结构关系（这就是"指针链"这个词的全部含义）：
 *
 *   模块 .data 里的一个格子      &g_world  （固定，= 基址 + 某偏移）
 *        └─ 8 字节，存着 ──────>  World 结构（堆）
 *                                   └─ +0x00 8 字节，存着 ──> Entity 数组（堆）
 *                                                                └─ +0x10  float hp
 *
 * 所以拿到 hp 要走三步：读模块里的格子 -> 读 World 里的格子 -> 加 0x10 读 float。
 * 少走任何一步，你手上的地址都是错的。
 *
 * 编译：gcc -g -O0 -no-pie -Wall -o target.exe target.c
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <conio.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>

#include "../01-struct-layout/entity.h"   /* 结构体真相源，和 01/03 同一份 */

#define N 4

/* World：多包一层的意义在于让链条有两跳。真实引擎全这么干：
 * 全局只存一个世界对象，世界对象里挂着实体列表。 */
typedef struct {
    Entity *units;     /* +0x00  8 字节：指向堆上的 Entity 数组 */
    int     count;     /* +0x08  4 字节 */
    int     _pad;      /* +0x0C  4 字节：让 sizeof 是 8 的倍数 */
} World;

static World *g_world;   /* 全局指针：它自己住在模块里（地址固定），值是堆地址（每次变） */

static void init(void)
{
    World *w = (World *)calloc(1, sizeof(World));
    Entity *u = (Entity *)calloc(N, sizeof(Entity));
    if (!w || !u) {
        printf("alloc failed\n");
        exit(1);
    }

    w->units = u;
    w->count = N;

    for (int i = 0; i < N; i++) {
        u[i].id   = (uint32_t)(i + 1);
        u[i].hp   = 100.0f - i * 10.0f;
        u[i].team = (uint8_t)(i % 2);
        snprintf(u[i].name, sizeof(u[i].name), "bot-%d", i + 1);
    }

    g_world = w;
}

static void draw(int frame)
{
    COORD top = {0, 4};               /* 前四行是固定信息，动画从第 5 行开始 */
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), top);

    printf("frame %-5d  hp -2.5 every 100 ms                                  \n", frame);
    printf("-------------------------------------------------------------------\n");
    for (int i = 0; i < N; i++) {
        Entity *e = &g_world->units[i];
        printf("   [%d] %-6s  hp = %8.1f   %s        \n",
               i, e->name, e->hp, e->hp <= 0.0f ? "<DEAD>" : "");
    }
    printf("-------------------------------------------------------------------\n");
    printf("ESC = quit                                                        \n");
}

int main(void)
{
    init();

    printf("slot   &g_world        = %p   <- fixed: module base + offset\n", (void *)&g_world);
    printf("heap   g_world         = %p   <- heap: different every run\n", (void *)g_world);
    printf("heap   g_world->units  = %p   <- heap: different every run\n", (void *)g_world->units);
    printf("layout: World sizeof=0x%zX units@+0x%zX | Entity sizeof=0x%zX hp@+0x%zX name@+0x%zX\n",
           sizeof(World), offsetof(World, units),
           sizeof(Entity), offsetof(Entity, hp), offsetof(Entity, name));

    int frame = 0;
    for (;;) {
        if (_kbhit()) {
            int c = _getch();
            if (c == 27)               /* ESC */
                break;
        }

        for (int i = 0; i < N; i++) {
            g_world->units[i].hp -= 2.5f;
            if (g_world->units[i].hp < 0.0f)
                g_world->units[i].hp = 0.0f;
        }

        draw(++frame);
        Sleep(100);
    }

    free(g_world->units);
    free(g_world);
    printf("\nbye.\n");
    return 0;
}
