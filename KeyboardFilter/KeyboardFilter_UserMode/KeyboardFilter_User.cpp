#define _CRT_SECURE_NO_WARNINGS

#include <iostream>
#include <ctype.h>
#include <windows.h>

#define ORDER_COUNT 5
#define DEVICE_PATH "\\\\.\\cnHHHHHcn_KeyboardFilter"

#define IOCTL_ENABLE_FILTER		CTL_CODE(FILE_DEVICE_KEYBOARD, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_ENABLE_LOG		CTL_CODE(FILE_DEVICE_KEYBOARD, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_ORDER_ADDKEY		CTL_CODE(FILE_DEVICE_KEYBOARD, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_ORDER_DELKEY		CTL_CODE(FILE_DEVICE_KEYBOARD, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_READ_KRNLDATA		CTL_CODE(FILE_DEVICE_KEYBOARD, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef enum _SWITCH_STATUS : char {
    OFF = 0,
    ON  = 1,
    ERR = 2
} SWITCH_STATUS, *PSWITCH_STATUS;

typedef struct _DEVICE_CONTROL_CONTEXT {
    DWORD Size;
    DWORD BlockedCount;
    SWITCH_STATUS IsEnable;
    SWITCH_STATUS IsLog;
    USHORT ScanCode[1];
}DEVICE_CONTROL_CONTEXT, * PDEVICE_CONTROL_CONTEXT;

typedef DWORD (*ScanCodeFunc)(PDEVICE_CONTROL_CONTEXT Data, INPUT_RECORD InputRecord);

int main();

DWORD GetOrder();
void InteractData(HANDLE hDevice, DWORD Order, PDEVICE_CONTROL_CONTEXT Data);
SWITCH_STATUS GetFlag();
void ExecuteScanCodeCallbacks(PDEVICE_CONTROL_CONTEXT Data, ScanCodeFunc cbFunc);
DWORD AddScanCode(PDEVICE_CONTROL_CONTEXT Data, INPUT_RECORD InputRecord);
DWORD DelScanCode(PDEVICE_CONTROL_CONTEXT Data, INPUT_RECORD InputRecord);

const char* GetKeyName(USHORT ScanCode);

HANDLE InitKeyBoardFilter();
void ReleaseKeyBoardFilter(HANDLE hDevice);


int main() {
    HANDLE hDevice = NULL;
    DWORD Order = 0;
    DWORD BytesReturned = 0;
    PDEVICE_CONTROL_CONTEXT pDevCtrlContext = NULL;
    hDevice = InitKeyBoardFilter();
    if (hDevice == NULL) return 0;
    pDevCtrlContext = (PDEVICE_CONTROL_CONTEXT)calloc(1, sizeof(DEVICE_CONTROL_CONTEXT) + sizeof(USHORT) * 255);
    if (!pDevCtrlContext) {
        ReleaseKeyBoardFilter(hDevice);
        return 0;
    }
    while (true) {
        Order = GetOrder();
        if (!Order) break;
        DeviceIoControl(hDevice, IOCTL_READ_KRNLDATA, NULL, 0, pDevCtrlContext, sizeof(DEVICE_CONTROL_CONTEXT) + sizeof(USHORT) * 255, &BytesReturned, NULL);
        if (Order == IOCTL_READ_KRNLDATA) {
            printf("Size: %d\r\n", pDevCtrlContext->Size);
            printf("BlockedCount: %d\r\n", pDevCtrlContext->BlockedCount);
            printf("IsEnable: %d\r\n", pDevCtrlContext->IsEnable);
            printf("Log: %d\r\n", pDevCtrlContext->IsLog);
            if (pDevCtrlContext->BlockedCount != 0) {
                printf("ScanCode\tKey\r\n");
                for (DWORD i = 0; i < pDevCtrlContext->BlockedCount; i++) {
                    const char* KeyName = GetKeyName(pDevCtrlContext->ScanCode[i]);
                    printf("%04x\t\t%s\r\n", pDevCtrlContext->ScanCode[i], KeyName);
                }
            }
            system("pause");
            continue;
        }
        InteractData(hDevice, Order, pDevCtrlContext);
        DeviceIoControl(hDevice, Order, pDevCtrlContext, pDevCtrlContext->Size, NULL, 0, &BytesReturned, NULL);
        system("pause");
    }
    ReleaseKeyBoardFilter(hDevice);
    system("pause");
    return 0;
}


