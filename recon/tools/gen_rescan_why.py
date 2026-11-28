# gen_rescan_why.py -- 为什么重开 + Rescan 之后候选会变少
from PIL import Image, ImageDraw, ImageFont

F_B = "C:/Windows/Fonts/msyhbd.ttc"
F_R = "C:/Windows/Fonts/msyh.ttc"
F_M = "C:/Windows/Fonts/consola.ttf"
INK = (28, 30, 36); MUTED = (108, 114, 126)
GREEN = (214, 245, 226); RED = (255, 222, 218); AMBER = (255, 238, 204); GREY = (244, 245, 248)

W, H = 1500, 760
img = Image.new("RGB", (W, H), (252, 252, 250))
d = ImageDraw.Draw(img)
f = lambda p, s: ImageFont.truetype(p, s)
h1, h2, body, small, mono, tin = f(F_B, 34), f(F_B, 21), f(F_R, 18), f(F_R, 16), f(F_M, 15), f(F_R, 14)

d.text((46, 26), "为什么 Rescan 之后候选会变少", font=h1, fill=INK)
d.text((46, 72), "第一局：几千条路径都能指到血量地址。重开后目标换了地方，只有「真链」还能算到它。", font=small, fill=MUTED)

# 表头
cols = [46, 460, 760, 1060, 1290]
d.rectangle([46, 112, 1454, 152], fill=GREY)
for x, t in zip(cols, ["候选路径（第一局扫出来的）", "第一局算到", "重开后算到", "结果"]):
    d.text((x + 10, 122), t, font=h2, fill=MUTED)

rows = [
    ("ac_client.exe+1A2B3C -> +0x10 -> +0x04", "009A4F2C", "00C3D404", "命中，留下", GREEN),
    ("0x0019FF20 (stack) -> +0x04", "009A4F2C", "0061A880", "断了，删除", RED),
    ("0x00B4E000 (heap) -> +0x08 -> +0x04", "009A4F2C", "00D1F204", "断了，删除", RED),
    ("ac_client.exe+1C2A50 -> +0x04", "009A4F2C", "009A4F30", "断了，删除", RED),
]
y = 160
for path, a, b, res, col in rows:
    d.rectangle([46, y, 1454, y + 84], fill=col, outline=(210, 214, 222))
    d.text((56, y + 12), path, font=mono, fill=INK)
    d.text((cols[1] + 10, y + 12), a, font=mono, fill=INK)
    d.text((cols[2] + 10, y + 12), "= " + b if col == GREEN else "= " + b, font=mono, fill=INK)
    d.text((cols[3] + 10, y + 12), res, font=body, fill=INK)
    y += 92

d.rounded_rectangle([46, 548, 1454, 660], radius=12, fill=AMBER, outline=INK, width=2)
d.text((66, 562), "第一局「都能到」，是因为它们都恰好装着一个当时的地址；第二局，只有第一条是「按地图走」，不是「背门牌号」。",
       font=body, fill=INK)
d.text((66, 598), "所以：Rescan = 拿新地址当答案，把每条路径重新算一遍，算不中的删掉。", font=body, fill=(150, 30, 20))
d.text((66, 628), "扫两三次是为了降低「运气好蒙对」的概率：假路径每次都存活的概率极低。", font=small, fill=MUTED)

d.text((46, 690), "极端情况：Rescan 后剩 0 条——说明这个字段没有稳定的静态引用（对象是临时创建的）。", font=small, fill=INK)
d.text((46, 716), "那时就要换思路：用特征码定位「访问它的代码」，再读寄存器里的对象地址（lab05 + lab09 的组合）。",
       font=small, fill=MUTED)
img.save("rescan-why.png")
print("saved rescan-why.png")
