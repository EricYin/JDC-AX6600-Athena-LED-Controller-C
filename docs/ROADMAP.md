# 路线图

## v0.1（当前）：工程骨架 + 核心算法
- [x] Makefile（CC/CFLAGS/LDFLAGS/LDLIBS/DESTDIR/PREFIX 全规范兼容）
- [x] 字模表与列渲染（可变宽 3–4 列字形）
- [x] 终端模拟器（27×5，■/·，n/p 切页）
- [x] 本地系统监控（CPU/内存/温度/网速，仅读 /proc /sys）
- [x] 农历转换（1900–2100，含闰月）
- [x] 日出日落（NOAA 本地算法）
- [x] TM1628A 双屏 GPIO cdev/sysfs 驱动层

## v0.2：按键与状态机
- [ ] `button.c`：GPIO cdev 输入 + debugfs 兜底双后端
- [ ] 短按切页、双击回首页、长按 2s 息屏/唤醒
- [ ] 模拟器中以键盘模拟物理按键

## v0.3：调度与电源
- [ ] Profile 配置（页面顺序与停留时间）
- [ ] 定时息屏（跨午夜窗口）
- [ ] 夜间亮度自动降档
- [ ] SIGTERM/SIGINT 优雅退出（息屏并释放 GPIO）

## v0.4：OpenWrt 集成
- [ ] `openwrt/` 目录提供 package makefile
- [ ] procd init 脚本 + UCI 配置
- [ ] 交叉编译验证（aarch64 musl）