DWORD GetOrder(){
    DWORD IOCTLArray[ORDER_COUNT] = { IOCTL_ENABLE_FILTER, IOCTL_ENABLE_LOG, IOCTL_ORDER_ADDKEY, IOCTL_ORDER_DELKEY, IOCTL_READ_KRNLDATA };
    char  OrderArray[ORDER_COUNT][4] = { "swi", "log", "add", "del", "get"};
    char Order[4] = { 0 };

    while (true) {
        system("cls");
        printf("Order\t\tComment\r\n");
        printf("swi\t\tEnable Keyboard Filter Function\r\n");
        printf("log\t\tEnable Keyboard Filter Log\r\n");
        printf("add\t\tKeyboard Filter Add Blocked Key\r\n");
        printf("del\t\tKeyboard Filter Del Blocked Key\r\n");
        printf("get\t\tKeyboard Filter Get Blocked Key\r\n");
        printf("ext\t\tExit Process\r\n\r\n");
        printf("Order>");
        int tmp = scanf("%3s", Order);
        size_t i = 0;
        while (Order[i]) {
            Order[i] = tolower(Order[i]);
            i++;
        }
        if (strcmp(Order, "ext") == 0) return 0;
        for (size_t j = 0; j < ORDER_COUNT; j++) {
            if (strcmp(Order, OrderArray[j]) == 0) {
                return IOCTLArray[j];
            }
        }
    }
}

void InteractData(HANDLE hDevice, DWORD Order, PDEVICE_CONTROL_CONTEXT Data) {
    SWITCH_STATUS Flag = OFF;
    int KeyCount = 0;
    char inputBuffer[128] = { 0 };

    switch (Order) {
    case IOCTL_ENABLE_FILTER:
        printf("Enable Keyboard Filter Switch \t Value: 0/1\n");
        Flag = GetFlag();
        Data->IsEnable = Flag;
        Data->Size = sizeof(DEVICE_CONTROL_CONTEXT);
        printf("Keyboard Filter is now %s.\n", Flag ? "ENABLED" : "DISABLED");
        break;

    case IOCTL_ENABLE_LOG:
        printf("Enable Keyboard Filter Log \t Value: 0/1\n");
        Flag = GetFlag();
        Data->IsLog = Flag;
        Data->Size = sizeof(DEVICE_CONTROL_CONTEXT);
        printf("Keyboard Log is now %s.\n", Flag ? "ENABLED" : "DISABLED");
        break;

    case IOCTL_ORDER_ADDKEY:
        printf("Keyboard filter: adding filtered keys...\r\nRight mouse click blocked.\r\n");
        ExecuteScanCodeCallbacks(Data, AddScanCode);
        printf("adding filtered keys Successfully!\r\n");
        break;

    case IOCTL_ORDER_DELKEY:
        SWITCH_STATUS ss = OFF;
        if (Data->IsEnable == ON) {
            ss = ERR;
            Data->IsEnable = OFF;
            Data->Size = sizeof(DEVICE_CONTROL_CONTEXT);
            DeviceIoControl(hDevice, IOCTL_ENABLE_FILTER, Data, Data->Size, NULL, 0, NULL, NULL);
        }
        printf("Keyboard filter: removing filtered keys...\r\nRight mouse click blocked.\r\n");
        ExecuteScanCodeCallbacks(Data, DelScanCode);
        printf("removing filtered keys Successfully!\r\n");
        if (ss == ERR) {
            Data->IsEnable = ON;
            DeviceIoControl(hDevice, IOCTL_ENABLE_FILTER, Data, Data->Size, NULL, 0, NULL, NULL);
        }

        break;
    }
    return;
}

SWITCH_STATUS GetFlag(){
    DWORD Flag = OFF;
    while (true) {
        printf("Value> ");
        if (scanf("%d", &Flag) != 1) {
            // 如果输入的不是数字，清空缓冲区并提示重试
            while (getchar() != '\n');
            printf("Invalid input. Please enter 0 or 1.\n");
            continue;
        }
        if (Flag == 0 || Flag == 1) break;
        printf("Invalid value. Please enter 0 or 1.\n");
    }
    // 清空缓冲区中残留的换行符
    while (getchar() != '\n');
    return (SWITCH_STATUS)Flag;
}

void ExecuteScanCodeCallbacks(PDEVICE_CONTROL_CONTEXT Data, ScanCodeFunc cbFunc){
    HANDLE hConsole = NULL;
    INPUT_RECORD InputRecord = { 0 };
    DWORD ReturnBytes = 0;
    DWORD ConsoleMode = 0;
    hConsole = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hConsole, &ConsoleMode);
    SetConsoleMode(hConsole, ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS);
    while (true) {
        ReadConsoleInput(hConsole, &InputRecord, 1, &ReturnBytes);
        if (ReturnBytes == 1) {
            if(!cbFunc(Data, InputRecord)) break;
        }
    }
    SetConsoleMode(hConsole, ConsoleMode);
}

