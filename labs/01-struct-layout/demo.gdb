# gdb 脚本：观察 Player 的内存布局
#
# 用法：
#   gdb -q -batch -x demo.gdb player_O0.exe
#
# 注意：断点必须下在函数体第一行，不能下在函数入口。
# 入口处序言还没执行，gdb 解析参数位置会出错（会看到 p=0x2 这种垃圾值）。

set pagination off
set confirm off

break player.c:8
run

echo \n===== 断点命中：已进入 take_damage 函数体 =====\n
info args
p *p

echo \n===== 手工按偏移读内存，这些数字就是逆向要「反推」的东西 =====\n
p/x *(int*)p
p/x *(int*)((char*)p+4)
p/x *(float*)((char*)p+8)
p/x *(float*)((char*)p+12)

echo \n===== 执行完函数，hp 从 100 变成 70 =====\n
finish

quit
