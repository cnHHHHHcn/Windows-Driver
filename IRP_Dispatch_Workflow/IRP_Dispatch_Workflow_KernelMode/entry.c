#include <ntifs.h>
#include <ntstrsafe.h>

// ******************************************************************************************************
// Created symbolic device and test device stack after 
// TestDeviceN -> ... -> TestDevice2 -> TestDevice1 -> TestDevice0 -> SymboliceDevcie
// ------------------------------------------------------------------------------------------------------
// Symbolic device attached test device stack after
// SymbolicDevice -> TestDeviceN -> ... -> TestDevice2 -> TestDevice1 -> TestDevice0 
// ******************************************************************************************************


#define DEVICE_COUNT			10
#define SYMBOLIC_LINK_NAME		L"\\DosDevices\\TestDevice"
#define SYMBOLIC_DEVICE_NAME	L"\\Device\\TestDevice"
#define TEST_DEVICE_NAME		L"\\Device\\TestDevice%d"


#define IOCTL_READ_KRNLDATA				CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SET_COMPLETION_DEVICE		CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_INIT_DEVICE_STACK			CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_RELE_DEVICE_STACK			CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)


typedef struct _DEVICE_CONTROL_CONTEXT {
	DWORD Size;
	DWORD DeviceCount;
	PDEVICE_OBJECT pCompletionDevice;
	PDEVICE_OBJECT pDeviceArray[1];
} DEVICE_CONTROL_CONTEXT, *PDEVICE_CONTROL_CONTEXT;

typedef struct _DEVICE_STACK_INFO {
	PDEVICE_OBJECT pLowerBoundDevice;	// 设备对象指针 (设备栈底层)
	PDEVICE_OBJECT pCompletionDevice;	// 设备对象指针 (设备栈中间层)
	PDEVICE_OBJECT pUpperBoundDevice;	// 设备对象指针 (设备栈顶层)
}DEVICE_STACK_INFO, *PDEVICE_STACK_INFO;

typedef struct _GLOBAL_CONTEXT{
	PDRIVER_OBJECT pDriver;				// 驱动对象指针
	DEVICE_STACK_INFO DeviceStackInfo;	// 设备栈信息
	PDEVICE_OBJECT pSymbolicDevice;		// 符号链接设备对象指针 (设备栈顶层)
	UNICODE_STRING SymbolicName;		// 符号链接名称
	BOOLEAN IsSymbolicName;				// 是否创建了符号链接
	BOOLEAN IsRelease;					// 是否已释放设备
	DWORD DeviceCount;					// 设备栈中设备数量
}GLOBAL_CONTEXT, *PGLOBAL_CONTEXT;

typedef struct _DEVICE_EXTENSION {
	PDEVICE_OBJECT pLowerDevice;
}DEVICE_EXTENSION, *PDEVICE_EXTENSION;

GLOBAL_CONTEXT gContext = { 0 };

NTSTATUS DriverEntry(PDRIVER_OBJECT pDriverObject, PUNICODE_STRING pRegisterPath);
NTSTATUS DriverUnload(PDRIVER_OBJECT pDriverObject);

NTSTATUS CreateSymbolicDevice(PDRIVER_OBJECT pDriverObject,  PDEVICE_OBJECT* pDeviceObject);
NTSTATUS DeleteSymbolicDevice(PDRIVER_OBJECT pDriverObject, PDEVICE_OBJECT pSymbolicDevice);
NTSTATUS AttachSymbolicDeviceToTestDeviceStack(PDRIVER_OBJECT pDriverObject,  PDEVICE_OBJECT pSymbolicDevice);
VOID DetachSymbolicDeviceFromTestDeviceStack(PDRIVER_OBJECT pDriverObject, PDEVICE_OBJECT pSymbolicDevice);

