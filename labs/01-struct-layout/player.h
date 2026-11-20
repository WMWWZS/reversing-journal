/* player.h —— 这一课的核心：结构体在内存里到底长什么样
 * 逆向工程的第一性原理就是还原这个文件里描述的东西。
 */
#ifndef PLAYER_H
#define PLAYER_H

typedef struct {
    int   hp;          /* 当前血量，偏移 0x00 */
    int   max_hp;      /* 最大血量，偏移 0x04 */
    float x, y, z;     /* 世界坐标，偏移 0x08 / 0x0c / 0x10 */
    char  name[16];    /* 名字，偏移 0x14 */
    int   flags;       /* 状态标记，偏移 0x24 */
} Player;

void take_damage(Player *p, int dmg);
void heal(Player *p, int amount);

#endif
