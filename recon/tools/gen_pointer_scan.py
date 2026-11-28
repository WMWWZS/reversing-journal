# gen_pointer_scan.py -- recon 用：解释"指针扫描在找什么"
from PIL import Image, ImageDraw, ImageFont
import math

F_B = "C:/Windows/Fonts/msyhbd.ttc"
F_R = "C:/Windows/Fonts/msyh.ttc"
F_M = "C:/Windows/Fonts/consola.ttf"
INK = (28, 30, 36); MUTED = (108, 114, 126)
BLUE = (214, 234, 255); GREEN = (214, 245, 226); RED = (255, 222, 218)
AMBER = (255, 238, 204); GREY = (240, 241, 244)

W, H = 1500, 880
img = Image.new("RGB", (W, H), (252, 252, 250))
d = ImageDraw.Draw(img)
f = lambda p, s: ImageFont.truetype(p, s)
h1, h2, body, small, mono, tin = f(F_B, 34), f(F_B, 21), f(F_R, 18), f(F_R, 16), f(F_M, 15), f(F_R, 14)


def arrow(x1, y1, x2, y2, color=(90, 96, 110), w=3):
    d.line([x1, y1, x2, y2], fill=color, width=w)
    a = math.atan2(y2 - y1, x2 - x1); L = 12
    d.polygon([(x2, y2),
               (x2 + L * math.cos(a + math.radians(155)), y2 + L * math.sin(a + math.radians(155))),
               (x2 + L * math.cos(a - math.radians(155)), y2 + L * math.sin(a - math.radians(155)))], fill=color)


def box(x, y, w, h, lines, fill=(255, 255, 255), fonts=None):
    d.rounded_rectangle([x, y, x + w, y + h], radius=10, fill=fill, outline=INK, width=2)
    yy = y + 8
    for i, ln in enumerate(lines):
        d.text((x + 12, yy), ln, font=fonts[i], fill=INK)
        yy += fonts[i].size + 5


d.text((46, 28), "指针扫描到底在找什么", font=h1, fill=INK)
d.text((46, 74), "你要的不是「这个地址」，而是「每次都能走到这个地址的那条路」", font=small, fill=MUTED)

# 面板 1
d.rounded_rectangle([46, 120, 470, 400], radius=14, outline=(226, 228, 234), width=2, fill=(255, 255, 255))
d.text((66, 136), "① 你手上有什么", font=h2, fill=INK)
box(66, 180, 380, 78, ["addr 00945884", "value 100  = health (this round)"], RED, [mono, body])
d.text((66, 276), "关掉游戏重开：", font=small, fill=MUTED)
box(66, 300, 380, 78, ["addr 00A1B2C3", "value 100  = same health, new addr"], RED, [mono, body])

# 面板 2
d.rounded_rectangle([500, 120, 960, 400], radius=14, outline=(226, 228, 234), width=2, fill=(255, 255, 255))
d.text((520, 136), "② 扫描器在试什么", font=h2, fill=INK)
d.text((520, 168), "从「永远不变」的模块出发，试成千上万种偏移组合", font=tin, fill=MUTED)
box(520, 196, 400, 70, ["ac_client.exe + 0x??????", "（模块里，位置固定）"], BLUE, [mono, small])
box(520, 282, 400, 70, ["+ 偏移 1  →  某个对象", "+ 偏移 2  →  … →  血量"], GREEN, [small, small])
arrow(720, 268, 720, 280)
d.text((520, 360), "找到的候选可能上千条，大多会断", font=tin, fill=(180, 40, 30))

# 面板 3
d.rounded_rectangle([990, 120, 1450, 400], radius=14, outline=(226, 228, 234), width=2, fill=(255, 255, 255))
d.text((1010, 136), "③ 重启后「重新扫描」", font=h2, fill=INK)
d.text((1010, 168), "目标换成新地址；只留能走到它的路径", font=tin, fill=MUTED)
box(1010, 196, 400, 70, ["旧路径：断（对象搬家了）", "新路径：通（模块 + 偏移仍有效）"], GREY, [small, small])
box(1010, 282, 400, 70, ["过滤后剩几条", "→ 这就是静态链"], AMBER, [body, small])
d.text((1010, 362), "重开 2~3 次、每次重扫，剩下的就是它", font=tin, fill=MUTED)

# 底部：结果形状
d.rounded_rectangle([46, 440, 1450, 560], radius=14, fill=AMBER, outline=INK, width=2)
d.text((66, 456), "最后你得到的是一条「路」，不是「一个地址」：", font=h2, fill=INK)
d.text((66, 494), "ac_client.exe + 0x??????   -->  +0x??   -->  playerent  -->  +0x04  =  health",
       font=mono, fill=(150, 30, 20))
d.text((66, 524), "模块基址每次开机可能变，但「模块 + 偏移」不变——所以链条第一跳永远写成 模块+偏移。",
       font=small, fill=INK)

# 底部：从指令读到的东西
d.text((46, 590), "你截图里那条指令已经告诉你一个偏移了：", font=h2, fill=INK)
d.text((46, 628), "0041C223 - 29 73 04  -  sub [ebx+04], esi      ; ebx = player, health at +0x04",
       font=mono, fill=INK)
d.text((46, 660), "ebx 是寄存器里的「对象地址」，它会随每局变化；指针扫描要做的，就是找到「谁存着这个 ebx」并一路回溯到模块。",
       font=small, fill=MUTED)

d.text((46, 712), "两条路都行：", font=h2, fill=INK)
box(46, 748, 660, 100, ["A. 直接在血量地址上做「指针扫描」",
                        "   CE: 右键地址 → Pointer scan → 默认层数 3~5 → 扫",
                        "   重开游戏 → 重新找到血量 → Pointer scanner → Rescan memory"], GREY, [small, small, small])
box(760, 748, 690, 100, ["B. 先拿到 ebx（对象地址），再对 ebx 做指针扫描",
                         "   CE 的「找出是什么访问」窗口里选中那行，下方会显示 EBX 的值",
                         "   对那个值做指针扫描，层数通常更浅"], BLUE, [small, small, small])
img.save("pointer-scan.png")
print("saved pointer-scan.png")
