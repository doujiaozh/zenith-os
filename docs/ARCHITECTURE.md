# ZenithOS 架构文档（Architecture）

> **状态：v6.0（重写进行中）。** 本文档描述 v6.0 的**目标架构**与已落地的事实：
> 模块化混合内核、机制与策略分离、HAL 分层、四模块目录映射、物理内存映射、BootInfo ABI 与
> 启动流程。凡当前工作树尚未实现的结构，均以「目标结构」标注，不冒充已完成事实。
> 若本文与 `src/include/config.h` / `config.inc` 冲突，**以源码为准，且本文视为缺陷**。

关联文档：[README.md](../README.md)、[ROADMAP.md](../ROADMAP.md)、
[SECURITY.md](SECURITY.md)、[ECOSYSTEM.md](ECOSYSTEM.md)。

---

## 目录

- [1. 系统形态：模块化混合内核](#1-系统形态模块化混合内核)
- [2. 机制与策略分离](#2-机制与策略分离)
- [3. HAL 分层](#3-hal-分层)
- [4. 四个模块到目录树的映射](#4-四个模块到目录树的映射)
- [5. 物理内存映射](#5-物理内存映射)
- [6. BootInfo ABI](#6-bootinfo-abi)
- [7. 启动流程](#7-启动流程)
- [8. 内核模块目录结构](#8-内核模块目录结构)
- [9. 构建与链接](#9-构建与链接)
- [10. 四角色工作区](#10-四角色工作区)
- [11. 平台派生矩阵](#11-平台派生矩阵)

---

## 1. 系统形态：模块化混合内核

ZenithOS v6.0 是**模块化混合内核（hybrid/microkernel-forward）**：当前所有模块运行在 32 位
保护模式单一地址空间、ring 0，但按「机制在内核、策略在配置、服务可迁出」的方式分层，为
v6.1 的能力制进程模型与 v6.2 的微内核化服务预留边界。

| 属性 | v6.0 值 |
| --- | --- |
| CPU 模式 | 32 位保护模式，平坦 4 GiB 段（代码 `0x08` / 数据 `0x10`），ring 0 |
| 内核组成 | C（TCC）+ ASM（NASM），ELF32 对象由 GNU `ld` 链接、`objcopy -O binary` 平坦化为 `kernel.bin` |
| 引导介质 | 原始 1.44 MB 软盘镜像，LBA 0 = 512 字节引导扇区 |
| 内核加载 | 物理 `0x10000`（`KERNEL_ORG`），120 扇区 = 61,440 字节上限 |
| 内存 | 位图 PMM（4 KiB 页）+ 分页（恒等映射）+ 静态内核堆 |
| 显示 | VGA 模式 13h，320×200×256，双缓冲（`0x20000` → `0xA0000`） |
| 引导抽象 | `BootInfo.boot_type`：BIOS=`0`（已实现）/ UEFI=`1`（接口） |
| 架构抽象 | x86_32（已实现）/ arm64（接口） |
| 用户态 | 无（v6.1 能力制进程模型引入） |
| 构建依赖 | Node 18+ + NASM 2.13+ + TCC 0.9.27 i386，OS 构建零 npm |

混合内核的目标不是「大一统」，而是**把可交换性做进结构**：驱动、子系统、乃至整个协议栈都可以
在后续版本迁出内核而不改变其调用契约（见 §2 与 `docs/SECURITY.md` §驱动隔离）。

---

## 2. 机制与策略分离

ZenithOS 的核心纪律：**机制（mechanism）是可复用的原语，策略（policy）是让机制可配置的数据。**

| 层次 | 机制（放哪里） | 策略（放哪里） |
| --- | --- | --- |
| 定时 | PIT/定时器驱动 `src/drivers/pit`，中断投递 `src/arch/x86_32/isr` | 频率 `PIT_HZ 100`、基准 `PIT_BASE 1193182` 在 `src/include/config.h` |
| 分页 | 页目录/页表填充与 CR3 切换 `src/mm/paging` | 恒等映射范围 `IDENT_MAP_BYTES (16 MiB)`、页目录/页表地址在 `config.h` |
| 内存分配 | 位图扫描/置位 `src/mm/pmm` | 位图大小 `PMM_BITMAP_BYTES 16384`、可寻址上限（512 MiB）在 `config.h` |
| 输出 | 帧缓冲/串口绘制原语 `src/drivers/{vga,serial}` | 输出路径选择 `video_mode`/`flags`（VGA/文本/串口/无头）在 `BootInfo` + `config.h` |
| 特权指令 | 封装后的 `cli/sti/CRx/lgdt/lidt` 在 `src/hal/x86/cpu` | （后续）能力门控决定谁可调用，v6.1 引入 |

规则：**任何常量、频率、映射范围、路径选择都不允许硬编码进机制实现**，必须收敛到
`src/include/config.h`（C 侧）与 `src/include/config.inc`（汇编侧，二者镜像）或 `BootInfo`
（启动期事实）。这是「一次编写，多设备派生」得以成立的前提。

---

## 3. HAL 分层

硬件抽象层把「与具体 CPU/板级有关的字节」隔离在最小面内，向上暴露稳定接口。

```text
  ┌─────────────────────────────────────────────────────────────┐
  │                    内核 / 驱动 / UI                          │
  │        （策略与业务，不含平台字节）                            │
  ├─────────────────────────────────────────────────────────────┤
  │  src/arch      架构层：GDT / IDT / ISR / PIC（x86_32）         │
  │                 arm64 接口骨架（arch.h / README.md）          │
  ├─────────────────────────────────────────────────────────────┤
  │  src/hal       HAL：端口 I/O（outb/inb/...）+ CPU 控制        │
  │                （cli/sti/hlt/rdtsc/CRx/invlpg/lgdt/lidt）     │
  ├─────────────────────────────────────────────────────────────┤
  │  src/drivers    设备驱动：VGA / serial / keyboard / pit /     │
  │                rtc / device（经 HAL 访问寄存器）              │
  ├─────────────────────────────────────────────────────────────┤
  │  boot/bios      BIOS 引导（16 位） ；boot/uefi 接口骨架        │
  └─────────────────────────────────────────────────────────────┘
```

### 3.1 HAL 接口（已实现）

`src/hal/x86/port.{h,asm}`：

```c
void     outb(uint16_t port, uint8_t value);
uint8_t  inb(uint16_t port);
void     outw(uint16_t port, uint16_t value);
uint16_t inw(uint16_t port);
void     outl(uint16_t port, uint32_t value);
uint32_t inl(uint16_t port);
void     io_wait(void);         /* out 0x80, al */
```

`src/hal/x86/cpu.{h,asm}`：

```c
void     cpu_cli(void);  void cpu_sti(void);  void cpu_hlt(void);
uint64_t cpu_rdtsc(void);
uint32_t cpu_read_cr0(void); uint32_t cpu_read_cr2(void); uint32_t cpu_read_cr3(void);
void     cpu_write_cr0(uint32_t v); void cpu_write_cr3(uint32_t v);
void     cpu_invlpg(uint32_t addr);
void     cpu_lgdt(const void *gdtr); void cpu_lidt(const void *idtr);
```

`src/hal/hal.h` 聚合 `port.h` 与 `cpu.h`。**这一层面是能力门控的最小入口**：任何越过 HAL 直接
`out`/`in`/改 `CRx` 的代码都违反分层，并会在 `docs/SECURITY.md` 的能力制路径中被拒之门。

### 3.2 架构层（目标结构）

`src/arch/arch.h`（入口）+ `src/arch/x86_32/{arch.h,gdt,idt,isr,pic}.{c,h,asm}`：
GDT（平坦段）/ IDT（256 门）/ ISR（异常与 IRQ stub、分发、panic）/ PIC（8259 重映射与 EOI）。
`src/arch/arm64/{arch.h,README.md}` 只定义接口与文档，**不参与 x86 构建**（见 `docs/ECOSYSTEM.md`）。

---

## 4. 四个模块到目录树的映射

v6.0 把整个仓库划为四个模块。模块划分是**逻辑归属**，用于说明「哪些目录承载什么职责」，
src 目录本身仍按 HAL/arch/mm/lib/drivers/kernel/ui 分层。

### 模块一 · 底层（Core）

内核自身的机制与入口。

| 目录/文件 | 状态 | 职责 |
| --- | --- | --- |
| `boot/bios/boot.asm` | [已实现] | 512 字节 BIOS 引导（§7） |
| `src/include/` | [已实现] | 自由站立头文件 + ABI：`zenith.h, config.h, config.inc, bootinfo.h, stdint.h, stddef.h, stdbool.h, stdarg.h` |
| `src/mm/` | [已编译·未接线] | `mm.h, pmm, paging, heap`：4 KiB 位图分配器、恒等映射分页、内核堆 |
| `src/lib/` | [已实现] / [已编译·未接线] | `string, math`（运行中）/ `fmt, log, panic`（已链接未调用） |
| `src/kernel/` | [已实现] / [目标架构] | `startup.asm, kernel_main.c`（交互式 Shell，§9）/ `console, sysinfo`（未创建） |

### 模块二 · 多设备（Hardware）

跨设备一致的硬件访问与平台派生。

| 目录/文件 | 状态 | 职责 |
| --- | --- | --- |
| `src/hal/` | [已实现] | `hal.h, hal/x86/{port,cpu}.{h,asm}`：端口 I/O、CPU 控制 |
| `src/arch/` | [已实现] / [接口骨架] | `arch/x86_32/*`（GDT/IDT/ISR/PIC 启动时安装并路由 IRQ）/ `arch/arm64/*` |
| `src/drivers/` | [已实现] / [已编译·未接线] | `vga, serial, keyboard, pit`（运行中）/ `rtc, device`（已链接未调用） |
| `boot/uefi/` | [接口骨架] | UEFI 移交规范接口骨架（§11） |

### 模块三 · 桌面（Desktop）

用户环境与四角色工作区。

| 目录/文件 | 状态 | 职责 |
| --- | --- | --- |
| `src/ui/font` | [已实现] | 5×7 位图字体（Shell 文本层逐字符绘制） |
| `src/ui/render3d` | [已实现] | 3D 神经网格渲染（Q8.8 定点旋转 + 透视投影，`3d` 命令进入，ESC 返回） |
| `src/ui/theme` | [目标架构] | 调色板与主题 |
| `src/ui/desktop` | [目标架构] | GUI 桌面 |
| `src/ui/terminal` | [目标架构] | 硬核终端（开发者模式） |
| `src/ui/roles` | [目标架构] | 角色/无障碍/管理面板 |
| `src/ui/scene3d` | [目标架构] | 3D 线框视觉（创作者模式；`render3d` 是其地基） |

### 模块四 · 生态（Ecosystem）

构建、验证、治理与配套。

| 目录/文件 | 状态 | 职责 |
| --- | --- | --- |
| `build.js` | [已实现] | 多目标编译 + GNU `ld`/`objcopy` 链接 + 镜像打包 + 校验 |
| `tools/` | [已实现] | `emulate.mjs`（v86 无头验证）、`lib/png.mjs`（无依赖 PNG） |
| `docs/` | [已实现] | ARCHITECTURE / SECURITY / ECOSYSTEM / ROADMAP 等 |
| `.github/workflows/` | [已实现] | CI（构建 + 校验 + v86 视觉回归） |

---

## 5. 物理内存映射

唯一事实来源：`src/include/config.inc` 与 `src/include/config.h`（镜像）。引导器与内核共享，
以防漂移。以下均在物理 32 位地址空间、1 MiB 以下静态分配。

| 地址 | 大小 | 常量 | 用途 |
| --- | --- | --- | --- |
| `0x00000500` | 592 字节 | `BOOTINFO_ADDR` | `BootInfo`：16 字节头 + ≤24×24 字节 E820 描述符 |
| `0x00010000` | 60 KiB | `KERNEL_ORG`, `KERNEL_SECTORS 120` | `kernel.bin` 加载地址，120 扇区窗口 |
| `0x00020000` | 64,000 字节 | `VGA_BACKBUF` | VGA 双缓冲（320×200） |
| `0x00030000` | 16 KiB | `PMM_BITMAP_ADDR` | PMM 位图：131,072 × 4 KiB = 512 MiB |
| `0x00034000` | → `0x00070000` | `HEAP_START`…`HEAP_END` | 内核堆（≈240 KiB，末字节 `0x6FFFF`，区间右开） |
| `0x00070000` | 4 KiB | —（保留） | **隔离保护页**：堆与分页结构之间的死区，任何分配器都不得越过 |
| `0x00071000` | 4 KiB | `PAGE_DIR_ADDR` | 页目录 |
| `0x00072000` | 16 KiB | `PAGE_TABLES_ADDR` | 页表：恒等映射前 16 MiB（`IDENT_MAP_BYTES`） |
| `0x0009F000` | 向下增长 | `KERNEL_STACK_TOP` | 内核栈顶 |
| `0x000A0000` | 64,000 字节 | `VGA_FB` | VGA 线性帧缓冲（模式 13h，320×200×256） |

```text
 0x000000  ┌──────────────────┐
           │ IVT / BDA（启动后弃用）│
 0x000500  │ BootInfo（592 B）  │
 0x0007C00 │ 引导扇区（瞬态）      │
 0x0010000 │ kernel.bin（≤60 KiB）│
 0x0020000 │ VGA 双缓冲（64,000 B）│
 0x0030000 │ PMM 位图（16 KiB）   │
 0x0034000 │ 内核堆（≈240 KiB）   │
 0x0070000 │ 隔离保护页（死区）   │
 0x0071000 │ 页目录（4 KiB）      │
 0x0072000 │ 页表（16 KiB，恒等 16 MiB）│
 0x009F000 │ 内核栈顶（向下）      │
 0x00A0000 │ VGA 帧缓冲（模式 13h）│
 0x00F0000 + BIOS ROM / 固件 MMIO（不触碰）│
```

> 内核加载到 `0x10000` 而非 `0x1000`，因为引导扇区仍在 `0x7C00` 执行，60 KiB 窗口不能覆盖
> 它；`0x10000` 使整段窗口安全落在引导扇区之上、双缓冲之下。PMM 仅分配 1 MiB 以上的帧，
> 内核自身结构位于保留的低 1 MiB 窗内。

---

## 6. BootInfo ABI

`struct BootInfo`（`packed`，定义于 `src/include/bootinfo.h`）是引导器 → 内核的稳定移交契约，
由 BIOS 引导（16 位）写入，`kernel_main`（32 位 C）读取；UEFI（64 位）路径写入同一结构。

固定头 16 字节，其后跟 E820 描述符表：

| 偏移 | 字段 | 类型 | 值/含义 |
| --- | --- | --- | --- |
| `0x00` | `magic` | `u32` | `'ZENO'` = `0x4F4E455A` |
| `0x04` | `version` | `u16` | `6` |
| `0x06` | `e820_count` | `u16` | 有效 `entries[]` 条数（≤ `BOOTINFO_MAX_E820`=`24`） |
| `0x08` | `boot_drive` | `u8` | 引导盘号（BIOS 下为 `DL`；`0x00` 软盘 / `0x80` 硬盘） |
| `0x09` | `boot_type` | `u8` | `BOOT_TYPE_BIOS`=`0` / `BOOT_TYPE_UEFI`=`1` |
| `0x0A` | `video_mode` | `u8` | `VIDEO_MODE_13H`=`0x13` / `_TEXT`=`0x03` / `_NONE`=`0x00` |
| `0x0B` | `flags` | `u8` | `BOOTF_VGA`(0x01)·`BOOTF_KEYBOARD`(0x02)·`BOOTF_HEADLESS`(0x04)·`BOOTF_SERIAL`(0x08) |
| `0x0C` | `checksum` | `u16` | 头相加校验（`0`=关闭） |
| `0x0E` | `reserved` | `u16` | 保留 |
| `0x10` | `entries[24]` | `E820Entry[]` | 每条 24 字节 |

`E820Entry`（每条 24 字节，ACPI 3.0 形状）：

| 偏移 | 字段 | 类型 | 含义 |
| --- | --- | --- | --- |
| `+0` | `base` | `u64` | 区域起始地址 |
| `+8` | `length` | `u64` | 区域字节长度 |
| `+16` | `type` | `u32` | `1`=RAM，`2`=保留，`3/4`=ACPI… |
| `+20` | `attrs` | `u32` | ACPI 3.0 扩展属性 |

当前 BIOS 引导写 `magic/version/boot_drive/boot_type(=0)/video_mode(=0x13)/flags(=0x03)`，
`e820_count` 由 E820 探测写入；`checksum` 默认 `0`（关闭）。

---

## 7. 启动流程

一条主线：**BIOS boot → 保护模式 → `startup.asm` → `kernel_main` → 子系统初始化 → 主循环**。
固件在进保护模式后即被放弃，内核不再调用任何 BIOS。

### 7.1 BIOS 引导（`boot/bios/boot.asm`，512 字节）

| 步 | 动作 | 代码 |
| :-: | --- | --- |
| 1 | 固件加载 LBA 0 到 `0000:7C00` 并跳入 | `boot_main` |
| 2 | `CLI`，段清零，`SP=0x7C00`，`CLD`，保存 `DL`（引导盘） | `boot_main` |
| 3 | A20：先测，再 BIOS `INT 15h AX=2401h`，再端口 `0x92` 位 1；每次以 `0x0500`/`0x0510` 别名测试验证 | `enable_a20` / `a20_check` |
| 4 | E820 探测：`INT 15h EAX=E820 EDX='SMAP' ECX=24`，描述符写入 `BOOTINFO_ENTRIES`（`0x510`），计数写入 `+6`（≤24） | `e820_probe` |
| 5 | CHS 加载内核：LBA 1..120 逐扇区 `INT 13h AH=02h` 到 `ES:BX=KERNEL_ORG`，每扇区 3 次重试 + 控制器复位 | `load_kernel` |
| 6 | 切 VGA 模式 13h（`INT 10h AX=0013h`）——在进保护模式**前**完成 | `boot_main` |
| 7 | 写 BootInfo 头（`magic/version/boot_drive/boot_type=0/video_mode=0x13/flags=0x03`） | `boot_main` |
| 8 | `CLI`，`LGDT`，置 `CR0.PE`，`jmp 0x08:pm_entry` | `boot_main` |
| 9 | 32 位跳板：段 = `0x10`，`ESP=KERNEL_STACK_TOP`，`EBP=0`，`EBX=BOOTINFO_ADDR`，`EAX=KERNEL_ORG`，`JMP EAX` | `pm_entry` |

### 7.2 内核入口（`src/kernel/startup.asm`）

`_start` 必须位于内核镜像偏移 0（构建强制检查）。它清段选择子、设 `ESP=0x9F000`，
把 `[__bss_start, __bss_end)` 清零（链接器提供的合成符号），然后 `push EBX`（BootInfo*）
调用 `kernel_main(BootInfo*)`，返回后 `hlt` 停机。

### 7.3 `kernel_main` 与子系统初始化

当前 `src/kernel/kernel_main.c` 已实现最小序（可作为目标流程的前缀）：

```text
serial_init()      COM1 16550 初始化，输出 "ZenithOS v6.0 [x86_32] booting..."
vga_init()         模式 13h 初始化 + 双缓冲清屏
vga_*()            输出横幅 / 调色板文本
vga_swap()         双缓冲 -> 0xA0000
cpu_hlt()          空闲循环
```

目标（v6.0 完整）初始化顺序，在 HAL/arch/mm/drivers 落地后扩为：

```text
_ start（startup.asm：清 BSS，EBX=BootInfo*）
  → kernel_main(BootInfo*)
      1. 校验 BootInfo（magic 0x4F4E455A、version 6、e820_count ≤ 24）
      2. HAL：cpu_cli / 串口输出
      3. 内存：pmm_init（E820 → 位图）→ paging_init（恒等映射 + 启 CR0.PG）→ heap_init
      4. 架构：gdt_init → idt_init → isr_init → pic_init
      5. 驱动：pit_init / rtc_init / keyboard_init / device 探测树
      6. 自适应输出：依 video_mode/flags 选 VGA/文本/串口/无头
      7. UI：theme_init → desktop/terminal/roles/scene3d 初始化
      8. cpu_sti：开中断
      9. 主循环（四角色工作区调度）
```

> 中断在 IDT/PIC/处理程序就绪前保持关闭；`STI` 只在全部就绪后执行，避免半初始化处理器被触发。

---

## 8. 内核模块目录结构

图例：**[已实现]** 引导后实际运行（v86 可回放）· **[已编译·未接线]** 源码已存在并通过编译/链接，
但 `kernel_main` 尚未调用 · **[目标架构]** 尚无实现 · **[接口骨架]** 仅头/文档，不参与编译。

```text
src/
├── include/
│   ├── zenith.h           [已实现] 主头：自由站立类型、ABI 常量、BootInfo 契约
│   ├── config.h           [已实现] C 侧内存映射与 ABI（与 config.inc 镜像）
│   ├── config.inc         [已实现] NASM 引导契约（纯常量）
│   ├── bootinfo.h         [已实现] BootInfo 与 E820Entry（packed）
│   ├── stdint.h / stddef.h / stdbool.h / stdarg.h  [已实现] 自由站立标准子集
├── hal/
│   ├── hal.h              [已实现] HAL 入口（聚合 port.h / cpu.h）
│   └── x86/
│       ├── port.{h,asm}   [已实现] 端口 I/O（outb/inb/outw/inw/outl/inl/io_wait）
│       └── cpu.{h,asm}    [已实现] CPU 控制（cli/sti/hlt/rdtsc/CRx/invlpg/lgdt/lidt）
├── arch/
│   ├── arch.h             [已实现] 架构层入口（头文件分发）
│   ├── x86_32/            [已实现] arch.h / gdt / idt / isr / pic（.{c,h,asm}）
│   └── arm64/             [接口骨架] arch.h / README.md（不参与 x86 编译）
├── mm/
│   ├── mm.h               [已实现] 内存层入口
│   ├── pmm.{c,h}          [已编译·未接线] 位图物理页分配器（4 KiB）
│   ├── paging.{c,h}       [已编译·未接线] 恒等映射分页 + 启用
│   └── heap.{c,h}         [已编译·未接线] 内核堆
├── lib/
│   ├── string.{c,h}       [已实现] kmemset/kmemcpy/kmemmove/kmemcmp/kstrlen/kstrcmp/...
│   ├── math.{c,h}         [已实现] 定点数学库（render3d 旋转矩阵；供 scene3d 线框变换）
│   ├── fmt.{c,h}          [已编译·未接线] 格式化输出（hex/dec）
│   ├── log.{c,h}          [已编译·未接线] 统一日志（串口 + 内存环形 + 级别/时间戳）
│   └── panic.{c,h}        [已编译·未接线] 异常报告 + panic 屏
├── drivers/
│   ├── vga.{c,h}          [已实现] 模式 13h 原语 + 5×7 文本 + 滚动（双缓冲、vga_swap）
│   ├── serial.{c,h}       [已实现] 16550 串口（COM1 0x3F8）
│   ├── keyboard.{c,h}     [已实现] PS/2 键盘（scancode set 1、环形缓冲、键状态、IRQ1）
│   ├── pit.{c,h}          [已实现] 8254 可编程间隔定时器（100 Hz，动画/Shell 计时）
│   ├── rtc.{c,h}          [已编译·未接线] CMOS 实时时钟
│   └── device.{c,h}       [已编译·未接线] 设备探测 / 设备树
├── kernel/
│   ├── startup.asm        [已实现] 内核入口（_start 位于偏移 0，清 BSS，调 kernel_main）
│   ├── kernel_main.c      [已实现] 交互式 Shell：提示符/回显/Backspace/Enter
│   │                              + help / clear / reboot / 3d
│   ├── console.{c,h}      [目标架构] 控制台壳（当前位置：kernel_main.c 内的 term_* 层）
│   └── sysinfo.{c,h}      [目标架构] 系统信息采集
└── ui/
    ├── font.{c,h}         [已实现] 5×7 位图字体（FONT_WIDTH 5 / FONT_HEIGHT 7）
    ├── render3d.{c,h}     [已实现] 3D 神经网格（Q8.8 旋转 + 透视投影，ESC 返回）
    ├── theme.{c,h}        [目标架构] 调色板与主题
    ├── desktop.{c,h}      [目标架构] GUI 桌面
    ├── terminal.{c,h}     [目标架构] 终端（开发者模式）
    ├── roles.{c,h}        [目标架构] 角色 / 无障碍 / 管理面板
    └── scene3d.{c,h}      [目标架构] 3D 线框视觉（创作者模式）
```

> `[已编译·未接线]` 的模块在 v6.0 重写中已落地源码、并随 `node build.js` 编译链接进
> `kernel.bin`（否则无法通过 GNU `ld` 的符号解析），但启动路径尚未调用它们——IDT/IRQ
> 已经打开（键盘与 100 Hz 节拍中断驱动），分页启用是紧随其后的工作。`[目标架构]` 文件
> 尚未在仓库创建。构建系统自动发现 `src/**/*.{c,asm}`，新增模块无需改 `build.js`。

---

## 9. 构建与链接

`node build.js`（零 npm 依赖）管线：

```text
1. 定位工具链         $NASM / $TCC 环境变量 → 仓库根（nasm.exe、tcc/tcc.exe）→ PATH；
                      ld / objcopy / nm：$LD / $OBJCOPY → PATH → 常见 mingw/msys 目录
2. 解析 config.inc    KERNEL_ORG、KERNEL_SECTORS（120）
3. 发现源码           src/**/*.{c,asm}（startup.asm 排最前，保证 _start 在偏移 0）；
                      对象名保留源扩展名（foo.c.o / foo.asm.o），避免 idt.c 与 idt.asm 互踩
4. 编译/汇编          TCC -c 编译 .c；NASM -f elf32 汇编 .asm → 各产出 ELF32 .o
5. 链接               GNU ld + 生成的 build/kernel.ld：
                        - 段布局 .text/.rodata/.data/.bss，起点 KERNEL_ORG
                        - 合成符号 __bss_start/__bss_end/__end/__kernel_end/__kernel_size
                        - 解析全部符号与重定位（ld 全功能，非自造链接器）
                        - mingw PE 模拟另需 --image-base ORG-0x1000 为 PE 头留页
                      → kernel.exe（PE 或 ELF，取决于宿主 binutils）
6. 平坦化             objcopy -O binary kernel.exe kernel.bin
                        - 剥掉容器头，文件偏移 0 即 .text 起点（_start）
                        - .bss 为 NOBITS，不出现在文件中，由 startup.asm 运行时清零
7. 组装引导           nasm -f bin boot/bios/boot.asm → build/boot.bin（512 B + 0xAA55）
8. 打包               引导@0，内核@512，零填充至 1,474,560 → zenithos.img（根 + build/）
9. 校验               nm 回读 _start == KERNEL_ORG；引导尺寸/签名、内核预算、回读；
                      写 build/report.json + SHA-256
```

CLI：`--quiet` / `--verbose`。`build/report.json` 记录 `nasm`/`tcc`/`ld`/`objcopy` 路径、对象
清单、内核字节数/预算/填充率、`bss` 起止、入口、镜像与内核 SHA-256。

> **设计决定：** 链接交给 GNU `ld` 而不是在 `build.js` 里手写 ELF32 链接器——前者是几十年的
> 成熟工具，后者是无穷尽的边界情形（重定位类型、段对齐、弱符号、COMMON 合并）。构建脚本只做
> 三件事：生成链接脚本、调度编译器/链接器、校验产物契约。

---

## 10. 四角色工作区

模块三（桌面）的目标是**四角色工作区**，v6.0 交付前三类；第四类（无障碍）在 v6.2 完整实现：

| 角色 | 交互 | 实现 |
| --- | --- | --- |
| **桌面 ↔ 终端** | 通用 GUI 桌面与终端通过快捷键**无损热切换** | `src/ui/desktop`、`src/ui/terminal` |
| **开发者模式** | 硬核终端；可查看**内存页 / 寄存器状态** | `src/ui/terminal`（+ `lib/fmt`、`lib/log`） |
| **创作者模式** | 极简沉浸式 **3D 线框视觉桌面**（定点数学库） | `src/ui/scene3d`（+ `lib/math`） |
| **无障碍/管理面板** | 极高对比度纯文本大字体面板，**监控系统健康** | `src/ui/roles`（v6.2 完整实现） |

「三角色工作区」（ROADMAP v6.0 用语）指 v6.0 完整交付的桌面/开发者/创作者三类；第四角色为
无障碍/管理面板，v6.0 出骨架、v6.2 完整实现，构成「四角色工作区」。

---

## 11. 平台派生矩阵

| 维度 | 已实现 | 接口（骨架） |
| --- | --- | --- |
| CPU 架构 | x86_32（32 位保护模式） | arm64（`src/arch/arm64/{arch.h,README.md}`） |
| 固件 | BIOS（`boot/bios/boot.asm`） | UEFI（`boot/uefi/`，移交规范前述） |

统一 HAL + `BootInfo.boot_type`/`video_mode` 抽象保证：内核主体不感知固件/架构来源，驱动经
HAL 访问寄存器。派生一条新平台 = 提供等价的 `boot/*` 与 `arch/*` + 复用 `hal` 之上的全部代码。
详见 [docs/ECOSYSTEM.md](ECOSYSTEM.md)。

---

*ZenithOS v6.0 架构文档结束。若本文与源码冲突，源码胜出，本文即缺陷。*
