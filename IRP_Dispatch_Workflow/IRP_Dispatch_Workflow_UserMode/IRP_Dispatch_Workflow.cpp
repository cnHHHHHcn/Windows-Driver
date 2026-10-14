#define _CRT_SECURE_NO_WARNINGS

#include <iostream>
#include <ctype.h>
#include <windows.h>

// =======================================================================================================
// IRP_Dispatch_Workflow : DriverTest.sys 的用户态(R3)测试工具
//
// DriverTest 驱动加载时建立如下的设备栈(见 entry.c 顶部注释):
//     SymbolicDevice(\\.\TestDevice) -> TestDevice{N-1} -> ... -> TestDevice1 -> TestDevice0
//
// 驱动分发 IRP 时的行为(entry.c / DispatchRoutine):
//     * 栈底设备 TestDevice0            : 直接完成 IRP (STATUS_SUCCESS, Information = 0)
//     * 完成例程设备(中间层设备)         : 注册完成例程后继续下发, IRP 返回时由 CompleteCallBack 覆写状态
//     * 其它设备(含符号设备)             : IoSkipCurrentIrpStackLocation 后继续下发
//     * 没有下层设备(pLowerDevice 为空)  : 直接返回 STATUS_INVALID_DEVICE_REQUEST
//
// 控制码与驱动中的定义保持一致:
//     0x800 IOCTL_READ_KRNLDATA           读取内核态设备栈信息
//     0x801 IOCTL_SET_COMPLETION_DEVICE   指定中间层设备作为完成例程设备
//     0x802 IOCTL_INIT_DEVICE_STACK       按指定数量重建设备栈
//     0x803 IOCTL_RELE_DEVICE_STACK       释放设备栈
// =======================================================================================================

#define DEVICE_PATH                 "\\\\.\\TestDevice"
#define SYMBOLIC_LINK_NAME          L"\\DosDevices\\TestDevice"

#define IOCTL_READ_KRNLDATA             CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SET_COMPLETION_DEVICE     CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_INIT_DEVICE_STACK         CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_RELE_DEVICE_STACK         CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_UNKNOWN_REQUEST           CTL_CODE(FILE_DEVICE_UNKNOWN, 0x8FF, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define MAX_DEVICE_COUNT    64
#define CONTEXT_BUFFER_SIZE (sizeof(DEVICE_CONTROL_CONTEXT) + sizeof(PVOID) * (MAX_DEVICE_COUNT - 1))

// 与驱动中的 DEVICE_CONTROL_CONTEXT 保持二进制一致:
// 内核态存放的是 DEVICE_OBJECT 指针, 这里用 PVOID 保证指针宽度与平台一致
typedef struct _DEVICE_CONTROL_CONTEXT {
    DWORD Size;              // 结构体总大小(含设备数组)
    DWORD DeviceCount;       // 设备栈中的设备数量
    PVOID pCompletionDevice; // 完成例程设备指针
    PVOID pDeviceArray[1];   // 设备对象数组, 下标 0 为栈底设备
} DEVICE_CONTROL_CONTEXT, *PDEVICE_CONTROL_CONTEXT;

// 缓存最近一次从内核读取到的设备栈信息
typedef struct _WORKFLOW_STATE {
    DWORD DeviceCount;
    PVOID pCompletionDevice;
    PVOID pDeviceArray[MAX_DEVICE_COUNT];
} WORKFLOW_STATE, *PWORKFLOW_STATE;

typedef struct _ORDER_TABLE {
    const char* Name;
    const char* Comment;
} ORDER_TABLE, *PORDER_TABLE;

#define ORDER_COUNT 9
#define ORDER_EXIT  (-1)
#define ORDER_NONE  (-2)

enum ORDER_ID {
    ORDER_INIT = 0,
    ORDER_GET,
    ORDER_SET,
    ORDER_SEND_READ,
    ORDER_SEND_WRITE,
    ORDER_SEND_CTL,
    ORDER_RELEASE,
    ORDER_RUN
};

