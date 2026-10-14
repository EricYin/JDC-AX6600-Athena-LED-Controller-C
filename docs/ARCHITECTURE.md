# 模块架构

```
include/          公共头文件
src/
  main.c          入口与命令行解析
  app.c           运行模式分发（硬件 / 模拟器 / 自检）
  pages.c         页面文本渲染（时间/日期/农历/日出日落/监控）
  font.c          27×5 点阵字模表与文本→列数据渲染
  sim.c           PC 终端模拟器（■/· 输出、非阻塞键盘输入）
  screen.c        TM1628A 硬件驱动（GPIO cdev + sysfs 双后端）
  button.c        物理按键监听与信号响应（单击/双击/长按状态机）
  monitor.c       本地系统监控（/proc、/sys 采样）
  lunar.c         公历→农历转换（1900–2100 查表法）
  sun.c           NOAA 日出日落本地算法
```

## 数据流

```
monitor_poll() ─┐
                ├─> page_render() ─> font_render() ─> [scroll/center] ─> screen_write_frame()/sim_print_frame()
按钮事件 ────────┘
```

每 `--poll-ms`（默认 250ms）执行一轮：采样 → 渲染 → 输出 → `clock_nanosleep()`
精确休眠。无忙等待，无重复文件读取。

采样范围由 `pages` 配置聚合而成：勾选 CPU 则持续采样 CPU，勾选上传/下载则
持续采样网卡；未勾选的指标完全不读 `/proc`/`/sys`。独立指示灯使用同一份
持续采样结果，因此切页后不会显示过期速率。

指示灯阈值可配置：CPU 超过 `indicator_cpu_percent`（默认 5%）点亮；
上/下行速率超过 `indicator_net_kbytes`（默认 10 KB/s，1 KB = 1000 B）点亮。

监控页使用固定宽度文本槽位：标签、数字、小数点与单位的位置不随数值变化，
避免网速等高频刷新内容在屏幕上左右跳动；前导 0 以等宽空白槽位显示。

网络速率先按计数器差值计算瞬时值，再按 `net_smoothing_secs`（默认 2 秒，
0 为关闭）做 EMA 平滑后供页面显示和指示灯判断使用，避免突发流量导致
数字和箭头高频抖动。
