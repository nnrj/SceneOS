# SceneOS 会话交接

> 更新时间：2026-10-31
> 新工作区：`E:\WORKSPACE\AgentWork\OSWork`  
> SceneOS 仓库：`E:\WORKSPACE\AgentWork\OSWork\SceneOS`

## 新会话首先执行

1. 确认当前目录确实是新路径，不要继续使用已失效的
   `E:\WORKSPACE\00ProjectSpace\OSWork`。
2. 在 `SceneOS` 中执行只读检查：

   ```powershell
   Get-Location
   git status --short
   git log -5 --oneline
   ```

3. 预期分支为 `master`，本次滚轮开发的基准提交为（最新 HEAD 以 Git 为准）：

   ```text
   777c1bd feat: add US keyboard text translation and UTF-8
   ```

   用户已推送此前的迁移恢复、时钟诊断和 Git 忽略规则改动（`f094ef3`）。
   Local APIC timer 与 PIC/PIT 修复为 `7e3c674`，文本输入为 `777c1bd`；
   后续滚轮与线程开发的实现、验证见下文。用户已授权完成验证后执行 `git add .` 和 `git commit`
   保存阶段性成果，不推送远端。提交与工作树状态以实际 Git 为准。

4. 先阅读本文以及：

   - `README.md`
   - `docs/zh/project/architecture.md`
   - `docs/zh/project/roadmap.md`
   - `docs/zh/development/build-and-run.md`
   - `docs/zh/development/interrupts-and-time.md`
   - `docs/zh/api/interrupt-and-time.md`
   - `docs/zh/development/input-and-ps2.md`
   - `docs/zh/api/input-events.md`

## 项目定位与已确定原则

- SceneOS 是从零实现的 x86-64 桌面操作系统；NNOS 仅作历史和行为参考，不能
  直接复制成新内核。
- 图形界面采用 Windows 风格和常见操作习惯；命令行采用 Unix/Linux 语义：
  区分大小写、路径分隔符为 `/`。
- GUI 与内核解耦，没有 GUI 时系统仍能运行；但 GUI 和命令行均是一等交互界面。
- 当前内核采用模块化单体架构，并通过清晰边界保留未来拆分服务/微内核组件的
  可能性。
- 初期只支持 x86-64；架构相关代码放在 `kernel/src/arch/x86_64`，核心接口避免
  泄漏不必要的 x86 细节，为以后 ARM64 留出边界。
- 语言为 C17 + NASM；构建采用 CMake Presets + Ninja，Clang/LLD 工具链。
- 启动使用固定版本 Limine；Windows 下 QEMU 可直接用 UEFI 虚拟 FAT 介质，
  不依赖 xorriso。发布 ISO 在 Linux/macOS 或 CI 中用 xorriso。
- 许可证为 MIT。
- AI 模块属于后期用户态系统服务/应用层，并通过结构化 JSON 操作协议、权限和
  审计与系统交互，不进入内核。
- 稳定版前允许 API 随实现同步修改；首个稳定版之后强调长期兼容。
- 动态依赖远期采用“应用私有版本优先、系统共享版本回退”的策略，避免依赖地狱。

## 当前实现状态

### M0：可复现启动骨架

已完成：

- C17/NASM freestanding x86-64 工程。
- CMake Presets、Ninja、Clang/LLD。
- 固定版本 Limine，Windows/Linux 一键 bootstrap/build/run 脚本。
- 串口日志、panic、Limine boot info、内存图摘要。
- 简单 framebuffer 启动画面；无 framebuffer 时可 headless 继续。
- QEMU UEFI 虚拟 FAT 冒烟测试和 GitHub Actions。

### M1：异常与内存（初版主体已完成）

- 自有 GDT/TSS/IDT、异常入口和寄存器现场。
- Ring 0 栈、关键异常 IST、异常与缺页诊断自测。
- 每区域位图物理页分配、释放和复用。
- SceneOS 自有 CR3、四级页表 4 KiB 映射/取消映射、地址翻译。
- 高半内核堆，支持块拆分、相邻空闲块合并、释放和复用。

仍待完成：中间页表回收、保留区登记、连续页分配、堆并发和 slab/对象缓存等。

### M2：时间、中断与输入（当前阶段）