NTSTATUS CreateTestDeviceStack(PDRIVER_OBJECT pDriverObject, DWORD deviceCount, PDEVICE_STACK_INFO pDeviceInfo);
VOID PrintTestDeviceStack(PDRIVER_OBJECT pDriverObject);
NTSTATUS DeleteTestDeviceStack(PDRIVER_OBJECT pDriverObject, PDEVICE_OBJECT pSymbolicDevice, PDEVICE_STACK_INFO pDeviceStackInfo);

NTSTATUS DispatchRoutine(PDEVICE_OBJECT pDeviceObject, PIRP pIrp);
NTSTATUS DispatchDevCtrl(PDEVICE_OBJECT pDeviceObject, PIRP pIrp);
NTSTATUS CompleteCallBack(PDEVICE_OBJECT pDeviceObject, PIRP pIrp, PVOID Context);

PWCHAR GetDispatchName(UCHAR MajorFunction);

NTSTATUS DriverEntry(PDRIVER_OBJECT pDriverObject, PUNICODE_STRING pRegisterPath){

	UNREFERENCED_PARAMETER(pRegisterPath);

	NTSTATUS status = STATUS_SUCCESS;
	PDEVICE_OBJECT pSymbolicDevice = NULL;

	RtlZeroMemory(&gContext, sizeof(GLOBAL_CONTEXT));

	for(int i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++){
		pDriverObject->MajorFunction[i] = DispatchRoutine;
	}
	pDriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchDevCtrl;
	pDriverObject->DriverUnload = DriverUnload;

	DbgBreakPoint();

	gContext.pDriver = pDriverObject;

	status = CreateSymbolicDevice(pDriverObject, &pSymbolicDevice);
	if (!NT_SUCCESS(status)) {
		DbgPrint("CreateSymbolicDevice failed: 0x%X\r\n", status);
		return status;
	}
	if (!pSymbolicDevice) return STATUS_SUCCESS;
	gContext.pSymbolicDevice = pSymbolicDevice;
	gContext.IsSymbolicName = TRUE;
	RtlInitUnicodeString(&gContext.SymbolicName, SYMBOLIC_LINK_NAME);

	status = CreateTestDeviceStack(pDriverObject, DEVICE_COUNT, &gContext.DeviceStackInfo);
	if (!NT_SUCCESS(status)) {
		DbgPrint("CreateTestDeviceStack failed: 0x%X\r\n", status);
		status = DeleteSymbolicDevice(pDriverObject, pSymbolicDevice);
		if (!NT_SUCCESS(status)) {
			DbgPrint("DeleteSymbolicDevice failed: 0x%X\r\n", status);
			return status;
		}
		return STATUS_SUCCESS;
	}

	PrintTestDeviceStack(pDriverObject);

	status = AttachSymbolicDeviceToTestDeviceStack(pDriverObject, pSymbolicDevice);
	if (!NT_SUCCESS(status)) {
		DbgPrint("AttachSymbolicDeviceToTestDeviceStack failed: 0x%X\r\n", status);
		status = DeleteTestDeviceStack(pDriverObject, pSymbolicDevice, &gContext.DeviceStackInfo);
		if (!NT_SUCCESS(status)) {
			DbgPrint("DeleteTestDeviceStack failed: 0x%X\r\n", status);
		}
		status = DeleteSymbolicDevice(pDriverObject, pSymbolicDevice);
		if (!NT_SUCCESS(status)) {
			DbgPrint("DeleteSymbolicDevice failed: 0x%X\r\n", status);
			return status;
		}
		return STATUS_SUCCESS;
	}

	gContext.IsRelease = FALSE;

	return STATUS_SUCCESS;
}

NTSTATUS DriverUnload(PDRIVER_OBJECT pDriverObject){
	DbgPrint("Driver Unload Calling\r\n");
	gContext.IsRelease = TRUE;
	DetachSymbolicDeviceFromTestDeviceStack(pDriverObject, gContext.pSymbolicDevice);
	DeleteTestDeviceStack(pDriverObject, gContext.pSymbolicDevice, &gContext.DeviceStackInfo);
	DeleteSymbolicDevice(pDriverObject, gContext.pSymbolicDevice);
	return STATUS_SUCCESS;
}

