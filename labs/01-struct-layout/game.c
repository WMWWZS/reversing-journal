/* game.c —— 一个玩具"游戏"：只有一件事，每帧掉血
 * 它的唯一用途：给你一个可以观察、可以下断点、可以读内存的靶子
 * 编译：gcc -g -O0 -no-pie -o game.exe game.c
 */
#include <stdio.h>
#include "entity.h"

#define N 4

static Entity g_entities[N];   /* 全局数组：地址写死在 exe 里，好找 */

static void init(void)
{
    for (int i = 0; i < N; i++) {
        g_entities[i].id   = (uint32_t)(i + 1);
        g_entities[i].hp   = 100.0f - i * 10.0f;
        g_entities[i].team = (uint8_t)(i % 2);
        snprintf(g_entities[i].name, sizeof(g_entities[i].name), "bot-%d", i + 1);
    }
}

int main(void)
{
    init();
    printf("g_entities        = %p   (array base)\n", (void *)g_entities);
    printf("&g_entities[0].hp = %p   = g_entities + 0x%02X\n\n",
           (void *)&g_entities[0].hp, (unsigned)offsetof(Entity, hp));

    for (int frame = 1; frame <= 3; frame++) {
        for (int i = 0; i < N; i++)
            g_entities[i].hp -= 2.5f;

        printf("frame %d | [0].hp = %p | hp = %6.1f | [1].hp = %6.1f\n",
               frame, (void *)&g_entities[0].hp,
               g_entities[0].hp, g_entities[1].hp);
    }

    printf("\nprocess stays alive. press Enter to exit.\n");
    getchar();
    return 0;
}