const ORDER_TABLE gOrderTable[ORDER_COUNT] = {
    { "ini", "Init device stack (rebuild the stack with N test devices)" },
    { "get", "Read kernel data (show the device stack layout)" },
    { "set", "Set completion device (pick a middle layer: index 1 - N-2)" },
    { "rd",  "Send an IRP_MJ_READ down the device stack" },
    { "wr",  "Send an IRP_MJ_WRITE down the device stack" },
    { "ctl", "Send an unknown IOCTL to the symbolic device" },
    { "rel", "Release the device stack" },
    { "run", "Run the whole IRP dispatch workflow in one go" },
    { "ext", "Exit Process" }
};

int main();

int   GetOrder();
BOOL  ReadLine(char* Buffer, size_t Size);
BOOL  ReadDword(const char* Prompt, DWORD* pValue, DWORD MinValue, DWORD MaxValue);

const char* GetErrorText(DWORD ErrorCode);
const char* GetStatusHint(DWORD ErrorCode);
void  PrintErrorHint(DWORD ErrorCode);
PVOID* GetDeviceArray(PDEVICE_CONTROL_CONTEXT pContext);

HANDLE InitTestDevice();
void   ReleaseTestDevice(HANDLE hDevice);

void   PrintIoctlResult(const char* IoctlName, BOOL Result, DWORD BytesReturned, DWORD ErrorCode);
void   PrintStackLayout(PDEVICE_CONTROL_CONTEXT pContext, DWORD BytesReturned);

BOOL   ReadKernelData(HANDLE hDevice, PDEVICE_CONTROL_CONTEXT pContext, PWORKFLOW_STATE pState);
BOOL   InitDeviceStack(HANDLE hDevice, PDEVICE_CONTROL_CONTEXT pContext, PWORKFLOW_STATE pState, DWORD DeviceCount);
BOOL   SetCompletionDevice(HANDLE hDevice, PDEVICE_CONTROL_CONTEXT pContext, PWORKFLOW_STATE pState, DWORD DeviceIndex);
BOOL   ReleaseDeviceStack(HANDLE hDevice, PDEVICE_CONTROL_CONTEXT pContext, PWORKFLOW_STATE pState);
BOOL   SendIrpRead(HANDLE hDevice);
BOOL   SendIrpWrite(HANDLE hDevice);
BOOL   SendUnknownIoctl(HANDLE hDevice, PDEVICE_CONTROL_CONTEXT pContext);
BOOL   RunDispatchWorkflow(HANDLE hDevice, PDEVICE_CONTROL_CONTEXT pContext, PWORKFLOW_STATE pState);


int main() {
    HANDLE hDevice = NULL;
    int Order = ORDER_NONE;
    WORKFLOW_STATE State = { 0 };
    PDEVICE_CONTROL_CONTEXT pDevCtrlContext = NULL;

    hDevice = InitTestDevice();
    if (hDevice == NULL) {
        system("pause");
        return 0;
    }

    pDevCtrlContext = (PDEVICE_CONTROL_CONTEXT)malloc(CONTEXT_BUFFER_SIZE);
    if (pDevCtrlContext == NULL) {
        printf("Allocate device control context failed\r\n");
        ReleaseTestDevice(hDevice);
        system("pause");
        return 0;
    }

    while (true) {
        Order = GetOrder();
        if (Order == ORDER_EXIT) break;

        printf("\r\n");
        switch (Order) {
        case ORDER_INIT: {
            DWORD DeviceCount = 0;
            if (ReadDword("DeviceCount (3 - 64)> ", &DeviceCount, 3, MAX_DEVICE_COUNT)) {
                InitDeviceStack(hDevice, pDevCtrlContext, &State, DeviceCount);
            }
            break;
        }
        case ORDER_GET:
            ReadKernelData(hDevice, pDevCtrlContext, &State);
            break;
        case ORDER_SET: {
            DWORD DeviceIndex = 0;
            char Prompt[64] = { 0 };
            if (State.DeviceCount == 0) {
                ReadKernelData(hDevice, pDevCtrlContext, &State);
            }
            if (State.DeviceCount == 0) {
                printf("Device stack is not available, run 'ini' first.\r\n");
                break;
            }
            sprintf_s(Prompt, sizeof(Prompt), "Completion device index (0 - %u)> ", State.DeviceCount - 1);
            if (ReadDword(Prompt, &DeviceIndex, 0, State.DeviceCount - 1)) {
                SetCompletionDevice(hDevice, pDevCtrlContext, &State, DeviceIndex);
            }
            break;
        }
        case ORDER_SEND_READ:
            SendIrpRead(hDevice);
            break;
        case ORDER_SEND_WRITE:
            SendIrpWrite(hDevice);
            break;
        case ORDER_SEND_CTL:
            SendUnknownIoctl(hDevice, pDevCtrlContext);
            break;
        case ORDER_RELEASE:
            ReleaseDeviceStack(hDevice, pDevCtrlContext, &State);
            break;
        case ORDER_RUN:
            RunDispatchWorkflow(hDevice, pDevCtrlContext, &State);
            break;
        default:
            break;
        }

        printf("\r\n");
        system("pause");
    }

    if (State.DeviceCount != 0) {
        ReleaseDeviceStack(hDevice, pDevCtrlContext, &State);
    }
    free(pDevCtrlContext);
    ReleaseTestDevice(hDevice);
    return 0;
}