- 8259A 兼容启动，ACPI RSDP/XSDT/RSDT/MADT/HPET 查询。
- Local APIC + IOAPIC，MADT IRQ override 路由。
- 固定 16 项 legacy IRQ 注册表；PIT、键盘和鼠标均经统一注册接口。
- PIT 100 Hz tick 与 watchdog。
- 首选 64 位 HPET 单调时钟，PIT 校准 TSC 回退，纳秒换算。
- CPUID.15H 声明频率与 PIT 实测 TSC 频率交叉校验；记录 invariant 标志、
  5% 容差、偏差及缺失/异常状态。诊断不覆盖 HPET 优先级或实测回退频率。
- 可选 BSP Local APIC timer：HPET/PIT 校准、单次/周期触发、停止、待处理中断
  清理与重新启动；专用向量 48，不占 legacy IRQ 注册项，普通启动仍使用 PIT。
- 强制 PIC 回归入口；PIC 回退在设备 IRQ 启用前关闭固件遗留的 Local APIC，
  修复兼容路径下 PIT 中断无法送达的问题。
- 固定 32 项小顶堆定时器队列，支持调度、取消和非 IRQ 上下文回调。
- 共享 i8042 控制器层。
- PS/2 Set 1 键盘：IRQ1、语义键码、左右修饰键、Caps Lock、按下/释放/重复。
- PS/2 鼠标：IRQ12、IntelliMouse ID 3 滚轮协商、ID 0 三字节兼容、九位 X/Y
  相对移动、左/右/中键当前值和变化掩码；正值滚轮向上。
- 固定容量统一输入事件队列，带序列号和丢失计数。
- 消费者侧最小 US 键盘布局、Shift/Caps 规则、Ctrl/Alt 文本抑制、派生 Unicode
  文本事件及 UTF-8 编码；文本携带原始事件序列号与重复标志，不回写硬件队列。

当前输入队列依赖单 BSP、可屏蔽中断 handler 不嵌套和 x86 有序内存模型，尚不
是 SMP 安全通用队列。更多布局、死键/AltGr、输入法与组合文本、五键/水平滚轮、
绝对坐标设备、焦点路由和多消费者分发尚未实现。普通启动没有文本框或终端，
文本转换目前由独立验收消费者使用；UTF-8 编码能力不等于中文输入法已完成。

## 最近关键提交

```text
777c1bd feat: add US keyboard text translation and UTF-8
7e3c674 feat: add BSP local timer and harden PIT/PIC paths
f094ef3 update
c8a57cb update
22cae93 feat: add PS/2 mouse input pipeline
8c35044 feat: add PS/2 keyboard input pipeline
aabc6e6 feat: add generic legacy IRQ registry
c9bd1f6 feat: update
131342d feat: update
8b3038d init
```

## 已验证命令

在 `E:\WORKSPACE\AgentWork\OSWork\SceneOS` 中：

```powershell
python tools/scenectl.py doctor
python tools/scenectl.py build --preset dev
python tools/scenectl.py build --preset release
python tools/scenectl.py smoke --timeout 10
python tools/scenectl.py smoke --exception --timeout 8
python tools/scenectl.py smoke --page-fault --timeout 8
python tools/scenectl.py smoke --tsc-clock --timeout 8
python tools/scenectl.py smoke --keyboard --timeout 10
python tools/scenectl.py smoke --mouse --timeout 10
```

截至提交 `22cae93`，以上构建和 QEMU 测试均通过。键盘测试通过 QEMU Monitor
注入 `A`，鼠标测试注入相对移动，分别验证 IRQ1/IRQ12、解码器和统一事件队列。
普通 `run.ps1` 中暂时看不到光标移动或键盘反馈，因为底层输入尚未接到 GUI。

### 2026-10-29 新路径验证（后续已由用户提交）

- 原 Dev/Release 构建目录已保留到 `build/before-relocation-20261029/`。
  新的两份 CMake 缓存均指向 `E:/WORKSPACE/AgentWork/OSWork/SceneOS`。
- 增加 `configure/build --fresh`，通过 CMake 重建所选 preset 的缓存。
  已用备份中的原始旧路径缓存实测 Dev、Release 恢复；串口历史日志和 SDK
  标记文件均保持原样。以后迁移无需手动删除整个构建目录：

  ```powershell
  python tools/scenectl.py build --preset dev --fresh
  python tools/scenectl.py build --preset release --fresh
  ```

- 修改前的新路径基线：Dev/Release 构建及六项原有 QEMU 测试全部通过，日志在
  `build/verify/baseline-20261029/`。