NTSTATUS CreateSymbolicDevice(PDRIVER_OBJECT pDriverObject, PDEVICE_OBJECT* pDeviceObject){
	NTSTATUS status = STATUS_SUCCESS;
	UNICODE_STRING DeviceName = { 0 };
	UNICODE_STRING SymbolliLinkName = { 0 };
	PDEVICE_OBJECT pSymDev = NULL;
	if (!pDriverObject) return STATUS_INVALID_PARAMETER;
	RtlInitUnicodeString(&DeviceName, SYMBOLIC_DEVICE_NAME);
	RtlInitUnicodeString(&SymbolliLinkName, SYMBOLIC_LINK_NAME);

	// 创建符号设备对象
	status = IoCreateDevice(pDriverObject, sizeof(DEVICE_EXTENSION), &DeviceName, FILE_DEVICE_UNKNOWN, 0, FALSE, &pSymDev);
	if (!NT_SUCCESS(status)) {
		DbgPrint("Failed to create symbolic device %ws: 0x%X\r\n", DeviceName.Buffer, status);
		return status;
	}
	pSymDev->Flags |= DO_BUFFERED_IO;
	pSymDev->Flags &= ~DO_DEVICE_INITIALIZING;

	// 创建符号链接
	status = IoCreateSymbolicLink(&SymbolliLinkName, &DeviceName);
	if(!NT_SUCCESS(status)){
		DbgPrint("Failed to create symbolic link name 0x%X\r\n", status);
		IoDeleteDevice(pSymDev);
		return status;
	}
	if (pSymDev) *pDeviceObject = pSymDev;
	return status;
}

NTSTATUS DeleteSymbolicDevice(PDRIVER_OBJECT pDriverObject, PDEVICE_OBJECT pSymbolicDevice){
	NTSTATUS status = STATUS_SUCCESS;
	PDEVICE_OBJECT pDeviceObject = NULL;

	if (!pDriverObject) return STATUS_INVALID_PARAMETER;
	if (pSymbolicDevice == NULL) return STATUS_SUCCESS;

	pDeviceObject = pDriverObject->DeviceObject;
	if (pSymbolicDevice != pDeviceObject) return STATUS_UNSUCCESSFUL;


	// 删除符号链接
	status = IoDeleteSymbolicLink(&gContext.SymbolicName);
	if (!NT_SUCCESS(status)) {
		DbgPrint("Failed to delete symbolic link %ws: 0x%X\r\n", SYMBOLIC_DEVICE_NAME, status);
	}

	// 删除符号设备对象
	if(gContext.pSymbolicDevice == pSymbolicDevice){
		IoDeleteDevice(pSymbolicDevice);
		gContext.pSymbolicDevice = NULL;
	}
	gContext.IsSymbolicName = FALSE;

	return STATUS_SUCCESS;
}

NTSTATUS AttachSymbolicDeviceToTestDeviceStack(PDRIVER_OBJECT pDriverObject, PDEVICE_OBJECT pSymbolicDevice){
	NTSTATUS status = STATUS_SUCCESS;
	PDEVICE_OBJECT pDeviceObject = NULL;
	PDEVICE_OBJECT pLowerDevice = NULL;
	PDEVICE_EXTENSION pDevExt = NULL;

	if (!pDriverObject) return STATUS_INVALID_PARAMETER;
	if (!pSymbolicDevice) return STATUS_INVALID_PARAMETER;

	// 符号设备对象附加到设备栈的最上层设备对象
	pDeviceObject = pDriverObject->DeviceObject;
	if (pDeviceObject == pSymbolicDevice) return  STATUS_UNSUCCESSFUL;
	pLowerDevice = IoAttachDeviceToDeviceStack(pSymbolicDevice, pDeviceObject);
	if (!pLowerDevice) {
		DbgPrint("Failed to attach symbolic device to device stack\r\n");
		return STATUS_UNSUCCESSFUL;
	}
	pDevExt = pSymbolicDevice->DeviceExtension;
	pDevExt->pLowerDevice = pLowerDevice;

	return status;
}