int GetOrder() {
    char Order[16] = { 0 };
    size_t i = 0;

    system("cls");
    printf("IRP_Dispatch_Workflow\ttest tool for DriverTest.sys\r\n");
    printf("Device: %s\r\n\r\n", DEVICE_PATH);
    printf("Order\t\tComment\r\n");
    for (i = 0; i < ORDER_COUNT; i++) {
        printf("%s\t\t%s\r\n", gOrderTable[i].Name, gOrderTable[i].Comment);
    }
    printf("\r\nOrder>");

    if (!ReadLine(Order, sizeof(Order))) return ORDER_EXIT;
    for (i = 0; i < strlen(Order); i++) {
        Order[i] = (char)tolower((unsigned char)Order[i]);
    }
    for (i = 0; i < ORDER_COUNT; i++) {
        if (strcmp(Order, gOrderTable[i].Name) == 0) {
            // 最后一项是退出命令
            return (i + 1 == ORDER_COUNT) ? ORDER_EXIT : (int)i;
        }
    }
    return ORDER_NONE;
}

BOOL ReadLine(char* Buffer, size_t Size) {
    size_t Length = 0;

    if (fgets(Buffer, (int)Size, stdin) == NULL) return FALSE;
    Length = strlen(Buffer);
    while (Length > 0 && (Buffer[Length - 1] == '\n' || Buffer[Length - 1] == '\r')) {
        Buffer[--Length] = '\0';
    }
    return TRUE;
}

BOOL ReadDword(const char* Prompt, DWORD* pValue, DWORD MinValue, DWORD MaxValue) {
    char Line[32] = { 0 };
    char* pEnd = NULL;
    unsigned long Value = 0;

    while (true) {
        printf("%s", Prompt);
        if (!ReadLine(Line, sizeof(Line))) return FALSE;
        pEnd = Line;
        Value = strtoul(Line, &pEnd, 0);
        if (pEnd != Line && *pEnd == '\0' && Value >= MinValue && Value <= MaxValue) {
            *pValue = (DWORD)Value;
            return TRUE;
        }
        printf("Invalid value, expected %u - %u (decimal or 0x hex).\r\n", MinValue, MaxValue);
    }
}


const char* GetErrorText(DWORD ErrorCode) {
    static char szBuffer[256] = { 0 };
    DWORD Length = 0;

    szBuffer[0] = '\0';
    Length = FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_MAX_WIDTH_MASK,
        NULL, ErrorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), szBuffer, (DWORD)sizeof(szBuffer), NULL);
    if (Length == 0) {
        sprintf_s(szBuffer, sizeof(szBuffer), "Win32 error 0x%08X", ErrorCode);
        return szBuffer;
    }
    while (Length > 0 && (szBuffer[Length - 1] == '\r' || szBuffer[Length - 1] == '\n' || szBuffer[Length - 1] == ' ')) {
        szBuffer[--Length] = '\0';
    }
    return szBuffer;
}