- 时钟改动后：Dev/Release 构建、六项原有 QEMU 测试、Dev 的 HPET/TSC 两种
  时钟边界自测，以及 Release 的 HPET 边界自测全部通过，日志在
  `build/verify/clock-20261029/`。新增验收命令：

  ```powershell
  python tools/scenectl.py smoke --clock-check --timeout 10
  python tools/scenectl.py smoke --clock-check --tsc-clock --timeout 10
  python tools/scenectl.py smoke --preset release --clock-check --timeout 10
  ```

- 测试后 Dev/Release 均恢复普通构建配置，所有自测开关及强制 TSC 开关为 OFF。
- 当前默认 QEMU CPU 未枚举可用 CPUID.15H 频率，真实诊断是 `unavailable`。
  22 组人工输入用例覆盖解析、乘积与容差等边界；这不等同于真机频率验证。
- CI 配置和中英文项目/开发/API 文档已同步；远端 CI、ISO 和真机本次未运行。
- 当前执行沙箱内 Ninja 曾两次停滞，退出本次启动的构建进程后，在沙箱外运行
  项目本地构建和有时限的无界面 QEMU 测试成功。未修改系统环境或 NNOS。
- 当日代理未 commit/push；这些改动后来已由用户提交并推送。
  `--fresh` 的使用与影响见构建文档。

### 2026-10-31 Local APIC timer 开发

- 修改前默认 Dev smoke 通过；本轮日志归档在 `build/verify/lapic-20261031/`。
- 新增 `scene/local_timer.h`、核心状态/校准/计数换算、xAPIC 硬件适配与向量 48
  入口，以及独立的自测编译单元。寄存器操作仅在架构层；启动、停止和快照操作
  保存/恢复 IF。活动定时器不能隐式覆盖，旧 pending 中断清理前拒绝重新启动。
- 增加 `smoke --lapic-timer`、`--pic` 和 panic 检查，默认所有新增开关关闭。
  `--lapic-timer --tsc-clock` 使用 PIT 校准；`--lapic-timer --pic` 验证硬件不可用
  时继续使用 PIT。该接口当前不接入调度器或软件定时器队列，也不改变 GUI。
- 首轮 PIC 测试捕获到 PIT watchdog 超时；修复后通过。失败现场保存在
  `dev-pic-run.log` / `dev-pic-serial.log`，修复后日志使用 `dev-pic-fixed-*`，
  未覆盖失败证据。
- 比较 HPET/PIT 校准结果时发现原 PIT 控制字 `0x36` 实为 mode 3，IOAPIC 路径
  的 tick 交付约为配置值的两倍。本地定时器测得约 62.4 MHz（HPET）与
  31.7 MHz（PIT），修改前基线也有同类速率现象。改用原本意图的 mode 2
  `0x34`，并增加以 HPET 实测 PIT 频率的 75%–125% 验收，防止仅有中断就通过。
  修复后 Dev 实测 PIT 约 99–101 Hz，本地定时器两种校准约 62.4/63.0 MHz。
  临时恢复旧控制字的负向验证实测 201 Hz，自测按预期失败；脚本已恢复新代码，
  证据为 `old-pit-control-negative-*`。修改前日志保留；最终回归日志放在同目录
  的 `final/` 下。
- 最终 16 组 QEMU 验收全部通过，均在修改后的代码上构建：

  | 配置 | 验证路径 | 组数 |
  | --- | --- | --- |
  | Dev / Release | `--lapic-timer`，分别使用 HPET、`--tsc-clock`、`--pic` | 6 |
  | Dev | `--exception`、`--page-fault` | 2 |
  | Dev | `--clock-check`，分别使用 HPET、`--tsc-clock` | 2 |
  | Dev | `--keyboard`、`--mouse`，分别使用 APIC、`--pic` | 4 |
  | Dev / Release | 普通 `smoke` | 2 |

- 各次命令均使用 `--timeout 10`，逐项保存 `*-run.log` 与 `*-serial.log`。
  Release 的 HPET/PIT 本地定时器校准分别约为 62.45/62.52 MHz；有 HPET 的四个
  自测中 PIT 实测为 99–101 Hz。负向用例按预期失败，不计入上述成功的 16 组。
- 最后两次普通 smoke 完成 Dev/Release 构建恢复：8 个自测/强制模式开关全为 OFF，
  两个默认串口日志都包含 `Bootstrap complete`，未残留本地定时器自测入口。
- `git diff --check`、Python 语法与 smoke 参数组合/互斥检查、双语文档结构和
  仓库文档链接检查通过。NNOS 仍仅有前述两个历史产物改动。