VOID DetachSymbolicDeviceFromTestDeviceStack(PDRIVER_OBJECT pDriverObject, PDEVICE_OBJECT pSymbolicDevice){
	PDEVICE_OBJECT pDeviceObject = NULL;
	PDEVICE_EXTENSION pDevExt = NULL;

	if (!pDriverObject) return;
	if (!pSymbolicDevice) return;

	// 符号设备对象从设备栈的最上层设备对象分离
	pDeviceObject = pDriverObject->DeviceObject;
	if (pDeviceObject == pSymbolicDevice) return;
	
	pDevExt = pSymbolicDevice->DeviceExtension;
	if (pDevExt->pLowerDevice) {
		IoDetachDevice(pDevExt->pLowerDevice);
		pDevExt->pLowerDevice = NULL;
	}

}

NTSTATUS CreateTestDeviceStack(PDRIVER_OBJECT pDriverObject, DWORD deviceCount, PDEVICE_STACK_INFO pDeviceInfo){
	
	NTSTATUS status = STATUS_SUCCESS;
	WCHAR deviceNameBuffer[64] = { 0 };
	UNICODE_STRING DeviceName = { 0 };
	UNICODE_STRING SymbolicLinkName = { 0 };
	PDEVICE_OBJECT pDeviceObject = NULL;
	PDEVICE_OBJECT pLowerDevice = NULL;
	PDEVICE_EXTENSION pDevExt = NULL;

	if (!pDriverObject) return STATUS_INVALID_PARAMETER;
	if (deviceCount <= 0) return STATUS_INVALID_PARAMETER;
	if (!pDeviceInfo) return STATUS_INVALID_PARAMETER;

	// 创建设备栈
	for (int i = 0; i < deviceCount; i++) {
		RtlStringCbPrintfW(deviceNameBuffer, sizeof(deviceNameBuffer), TEST_DEVICE_NAME, i);
		RtlInitUnicodeString(&DeviceName, deviceNameBuffer);
		pDeviceObject = NULL;
		status = IoCreateDevice(pDriverObject, sizeof(PVOID), &DeviceName, FILE_DEVICE_UNKNOWN, 0, FALSE, &pDeviceObject);
		if (!NT_SUCCESS(status)) {
			DbgPrint("Failed to create device %ws: 0x%X\r\n", DeviceName.Buffer, status);
			return status;
		}
		pDevExt = (PDEVICE_EXTENSION)pDeviceObject->DeviceExtension;
		RtlZeroMemory(pDevExt, sizeof(DEVICE_EXTENSION));
		pDeviceObject->Flags |= DO_BUFFERED_IO;
		pDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

		// 将设备对象附加到设备栈
		if (i) {
			pLowerDevice = IoAttachDeviceToDeviceStack(pDeviceObject, pLowerDevice);
			if (!pLowerDevice) {
				return STATUS_UNSUCCESSFUL;
			}
			pDevExt->pLowerDevice = pLowerDevice;

		}
		else {
			pDeviceInfo->pLowerBoundDevice = pDeviceObject;
		}
		pLowerDevice = pDeviceObject;
	}
	pDeviceInfo->pUpperBoundDevice = pDeviceObject;
	gContext.DeviceCount = deviceCount;
	return status;
}