const char* GetStatusHint(DWORD ErrorCode) {
    switch (ErrorCode) {
    case ERROR_INVALID_FUNCTION:
        return "STATUS_INVALID_DEVICE_REQUEST(0xC0000010): no lower device to forward the IRP, or an unsupported request";
    case ERROR_INVALID_PARAMETER:
        return "STATUS_INVALID_PARAMETER(0xC000000D): the test device stack is not created";
    case ERROR_INSUFFICIENT_BUFFER:
        return "STATUS_BUFFER_TOO_SMALL(0xC0000023): the output buffer is too small";
    case ERROR_NOT_ENOUGH_MEMORY:
        return "STATUS_INSUFFICIENT_RESOURCES(0xC000009A): the driver failed to allocate a non paged pool";
    case ERROR_ACCESS_DENIED:
        return "STATUS_ACCESS_DENIED(0xC0000022): run this tool as administrator";
    default:
        return "";
    }
}

void PrintErrorHint(DWORD ErrorCode) {
    const char* Hint = GetStatusHint(ErrorCode);
    if (Hint[0] != '\0') printf("    -> %s\r\n", Hint);
}

PVOID* GetDeviceArray(PDEVICE_CONTROL_CONTEXT pContext) {
    return (PVOID*)((PCHAR)pContext + sizeof(DEVICE_CONTROL_CONTEXT) - sizeof(PVOID));
}


HANDLE InitTestDevice() {
    HANDLE hDevice = CreateFileA(DEVICE_PATH, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hDevice == INVALID_HANDLE_VALUE) {
        DWORD ErrorCode = GetLastError();
        printf("Open %s failed: %s\r\n", DEVICE_PATH, GetErrorText(ErrorCode));
        if (ErrorCode == ERROR_FILE_NOT_FOUND) {
            printf("    -> DriverTest.sys is not loaded (symbolic link %ws is missing).\r\n", SYMBOLIC_LINK_NAME);
        }
        else {
            PrintErrorHint(ErrorCode);
        }
        return NULL;
    }
    printf("Open %s success\r\n", DEVICE_PATH);
    return hDevice;
}

void ReleaseTestDevice(HANDLE hDevice) {
    if (hDevice == NULL || hDevice == INVALID_HANDLE_VALUE) return;
    CloseHandle(hDevice);
    printf("Device %s closed\r\n", DEVICE_PATH);
}


void PrintIoctlResult(const char* IoctlName, BOOL Result, DWORD BytesReturned, DWORD ErrorCode) {
    if (Result) {
        printf("%s: success, %u byte(s) returned\r\n", IoctlName, BytesReturned);
        return;
    }
    printf("%s: failed, %s\r\n", IoctlName, GetErrorText(ErrorCode));
    PrintErrorHint(ErrorCode);
}

void PrintStackLayout(PDEVICE_CONTROL_CONTEXT pContext, DWORD BytesReturned) {
    PVOID* pDeviceArray = GetDeviceArray(pContext);
    DWORD i = 0;

    printf("Kernel data:\r\n");
    printf("  ContextSize   : %u (0x%X), bytes returned: %u\r\n", pContext->Size, pContext->Size, BytesReturned);
    printf("  DeviceCount   : %u\r\n", pContext->DeviceCount);
    printf("  CompletionDev : %p%s\r\n", pContext->pCompletionDevice,
        (pContext->pCompletionDevice == NULL) ? " (not set)" : "");
    printf("  DeviceArray   :\r\n");
    for (i = 0; i < pContext->DeviceCount && i < MAX_DEVICE_COUNT; i++) {
        BOOL IsCompletion = (pContext->pCompletionDevice != NULL && pDeviceArray[i] == pContext->pCompletionDevice);
        printf("    [%2u] TestDevice%u\t%p%s%s%s\r\n", i, i, pDeviceArray[i],
            (i == 0) ? "  <- lower bound" : "",
            ((i + 1) == pContext->DeviceCount) ? "  <- upper bound" : "",
            IsCompletion ? "  <- completion device" : "");
    }
}


