# ZenithOS v6.0

**一个从零编写的裸机 32 位 x86 操作系统：512 字节 BIOS 引导、C 与汇编混合内核、统一 HAL 与架构抽象、4 KiB 分页、面向多设备（BIOS/UEFI、x86_32/arm64）的微内核化路线图。**

[![Build](https://github.com/doujiaozh/zenith-os/actions/workflows/build.yml/badge.svg?branch=main)](https://github.com/doujiaozh/zenith-os/actions/workflows/build.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE.md)
[![Architecture: x86-32](https://img.shields.io/badge/arch-x86--32-blue.svg)](#architecture)
[![Language: C + NASM](https://img.shields.io/badge/kernel-C%20%2B%20NASM-purple.svg)](#build)
[![Build: NASM 2.16.01 + TCC 0.9.27 + GNU ld](https://img.shields.io/badge/toolchain-NASM%202.16.01%20%2B%20TCC%200.9.27%20%2B%20GNU%20ld-orange.svg)](#build)
[![Image: 1.44 MB](https://img.shields.io/badge/image-1.44%20MB-green.svg)](#build)

```text
    ███████╗███████╗███╗   ██╗██╗████████╗██╗  ██╗ ██████╗ ███████╗
    ╚══███╔╝██╔════╝████╗  ██║██║╚══██╔══╝██║  ██║██╔═══██╗██╔════╝
      ███╔╝ █████╗  ██╔██╗ ██║██║   ██║   ███████║██║   ██║███████╗
     ███╔╝  ██╔══╝  ██║╚██╗██║██║   ██║   ██╔══██║██║   ██║╚════██║
    ███████╗███████╗██║ ╚████║██║   ██║   ██║  ██║╚██████╔╝███████║
    ╚══════╝╚══════╝╚═╝  ╚═══╝╚═╝   ╚═╝   ╚═╝  ╚═╝ ╚═════╝   ╚══════╝
```

---

## 一句话定位

> **ZenithOS 是一个以「微内核化 + 机制与策略分离」为宪法的裸机操作系统：机制（分页、中断、驱动、CPU 控制）在 `kernel/hal/arch/mm/drivers`，策略（频率、映射、输出方式）在配置与各模块；桌面、终端、3D 视觉三类工作区在统一 HAL 之上，可在 BIOS/UEFI、x86_32/arm64 之间低迁移成本地派生。**

---

## 项目状态

v6.0 是**进行中的重写**：仓库从 v1.x 的「全 NASM、单地址空间、ring-0 神经网络桌面」重构为
「C(TCC) + ASM(NASM) 混合、HAL 分层、4 KiB 分页」的架构。当前工作树已验证的设施：BIOS
引导、BootInfo 移交、C 内核入口存活（串口 + VGA 文本 + 空闲循环）、HAL 的端口 I/O 与
CPU 控制、字符串库、5×7 字体、以及完整的多目标构建/链接/镜像打包管线。下文目录树中标注
`✅` 的为已存在文件，标注路线的为 v6.0 目标结构。

> 诚实边界：v6.0 目前**尚未**启用分页、没有 ring 3 用户态、没有进程模型、没有文件系统、
> 没有网络栈、没有持久化存储、没有 SMP。所有代码运行在 32 位保护模式的单一地址空间内。
> 这些是 `ROADMAP.md` 中 v6.1/v6.2/v7.0 的交付项，而非已完成事实。

---

## 实现状态总表

仓库里**凡标注 `[已实现]` 的模块都在引导后的内核里实际运行**（v86 无头串口/帧捕获可回放）；
标注 `[已编译·未接线]` 的模块源码已存在、能编译并通过链接，但 `kernel_main` 尚未调用，
属于骨架先行；标注 `[目标架构]` 的尚无实现，是路线图条目。

| 模块 | 状态 | 说明 |
| --- | --- | --- |
| BIOS 引导（`boot/bios/boot.asm`） | **[已实现]** | 512 B 引导扇区：A20 / E820 / CHS 加载 / 模式 13h / BootInfo / 保护模式 |
| BootInfo 移交 | **[已实现]** | `magic=0x4F4E455A`、`version=6`、≤24 条 E820 描述符 |
| C 内核入口（`src/kernel/kernel_main.c`） | **[已实现]** | `startup.asm` → `kernel_main(BootInfo*)`，串口横幅 + VGA 启动画面 + 空闲循环 |
| 串口驱动（`src/drivers/serial`） | **[已实现]** | 16550 UART COM1 @115200，轮询输出，v86 抓到完整引导日志 |
| VGA 驱动（`src/drivers/vga`） | **[已实现]** | 模式 13h 320×200×256，双缓冲 + `vga_puts` 文本绘制（5×7 字体放大） |
| HAL（`src/hal/x86`） | **[已实现]** | 端口 I/O（`outb/inb/...`）+ CPU 控制（`cli/sti/hlt/CRx/...`） |
| 自由站立 libc（`src/lib/string`） | **[已实现]** | `kmemset/kmemcpy/kstrlen/kstrcmp/...` |
| 构建管线（`build.js`） | **[已实现]** | TCC + NASM → **GNU `ld` + `objcopy`** → 平坦 `kernel.bin` → 1.44 MB 镜像 |
| GDT / IDT / ISR / PIC（`src/arch/x86_32`） | **[已编译·未接线]** | 代码已链接进镜像，`kernel_main` 尚未安装（下一里程碑打开中断） |
| PMM / 内核堆 / 分页（`src/mm`） | **[已编译·未接线]** | 位图分配器、bump heap、恒等映射页目录填表代码齐备，均未在启动路径调用 |
| 键盘 / PIT / RTC（`src/drivers`） | **[已编译·未接线]** | 驱动已链接，等待 IDT/IRQ 接线后启用 |
| 5×7 字体（`src/ui/font`） | **[已编译·未接线]** | 位图字体表已链接，UI 层未绘制 |
| 桌面 / 终端 / 3D / 角色（`src/ui/{desktop,terminal,scene3d,roles,theme}`） | **[目标架构]** | v6.1/v6.2 的四角色工作区 |
| 分页启用 | **[目标架构]** | 页目录 `0x71000`、页表 `0x72000` 地址已预留，`paging_enable` 未调用 |
| UEFI 引导 / arm64 / 用户态 / FS / 网络 | **[目标架构]** | 仅接口骨架（`boot/uefi`、`src/arch/arm64` 为文档+头文件） |

---

## 特性亮点（对应宪法七原则）

ZenithOS 的宪法是七条可检验的原则。下表说明每条原则在本仓库中的**落点**（目录、常量、
函数或文档）：

| # | 宪法原则 | 本仓库的落点 |
| --- | --- | --- |
| 1 | **微内核化 + 机制/策略分离** | 内核入口只做组装（`src/kernel/startup.asm` → `kernel_main`）；机制在 `src/hal`、`src/arch`、`src/mm`、`src/drivers`；策略集中在 `src/include/config.h`（如 `PIT_HZ 100`、`IDENT_MAP_BYTES 16 MiB`）。详见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) |
| 2 | **默认安全** | 最小攻击面、BootInfo 校验（`magic`/`version`/`checksum`）、构建期尺寸/签名/回读校验、确定性构建出 SHA-256。详见 [docs/SECURITY.md](docs/SECURITY.md) |
| 3 | **最小权限 + 能力制** | 特权指令全部经 HAL 封装（`src/hal/x86/cpu.{h,asm}`：`cli/sti/hlt/rdtsc/CRx/invlpg/lgdt/lidt`），为能力门控预留唯一入口；能力制进程模型是 v6.1 交付项 |
| 4 | **强隔离** | 4 KiB 分页：**[目标架构]**——页目录保留在 `0x71000`、恒等映射页表在 `0x72000`（前 16 MiB），当前未启用；驱动隔离用户态是 v6.2 交付项 |
| 5 | **隐私内置** | 无网络、无遥测、无持久化；日志默认仅 COM1 串口 + 内存环形缓冲，不外发；BootInfo 提供 `headless` 标志用于无人值守 |
| 6 | **多设备一致 + 低迁移成本** | 统一 HAL 与架构抽象：x86_32（已实现）/arm64（接口）、BIOS（已实现）/UEFI（接口）。详见 [docs/ECOSYSTEM.md](docs/ECOSYSTEM.md) |
| 7 | **开放治理 + 生态** | MIT 开源、文档先行、可复现构建、CI、公开路线图与治理流程。详见 [docs/ECOSYSTEM.md](docs/ECOSYSTEM.md) 与 [ROADMAP.md](ROADMAP.md) |

---

## 目录树概览

图例：**[已实现]** = 引导后实际运行（v86 可回放）· **[已编译·未接线]** = 源码已链接进镜像，
`kernel_main` 尚未调用 · **[目标架构]** = 尚无实现，路线图条目 · **[接口骨架]** = 仅头文件/文档，
不参与编译。

```text
.
├── boot/
│   ├── bios/boot.asm        [已实现] 512 字节 BIOS 引导：A20 / E820 / CHS 加载内核
│   │                                  模式 13h / 写 BootInfo / 进入保护模式
│   └── uefi/                [接口骨架] UEFI 移交规范（README 描述，暂不编译）
├── src/
│   ├── include/             [已实现] 自由站立头文件 + ABI：zenith.h / config.h /
│   │                                  config.inc / bootinfo.h / stdint.h / stddef.h /
│   │                                  stdbool.h / stdarg.h
│   ├── hal/
│   │   ├── hal.h            [已实现] HAL 入口（聚合 port.h / cpu.h）
│   │   └── x86/             [已实现] port.{h,asm}、cpu.{h,asm}：端口 I/O、CPU 控制
│   ├── arch/
│   │   ├── arch.h           [目标架构] 架构层入口
│   │   ├── x86_32/          [已编译·未接线] arch.h / gdt / idt / isr / pic（.{c,h,asm}）
│   │   └── arm64/           [接口骨架] arch.h / README.md（不参与 x86 编译）
│   ├── mm/                  [已编译·未接线] mm.h / pmm / paging / heap（.{c,h}）
│   ├── lib/                 [已实现] string（kmemset/…）
│   │                        [已编译·未接线] math / fmt / log / panic（.{c,h}）
│   ├── drivers/             [已实现] vga（13h 双缓冲）/ serial（16550 COM1）
│   │                        [已编译·未接线] keyboard / pit / rtc / device
│   ├── kernel/              [已实现] startup.asm / kernel_main.c
│   │                        [目标架构] console / sysinfo
│   └── ui/                  [已编译·未接线] font（5×7 位图）
│                            [目标架构] theme / desktop / terminal / roles / scene3d
├── build.js                 [已实现] 构建系统（TCC + NASM → GNU ld + objcopy）
├── tools/
│   ├── emulate.mjs          [已实现] v86 无头模拟（截图 + 串口 + 按键注入）
│   └── lib/png.mjs          [已实现] 无依赖 PNG 编码器
├── docs/                    文档（含 ARCHITECTURE / SECURITY / ECOSYSTEM / ROADMAP 等）
├── LICENSE.md               [已实现] MIT
└── .github/workflows/build.yml [已实现] CI（构建 + 校验 + v86 视觉回归）
```

完整、逐模块注释的目录结构见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#内核模块目录结构)。

---

## 构建

### 前置条件

| 依赖 | 最低版本 | 说明 |
| --- | --- | --- |
| [Node.js](https://nodejs.org/) | 18+ | 仅用于运行 `build.js` / `tools/emulate.mjs`；**OS 构建零 npm 依赖** |
| [NASM](https://www.nasm.us/) | **2.13+**（仓库自带 **2.16.01** 的 `nasm.exe`） | 汇编 `.asm`（`-f elf32`，引导扇区另用 `-f bin`） |
| [TCC](https://bellard.org/tcc/) | **0.9.27 i386**（仓库自带 `tcc/tcc.exe`） | 编译 `.c`（`-c` 输出 ELF32 可重定位对象） |
| GNU binutils（`ld` + `objcopy`，可选 `nm`） | 任意 | 链接与平坦化：`ld` 解析全部符号与重定位，`objcopy -O binary` 生成 `kernel.bin` |

`build.js` 依次在 `$NASM`/`$TCC` 环境变量、仓库根（`./nasm.exe`、`./tcc/tcc.exe`）、
`PATH` 中查找编译器；`ld`/`objcopy` 在 `$LD`/`$OBJCOPY`、`PATH` 与常见 mingw/msys 安装目录
（`C:\mingw64\bin` 等）中查找。找不到时报错中止。

### 构建步骤

```bash
node build.js
```

产物：

```text
build/boot.bin        512 字节     引导扇区，以 0xAA55 结尾
build/kernel.bin      ≤ 60 KiB     平坦 32 位内核（KERNEL_ORG=0x10000，60 KiB 预算）
build/zenithos.img    1,474,560 字节 1.44 MB 原始软盘镜像
zenithos.img                        仓库根下的同名副本
build/report.json                  机器可读构建报告（模块、哈希、校验、版本）
```

构建管线：发现 `src/**/*.{c,asm}` → TCC 编译 `.c`、NASM 汇编 `.asm`（均为 ELF32，对象名保留
源扩展名 `foo.c.o` / `foo.asm.o`，避免 `idt.c` 与 `idt.asm` 互踩）→ **GNU `ld`** 用生成的
`build/kernel.ld` 脚本把所有对象链接到 `KERNEL_ORG`（mingw 的 PE 模拟需要
`--image-base ORG-0x1000` 为 PE 头留出一页，脚本再把 `.text` 精确放到 `ORG`；原生 ELF ld 直接
取脚本）→ **`objcopy -O binary`** 剥掉容器头、平坦化出 `kernel.bin`（`_start` 位于文件偏移 0）→
组装 `boot.bin` → 打包 `zenithos.img` → 校验入口地址/尺寸/签名/预算并写 `build/report.json`。

> 链接脚本生成 `__bss_start` / `__bss_end` / `__end` / `__kernel_end` 符号；
> `build.js` 用 `nm` 回读校验 `_start` 恰在 `KERNEL_ORG`（`src/kernel/startup.asm` 导出）。
> 链接期一切符号解析与重定位都交给 GNU `ld`——**构建脚本不自造链接器**。

---

## 运行

推荐用 v86 无头模拟验证（无需显示器）：

```bash
node tools/emulate.mjs
```

或指定截图/串口输出：

```bash
node tools/emulate.mjs --out screenshots/emu-boot.png --serial build/serial.log
```

在 [v86](https://github.com/copy/v86)（WebAssembly x86 模拟器）中无头启动 `zenithos.img`，
等待内核进入图形模式，捕获 320×200 VGA 帧并写入 PNG，可选注入按键（`--keys "w,w,d,r"`）
比对前后帧、抓取 COM1 串口输出。退出码：`0` 通过、`2` 模拟器故障、`3` 空/非图形帧、`4` 参数/IO 错误。

若需要交互式真机/模拟器运行，可将镜像挂到任意软盘引导的 x86 模拟器：

```bash
qemu-system-i386 -fda zenithos.img -boot a
```

---

## 内存映射（物理，32 位）

`src/include/config.inc` 与 `src/include/config.h` 是唯一事实来源（互相镜像）。

| 地址 | 大小 | 用途 |
| --- | --- | --- |
| `0x00000500` | 592 字节 | `BootInfo`（16 字节头 + ≤24 条 24 字节 E820 描述符） |
| `0x00010000` | 60 KiB | `kernel.bin` 加载地址（`KERNEL_ORG`），120 扇区窗口 |
| `0x00020000` | 64,000 字节 | VGA 双缓冲（320×200） |
| `0x00030000` | 16 KiB | PMM 位图（131,072 个 4K 页 = 512 MiB） |
| `0x00034000` | 至 `0x00070000` | 内核堆（`HEAP_START`…`HEAP_END`，≈240 KiB，末字节 `0x6FFFF`） |
| `0x00070000` | 4 KiB | **隔离保护页**（堆与分页结构之间的死区，禁止分配） |
| `0x00071000` | 4 KiB | 页目录（`PAGE_DIR_ADDR`） |
| `0x00072000` | 16 KiB | 页表（恒等映射前 16 MiB，`PAGE_TABLES_ADDR`） |
| `0x0009F000` | 向下增长 | 内核栈顶（`KERNEL_STACK_TOP`） |
| `0x000A0000` | 64,000 字节 | VGA 线性帧缓冲（模式 13h，320×200×256） |

**BootInfo ABI**（`struct BootInfo`，`packed`，`src/include/bootinfo.h`）：

| 偏移 | 字段 | 值/含义 |
| --- | --- | --- |
| `0` | `magic` (u32) | `'ZENO'` = `0x4F4E455A` |
| `4` | `version` (u16) | `6` |
| `6` | `e820_count` (u16) | 有效 E820 条目数（≤24） |
| `8` | `boot_drive` (u8) | BIOS 引导盘号（`DL`） |
| `9` | `boot_type` (u8) | `0`=BIOS，`1`=UEFI |
| `10` | `video_mode` (u8) | `0x13`（文本 `0x03` / 无 `0x00`） |
| `11` | `flags` (u8) | `VGA` / `KEYBOARD` / `HEADLESS` / `SERIAL` 位 |
| `12` | `checksum` (u16) | 头相加校验（`0`=关闭） |
| `14` | `reserved` (u16) | 保留 |
| `16` | `entries[24]` | 每条 24 字节：`base`(u64)+`length`(u64)+`type`(u32)+`attrs`(u32) |

完整映射与启动流程见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)。

---

## 三角色工作区与四角色目标

模块三（桌面）的目标是**四角色工作区**，其中 v6.0 交付前三类，第四类（无障碍）在 v6.2 完整实现：

| 角色 | 说明 | 目录 |
| --- | --- | --- |
| 通用桌面 ↔ 终端 | 快捷键无损热切换 | `src/ui/desktop`、`src/ui/terminal` |
| 开发者模式 | 硬核终端，可查看内存页 / 寄存器状态 | `src/ui/terminal` |
| 创作者模式 | 极简沉浸式 3D 视觉桌面（线框 3D 场景，定点数学库） | `src/ui/scene3d` |
| 无障碍/管理面板 | 极高对比度纯文本大字体面板，监控系统健康 | `src/ui/roles` |

详情见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#四角色工作区)。

---

## 文档索引

| 文档 | 内容 |
| --- | --- |
| [ROADMAP.md](ROADMAP.md) | 分阶段路线图：v6.0（当前）→ v6.1 → v6.2 → v7.0 |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | 混合内核、HAL 分层、四模块到目录树、内存映射、启动流程、内核模块目录 |
| [docs/SECURITY.md](docs/SECURITY.md) | 安全四原则、威胁模型、攻击面清单、当前缓解与下一步 |
| [docs/ECOSYSTEM.md](docs/ECOSYSTEM.md) | 多设备派生、统一 HAL、云边端一致、低迁移成本、开放治理 |

> v1.x 时代的 `docs/AI-NATIVE.md`、`docs/POSIX-COMPATIBILITY.md`、`docs/TESTING.md`、
> `docs/FAQ.md` 等描述旧「神经核心」架构，正在随 v6.0 重写迁移，其内容可能与当前
> 代码不一致；以本目录树与上文五个文档为准。

---

## 许可证

**MIT**，见 [LICENSE.md](LICENSE.md)。

NASM、TCC、QEMU、v86、Node.js 仅作为工具链/模拟器使用，不随 OS 镜像分发；仓库根的
`nasm.exe` 与 `tcc/` 是本地工具链副本，不属于项目代码许可范围。

---

<div align="center">

**ZenithOS v6.0** — 机制与策略分离，一次编写，多设备派生。

*构建它，无头跑起来，看内核自己说话。*

</div>
