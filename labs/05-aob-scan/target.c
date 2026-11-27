/* target.c -- lab05 的靶子：v2。偏移真的变了，不是假的
 *
 * "游戏更新"在链接产物上的表现：新增的全局变量挤进了同一段，把后面的变量整体推后。
 *
 * 注意 g_world 是**未初始化**的（走 .bss）。所以新加的变量也必须是未初始化/零初始化，
 * 才会和它排在同一个段里、把它推后。如果新变量带非零初值，会进 .data，跟 .bss 不挨着，
 * 偏移一点都不会变 —— 第一版就是这么写错的，实测 g_world 仍在 0x7030。
 *
 * 编译：gcc -fexec-charset=GBK -g -O0 -no-pie -Wall -o target.exe target.c
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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

/* v2 新增的那些全局（零初始化 -> 和 g_world 同段，会把 g_world 推后） */
static unsigned long long g_build_stamp;
static unsigned long long g_frame_counter;
static int  g_verbose;
static int  g_max_units;
static char g_version[8];

static World *g_world;

int main(void)
{
    World *w = (World *)calloc(1, sizeof(World));
    Entity *u = (Entity *)calloc(4, sizeof(Entity));
    if (!w || !u) {
        printf("分配失败\n");
        return 1;
    }

    w->units = u;
    w->count = 4;

    for (int i = 0; i < 4; i++) {
        u[i].id   = (uint32_t)(i + 1);
        u[i].hp   = 100.0f - i * 10.0f;
        u[i].team = (uint8_t)(i % 2);
        snprintf(u[i].name, sizeof(u[i].name), "bot-%d", i + 1);
    }
    g_world = w;

    snprintf(g_version, sizeof(g_version), "2.0.0");
    g_max_units  = 4;
    g_frame_counter = 1;

    printf("=== v2 靶子 ===\n");
    printf("pid            = %lu\n", (unsigned long)GetCurrentProcessId());
    printf("版本           = %s\n", g_version);
    printf("g_world 槽位   = %p\n", (void *)&g_world);
    printf("帧计数         = %llu\n", (unsigned long long)g_frame_counter);
    printf("旧变量         = verbose %d, stamp %llu\n", g_verbose,
           (unsigned long long)g_build_stamp);
    printf("World 堆地址   = %p\n", (void *)g_world);
    printf("units 堆地址   = %p\n", (void *)g_world->units);
    printf("hp             = %g\n", g_world->units[0].hp);
    printf("\n注意：槽位地址已经不是 0x...7030 了。\n");
    printf("按回车结束\n");

    getchar();
    free(g_world->units);
    free(g_world);
    return 0;
}