VOID PrintTestDeviceStack(PDRIVER_OBJECT pDriverObject){
	WCHAR DeviceInfo[256] = { 0 };
	WCHAR TempString[64] = { 0 };
	PDEVICE_OBJECT pDeviceObject = NULL;
	
	if (!pDriverObject) return;

	// 打印设备栈信息
	pDeviceObject = pDriverObject->DeviceObject;
	DbgPrint("Printing test devices:\r\n");
	do{
		if (pDeviceObject) {
			RtlStringCbPrintfW(&TempString, sizeof(TempString), L"%p\r\n", pDeviceObject);
			RtlStringCbCatW(&DeviceInfo, sizeof(DeviceInfo), &TempString);
			pDeviceObject = pDeviceObject->NextDevice;
		}
	} while (pDeviceObject);
	DbgPrint("%ws", DeviceInfo);

}

NTSTATUS DeleteTestDeviceStack(PDRIVER_OBJECT pDriverObject, PDEVICE_OBJECT pSymbolicDevcie, PDEVICE_STACK_INFO pDeviceStackInfo) {

	PDEVICE_OBJECT pDeviceObject = NULL;
	PDEVICE_EXTENSION pDevExt = NULL;

	if (!pDriverObject) return STATUS_INVALID_PARAMETER;
	if (!pDeviceStackInfo) return STATUS_INVALID_PARAMETER;

	pDeviceObject = pDriverObject->DeviceObject;

	if (pSymbolicDevcie == pDeviceObject) return STATUS_SUCCESS;

	// DriverObject->DeviceObject 的表头是最后创建的设备，也就是设备栈的栈顶。
	// 从表头往下依次分离并删除：先 IoDetachDevice，再 IoDeleteDevice，
	// 这样每次用到的下层设备一定还存在，不会去碰已经删掉的设备对象。
	while (pDeviceObject  != NULL) {
		if (pDeviceObject == pSymbolicDevcie) break;

		pDevExt = (PDEVICE_EXTENSION)pDeviceObject->DeviceExtension;

		if (pDevExt->pLowerDevice != NULL) {
			IoDetachDevice(pDevExt->pLowerDevice);
			pDevExt->pLowerDevice = NULL;
		}

		IoDeleteDevice(pDeviceObject);

		pDeviceObject = pDriverObject->DeviceObject;
	}

	pDeviceStackInfo->pLowerBoundDevice = NULL;
	pDeviceStackInfo->pCompletionDevice = NULL;
	pDeviceStackInfo->pUpperBoundDevice = NULL;
	gContext.DeviceCount = 0;
	
	return STATUS_SUCCESS;
}

