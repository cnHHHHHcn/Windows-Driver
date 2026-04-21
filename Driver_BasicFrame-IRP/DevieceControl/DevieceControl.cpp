#include <windows.h>
#include <iostream>

#define CTL_CODE( DeviceType, Function, Method, Access ) (                 \
    ((DeviceType) << 16) | ((Access) << 14) | ((Function) << 2) | (Method) \
)

#define IOCTL_SEND_DATA CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_RECEIVE_DATA CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define DEVICE_PATH "\\\\.\\cnHHHHHcn" // 设备路径

int main()
{
    HANDLE hDevice = NULL;
    hDevice = CreateFileA(DEVICE_PATH, GENERIC_WRITE | GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hDevice == INVALID_HANDLE_VALUE) {
		std::cout << "无法打开设备，错误码: " << GetLastError() << std::endl;
    }else {
        std::cout << "设备打开成功！" << std::endl;
    }
	DWORD bytesReturned = 0;
	system("pause");
	void* pInBuffer = (void*)"User Talk: Hello Driver!";
    DeviceIoControl(hDevice, IOCTL_SEND_DATA, pInBuffer, 25, NULL, 0, &bytesReturned, NULL);
    system("pause");
	char pOutBuffer[256] = { 0 };
	DeviceIoControl(hDevice, IOCTL_RECEIVE_DATA, NULL, 0, pOutBuffer, sizeof(pOutBuffer), &bytesReturned, NULL);
	Sleep(1000); // 等待驱动处理完数据
	std::cout << "从驱动接收到的数据: " << pOutBuffer << std::endl;
    system("pause");
    CloseHandle(hDevice);
    return 0;
}
