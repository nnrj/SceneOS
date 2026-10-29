# SceneOS 会话交接

> 更新时间：2026-10-29  
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

3. 预期分支为 `main`，当前开发基准 HEAD 为：

   ```text
   22cae93 feat: add PS/2 mouse input pipeline
   ```

   2026-10-29 的迁移恢复和时钟诊断改动尚未提交，工作树不再干净；本交接文件
   本身也尚未跟踪。请以实际 Git 状态为准，保留这些改动。

4. 先阅读本文以及：

   - `README.md`
   - `docs/zh/project/architecture.md`
   - `docs/zh/project/roadmap.md`
   - `docs/zh/development/build-and-run.md`
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
- 固定 32 项小顶堆定时器队列，支持调度、取消和非 IRQ 上下文回调。
- 共享 i8042 控制器层。
- PS/2 Set 1 键盘：IRQ1、语义键码、左右修饰键、Caps Lock、按下/释放/重复。
- PS/2 三字节鼠标：IRQ12、相对移动、左/右/中键当前值和变化掩码。
- 固定容量统一输入事件队列，带序列号和丢失计数。

当前输入队列依赖单 BSP、可屏蔽中断 handler 不嵌套和 x86 有序内存模型，尚不
是 SMP 安全通用队列。键盘布局、Unicode 文本转换、输入法、鼠标滚轮协商、
绝对坐标设备、焦点路由和多消费者分发尚未实现。

## 最近关键提交

```text
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

### 2026-10-29 新路径验证与未提交更新

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
- 本次未 commit、未 push。`--fresh` 的使用与影响见构建文档。

## 建议下一步

优先继续 M2，不要过早进入完整桌面：

1. 评估 Local APIC timer；保留 PIT 作为校准/兼容路径。
2. 在输入核心上增加最小键盘布局与按键到 Unicode/文本事件的转换层，但不要把
   布局逻辑放进 PS/2 驱动。
3. 增加 PS/2 IntelliMouse 滚轮协商与四字节包测试。
4. 随后再决定进入 M3（线程/用户态）还是先做一个仅内核期的可见光标原型；正式
   窗口系统仍应按路线图置于用户态。

每个阶段都应同步：实现、自动化 QEMU 验收、CI、中英文项目/开发/API 文档。
仅在用户授权时本地 commit；除非用户明确要求，不推送远端。

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