BOOL ReadKernelData(HANDLE hDevice, PDEVICE_CONTROL_CONTEXT pContext, PWORKFLOW_STATE pState) {
    DWORD BytesReturned = 0;
    BOOL Result = FALSE;
    DWORD ErrorCode = 0;
    PVOID* pDeviceArray = NULL;
    DWORD i = 0;

    RtlZeroMemory(pContext, CONTEXT_BUFFER_SIZE);
    Result = DeviceIoControl(hDevice, IOCTL_READ_KRNLDATA, NULL, 0, pContext, (DWORD)CONTEXT_BUFFER_SIZE, &BytesReturned, NULL);
    ErrorCode = GetLastError();
    PrintIoctlResult("IOCTL_READ_KRNLDATA", Result, BytesReturned, ErrorCode);
    if (!Result) {
        // 内核态没有可用的设备栈信息, 缓存一并清空
        RtlZeroMemory(pState, sizeof(WORKFLOW_STATE));
        return FALSE;
    }

    PrintStackLayout(pContext, BytesReturned);

    RtlZeroMemory(pState, sizeof(WORKFLOW_STATE));
    pState->DeviceCount = pContext->DeviceCount;
    pState->pCompletionDevice = pContext->pCompletionDevice;
    pDeviceArray = GetDeviceArray(pContext);
    for (i = 0; i < pContext->DeviceCount && i < MAX_DEVICE_COUNT; i++) {
        pState->pDeviceArray[i] = pDeviceArray[i];
    }
    return TRUE;
}

BOOL InitDeviceStack(HANDLE hDevice, PDEVICE_CONTROL_CONTEXT pContext, PWORKFLOW_STATE pState, DWORD DeviceCount) {
    DWORD BytesReturned = 0;
    BOOL Result = FALSE;

    RtlZeroMemory(pContext, CONTEXT_BUFFER_SIZE);
    pContext->Size = (DWORD)CONTEXT_BUFFER_SIZE;
    pContext->DeviceCount = DeviceCount;

    Result = DeviceIoControl(hDevice, IOCTL_INIT_DEVICE_STACK, pContext, sizeof(DEVICE_CONTROL_CONTEXT), NULL, 0, &BytesReturned, NULL);
    PrintIoctlResult("IOCTL_INIT_DEVICE_STACK", Result, BytesReturned, GetLastError());
    if (!Result) return FALSE;

    printf("Device stack rebuilt with %u test device(s).\r\n", DeviceCount);
    // 设备栈已经变化, 立即重新读取一次内核态信息
    return ReadKernelData(hDevice, pContext, pState);
}

BOOL SetCompletionDevice(HANDLE hDevice, PDEVICE_CONTROL_CONTEXT pContext, PWORKFLOW_STATE pState, DWORD DeviceIndex) {
    DWORD BytesReturned = 0;
    BOOL Result = FALSE;
    PVOID pCompletionDevice = NULL;

    if (DeviceIndex >= pState->DeviceCount) {
        printf("Invalid device index, run 'get' first.\r\n");
        return FALSE;
    }
    pCompletionDevice = pState->pDeviceArray[DeviceIndex];
    if (pCompletionDevice == NULL) {
        printf("TestDevice%u is not available in the kernel device array.\r\n", DeviceIndex);
        return FALSE;
    }

    RtlZeroMemory(pContext, CONTEXT_BUFFER_SIZE);
    pContext->Size = sizeof(DEVICE_CONTROL_CONTEXT);
    pContext->DeviceCount = pState->DeviceCount;
    pContext->pCompletionDevice = pCompletionDevice;

    Result = DeviceIoControl(hDevice, IOCTL_SET_COMPLETION_DEVICE, pContext, sizeof(DEVICE_CONTROL_CONTEXT), NULL, 0, &BytesReturned, NULL);
    PrintIoctlResult("IOCTL_SET_COMPLETION_DEVICE", Result, BytesReturned, GetLastError());
    if (!Result) return FALSE;

    printf("Completion device set to TestDevice%u (%p).\r\n", DeviceIndex, pCompletionDevice);
    // 回读一次, 确认内核态记录的完成例程设备
    return ReadKernelData(hDevice, pContext, pState);
}

