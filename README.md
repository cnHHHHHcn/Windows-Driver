# Windows-Driver

这是一个个人 Windows 驱动开发的练手项目仓库。

每个练习都按「内核态驱动 + 用户态 (R3) 测试程序」成对实现，内容覆盖驱动入口与设备对象、IRP 分发与设备栈附加、内核内存池与内核对象，以及键盘过滤驱动。

## 目录结构

| 目录 | 说明 |
| --- | --- |
| `Driver_BasicFrame-IRP` | 基础框架与 IRP / IOCTL 通信 |
| `Driver_KernelMemoryPool` | 内核内存池与池头结构解析 |
| `IRP_Dispatch_Workflow` | 设备栈构建与 IRP 分发流程 |
| `KeyboardFilter` | 键盘过滤驱动（按键拦截与记录） |
| `ObjectTest` | 内核对象与内核 API 练习 |
| `Tool` | 调试与测试辅助工具 |

## 项目内容

### Driver_BasicFrame-IRP
核心入门项目：驱动标准骨架 + 最简单的内核态/用户态双向通信。
- **基本框架**：实现驱动的标准入口 (`DriverEntry`)、卸载例程 (`DriverUnload`)，以及设备对象 (`\Device\cnHHHHHcn`) 与符号链接 (`\??\cnHHHHHcn`) 的创建与释放流程，设备以 `DO_BUFFERED_IO` 方式工作。
- **IRP 通信**：注册并实现 `IRP_MJ_CREATE`、`IRP_MJ_CLOSE`、`IRP_MJ_DEVICE_CONTROL` 等关键例程，通过缓冲 I/O (`SystemBuffer`) 实现内核态与用户态的数据交互；另外还实现了按 `MajorFunction` 索引转发的通用分发函数 `DispatchRoute`，作为另一种分发写法的参考。
- **用户态测试**：`DevieceControl` (C++) 用 `CreateFileA` 打开设备，通过 `IOCTL_SEND_DATA` (0x800) 下发字符串，再用 `IOCTL_RECEIVE_DATA` (0x801) 取回驱动返回的数据。

### Driver_KernelMemoryPool
内核非分页/分页池结构深度解析。
- **智能内存分配**：封装了 AllocMemory 函数，根据分配大小（>4KB）与当前 IRQL 自动在 NonPagedPool / PagedPool 之间选择，并包含防呆校验与调试打印。
- **池头结构解析**：直接操作内存指针，解析并打印 NON_POOL_HEADER 结构，验证了 Pool Header 中的 PreviousSize、PoolIndex、BlockSize、PoolType、PoolTag 及 ProcessBilled 等关键字段的内存布局。
- **大页内存机制**：演示了大内存（>2MB）分配时的 BIG_PAGE_POOL_HEADER 结构特性，通过 DbgBreakPoint 中断到调试器后再安全分析大页池元数据，避免直接访问非法地址导致的蓝屏（BugCheck 0x50，PAGE_FAULT_IN_NONPAGED_AREA）。

### IRP_Dispatch_Workflow
设备栈构建与 IRP 分发流程演示。
- **设备栈构建**：创建符号设备 `\Device\TestDevice`（链接 `\DosDevices\TestDevice`），再创建 `TestDevice0 ~ TestDevice{N-1}` 一串测试设备，用 `IoAttachDeviceToDeviceStack` 逐层附加，最后把符号设备挂到栈顶；卸载时反向 `IoDetachDevice` / `IoDeleteDevice`。
- **IRP 分流**：所有 `MajorFunction` 默认走 `DispatchRoutine`，分发时打印设备对象指针与 MajorFunction 名称，并按位置分流——栈底设备直接完成 IRP；被指定的中间层设备注册完成例程 `CompleteCallBack` 后继续下发，由完成例程覆写返回状态；其余设备 `IoSkipCurrentIrpStackLocation` 后继续下发；没有下层设备时返回 `STATUS_INVALID_DEVICE_REQUEST`。释放流程中还会对已释放设备的 IRP 返回 `STATUS_DELETE_PENDING`。
- **控制码**：`IOCTL_READ_KRNLDATA` (0x800) 读取内核设备栈信息，`IOCTL_SET_COMPLETION_DEVICE` (0x801) 指定完成例程设备，`IOCTL_INIT_DEVICE_STACK` (0x802) 按指定数量重建设备栈，`IOCTL_RELE_DEVICE_STACK` (0x803) 释放设备栈。
- **用户态测试**：交互式命令行工具，支持 `ini` / `get` / `set` / `rd` / `wr` / `ctl` / `rel` / `run` / `ext` 等命令，可单独下发 `IRP_MJ_READ`、`IRP_MJ_WRITE` 与未知 IOCTL，观察 IRP 在设备栈中的逐层分发结果。

