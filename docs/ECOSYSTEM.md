# ZenithOS 生态（Ecosystem）

> **目标：** 一次编写、多设备派生。同一内核主体（机制）在统一 HAL 与架构抽象之上，可在不同
> CPU（x86_32 / arm64）与固件（BIOS / UEFI）之间低迁移成本地派生，并支撑云（云端模拟/验证）、
> 边（边缘设备）、端（终端/裸机）一致的运行语义。治理保持开放。

关联文档：[README.md](../README.md)、[ARCHITECTURE.md](ARCHITECTURE.md)、[SECURITY.md](SECURITY.md)、
[ROADMAP.md](../ROADMAP.md)。

---

## 目录

- [1. 多设备派生矩阵](#1-多设备派生矩阵)
- [2. 统一 HAL 与驱动隔离](#2-统一-hal-与驱动隔离)
- [3. 云边端一致](#3-云边端一致)
- [4. 低迁移成本](#4-低迁移成本)
- [5. 开放治理](#5-开放治理)

---

## 1. 多设备派生矩阵

| 维度 | 状态 | 目录 / 入口 |
| --- | --- | --- |
| **x86_32** | ✅ 已实现 | `src/hal/x86/`、`src/arch/x86_32/`（目标结构）、`boot/bios/boot.asm` |
| **arm64** | 🧩 接口 | `src/arch/arm64/{arch.h,README.md}`（仅头文件/文档，不参与 x86 编译） |
| **BIOS** | ✅ 已实现 | `boot/bios/boot.asm`（512 字节，`BootInfo.boot_type=0`） |
| **UEFI** | 🧩 接口 | `boot/uefi/`（移交规范接口骨架，`BootInfo.boot_type=1`，暂不编译） |

派生一条新平台 = 提供等价的 `boot/*`（生产 `BootInfo`）与 `arch/*`（GDT/IDT/ISR/PIC 或 AArch64
等价物），并**复用 `src/hal` 之上的全部代码**（`mm`/`lib`/`drivers`/`ui`/`kernel` 主体不变）。

### 1.1 BootInfo 作为跨固件契约

`BootInfo.boot_type`（`0`=BIOS / `1`=UEFI）与 `BootInfo.video_mode` / `flags` 是内核不感知来源的
关键：内核只读 `struct BootInfo`，不关心是谁写的。UEFI 64 位入口与 BIOS 16 位入口写**同一结构**，
因此内核主体零改动。

### 1.2 移交规范（UEFI，规划）

UEFI 路径的目标移交约定：64 位 UEFI 应用探测 UEFI 内存映射 / E820，获取 GOP 帧缓冲（记录
`video_mode`），把结果写入 `BOOTINFO_ADDR`（`0x500`）的 `struct BootInfo`（`boot_type=1`），退出
Boot Services 后进入 32 位保护模式跳转 `KERNEL_ORG`（`0x10000`），与 BIOS 路径汇合。

---

## 2. 统一 HAL 与驱动隔离

**统一 HAL** 是低迁移成本的前提：`src/hal/hal.h` 聚合 `port.h`（`outb/inb/outw/inw/outl/inl/io_wait`）
与 `cpu.h`（`cli/sti/hlt/rdtsc/CRx/invlpg/lgdt/lidt`）。驱动只通过 HAL 触碰硬件，不写裸 `out/in` 或
直接改控制寄存器。x86 与 arm64 各自提供等价 HAL 实现，上层完全一致。

**驱动隔离**（v6.2 规划）：驱动随后迁出内核、成为受能力约束的用户态服务，端口/MMIO 由内核按
能力窗口代理或授权。HAL 作为统一接口，让「驱动在内核内」到「驱动在用户态」的迁移**不改变驱动
调用契约**——这正是机制与策略分离在硬件访问上的体现（见 `SECURITY.md` §驱动隔离）。

---

## 3. 云边端一致

一致语义来自同一内核主体 + 同一验证管线：

- **云** —— 桌面 CI / 研究：`node tools/emulate.mjs` 在 v86（WebAssembly x86 模拟器）中无头运行，
  提供截图 + 串口 + 按键注入的一致性验证；可用于远端无人值守验证。
- **边 / 端** —— 同一 `zenithos.img` 可直接在软盘引导的 x86 裸机/模拟器（如 `qemu-system-i386`）上运行。

自适应输出（`BootInfo.video_mode`/`flags` 的 VGA/文本/串口/无头）保证了「无显示器的云端验证」与
「有显示器的终端」跑的是同一份内核，只是输出路径不同。

---

## 4. 低迁移成本

低迁移成本由三个结构保证：

1. **单一事实来源（`config.inc`/`config.h` 镜像）**：地址、映射范围、频率等策略集中在配置，不散落机制。
2. **统一 HAL 层**：平台差异收在 `port`/`cpu` 两个文件；上层零改动。
3. **模块化目录归属**：`底层 / 多设备 / 桌面 / 生态` 四模块边界清晰，新增平台只需在 `多设备` 内
   增补 `boot/*` + `arch/*`，不触碰 `底层`/`桌面` 其余代码。

迁移工作量的粗略账：新增一条平台 ≈ 提供 `boot/*` 的 BootInfo 生产 + `arch/*` 的 GDT/IDT/ISR/PIC
（或等价）+ HAL 的 `port`/`cpu` 等价实现；`mm`/`lib`/`drivers`/`ui`/`kernel` 主体复用。

---

## 5. 开放治理

ZenithOS 生态以开放治理为原则（宪法第七条）：

- **许可**：源码与文档 MIT（`LICENSE.md`）；工具链（NASM/TCC/QEMU/v86/Node）不随 OS 镜像分发。
- **公开透明**：路线图、架构、安全策略、威胁模型均在仓库文档内公开，可复现构建 + SHA-256 供任何人生成同一镜像。
- **变更流程**：小改动直提 PR；架构/ABI/内存模型变更需书面提案 + 可运行原型（见 `ROADMAP.md` §如何影响本路线图）。
- **安全协作**：负责任披露流程公开（`SECURITY.md` §7），善意研究受保护。

> 治理细节随 `docs/GOVERNANCE.md` 演进；本文只声明生态的全部公开原则。多设备派生与开放治理
> 相互支撑：统一 HAL 让移植者能以最小面接入，开放流程让派生能回流上游。

---

*ZenithOS 生态 —— 一个内核主体，统一 HAL，多设备派生，开放回流。*
