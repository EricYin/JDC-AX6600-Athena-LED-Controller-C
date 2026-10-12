# JDC-AX6600 Athena LED Controller (C edition)

京东云雅典娜路由器（JDCloud AX6600 / RE-CS-02）前置 27×5 LED 点阵屏控制程序。

**纯 C11 / 零第三方依赖 / 无 Rust / 无预编译二进制**，仅使用标准 C 与 Linux POSIX
原生接口，可直接在 PC 与 OpenWrt aarch64 工具链下编译。

## 功能范围

### 已实现（v0.1 骨架）
- **本地时间 / 日期**：`HH:MM^SS`（冒号每秒闪烁）、`YYYY-MM-DD`
- **中国农历**：纯本地查表算法（1900–2100 年，含闰月）
- **日出日落**：NOAA 本地天文算法（默认北京，可 `--sun-coords` 指定）
- **系统监控**：CPU 占用、内存、CPU/NSS 温度、指定网卡上下行实时速率
  （全部读取 `/proc`、`/sys`，零外部命令、零网络请求）
- **5×5 点阵字模**：数字 / 字母 / 常用符号完整表，3–4 列可变宽字形
- **终端模拟器**：`--sim` 在普通 PC 终端以 `■`/`·` 渲染 27×5 画面，
  支持 `n/p` 切页、`q` 退出
- **硬件驱动**：TM1628A 双屏位翻转协议，GPIO cdev（现代内核）+ sysfs（老固件）
  双后端自动回退；主控芯片自动探测

### 规划中（见 docs/ROADMAP.md）
- 物理按键（`screen` 键）监听：切页 / 轮播切换 / 长按息屏
- 定时息屏 / 夜间亮度
- 平滑滚动细化、静态文本自定义
- OpenWrt `package/` 集成与 procd 守护

### 坚决剔除（不做）
外网天气、HTTP 内容抓取、MQTT、ARP 新设备扫描、公网 IP 查询等一切
需要外网连接或复杂第三方依赖的功能。

## 构建

```sh
make              # 本地 PC 编译
make test         # 运行内置自检（农历锚点、日出日落、格式化）
./athena-led-c --sim   # 终端模拟器
make install DESTDIR=/opt/staging PREFIX=/usr
```

标准变量全部可用：`CC`、`CFLAGS`、`LDFLAGS`、`LDLIBS`、`DESTDIR`、`PREFIX`。

OpenWrt 交叉编译示例：

```sh
make CC=aarch64-openwrt-linux-musl-gcc CFLAGS="-Os"
```

## 运行参数

| 参数 | 默认 | 说明 |
| --- | --- | --- |
| `--sim` / `--test` | - | 终端模拟器模式 |
| `--no-clear` | - | 模拟器不逐帧清屏（便于重定向输出调试） |
| `--page-secs N` | 5 | 每页停留秒数 |
| `--scroll-ms N` | 128 | 滚动帧间隔（毫秒） |
| `--poll-ms N` | 250 | 系统采样 / 屏幕刷新间隔（毫秒） |
| `--brightness 0..7` | 5 | 屏幕亮度 |
| `--net-interface NAME` | br-lan | 网速统计接口 |
| `--sun-coords LAT,LON` | 39.90,116.40 | 日出日落计算坐标 |
| `--self-test` | - | 运行内置校验并退出 |

## 性能设计

- **严禁忙等待**：主循环通过 `clock_nanosleep()` 按帧精确休眠
- **低频采样**：`/proc`、`/sys` 默认 250ms 采样一次（CPU/内存/温度/网速），
  与屏幕刷新共用同一次 `poll`，不重复读文件
- **静态内存**：全部固定大小栈/静态缓冲，无 `malloc`，无动态增长，无泄漏面
- **极小体积**：当前 x86-64 构建约 26KB text，静态数据 < 1.2KB

## 许可证

Apache-2.0，见 [LICENSE](LICENSE)。