NTSTATUS DispatchRoutine(PDEVICE_OBJECT pDeviceObject, PIRP pIrp) {
	
	DEVICE_STACK_INFO DeviceStackInfo = { 0 };
	PDEVICE_EXTENSION pDevExt = NULL;

	DeviceStackInfo = gContext.DeviceStackInfo;

	// [ IRP 分发 ] - 打印调度信息
	DbgPrint("DispatchRoutine called for device: %p\r\n", pDeviceObject);
	PIO_STACK_LOCATION pStack = IoGetCurrentIrpStackLocation(pIrp);
	UCHAR Major = pStack->MajorFunction;
	if(IRP_MJ_MAXIMUM_FUNCTION < Major){
		DbgPrint("Invalid MajorFunction: %d\n", Major);
		pIrp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
		pIrp->IoStatus.Information = 0;
		IoCompleteRequest(pIrp, IO_NO_INCREMENT);
		return STATUS_INVALID_DEVICE_REQUEST;
	}
	else {
		DbgPrint("DispatchRoutine called for MajorFunction: %ws\n", GetDispatchName(Major));
	}

	// [ IRP 结束 ] - 检查是否已经释放设备，如果是，则返回 STATUS_DELETE_PENDING
	if(gContext.IsRelease){
		pIrp->IoStatus.Status = STATUS_DELETE_PENDING;
		pIrp->IoStatus.Information = 0;
		IoCompleteRequest(pIrp, IO_NO_INCREMENT);
		return STATUS_DELETE_PENDING;
	}
	
	// [ IRP 结束 ] - 检查是否为设备栈底层设备，如果是，则直接完成 IRP 
	if(DeviceStackInfo.pLowerBoundDevice && pDeviceObject == DeviceStackInfo.pLowerBoundDevice){
		pIrp->IoStatus.Status = STATUS_SUCCESS;
		pIrp->IoStatus.Information = 0;
		IoCompleteRequest(pIrp, IO_NO_INCREMENT);
		return STATUS_SUCCESS;
	}

	// [ IRP 注册 ] - 检查是否为设备栈中间层设备，如果是，则设置完成例程并调用下一个设备对象
	if (DeviceStackInfo.pCompletionDevice && pDeviceObject == DeviceStackInfo.pCompletionDevice) {
		pDevExt = (PDEVICE_EXTENSION)DeviceStackInfo.pCompletionDevice->DeviceExtension;
		if (!pDevExt->pLowerDevice) {
			pIrp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
			pIrp->IoStatus.Information = 0;
			IoCompleteRequest(pIrp, IO_NO_INCREMENT);
			return STATUS_INVALID_DEVICE_REQUEST;
		}
		IoCopyCurrentIrpStackLocationToNext(pIrp);
		IoSetCompletionRoutine(pIrp, CompleteCallBack, NULL, TRUE, TRUE, TRUE);
		return IoCallDriver(pDevExt->pLowerDevice, pIrp);
	}

	// [ IRP 分发 ] - 对于其他设备，直接跳过当前堆栈位置并调用下一个设备对象

	pDevExt = (PDEVICE_EXTENSION)pDeviceObject->DeviceExtension;
	if (!pDevExt->pLowerDevice) {
		pIrp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
		pIrp->IoStatus.Information = 0;
		IoCompleteRequest(pIrp, IO_NO_INCREMENT);
		return STATUS_INVALID_DEVICE_REQUEST;
	}
	IoSkipCurrentIrpStackLocation(pIrp);
	return IoCallDriver(pDevExt->pLowerDevice, pIrp);

}