DWORD AddScanCode(PDEVICE_CONTROL_CONTEXT Data, INPUT_RECORD InputRecord) {
    if (InputRecord.EventType == MOUSE_EVENT) {
        if (InputRecord.Event.MouseEvent.dwButtonState == RIGHTMOST_BUTTON_PRESSED) return 0;
    }
    if (InputRecord.EventType == KEY_EVENT) {
        DWORD i = 0;
        if (Data->BlockedCount > 255) return 0;
        for (; i < Data->BlockedCount; i++) {
            if (Data->ScanCode[i] == InputRecord.Event.KeyEvent.wVirtualScanCode) break;
        }
        if (i == Data->BlockedCount) {
            Data->ScanCode[i] = InputRecord.Event.KeyEvent.wVirtualScanCode;
            Data->BlockedCount = i + 1;
            Data->Size = sizeof(DEVICE_CONTROL_CONTEXT) + sizeof(USHORT) * (Data->BlockedCount - 1);
            int uChar = (int)InputRecord.Event.KeyEvent.uChar.UnicodeChar;
            printf("Key: %c ScanCode: %d\r\n", uChar, InputRecord.Event.KeyEvent.wVirtualScanCode);
        }
    }
    return 1;
}

DWORD DelScanCode(PDEVICE_CONTROL_CONTEXT Data, INPUT_RECORD InputRecord) {
    if (InputRecord.EventType == MOUSE_EVENT) {
        if (InputRecord.Event.MouseEvent.dwButtonState == RIGHTMOST_BUTTON_PRESSED) return 0;
    }
    if (InputRecord.EventType == KEY_EVENT) {
        DWORD i = 0;
        if (Data->BlockedCount == 0) return 0;
        for (; i < Data->BlockedCount; i++) {
            if (Data->ScanCode[i] == InputRecord.Event.KeyEvent.wVirtualScanCode) break;
        }
        if (i != Data->BlockedCount) {
            DWORD CopySize = (Data->BlockedCount - i - 1) * sizeof(USHORT);
            RtlCopyMemory(&Data->ScanCode[i], &Data->ScanCode[i + 1], CopySize);
            Data->BlockedCount--;
            Data->Size = sizeof(DEVICE_CONTROL_CONTEXT) + sizeof(USHORT) * (Data->BlockedCount - 1);
            int uChar = (int)InputRecord.Event.KeyEvent.uChar.UnicodeChar;
            printf("Key: %c ScanCode: %d\r\n", uChar, InputRecord.Event.KeyEvent.wVirtualScanCode);
        }
    }
    return 1;
}


const char* GetKeyName(USHORT ScanCode) {  // 去掉 Flags 参数
    static char szBuffer[64];
    LONG lParam = (ScanCode << 16);

    // 硬编码判断 E0 键
    if (ScanCode == 0x1C || ScanCode == 0x1D || ScanCode == 0x35 ||
        ScanCode == 0x38 || ScanCode == 0x47 || ScanCode == 0x48 ||
        ScanCode == 0x49 || ScanCode == 0x4B || ScanCode == 0x4D ||
        ScanCode == 0x4F || ScanCode == 0x50 || ScanCode == 0x51 ||
        ScanCode == 0x52 || ScanCode == 0x53 || ScanCode == 0x5B ||
        ScanCode == 0x5C || ScanCode == 0x5D) {
        lParam |= (1 << 24);
    }

    int len = GetKeyNameTextA(lParam, szBuffer, sizeof(szBuffer));
    return len > 0 ? szBuffer : "Unknown";
}


HANDLE InitKeyBoardFilter() {
    HANDLE hDevice = NULL;
    hDevice = CreateFileA(DEVICE_PATH, GENERIC_WRITE | GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hDevice == INVALID_HANDLE_VALUE) {
        printf("Initialize Keyboard Filter Failed\r\n");
        return NULL;
    }
    printf("Initialize Keyboard Filter Success\r\n ");
    return hDevice;
}

void ReleaseKeyBoardFilter(HANDLE hDevice) {
    if (hDevice == NULL || hDevice == INVALID_HANDLE_VALUE) {
        printf("Invalid Device Handle\r\n");
        return;
    }
    CloseHandle(hDevice);
    printf("Release Keyboard Filter Successful!\r\n");
}