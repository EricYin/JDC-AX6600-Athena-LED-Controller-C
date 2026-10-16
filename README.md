# JDC-AX6600 Athena LED Controller (C Edition)

[![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-JDCloud%20AX6600%20(RE--CS--02)-brightgreen.svg)](#)
[![Language](https://img.shields.io/badge/language-C11-orange.svg)](#)
[![Release](https://img.shields.io/github/v/release/perlyz/JDC-AX6600-Athena-LED-Controller-C?color=blue)](https://github.com/perlyz/JDC-AX6600-Athena-LED-Controller-C/releases)

京东云雅典娜路由器（JDCloud AX6600 / RE-CS-02）前置 27×5 LED 点阵屏控制程序与配套 LuCI Web 管理界面。

基于纯 C11 编写，无第三方运行时依赖（无 Python、Rust 或 Node），专为路由器有限的资源环境设计。

---

## 致谢 (Acknowledgements)

本项目硬件协议实现参考了 [unraveloop/JDC-AX6600-Athena-LED-Controller](https://github.com/unraveloop/JDC-AX6600-Athena-LED-Controller)，感谢原作者的工作。

---

## 特性 (Features)

- **高性能与低资源开销**：纯 C11 实现，二进制体积约 30KB，常驻运行内存 < 1MB，CPU 占用日常接近 0%。
- **硬件驱动与物理按键支持**：
  - 基于 TM1628A 芯片实现 GPIO 驱动，支持 cdev 与 sysfs 双后端自动探测。
  - 完整接入机身侧边 `screen` 实体按键：单击切页、双击返回首页、长按待机息屏/唤醒（长按 2 秒由 procd 超时即刻响应，状态持久化保存）。
  - 支持屏幕待机供电持久化与 Web 管理界面一键即时开关（立即熄屏 / 立即点亮），毫秒级生效且重启路由器依然保持。
  - 支持自定义单行文本显示（`text`），Web 界面输入实时限制在 27 列点阵以内并提供 Canvas 点阵仿真预览。
- **本地系统监控与网速平滑**：
  - 纯本地读取 `/proc` 与 `/sys` 状态（CPU、内存、温度、网络流量），无任何网络请求。
  - 支持网络速率指数移动平均（EMA）平滑滤波，采用固定位宽排版，避免数值刷新时界面抖动。
  - 农历（1900–2100）与日出日落采用纯本地算法实现。
- **LuCI Web 管理界面 (`luci-app-athena-led-c`)**：
  - 内置 27×5 HTML5 Canvas 实时点阵仿真预览。
  - 支持自定义轮播列表与页面排序（一键上移/下移）。
  - 支持亮度调节、轮播时间、采样率、网卡选择与指示灯阈值配置。
- **开箱即用的 OpenWrt 构建支持**：
  - 源码自包含，编译期间无需外网拉取依赖，完全适配 OpenWrt / ImmortalWrt Feed 机制。

---

## 显示功能列表

| 标识 | 功能 | 格式示例 | 说明 |
| :--- | :--- | :--- | :--- |
| `time` | 实时时间 | `12:34:56` | 27 列满屏排版，冒号常亮 |
| `net_down` | 下载速率 | `D 12.5M` | 支持 EMA 平滑滤波与固定列宽 |
| `net_up` | 上传速率 | `U  1.8M` | 支持 EMA 平滑滤波与固定列宽 |
| `mem` | 内存占用 | `M  35%` | 读取 `/proc/meminfo` 计算占用率 |
| `cpu` | CPU 占用 | `C  12%` | 250ms 差值计算总利用率 |
| `temp` | 系统温度 | `T 45.52` | 分别显示 CPU 与 NSS 核心温度 |
| `date` | 公历日期 | `10-14` | 紧凑显示月-日 |
| `lunar` | 中国农历 | `L:9.4` | 纯本地查表计算（含闰月） |
| `text` | 自定义文本 | `ATHENA` | 自定义文字，输入时实时限制在 27 列以内 |

---

## OpenWrt 编译集成

### 作为 Feed 引用（推荐）

在 OpenWrt 源码根目录的 `feeds.conf.default`（或 `feeds.conf`）添加：

```text
src-git athena_led https://github.com/perlyz/JDC-AX6600-Athena-LED-Controller-C.git
```

更新并安装 Feed：

```sh
./scripts/feeds update athena_led
./scripts/feeds install -a -p athena_led
```

在 `make menuconfig` 中勾选：
- `Utilities ---> <*>/<M> athena-led-c`
- `LuCI ---> 3. Applications ---> <*>/<M> luci-app-athena-led-c`

### 直接 Clone 到 Package 目录

```sh
git clone https://github.com/perlyz/JDC-AX6600-Athena-LED-Controller-C.git package/athena-led
```

---

## 本地编译与终端模拟器

在普通 Linux PC 开发机上进行本地调试或体验模拟器：

```sh
make              # 编译本地版本
make test         # 运行单元自检
./build/athena-led-c --sim   # 启动终端字符模拟器（n/p 切页，空格息屏，q 退出）
```

交叉编译示例：

```sh
make CC=aarch64-openwrt-linux-musl-gcc CFLAGS="-Os"
```

---

## 运行参数

```text
Usage: athena-led-c [OPTIONS]

Options:
  --sim, --test             运行终端字符模拟器 (27x5, ■/·)
  --no-clear                模拟器模式下不逐帧清屏
  --screen-on 0|1           初始屏幕电源状态 (1 = 点亮, 0 = 息屏待机; 默认: 1)
  --custom-text TEXT        自定义文本页面的文本内容 (最多 27 列点阵, 默认: ATHENA)
  --pages LIST              轮播页面列表 (默认: time,net_down,net_up,mem,cpu,temp,date,lunar)
  --page NAME               追加单个页面到轮播列表
  --page-secs N             每页自动停留秒数 (0 = 仅依靠物理按键手动翻屏)
  --poll-ms N               监控采样与屏幕刷新周期 (默认: 250ms)
  --indicator-cpu-percent N CPU 指示灯点亮阈值 (默认: 5%)
  --indicator-net-kbytes N  网络指示灯点亮阈值 (默认: 10 KB/s, 1 KB = 1000 B)
  --net-smoothing-secs N    网络速率 EMA 平滑时间 (默认: 2s; 0 为关闭)
  --brightness 0..7         屏幕亮度等级 (0..7, 默认: 5)
  --net-interface NAME      网络速率监控网卡 (默认: wan)
  --self-test               运行内置单元自检并退出
  -v, --version             输出版本号并退出
  -h, --help                输出帮助信息
```

---

## 配置文件说明 (`/etc/config/athena-led-c`)

```uci
config main 'main'
	option enabled '1'
	option screen_on '1'
	option custom_text 'ATHENA'
	option brightness '5'
	option page_secs '0'
	list pages 'time'
	list pages 'net_down'
	list pages 'net_up'
	list pages 'mem'
	list pages 'cpu'
	list pages 'temp'
	list pages 'date'
	list pages 'lunar'
	option poll_ms '250'
	option indicator_cpu_percent '5'
	option indicator_net_kbytes '10'
	option net_smoothing_secs '2'
	option net_interface 'wan'
```

---

## 许可证 (License)

本项目采用 [Apache License 2.0](LICENSE) 开源许可证。