// [ IRP 分发 ] - 处理设备控制请求
NTSTATUS DispatchDevCtrl(PDEVICE_OBJECT pDeviceObject, PIRP pIrp){

	UNREFERENCED_PARAMETER(pDeviceObject);

	NTSTATUS status = STATUS_SUCCESS;
	PDRIVER_OBJECT pDreObj = NULL;
	//PDEVICE_OBJECT pDevObj = NULL;
	PDEVICE_CONTROL_CONTEXT pDevCtrlContext = NULL;
	DWORD DeviceCount = 0;
	DWORD ControlCode = 0;
	ULONG InputLen = 0;
	ULONG OutputLen = 0;
	PVOID KernelBuffer = NULL;
	ULONG BytesReturned = 0;
	
	if (pDeviceObject == NULL || pIrp == NULL) return STATUS_INVALID_PARAMETER;
	if (pDeviceObject != gContext.pSymbolicDevice) return STATUS_INVALID_DEVICE_REQUEST;

	PIO_STACK_LOCATION pIrpStack = IoGetCurrentIrpStackLocation(pIrp);
	ControlCode = pIrpStack->Parameters.DeviceIoControl.IoControlCode;
	InputLen = pIrpStack->Parameters.DeviceIoControl.InputBufferLength;
	OutputLen = pIrpStack->Parameters.DeviceIoControl.OutputBufferLength;
	KernelBuffer = pIrp->AssociatedIrp.SystemBuffer;

	switch (ControlCode) {
		// [ IOCTL 处理 ] - 读取内核数据
		case IOCTL_READ_KRNLDATA: {

			PDEVICE_OBJECT* pDeviceArray = NULL;
			DWORD AllocateSize = sizeof(DEVICE_CONTROL_CONTEXT) + sizeof(PDEVICE_OBJECT) * (gContext.DeviceCount ? (gContext.DeviceCount - 1) : 0);
			if(gContext.DeviceCount <= 0){
				BytesReturned = 0;
				status = STATUS_INVALID_PARAMETER;
				break;
			}
			pDevCtrlContext = (PDEVICE_CONTROL_CONTEXT)ExAllocatePool(NonPagedPool, AllocateSize);
			if(pDevCtrlContext == NULL){
				BytesReturned = 0;
				status = STATUS_INSUFFICIENT_RESOURCES;
				break;
			}
			
			RtlZeroMemory(pDevCtrlContext, AllocateSize);
			pDevCtrlContext->Size = AllocateSize;
			pDevCtrlContext->DeviceCount = gContext.DeviceCount;
			pDevCtrlContext->pCompletionDevice = gContext.DeviceStackInfo.pCompletionDevice;
			// The upper bound device is the last created device, walk down to the lower bound
			// device through NextDevice so that index 0 is the lower bound device
			pDeviceArray = (PDEVICE_OBJECT*)((PCHAR)pDevCtrlContext + sizeof(DEVICE_CONTROL_CONTEXT) - sizeof(PDEVICE_OBJECT));
			PDEVICE_OBJECT pCurrentDevice = gContext.DeviceStackInfo.pUpperBoundDevice;
			for (DWORD i = gContext.DeviceCount; i > 0 && pCurrentDevice != NULL; i--) {
				pDeviceArray[i - 1] = pCurrentDevice;
				pCurrentDevice = pCurrentDevice->NextDevice;
			}

			if (OutputLen < AllocateSize) {
				ExFreePool(pDevCtrlContext);
				status = STATUS_BUFFER_TOO_SMALL;
				BytesReturned = 0;
				break;
			}

			RtlCopyMemory(KernelBuffer, pDevCtrlContext, AllocateSize);
			BytesReturned = AllocateSize;
			ExFreePool(pDevCtrlContext);
			break;
		}
		// [ IOCTL 处理 ] - 设置完成例程设备对象
		case IOCTL_SET_COMPLETION_DEVICE: {

			gContext.DeviceStackInfo.pCompletionDevice = ((PDEVICE_CONTROL_CONTEXT)KernelBuffer)->pCompletionDevice;

			break;
		}
		// [ IOCTL 处理 ] - 初始化设备栈
		case IOCTL_INIT_DEVICE_STACK: {

			DeviceCount = ((PDEVICE_CONTROL_CONTEXT)KernelBuffer)->DeviceCount;
			pDreObj = pDeviceObject->DriverObject;
			DetachSymbolicDeviceFromTestDeviceStack(pDreObj, pDeviceObject);
			DeleteTestDeviceStack(pDreObj, pDeviceObject, &gContext.DeviceStackInfo);

			status = CreateTestDeviceStack(pDreObj, DeviceCount, &gContext.DeviceStackInfo);
			if (!NT_SUCCESS(status)) {
				DbgPrint("Failed to create test devices: 0x%X\r\n", status);
				status = DeleteSymbolicDevice(pDreObj, pDeviceObject);
				if (!NT_SUCCESS(status)) {
					DbgPrint("DeleteSymbolicDevice failed: 0x%X\r\n", status);
					return status;
				}
			}
			status = AttachSymbolicDeviceToTestDeviceStack(pDreObj, pDeviceObject);
			if (!NT_SUCCESS(status)) {
				DbgPrint("AttachSymbolicDeviceToTestDeviceStack failed: 0x%X\r\n", status);
				status = DeleteTestDeviceStack(pDreObj, pDeviceObject, &gContext.DeviceStackInfo);
				if (!NT_SUCCESS(status)) {
					DbgPrint("DeleteTestDeviceStack failed: 0x%X\r\n", status);
				}
			}

			PrintTestDeviceStack(pDreObj);

			break;
		}
		// [ IOCTL 处理 ] - 释放设备栈
		case IOCTL_RELE_DEVICE_STACK: {

			pDreObj = pDeviceObject->DriverObject;
			DetachSymbolicDeviceFromTestDeviceStack(pDreObj, pDeviceObject);
			DeleteTestDeviceStack(pDreObj, pDeviceObject, &gContext.DeviceStackInfo);

			break;
		}
		// [ IOCTL 处理 ] - 未知的控制码
		default: {
			status = STATUS_INVALID_DEVICE_REQUEST;
		}
	}

	pIrp->IoStatus.Information = BytesReturned;
	pIrp->IoStatus.Status = status;
	IoCompleteRequest(pIrp, IO_NO_INCREMENT);
	return status;
}

