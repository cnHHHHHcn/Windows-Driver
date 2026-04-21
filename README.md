# Windows-Driver

这是一个个人 Windows 驱动开发的练手项目仓库。

## 项目内容

本项目主要包含以下几部分内容：

### 1. Driver_BasicFramework-IRP
核心练手项目。
- **基本框架**：实现了驱动的标准入口 (`DriverEntry`)、卸载例程 (`DriverUnload`)，以及设备对象与符号链接的创建流程。
- **IRP 通信**：演示了如何处理 I/O 请求包 (IRP)，包括 `IRP_MJ_CREATE`、`IRP_MJ_CLOSE`、`IRP_MJ_DEVICE_CONTROL` 等关键例程，实现内核态与用户态的数据交互。

### 2. Tools
辅助调试工具集。
- **Sysinternals Suite**：收录了 DebugView、Process Monitor 等驱动调试必备工具，方便查看内核日志和系统行为。
- **其他工具**：包含驱动加载、测试相关的辅助脚本或工具。

---

## ⚠️ 免责声明

**本项目仅用于学习和研究目的。**

内核驱动开发涉及系统底层操作，不当的代码可能导致系统蓝屏 (BSOD) 或不稳定。

> **重要提示：** 请务必在**虚拟机**中进行测试，避免在主力机上直接运行未经验证的驱动代码。
