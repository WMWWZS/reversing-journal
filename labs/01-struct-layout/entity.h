/* entity.h —— 全场唯一的"真相"：结构体长什么样，只在这里定义一次
 *
 * 铁律：注释里的偏移是"当前顺序"下的结果。改了字段，注释必须立刻跟着改，
 *      否则注释就成了谎言。唯一的裁判是 offsets.exe 的输出。
 *
 * 当前布局（64 位，gcc -O0）：
 *   0x00  id        4
 *   0x04  pos[3]   12
 *   0x10  hp        4
 *   0x14  team      1
 *   0x15  _pad      3   <- 填充：让后面的 16 字节数组落在 8 的倍数上
 *   0x18  name     16
 *   合计 0x28 = 40 字节（恰好是 8 的倍数，无需尾部填充）
 */
#ifndef ENTITY_H
#define ENTITY_H

#include <stdint.h>

typedef struct {
    uint32_t id;        /* +0x00  4 字节 */
    float    pos[3];    /* +0x04 12 字节  x, y, z */
    float    hp;        /* +0x10  4 字节  <- 整场战争的起点 */
    uint8_t  team;      /* +0x14  1 字节 */
    uint8_t  _pad[3];   /* +0x15  3 字节  显式填充，删掉后 name 的偏移会变 */
    char     name[16];  /* +0x18 16 字节 */
} Entity;               /* 合计 0x28 = 40 字节 */

#endif
