#include <stdio.h>
#include <string.h>
#include "player.h"

/* 最典型的游戏逻辑：受击扣血，不允许扣成负数 */
void take_damage(Player *p, int dmg)
{
    p->hp -= dmg;
    if (p->hp < 0)
        p->hp = 0;
}

/* 治疗，不超过上限 */
void heal(Player *p, int amount)
{
    p->hp += amount;
    if (p->hp > p->max_hp)
        p->hp = p->max_hp;
}

int main(void)
{
    Player p;

    memset(&p, 0, sizeof(p));
    p.hp     = 100;
    p.max_hp = 100;
    p.x = 1.5f;
    p.y = 2.5f;
    p.z = 3.5f;
    strcpy(p.name, "hero");
    p.flags = 0;

    take_damage(&p, 30);
    printf("after hit : hp=%d\n", p.hp);

    heal(&p, 50);
    printf("after heal: hp=%d\n", p.hp);

    return 0;
}