- CI 与中英文项目/开发/API 文档已同步。
- 短采样只用于初期评估；长期精度、ARAT/深度省电、SMP、TSC-deadline 和
  调度/睡眠唤醒集成尚未验证或实现。远端 CI、ISO 与真机本轮未运行。
- 本阶段已本地提交为 `7e3c674`，未推送远端。

### 2026-10-31 键盘布局与文本输入

- 新增 `scene/text_input.h` 与 `core/text_input.c`。转换是消费者调用的纯函数，
  当前仅支持显式 US 布局；不修改 PS/2 驱动或原始输入队列结构。
- 字母使用 Shift/Caps 异或，数字与标点只受 Shift 影响，保留重复与来源序列。
  Ctrl/Alt、释放、编辑/导航动作不产生文本；无效参数不修改输出。UTF-8 编码
  支持合法 Unicode 标量，拒绝代理码点、越界值和短缓冲区，且不附加终止符。
- `smoke --text-input --timeout 15` 增加独立自测：字母/修饰键组合、符号与重复、
  错误参数、UTF-8 标量与容量边界；QEMU Monitor 按就绪标记注入 15 组按下/
  释放动作，要求文本 `aAAa!b Z`、八个文本事件、正确来源序列和零丢失。
- 输入注入改为完整串口行匹配，避免步骤 1/10 冲突；未就绪不发送，错误退出时
  清理本次 QEMU。新增 `SCENEOS_TEXT_INPUT_SELF_TEST` 默认 OFF，普通配置显式
  重置为 OFF。CI 已增加 APIC、PIC、TSC 与 Release 文本验收步骤。
- 本阶段日志放在 `build/verify/text-input-20261031/`。14 组 QEMU 验收全部通过：

  | 配置 | 验证路径 | 组数 |
  | --- | --- | --- |
  | Dev | `--text-input`，分别使用默认 HPET、`--pic`、`--tsc-clock` | 3 |
  | Release | `--text-input`，分别使用默认 APIC、`--pic` | 2 |
  | Dev | `--keyboard`、`--mouse`，分别使用 APIC、`--pic` | 4 |
  | Dev | `--page-fault`、`--clock-check --tsc-clock`、`--lapic-timer` | 3 |
  | Dev / Release | 普通 `smoke` | 2 |

- 文本用例使用 `--timeout 15`，其余用例为 10 秒；各次命令和串口输出分别保存
  为 `*-run.log` / `*-serial.log`。五个文本路径都完成 15 组按下/释放，产生相同
  的八字符结果且队列零丢失。重复标志与非 ASCII 标量的编码由合成边界用例覆盖。
- 单独强制未达到注入标记的超时失败，验证本次启动的 QEMU 被回收、stdin 关闭；
  使用现有镜像，不改变正常构建缓存，日志在 `negative-timeout/result.log`。
- 最后的 Dev/Release 普通 smoke 通过；两套缓存共各 9 个自测/强制开关均 OFF。
  Python 语法、文本模式参数组合/互斥、双语文档结构及内部链接、`git diff --check`
  检查通过。原始输入队列与 PS/2 驱动未改，NNOS 的历史产物保持原状态。
- 中英文项目/开发/API 文档同步；此阶段不包含 GUI 文本控件、中文输入法、
  UTF-8 解码、Unicode 规范化、真机或远端 CI 验证。
- 按用户授权在完成验证后本地提交本阶段成果；提交记录以 Git 为准，不 push。

### 2026-10-31 IntelliMouse 滚轮与位移修复

- 鼠标启动默认按 200/100/80 探测并查询实际 ID，0/3 分别选择三/四字节；未知
  ID 或通信失败停止初始化。协议层与端口操作分开，命令/数据轮询拒绝 IF 开启
  时调用。成功注册 IRQ12 后才开启流模式，失败时注销本次 handler。
- 修复旧 X/Y `(int8_t)` 转换丢失包头符号位的问题，覆盖 -256 到 255 的九位值；
  ID 3 的第四字节按完整有符号八位解析，输入事件滚轮正值向上。保留溢出包丢弃、
  部分包跨 IRQ、三键快照、无变化包过滤和固定容量队列。
- 新增 `smoke --mouse-wheel`、`--mouse-basic`，均支持 PIC/TSC/Release。自测涵盖
  协商每一步失败、ID 0 回退、未知 ID、X/Y 和滚轮全值域、按键状态转换与溢出包；
  实际 QEMU 依次注入移动、三键按下/释放和双向滚轮，要求 9 / 7 个事件且零丢失。