// [ IRP 回调 ] - 完成例程回调函数，用于处理设备栈中间层设备的 IRP 完成
NTSTATUS CompleteCallBack(PDEVICE_OBJECT pDeviceObject, PIRP pIrp, PVOID Context) {
	UNREFERENCED_PARAMETER(Context);
	DbgPrint("CompleteCallBack called for device: %p\r\n", pDeviceObject);
	PIO_STACK_LOCATION pStack = IoGetCurrentIrpStackLocation(pIrp);
	UCHAR Major = pStack->MajorFunction;
	if (IRP_MJ_MAXIMUM_FUNCTION < Major) {
		DbgPrint("Invalid MajorFunction: %d\n", Major);
		pIrp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
		pIrp->IoStatus.Information = 0;
		return STATUS_INVALID_DEVICE_REQUEST;
	}
	else {
		DbgPrint("CompleteCallBack called for MajorFunction: %ws\r\n", GetDispatchName(Major));
	}
	pIrp->IoStatus.Information = 0;
	pIrp->IoStatus.Status = STATUS_SUCCESS;
	return STATUS_SUCCESS;
}

PWCHAR GetDispatchName(UCHAR MajorFunction){

	switch (MajorFunction) {
		case IRP_MJ_CREATE                   : return L"Create";
		case IRP_MJ_CREATE_NAMED_PIPE        : return L"Create Named Pipe";
		case IRP_MJ_CLOSE                    : return L"Close";
		case IRP_MJ_READ                     : return L"Read";
		case IRP_MJ_WRITE                    : return L"Write";
		case IRP_MJ_QUERY_INFORMATION        : return L"Query Information";
		case IRP_MJ_SET_INFORMATION          : return L"Set Information";
		case IRP_MJ_QUERY_EA                 : return L"Query Ea";
		case IRP_MJ_SET_EA                   : return L"Set Ea";
		case IRP_MJ_FLUSH_BUFFERS            : return L"Flush Buffers";
		case IRP_MJ_QUERY_VOLUME_INFORMATION : return L"Query Volume Information";
		case IRP_MJ_SET_VOLUME_INFORMATION   : return L"Set Volume Information";
		case IRP_MJ_DIRECTORY_CONTROL        : return L"Directory Control";
		case IRP_MJ_FILE_SYSTEM_CONTROL      : return L"File System Control";
		case IRP_MJ_DEVICE_CONTROL           : return L"Device Control";
		case IRP_MJ_INTERNAL_DEVICE_CONTROL  : return L"Internal Device Control";
		case IRP_MJ_SHUTDOWN                 : return L"Shutdown";
		case IRP_MJ_LOCK_CONTROL             : return L"Lock Control";
		case IRP_MJ_CLEANUP                  : return L"Cleanup";
		case IRP_MJ_CREATE_MAILSLOT          : return L"Create mailslot";
		case IRP_MJ_QUERY_SECURITY           : return L"Query Security";
		case IRP_MJ_SET_SECURITY             : return L"Set Security";
		case IRP_MJ_POWER                    : return L"Power";
		case IRP_MJ_SYSTEM_CONTROL           : return L"System Control";
		case IRP_MJ_DEVICE_CHANGE            : return L"Device Change";
		case IRP_MJ_QUERY_QUOTA              : return L"Query Quota";
		case IRP_MJ_SET_QUOTA                : return L"Set Quota";
		case IRP_MJ_PNP                      : return L"Pnp";
		default:							   return L"Unkown";
	}
	
}