BOOL ReleaseDeviceStack(HANDLE hDevice, PDEVICE_CONTROL_CONTEXT pContext, PWORKFLOW_STATE pState) {
    DWORD BytesReturned = 0;
    BOOL Result = FALSE;

    Result = DeviceIoControl(hDevice, IOCTL_RELE_DEVICE_STACK, NULL, 0, NULL, 0, &BytesReturned, NULL);
    PrintIoctlResult("IOCTL_RELE_DEVICE_STACK", Result, BytesReturned, GetLastError());
    if (!Result) return FALSE;

    RtlZeroMemory(pContext, CONTEXT_BUFFER_SIZE);
    RtlZeroMemory(pState, sizeof(WORKFLOW_STATE));
    printf("Device stack released, the symbolic device has no lower device now.\r\n");
    return TRUE;
}


BOOL SendIrpRead(HANDLE hDevice) {
    BYTE Buffer[64] = { 0 };
    DWORD BytesReturned = 0;

    if (ReadFile(hDevice, Buffer, (DWORD)sizeof(Buffer), &BytesReturned, NULL)) {
        printf("IRP_MJ_READ: STATUS_SUCCESS, information = %u\r\n", BytesReturned);
        return TRUE;
    }
    DWORD ErrorCode = GetLastError();
    printf("IRP_MJ_READ failed: %s\r\n", GetErrorText(ErrorCode));
    PrintErrorHint(ErrorCode);
    return FALSE;
}

BOOL SendIrpWrite(HANDLE hDevice) {
    BYTE Buffer[64] = { 0 };
    DWORD BytesReturned = 0;

    RtlFillMemory(Buffer, sizeof(Buffer), 0x41);
    if (WriteFile(hDevice, Buffer, (DWORD)sizeof(Buffer), &BytesReturned, NULL)) {
        printf("IRP_MJ_WRITE: STATUS_SUCCESS, information = %u\r\n", BytesReturned);
        return TRUE;
    }
    DWORD ErrorCode = GetLastError();
    printf("IRP_MJ_WRITE failed: %s\r\n", GetErrorText(ErrorCode));
    PrintErrorHint(ErrorCode);
    return FALSE;
}

BOOL SendUnknownIoctl(HANDLE hDevice, PDEVICE_CONTROL_CONTEXT pContext) {
    DWORD BytesReturned = 0;
    BOOL Result = FALSE;

    RtlZeroMemory(pContext, CONTEXT_BUFFER_SIZE);
    Result = DeviceIoControl(hDevice, IOCTL_UNKNOWN_REQUEST, pContext, sizeof(DEVICE_CONTROL_CONTEXT), NULL, 0, &BytesReturned, NULL);
    if (Result) {
        printf("Unknown IOCTL 0x%08X: unexpected success, %u byte(s) returned\r\n", IOCTL_UNKNOWN_REQUEST, BytesReturned);
        return TRUE;
    }
    DWORD ErrorCode = GetLastError();
    printf("Unknown IOCTL 0x%08X rejected by DispatchDevCtrl: %s\r\n", IOCTL_UNKNOWN_REQUEST, GetErrorText(ErrorCode));
    PrintErrorHint(ErrorCode);
    return FALSE;
}


BOOL RunDispatchWorkflow(HANDLE hDevice, PDEVICE_CONTROL_CONTEXT pContext, PWORKFLOW_STATE pState) {
    DWORD DeviceCount = 0;
    DWORD CompletionIndex = 0;

    printf("[1/6] Init device stack\r\n");
    if (!ReadDword("DeviceCount (1 - 64)> ", &DeviceCount, 1, MAX_DEVICE_COUNT)) return FALSE;
    if (!InitDeviceStack(hDevice, pContext, pState, DeviceCount)) return FALSE;

    printf("\r\n[2/6] Read kernel data\r\n");
    if (!ReadKernelData(hDevice, pContext, pState)) return FALSE;

    printf("\r\n[3/6] Set the completion device (middle layer of the stack)\r\n");
    CompletionIndex = pState->DeviceCount / 2;
    if (!SetCompletionDevice(hDevice, pContext, pState, CompletionIndex)) return FALSE;

    printf("\r\n[4/6] Send IRP_MJ_READ\r\n");
    SendIrpRead(hDevice);

    printf("\r\n[5/6] Send IRP_MJ_WRITE\r\n");
    SendIrpWrite(hDevice);

    printf("\r\n[6/6] Release the device stack\r\n");
    return ReleaseDeviceStack(hDevice, pContext, pState);
}
