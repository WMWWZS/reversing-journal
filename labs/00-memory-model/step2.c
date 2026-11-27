/* step2.c -- 偏移不是背下来的，是加出来的
 *
 * 这个程序只做一件事：把 lab01 里那个 +0x10 拆成加法的过程，一步一步打出来。
 *
 * 编译：gcc -fexec-charset=GBK -g -O0 -Wall -o step2.exe step2.c
 */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

/* 这就是实验 01 的 Entity，只是字段少一点。
 * 先别看右边的注释，先看程序打印出来的数，再回头对。 */
typedef struct {
    int   id;       /* 起始位置 */
    float pos[3];   /* 一个 float 4 字节，三个就是 12 字节 */
    float hp;       /* 你关心的那个数 */
} Entity;

int main(void)
{
    Entity e;
    e.id     = 1;
    e.pos[0] = 0.0f;
    e.pos[1] = 0.0f;
    e.pos[2] = 0.0f;
    e.hp     = 92.5f;

    printf("=== 1. 每个字段有多大 ===\n");
    printf("sizeof(int)      = %zu 字节\n", sizeof(int));
    printf("sizeof(float)    = %zu 字节\n", sizeof(float));
    printf("sizeof(float[3]) = %zu 字节\n", sizeof(float) * 3);
    printf("sizeof(Entity)   = %zu 字节  (0x%zX)\n\n", sizeof(Entity), sizeof(Entity));

    printf("=== 2. 每个字段从哪一格开始 ===\n");
    printf("&e      = %p\n", (void *)&e);
    printf("&e.id   = %p   offsetof = %2zu  (0x%02zX)\n",
           (void *)&e.id, offsetof(Entity, id), offsetof(Entity, id));
    printf("&e.pos  = %p   offsetof = %2zu  (0x%02zX)\n",
           (void *)&e.pos, offsetof(Entity, pos), offsetof(Entity, pos));
    printf("&e.hp   = %p   offsetof = %2zu  (0x%02zX)\n",
           (void *)&e.hp, offsetof(Entity, hp), offsetof(Entity, hp));

    printf("\n=== 3. 同样的数，用加法算一遍 ===\n");
    printf("  id  从 0 开始\n");
    printf("  pos 从 0 + sizeof(id) = 0 + %zu = %zu\n", sizeof(int), sizeof(int));
    printf("  hp  从 %zu + sizeof(pos) = %zu + %zu = %zu  (0x%zX)\n",
           offsetof(Entity, pos), offsetof(Entity, pos),
           sizeof(e.pos), offsetof(Entity, pos) + sizeof(e.pos),
           offsetof(Entity, pos) + sizeof(e.pos));

    printf("\n=== 4. 偏移和地址是两件不同的事 ===\n");
    printf("  地址 = %p   (这个对象这次碰巧住在哪)\n", (void *)&e.hp);
    printf("  偏移 = 0x%zX       (hp 离开头多远，永远不变)\n", offsetof(Entity, hp));
    printf("  地址 = 对象首地址 + 偏移 -> %p + 0x%zX = %p\n",
           (void *)&e, offsetof(Entity, hp),
           (void *)((char *)&e + offsetof(Entity, hp)));

    printf("\n=== 5. 验证：两条路走到同一格 ===\n");
    long diff = (long)((char *)&e.hp - (char *)&e);
    printf("  (char*)&e.hp - (char*)&e = %ld  (0x%lX)\n", diff, diff);
    printf("  offsetof(Entity, hp)     = %zu\n", offsetof(Entity, hp));
    printf("  相等吗？%s\n", diff == (long)offsetof(Entity, hp) ? "相等" : "不相等");

    return 0;
}
