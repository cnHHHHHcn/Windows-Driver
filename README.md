# Windows-Driver

这是一个个人 Windows 驱动开发的练手项目仓库。

## 项目内容

本项目主要包含以下几部分内容：

### Driver_BasicFramework-IRP
核心练手项目。
- **基本框架**：实现了驱动的标准入口 (`DriverEntry`)、卸载例程 (`DriverUnload`)，以及设备对象与符号链接的创建流程。
- **IRP 通信**：演示了如何处理 I/O 请求包 (IRP)，包括 `IRP_MJ_CREATE`、`IRP_MJ_CLOSE`、`IRP_MJ_DEVICE_CONTROL` 等关键例程，实现内核态与用户态的数据交互。

### Driver_KernelMemoryPool
核心练手项目。
- **内核非分页/分页池结构深度解析**
  - 智能内存分配：封装了 AllocMemory 函数，根据分配大小（>4KB）自动判断 IRQL 级别，智能选择 NonPagedPool 或 PagedPool，并包含防呆校验与调试打印。
  - 池头结构解析：直接操作内存指针，解析并打印 NON_POOL_HEADER 结构，验证了 Pool Header 中的 BlockSize、PoolTag 及 ProcessBilled 等关键字段的内存布局。
  - 大页内存机制：演示了大内存（>2MB）分配时的 BIG_PAGE_POOL_HEADER 结构特性，通过 DbgBreakPoint 中断展示了如何安全分析大页池元数据，避免非法访问导致的蓝屏（BugCheck 0x50）。

### Tools
辅助调试工具集。
- **Sysinternals Suite**：收录了 DebugView、Process Monitor 等驱动调试必备工具，方便查看内核日志和系统行为。
- **其他工具**：包含驱动加载、测试相关的辅助脚本或工具。
##### 注：RDCMan (Remote Desktop Connection Manager)
远程桌面管理工具，用于管理多台测试虚拟机。
- **文件说明**：`RDCMan-x96` 为分卷压缩包。
- **包含内容**：解压后包含 `RDCMan.exe` (主程序) 和 `RDCMan-x64.exe` (64位版本)。
---

## ⚠️ 免责声明

**本项目仅用于学习和研究目的。**

内核驱动开发涉及系统底层操作，不当的代码可能导致系统蓝屏 (BSOD) 或不稳定。

> **重要提示：** 请务必在**虚拟机**中进行测试，避免在主力机上直接运行未经验证的驱动代码。