### KeyboardFilter
键盘过滤驱动（附加在 kbdclass 之上）。
- **设备附着与移除**：遍历 `\Device\KeyboardClass0 ~ N`，为每个键盘设备创建过滤设备并 `IoAttachDeviceToDeviceStack` 附加；配合 `IO_REMOVE_LOCK` 与 PnP / Power 例程，保证驱动卸载与设备移除时的安全。
- **按键拦截与记录**：在 `IRP_MJ_READ` 上挂完成例程 `ReadCompleteRoutine`，逐条解析 `KEYBOARD_INPUT_DATA`，按扫描码表决定放行还是拦截，并记录被拦截的扫描码、按需输出按键日志。控制设备为 `\Device\cnHHHHHcn_KeyboardFilter`。
- **用户态测试**：`KeyboardFilter_User` 通过 `\\.\cnHHHHHcn_KeyboardFilter` 开关过滤 (`IOCTL_ENABLE_FILTER`)、开关日志 (`IOCTL_ENABLE_LOG`)、增删拦截按键 (`IOCTL_ORDER_ADDKEY` / `IOCTL_ORDER_DELKEY`)，并读取内核数据 (`IOCTL_READ_KRNLDATA`)。

### ObjectTest
内核对象与内核 API 练习，仓库中保留三份独立入口源码，编译时按需加入工程（保证只有一个 `DriverEntry`）。
- **File_entry.c**：`ZwCreateFile` / `ZwQueryInformationFile` / `ZwReadFile` / `ZwWriteFile` / `ZwClose` 的内核态文件操作，把源文件读入 `NonPagedPool` 缓冲后再写入目标文件。
- **Register_entry.c**：`ZwCreateKey` / `ZwSetValueKey` / `ZwQueryValueKey` / `ZwEnumerateValueKey` / `ZwEnumerateKey` / `ZwDeleteValueKey` / `ZwDeleteKey` 的注册表读写与枚举，并把注册表数据按「16 字节一行 + 十六进制 + ASCII」的形式 dump 出来；各步骤之间用 `DbgBreakPoint` 便于分步观察。
- **Segment_entry.c**：通过 `GetSegment` 读取 `CS` / `SS` / `DS` / `ES` / `FS` / `GS` 段选择子与 `GDTR`，解析 `RPL` / `TI` / `Index` 以及 `SEGMENT_DESCRIPTOR`、`CALLGATE_DESCRIPTOR` 的位域布局。

### Tool
辅助调试工具集。
- **Sysinternals Suite**：收录了 DebugView、Process Monitor、PsExec、ProcDump 等驱动调试必备工具，方便查看内核日志和系统行为。
- **WinDbg_DbgPrint Attach.reg**：注册表脚本，用于让 WinDbg 正常接收 `DbgPrint` 输出。
- **wdksetup.exe**：WDK 安装包。
- **环境辅助**：`关闭或者打开windows Defender.zip`、`DefenderRemover.zip`、`禁止系统更新.zip` 等系统设置脚本，以及 `驱动精灵 v9.2（绿色免安装版）.exe`、`KMS_激活工具.rar`；`网址.txt` 记录相关下载地址。
##### 注：RDCMan (Remote Desktop Connection Manager)
远程桌面管理工具，用于管理多台测试虚拟机。
- **文件说明**：`SysinternalsSuite\RDCMan-x96` 为分卷压缩包（`RDCMan-x96.zip` + `RDCMan-x96.z01 ~ z05`）。
- **包含内容**：解压后包含 `RDCMan.exe` (主程序) 和 `RDCMan-x64.exe` (64位版本)。

## 编译与运行

- 驱动工程使用 Visual Studio + WDK 编译，输出 x64 `.sys`；用户态测试程序为普通 C++ 控制台工程。
- 建议在内核调试环境下运行（WinDbg 双机调试 / 虚拟机 + DebugView 查看 `DbgPrint` 输出）。
- 未签名的驱动需要开启测试签名模式（`bcdedit /set testsigning on`）后才能加载。

## License

本项目基于 [MIT License](LICENSE) 开源。

---

## ⚠️ 免责声明

**本项目仅用于学习和研究目的。**

内核驱动开发涉及系统底层操作，不当的代码可能导致系统蓝屏 (BSOD) 或不稳定。

> **重要提示：** 请务必在**虚拟机**中进行测试，避免在主力机上直接运行未经验证的驱动代码。
