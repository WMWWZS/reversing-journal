/* offsets.c —— 把"内存地图"打印出来，不用猜
 * 编译：gcc -g -O0 -no-pie -o offsets.exe offsets.c
 */
#include <stdio.h>
#include <stddef.h>
#include "entity.h"

#define ROW(f) \
    printf("  %-6s  offset 0x%02X   size %2zu\n", #f, \
           (unsigned)offsetof(Entity, f), sizeof(((Entity *)0)->f))

int main(void)
{
    printf("sizeof(Entity) = 0x%zX  (%zu bytes)\n\n", sizeof(Entity), sizeof(Entity));
    ROW(id);
    ROW(pos);
    ROW(hp);
    ROW(team);
    ROW(_pad);
    ROW(name);

    Entity e;
    unsigned char *base = (unsigned char *)&e;

    printf("\ninstance base address   %p\n", (void *)&e);
    printf("  &e.id    %p   = base + 0x%02X\n", (void *)&e.id,
           (unsigned)((unsigned char *)&e.id - base));
    printf("  &e.hp    %p   = base + 0x%02X   <-- remember this line\n", (void *)&e.hp,
           (unsigned)((unsigned char *)&e.hp - base));
    printf("  &e.name  %p   = base + 0x%02X\n", (void *)&e.name,
           (unsigned)((unsigned char *)&e.name - base));

    printf("\nRule: address = base + offset. No magic.\n");
    return 0;
}