- 基线通过，最终 13 组 QEMU 回归全部通过，日志在
  `build/verify/mouse-wheel-20261031/final/`：

  | 配置 | 路径 | 组数 |
  | --- | --- | --- |
  | Dev | `--mouse-wheel`，分别使用 APIC、PIC、TSC | 3 |
  | Release | `--mouse-wheel`，分别使用 APIC、PIC | 2 |
  | Dev / Release | `--mouse-basic`（Dev 另加 PIC） | 3 |
  | Dev | `--text-input`，分别使用 APIC、PIC | 2 |
  | Dev | 原 `--mouse` | 1 |
  | Dev / Release | 普通 `smoke` | 2 |

- 最终用例均为 15 秒上限。首轮测试发现 HMP dz 的正值实际表示向上，已按 QEMU
  官方实现校正测试注入，驱动方向契约不变。初次失败证据为父目录 `dev-wheel-*`；
  `dev-wheel-diagnostic-run.log` 是诊断日志函数名误写导致的编译失败，其同名
  serial 文件是前一次运行副本，不作为独立运行证据。
- 临时恢复旧八位 X 解码的负向测试被边界用例按预期拒绝，证据为父目录
  `old-sign-negative-*`。脚本 finally 恢复正确源文件并重新执行普通 Dev smoke。
- CI 与中英文项目/开发/API 文档同步；Python 语法、参数互斥、文档结构与链接、
  `git diff --check` 通过。正常配置会关闭 11 个自测/强制模式开关。
- QEMU 四字节路径与跳过探测的三字节路径已验证；不支持滚轮的设备对探测返回
  ID 0 的场景由脚本化传输覆盖，不等于真机测试。Explorer ID 4、水平滚轮、
  热插拔和丢字节恢复仍待实现。未运行远端 CI、ISO 或真机。

## 建议下一步

继续进入 M3 的内核线程基础，不要过早进入完整桌面：

1. 实现 BSP 协作式内核线程、独立保护栈、软件上下文切换与退出回收，增加 QEMU
   调度/寄存器/栈隔离验收，再推进抢占、等待队列及用户态。
2. 正式窗口系统仍按路线图置于用户态；当前不插入内核期可见光标原型。
3. 文本消费者/终端形成时，再按交互需要选择布局、焦点和编辑动作策略；不把
   输入法、组合文本或布局规则放入 PS/2 驱动。
4. 进入调度/唤醒阶段时，再将本地定时器接入时钟事件源选择与 deadline 编程；
   当前保留 PIT 默认路径，不因可选自测通过就承诺跨 CPU 或电源状态可靠性。

每个阶段都应同步：实现、自动化 QEMU 验收、CI、中英文项目/开发/API 文档。
用户已授权在适当阶段执行 `git add .` 与 `git commit`，完成必要验收后可直接
提交阶段成果；不执行 push。用户要求持续推进，不要每个阶段结束就停下等待确认。

## 安全与仓库注意事项

- `OSWork` 下还有 `nnos` 历史仓库。不要执行其中的环境部署脚本，尤其是：
  `nnos/tool/04Oneykey.bat`、`nnos/tool/03部署环境.bat`，它们会修改环境变量或
  复制工具到系统位置。
- 不要运行 NNOS 的旧构建/运行脚本，除非用户明确要求且先审阅副作用。
- NNOS 当前已有两个历史未提交编译产物，保持原状：

  ```text
   M src/nnos/nnos.img
   M src/tools/qemu/fdimage0.bin
  ```

- SceneOS 的 bootstrap 只允许把固定版本依赖放在项目内 `.scene-sdk/`，不得修改
  系统 PATH、注册表、shell profile 或全局工具目录。
- 自动部署环境脚本若未来需要测试，应让用户在隔离环境运行，不要直接在物理机
  执行高副作用安装逻辑。
- 构建产物位于 `build/`，不要提交。
- 修改文档时维持 `docs/zh` 与 `docs/en` 的结构和内容对等。

## 新会话接手检查清单

- [ ] 当前 cwd 是新路径 `E:\WORKSPACE\AgentWork\OSWork\SceneOS`。
- [ ] `git status --short` 已检查，未覆盖用户改动。
- [ ] HEAD 与本文记录核对；若更晚，以 Git 和现有文档为准。
- [ ] 已阅读路线图、架构、构建和输入文档。
- [ ] 未执行 NNOS 危险脚本，未修改其两个历史产物。
- [ ] 开始修改前先跑一次默认 Dev build/smoke 建立新路径基线。
- [ ] 完成后跑与风险相称的全部回归，更新中英文文档并仅在用户授权时 commit。
