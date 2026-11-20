/* offsets.c —— 让编译器亲口告诉你每个字段的偏移和总大小
 * 这就是你以后在调试器里要「反推」的那张表。
 */
#include <stdio.h>
#include <stddef.h>
#include "player.h"

int main(void)
{
    printf("sizeof(Player) = %zu  (0x%zx)\n", sizeof(Player), sizeof(Player));
    printf("hp      offset = %2zu  (0x%02zx)\n", offsetof(Player, hp),      offsetof(Player, hp));
    printf("max_hp  offset = %2zu  (0x%02zx)\n", offsetof(Player, max_hp),  offsetof(Player, max_hp));
    printf("x       offset = %2zu  (0x%02zx)\n", offsetof(Player, x),       offsetof(Player, x));
    printf("y       offset = %2zu  (0x%02zx)\n", offsetof(Player, y),       offsetof(Player, y));
    printf("z       offset = %2zu  (0x%02zx)\n", offsetof(Player, z),       offsetof(Player, z));
    printf("name    offset = %2zu  (0x%02zx)\n", offsetof(Player, name),    offsetof(Player, name));
    printf("flags   offset = %2zu  (0x%02zx)\n", offsetof(Player, flags),   offsetof(Player, flags));
    return 0;
}
