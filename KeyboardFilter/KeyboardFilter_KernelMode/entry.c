#include <ntifs.h>
#include <ntstrsafe.h>
#include <ntddkbd.h>

#define FILTER_LOCKTAG	'kdft'
#define GLOBAL_LOCKTAG	'gdkd'
#define MEMORY_TAG		'mtag'
#define MAX_KEYBOARD_SCANCODE 0xFF
#define MAX_KEYBOARD_DEVICE_COUNT 10

#define IOCTL_ENABLE_FILTER		CTL_CODE(FILE_DEVICE_KEYBOARD, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_ENABLE_LOG		CTL_CODE(FILE_DEVICE_KEYBOARD, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_ORDER_ADDKEY		CTL_CODE(FILE_DEVICE_KEYBOARD, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_ORDER_DELKEY		CTL_CODE(FILE_DEVICE_KEYBOARD, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_READ_KRNLDATA		CTL_CODE(FILE_DEVICE_KEYBOARD, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _GLOBAL_CONTEXT {
	PDRIVER_OBJECT pDriverObject;
	PDEVICE_OBJECT pControlDevice;
	UNICODE_STRING SymbolicLinkName;
	IO_REMOVE_LOCK ioRemoveLock;
	BOOLEAN IsSymbolicLinkCreated;
	BOOLEAN IsUnloading;
}GLOBAL_CONTEXT, *PGLOBAL_CONTEXT;

typedef struct _KEYBOARD_DEVICE_EXTENSION {
	PDEVICE_OBJECT pLowerDeviceObject;						// 记录下家：我的直接下线是谁（用于转发请求）
	PDEVICE_OBJECT pSelfDeviceObject;						// 记录自己：我的设备对象是谁（用于转发请求）
	IO_REMOVE_LOCK ioRemoveLock;							// 移除锁 (最重要！防止设备被拔了但请求还在跑导致的蓝屏)
	ULONG TotalKeys;										// 总共按键数
	ULONG BlockedKeys;										// 总共被阻止的按键数
	ULONG BlockedCount;										// 被阻止的按键个数(和 BlockedScancodes 关联)
	USHORT BlockedScancodes[MAX_KEYBOARD_SCANCODE];			// 键盘扫描码数组，用于存储被阻止的按键扫描码
	BOOLEAN EnableFiler;									// 过滤器是否启用
	BOOLEAN IsLog;											// 日志是否启用
	BOOLEAN IsCtrlPressed;									// Ctrl键是否被按下
	BOOLEAN IsShiftPressed;									// Shift键是否被按下
	BOOLEAN IsAltPressed;									// Alt键是否被按下 
	BOOLEAN IsDetach;
	PIRP pPendingReadIrp;
	KSPIN_LOCK PendingIrpSpinLock;
} KEYBOARD_DEVICE_EXTENSION, *PKEYBOARD_DEVICE_EXTENSION;

typedef struct _DEVICE_CONTROL_CONTEXT {
	DWORD Size;
	DWORD BlockedCount;
	BOOLEAN IsEnable;
	BOOLEAN IsLog;
	USHORT ScanCode[1];
}DEVICE_CONTROL_CONTEXT, * PDEVICE_CONTROL_CONTEXT;

GLOBAL_CONTEXT g_GlobalContext = { 0 };

NTSTATUS DriverEntry(PDRIVER_OBJECT pDriverObject, PUNICODE_STRING pRegistryPath);
NTSTATUS DriverUnload(PDRIVER_OBJECT pDriverObject);
NTSTATUS CreateControlDevice(PDRIVER_OBJECT pDriverObject);
VOID DeleteControlDevice(VOID);

NTSTATUS AttachDeviceKeyBoard(PDRIVER_OBJECT pDriverObject);
NTSTATUS DetachDeviceKeyBoard(VOID);

NTSTATUS ReadCompleteRoutine(PDEVICE_OBJECT pDeviceObject, PIRP pIrp, PVOID Context);
PCSTR GetKeyName(USHORT ScanCode, USHORT Flag);
VOID LogKeyPress(PKEYBOARD_DEVICE_EXTENSION pKbdDevExt, PKEYBOARD_INPUT_DATA pKeyIn, BOOLEAN Block);
BOOLEAN ShouldKeyBlock(PKEYBOARD_DEVICE_EXTENSION pKbdDevExt, PKEYBOARD_INPUT_DATA pKeyIn);

NTSTATUS DispatchPass(PDEVICE_OBJECT pDeviceObject, PIRP pIrp);
NTSTATUS DispatchCreate(PDEVICE_OBJECT pDeviceObject, PIRP pIrp);
NTSTATUS DispatchClose(PDEVICE_OBJECT pDeviceObject, PIRP pIrp);
NTSTATUS DispatchRead(PDEVICE_OBJECT pDeviceObject, PIRP pIrp);
NTSTATUS DispatchDeviceControl(PDEVICE_OBJECT pDeviceObject, PIRP pIrp);
NTSTATUS DispatchPnp(PDEVICE_OBJECT pDeviceObject, PIRP pIrp);
NTSTATUS DispatchPower(PDEVICE_OBJECT pDeviceObject, PIRP pIrp);


NTSTATUS DriverEntry(PDRIVER_OBJECT pDriverObject, PUNICODE_STRING pRegistryPath) {

	//DbgBreakPoint();

	UNREFERENCED_PARAMETER(pRegistryPath);
	NTSTATUS rtn = 0;
	DbgPrint("[cnHHHHHcn] Driver Called\r\n");

	DbgPrint("[cnHHHHHcn] Initialize Global Context\r\n");
	RtlZeroMemory(&g_GlobalContext, sizeof(GLOBAL_CONTEXT));
	g_GlobalContext.pDriverObject = pDriverObject;
	g_GlobalContext.IsSymbolicLinkCreated = FALSE;
	g_GlobalContext.IsUnloading = FALSE;

	DbgPrint("[cnHHHHHcn] Set Dispatch Routine\r\n");
	for (size_t i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; i++) {
		pDriverObject->MajorFunction[i] = DispatchPass;
	}

	pDriverObject->MajorFunction[IRP_MJ_CREATE] = DispatchCreate;
	pDriverObject->MajorFunction[IRP_MJ_CLOSE] = DispatchClose;
	pDriverObject->MajorFunction[IRP_MJ_READ] = DispatchRead;
	pDriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchDeviceControl;
	pDriverObject->MajorFunction[IRP_MJ_PNP] = DispatchPnp;
	pDriverObject->MajorFunction[IRP_MJ_POWER] = DispatchPower;
	pDriverObject->DriverUnload = DriverUnload;

	rtn = CreateControlDevice(pDriverObject);
	if (!NT_SUCCESS(rtn)) {
		DbgPrint("[cnHHHHHcn] CreateControlDevice Failed: %08x\r\n", rtn);
		return rtn;
	}

	rtn = AttachDeviceKeyBoard(pDriverObject);
	if (!NT_SUCCESS(rtn)) {
		DbgPrint("[cnHHHHHcn] KeyBoardFilter Driver Attach Failed: %08x\r\n", rtn);
		DeleteControlDevice();
		return rtn;
	}

	DbgPrint("[cnHHHHHcn] Driver Entry Completed\r\n");

	return STATUS_SUCCESS;
}

NTSTATUS DriverUnload(PDRIVER_OBJECT pDriverObject) {
	UNREFERENCED_PARAMETER(pDriverObject);
	DbgPrint("[cnHHHHHcn] Driver Unloaded\r\n");
	g_GlobalContext.IsUnloading = TRUE;

	LARGE_INTEGER Delay = { 0 };
	Delay.QuadPart = -5000000L;	  // 1 second delay
	KeDelayExecutionThread(KernelMode, FALSE, &Delay);

	DetachDeviceKeyBoard();

	DeleteControlDevice();

	DbgPrint("[cnHHHHHcn] Driver Unload Completed\r\n");

	return STATUS_SUCCESS;
}

NTSTATUS CreateControlDevice(PDRIVER_OBJECT pDriverObject){
	NTSTATUS rtn = STATUS_SUCCESS;
	UNICODE_STRING DeviceName = { 0 };
	UNICODE_STRING SymbolicLinkName = { 0 };
	PDEVICE_OBJECT pControlDevice = NULL;

	DbgPrint("[cnHHHHHcn] Create Control Device called\r\n");

	RtlInitUnicodeString(&DeviceName, L"\\Device\\cnHHHHHcn_KeyboardFilter");
	RtlInitUnicodeString(&SymbolicLinkName, L"\\??\\cnHHHHHcn_KeyboardFilter");

	if(pDriverObject == NULL) {
		DbgPrint("[cnHHHHHcn] Driver Object is NULL\r\n");
		return STATUS_INVALID_PARAMETER;
	}

	rtn = IoCreateDevice(
		pDriverObject,
		0,
		&DeviceName,
		FILE_DEVICE_KEYBOARD,
		FILE_DEVICE_SECURE_OPEN,
		FALSE,
		&pControlDevice
	);
	if (!NT_SUCCESS(rtn)) {
		DbgPrint("[cnHHHHHcn] Create Control Device Failed: 0x%08x\r\n", rtn);
		pControlDevice = NULL;
		return rtn;
	}

	pControlDevice->Flags |= DO_BUFFERED_IO;
	pControlDevice->Flags &= ~DO_DEVICE_INITIALIZING;

	rtn = IoCreateSymbolicLink(&SymbolicLinkName, &DeviceName);
	if (!NT_SUCCESS(rtn)) {
		DbgPrint("[cnHHHHHcn] Create Symbolic Link Failed: 0x%08x\r\n", rtn);
		IoDeleteDevice(pControlDevice);
		g_GlobalContext.pControlDevice = NULL;
		RtlZeroMemory(&g_GlobalContext.SymbolicLinkName, sizeof(UNICODE_STRING));
		return rtn;
	}
	g_GlobalContext.IsSymbolicLinkCreated = TRUE;

	IoInitializeRemoveLock(&g_GlobalContext.ioRemoveLock, GLOBAL_LOCKTAG, 0, 0);
	rtn = IoAcquireRemoveLock(&g_GlobalContext.ioRemoveLock, (PVOID)pControlDevice);

	if (!NT_SUCCESS(rtn)) {
		DbgPrint("[cnHHHHHcn] Initialize Remove Lock Failed: 0x%08x\r\n", rtn);
		IoDeleteSymbolicLink(&SymbolicLinkName);
		IoDeleteDevice(pControlDevice);
		return rtn;
	}

	g_GlobalContext.pControlDevice = pControlDevice;

	RtlCopyMemory(&g_GlobalContext.SymbolicLinkName ,&SymbolicLinkName, sizeof(UNICODE_STRING));
	g_GlobalContext.IsSymbolicLinkCreated = TRUE;

	return rtn;
}

VOID DeleteControlDevice(VOID){
	if (g_GlobalContext.pControlDevice) {
		if (g_GlobalContext.IsSymbolicLinkCreated) {
			IoDeleteSymbolicLink(&g_GlobalContext.SymbolicLinkName);
			g_GlobalContext.IsSymbolicLinkCreated = FALSE;
		}
		IoReleaseRemoveLockAndWait(&g_GlobalContext.ioRemoveLock, g_GlobalContext.pControlDevice);
		IoDeleteDevice(g_GlobalContext.pControlDevice);
		g_GlobalContext.pControlDevice = NULL;
	}
}

NTSTATUS AttachDeviceKeyBoard(PDRIVER_OBJECT pDriverObject) {
	NTSTATUS rtn = STATUS_SUCCESS;							 
	UNICODE_STRING KbdClass = { 0 };						 // 键盘类设备名称
	PFILE_OBJECT pFileObject = NULL;						 // 文件对象指针，获取设备对象指针
	PDEVICE_OBJECT pKbdDeviceObject = NULL;					 // 键盘设备对象指针(真正)
	PDEVICE_OBJECT pFilterDeviceObject = NULL;				 // 过滤设备对象指针 
	PDEVICE_OBJECT pLowerDeviceObject = NULL;				 // 下层设备对象指针(设备栈)f
	PKEYBOARD_DEVICE_EXTENSION pKbdDevExt = NULL;			 
	ULONG DeviceCount = 0;								 // 成功绑定设备对象数量
	WCHAR DeviceNameBuffer[64] = { 0 };						 // 设备名称缓冲区
	
	

	for (ULONG i = 0; i < MAX_KEYBOARD_DEVICE_COUNT; i++) {
		RtlStringCbPrintfW(DeviceNameBuffer, sizeof(DeviceNameBuffer), L"\\Device\\KeyboardClass%u", i);
		RtlInitUnicodeString(&KbdClass, DeviceNameBuffer);
		rtn = IoGetDeviceObjectPointer(&KbdClass, FILE_READ_DATA, &pFileObject, &pKbdDeviceObject);
		if (!NT_SUCCESS(rtn)) continue;
		rtn = IoCreateDevice(
			pDriverObject,
			sizeof(KEYBOARD_DEVICE_EXTENSION),
			NULL,
			FILE_DEVICE_KEYBOARD,
			0,
			FALSE,
			&pFilterDeviceObject
		);
		if (!NT_SUCCESS(rtn)) {
			ObDereferenceObject(pFileObject);
			continue;
		}
		pLowerDeviceObject = IoAttachDeviceToDeviceStack(pFilterDeviceObject, pKbdDeviceObject);
		if (!pLowerDeviceObject) {
			IoDeleteDevice(pFilterDeviceObject);
			ObDereferenceObject(pFileObject);
			continue;
		}
		pKbdDevExt = (PKEYBOARD_DEVICE_EXTENSION)pFilterDeviceObject->DeviceExtension;
		RtlZeroMemory(pKbdDevExt, sizeof(KEYBOARD_DEVICE_EXTENSION));
		pKbdDevExt->pLowerDeviceObject = pLowerDeviceObject;
		pKbdDevExt->pSelfDeviceObject = pFilterDeviceObject;
		pKbdDevExt->EnableFiler = TRUE;
		pKbdDevExt->IsLog = TRUE;

		pKbdDevExt->TotalKeys = 0;
		pKbdDevExt->BlockedKeys = 0;

		pKbdDevExt->BlockedCount = 0;

		pKbdDevExt->IsAltPressed = FALSE;
		pKbdDevExt->IsCtrlPressed = FALSE;
		pKbdDevExt->IsShiftPressed = FALSE;

		IoInitializeRemoveLock(&pKbdDevExt->ioRemoveLock, FILTER_LOCKTAG, 0, 0);
		rtn = IoAcquireRemoveLock(&pKbdDevExt->ioRemoveLock, (PVOID)pFilterDeviceObject);
		if (!NT_SUCCESS(rtn)) {
			IoDetachDevice(pLowerDeviceObject);
			IoDeleteDevice(pFilterDeviceObject);
			ObDereferenceObject(pFileObject);
			continue;
		}

		KeInitializeSpinLock(&pKbdDevExt->PendingIrpSpinLock);
		pKbdDevExt->pPendingReadIrp = NULL;
		pKbdDevExt->IsDetach = FALSE;
		pFilterDeviceObject->Flags |= pLowerDeviceObject->Flags | (DO_BUFFERED_IO | DO_DIRECT_IO | DO_POWER_PAGABLE);
		pFilterDeviceObject->Characteristics = pLowerDeviceObject->Characteristics;
		pFilterDeviceObject->Type = pLowerDeviceObject->Type;
		pFilterDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

		ObDereferenceObject(pFileObject);

		DeviceCount++;

		DbgPrint("[cnHHHHHcn] Attached Device Name: %wZ\r\n", &KbdClass);
	}

	if (!DeviceCount) {
		DbgPrint("[cnHHHHHcn] Not Found KeyBoard Device\r\n");
		return STATUS_UNSUCCESSFUL;
	}

	DbgPrint("[cnHHHHHcn] Attached Device Name Count: %d\r\n", DeviceCount);

	return STATUS_SUCCESS;
}

NTSTATUS DetachDeviceKeyBoard(VOID)
{
	PDEVICE_OBJECT pCurrentDeviceObject = NULL;
	PDEVICE_OBJECT pNextDeviceObject = NULL;
	PKEYBOARD_DEVICE_EXTENSION pKbdDevExt = NULL;
	PDEVICE_OBJECT* pDeviceObjectArray = NULL;
	ULONG DeviceCount = 0;
	LARGE_INTEGER Delay = { 0 };
	KIRQL OldIrql = { 0 };

	pDeviceObjectArray = (PDEVICE_OBJECT*)ExAllocatePool(NonPagedPool, sizeof(PDEVICE_OBJECT) * MAX_KEYBOARD_DEVICE_COUNT);

	if (!pDeviceObjectArray) {

		return STATUS_INSUFFICIENT_RESOURCES;
	}
	pCurrentDeviceObject = g_GlobalContext.pDriverObject->DeviceObject;

	while (pCurrentDeviceObject && DeviceCount < MAX_KEYBOARD_DEVICE_COUNT) {
		pNextDeviceObject = pCurrentDeviceObject->NextDevice;
		if (pCurrentDeviceObject != g_GlobalContext.pControlDevice) {
			pDeviceObjectArray[DeviceCount] = pCurrentDeviceObject;
			DeviceCount++;
		}
		pCurrentDeviceObject = pNextDeviceObject;
	}
	for (size_t i = 0; i < DeviceCount; i++) {
		pCurrentDeviceObject = pDeviceObjectArray[i];
		pKbdDevExt = (PKEYBOARD_DEVICE_EXTENSION)pCurrentDeviceObject->DeviceExtension;
		KeAcquireSpinLock(&pKbdDevExt->PendingIrpSpinLock, &OldIrql);
		pKbdDevExt->IsDetach = TRUE;
		KeReleaseSpinLock(&pKbdDevExt->PendingIrpSpinLock, OldIrql);
	}

	DbgPrint("[cnHHHHHcn] Wait for Pending IRPs\r\n");
	for (size_t i = 0; i < DeviceCount; i++) {
		pCurrentDeviceObject = pDeviceObjectArray[i];
		pKbdDevExt = (PKEYBOARD_DEVICE_EXTENSION)pCurrentDeviceObject->DeviceExtension;
		IoReleaseRemoveLockAndWait(&pKbdDevExt->ioRemoveLock, (PVOID)pKbdDevExt->pSelfDeviceObject);
		IoDeleteDevice(pCurrentDeviceObject);
	}

	for (size_t i = 0; i < DeviceCount; i++) {
		pCurrentDeviceObject = pDeviceObjectArray[i];
		pKbdDevExt = (PKEYBOARD_DEVICE_EXTENSION)pCurrentDeviceObject->DeviceExtension;
		if (pKbdDevExt->pLowerDeviceObject) {
			IoDetachDevice(pKbdDevExt->pLowerDeviceObject);
			pKbdDevExt->pLowerDeviceObject = NULL;
		}
	}

	ExFreePool(pDeviceObjectArray);
	DbgPrint("[cnHHHHHcn] All Device Detached Successfully\r\n");
	return STATUS_SUCCESS;
}

NTSTATUS DispatchPass(PDEVICE_OBJECT pDeviceObject, PIRP pIrp) {
	if (pDeviceObject == g_GlobalContext.pControlDevice) {

		DbgPrint("[cnHHHHHcn] Dispatch Routine Called\r\n");

		pIrp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
		pIrp->IoStatus.Information = 0;

		IoCompleteRequest(pIrp, IO_NO_INCREMENT);
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	IoSkipCurrentIrpStackLocation(pIrp);
	PKEYBOARD_DEVICE_EXTENSION pKbdDevExt = (PKEYBOARD_DEVICE_EXTENSION)pDeviceObject->DeviceExtension;
	return IoCallDriver(pKbdDevExt->pLowerDeviceObject, pIrp);

}

NTSTATUS DispatchCreate(PDEVICE_OBJECT pDeviceObject, PIRP pIrp) {
	NTSTATUS rtn = STATUS_SUCCESS;
	PKEYBOARD_DEVICE_EXTENSION pKbdDevExt = NULL;

	if(pDeviceObject == g_GlobalContext.pControlDevice) {
		if(g_GlobalContext.IsUnloading) {
			pIrp->IoStatus.Status = STATUS_DELETE_PENDING;
			pIrp->IoStatus.Information = 0;
			IoCompleteRequest(pIrp, IO_NO_INCREMENT);
			return STATUS_DELETE_PENDING;
		}
		rtn = IoAcquireRemoveLock(&g_GlobalContext.ioRemoveLock, (PVOID)pIrp);
		if(!NT_SUCCESS(rtn)) {
			pIrp->IoStatus.Status = rtn;
			pIrp->IoStatus.Information = 0;
			IoCompleteRequest(pIrp, IO_NO_INCREMENT); 
			return rtn;
		}
		pIrp->IoStatus.Status = STATUS_SUCCESS;
		pIrp->IoStatus.Information = 0;
		IoCompleteRequest(pIrp, IO_NO_INCREMENT);
		IoReleaseRemoveLock(&g_GlobalContext.ioRemoveLock, (PVOID)pIrp);
		return STATUS_SUCCESS;
	}
	pKbdDevExt = (PKEYBOARD_DEVICE_EXTENSION)pDeviceObject->DeviceExtension;
	IoSkipCurrentIrpStackLocation(pIrp);
	return IoCallDriver(pKbdDevExt->pLowerDeviceObject, pIrp);
}

NTSTATUS DispatchClose(PDEVICE_OBJECT pDeviceObject, PIRP pIrp) {
	NTSTATUS rtn = STATUS_SUCCESS;
	PKEYBOARD_DEVICE_EXTENSION pKbdDevExt = NULL;

	if (pDeviceObject == g_GlobalContext.pControlDevice) {
		if (g_GlobalContext.IsUnloading) {
			pIrp->IoStatus.Status = STATUS_DELETE_PENDING;
			pIrp->IoStatus.Information = 0;
			IoCompleteRequest(pIrp, IO_NO_INCREMENT);
			return STATUS_DELETE_PENDING;
		}
		rtn = IoAcquireRemoveLock(&g_GlobalContext.ioRemoveLock, (PVOID)pIrp);
		if (!NT_SUCCESS(rtn)) {
			pIrp->IoStatus.Status = rtn;
			pIrp->IoStatus.Information = 0;
			IoCompleteRequest(pIrp, IO_NO_INCREMENT);
			return rtn;
		}
		pIrp->IoStatus.Status = STATUS_SUCCESS;
		pIrp->IoStatus.Information = 0;
		IoCompleteRequest(pIrp, IO_NO_INCREMENT);
		IoReleaseRemoveLock(&g_GlobalContext.ioRemoveLock, (PVOID)pIrp);
		return STATUS_SUCCESS;
	}
	IoSkipCurrentIrpStackLocation(pIrp);
	pKbdDevExt = (PKEYBOARD_DEVICE_EXTENSION)pDeviceObject->DeviceExtension;
	return IoCallDriver(pKbdDevExt->pLowerDeviceObject, pIrp);
}

NTSTATUS DispatchRead(PDEVICE_OBJECT pDeviceObject, PIRP pIrp) {
	NTSTATUS rtn = STATUS_SUCCESS;
	PKEYBOARD_DEVICE_EXTENSION pKbdDevExt = NULL;
	KIRQL OldIrql = 0;

	if (pDeviceObject == g_GlobalContext.pControlDevice) {
		pIrp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
		pIrp->IoStatus.Information = 0;
		IoCompleteRequest(pIrp, IO_NO_INCREMENT);
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	pKbdDevExt = (PKEYBOARD_DEVICE_EXTENSION)pDeviceObject->DeviceExtension;

	rtn = IoAcquireRemoveLock(&pKbdDevExt->ioRemoveLock, (PVOID)pIrp);

	if (!NT_SUCCESS(rtn)) {

		pIrp->IoStatus.Status = rtn;
		pIrp->IoStatus.Information = 0;

		IoCompleteRequest(pIrp, IO_NO_INCREMENT);

		return rtn;
	}

	KeAcquireSpinLock(&pKbdDevExt->PendingIrpSpinLock, &OldIrql);

	if (pKbdDevExt->IsDetach) {
		KeReleaseSpinLock(&pKbdDevExt->PendingIrpSpinLock, OldIrql);
		pIrp->IoStatus.Status = STATUS_DELETE_PENDING;
		pIrp->IoStatus.Information = 0;
		IoCompleteRequest(pIrp, IO_NO_INCREMENT);
		return STATUS_DELETE_PENDING;
	}
	pKbdDevExt->pPendingReadIrp = pIrp;
	KeReleaseSpinLock(&pKbdDevExt->PendingIrpSpinLock, OldIrql);

	if (!NT_SUCCESS(rtn)) {

		KeAcquireSpinLock(&pKbdDevExt->PendingIrpSpinLock, &OldIrql);
		pKbdDevExt->pPendingReadIrp = NULL;
		KeReleaseSpinLock(&pKbdDevExt->PendingIrpSpinLock, OldIrql);

		IoReleaseRemoveLock(&pKbdDevExt->ioRemoveLock, (PVOID)pIrp);
		pIrp->IoStatus.Status = rtn;
		pIrp->IoStatus.Information = 0;
		IoCompleteRequest(pIrp, IO_NO_INCREMENT);
		return rtn;
	}

	IoCopyCurrentIrpStackLocationToNext(pIrp);

	IoSetCompletionRoutine(pIrp, ReadCompleteRoutine, pDeviceObject, TRUE, TRUE, TRUE);

	return IoCallDriver(pKbdDevExt->pLowerDeviceObject, pIrp);
}

NTSTATUS DispatchDeviceControl(PDEVICE_OBJECT pDeviceObject, PIRP pIrp){
	UNREFERENCED_PARAMETER(pDeviceObject);

	NTSTATUS rtn = STATUS_SUCCESS;
	PDEVICE_OBJECT pFilterDevice = NULL;
	PIO_STACK_LOCATION pIrpStack = NULL;
	PKEYBOARD_DEVICE_EXTENSION pKbdDevExt = NULL;
	KIRQL OldIrql = { 0 };
	CHAR OrderStr[32] = { 0 };
	ULONG ControlCode = 0;
	ULONG InputBufferLen = 0;
	ULONG OutputBufferLen =0;
	PVOID SystemBuffer = NULL;
	PDEVICE_CONTROL_CONTEXT Buffer = NULL;
	ULONG BytesReturned = 0;

	pIrpStack = IoGetCurrentIrpStackLocation(pIrp);
	ControlCode = pIrpStack->Parameters.DeviceIoControl.IoControlCode;
	InputBufferLen = pIrpStack->Parameters.DeviceIoControl.InputBufferLength;
	OutputBufferLen = pIrpStack->Parameters.DeviceIoControl.OutputBufferLength;
	SystemBuffer = pIrp->AssociatedIrp.SystemBuffer;

	pFilterDevice = g_GlobalContext.pDriverObject->DeviceObject;
	pKbdDevExt = (PKEYBOARD_DEVICE_EXTENSION)pFilterDevice->DeviceExtension;

	switch (ControlCode) {
		case IOCTL_READ_KRNLDATA: {
			SIZE_T AllocateSize = sizeof(DEVICE_CONTROL_CONTEXT) + sizeof(USHORT) * (pKbdDevExt->BlockedCount ? (pKbdDevExt->BlockedCount - 1) : 0);
			Buffer = (PDEVICE_CONTROL_CONTEXT)ExAllocatePoolWithTag(NonPagedPool, AllocateSize, MEMORY_TAG);
			if (!Buffer) {
				rtn = STATUS_INSUFFICIENT_RESOURCES;
				BytesReturned = 0;
				break;
			}

			KeAcquireSpinLock(&pKbdDevExt->PendingIrpSpinLock, &OldIrql);
			RtlZeroMemory(Buffer, AllocateSize);
			Buffer->Size = AllocateSize;
			Buffer->IsEnable = pKbdDevExt->EnableFiler;
			Buffer->IsLog = pKbdDevExt->IsLog;
			Buffer->BlockedCount = pKbdDevExt->BlockedCount;
			RtlZeroMemory(Buffer->ScanCode, sizeof(USHORT) * pKbdDevExt->BlockedCount);
			if (pKbdDevExt->BlockedCount != 0) {
				RtlCopyMemory(Buffer->ScanCode, pKbdDevExt->BlockedScancodes, sizeof(USHORT) * pKbdDevExt->BlockedCount);
			}
			KeReleaseSpinLock(&pKbdDevExt->PendingIrpSpinLock, OldIrql);

			if (OutputBufferLen < AllocateSize) {
				ExFreePoolWithTag(Buffer, MEMORY_TAG);
				rtn = STATUS_BUFFER_TOO_SMALL;
				BytesReturned = 0;
				break;
			}

			RtlCopyMemory(SystemBuffer, Buffer, AllocateSize);
			BytesReturned = (ULONG)AllocateSize;
			ExFreePoolWithTag(Buffer, MEMORY_TAG);

			RtlInitAnsiString(&OrderStr, "Get Keyboard Filter Kernel Data");
			break;
		}
		case IOCTL_ENABLE_FILTER: {
			KeAcquireSpinLock(&pKbdDevExt->PendingIrpSpinLock, &OldIrql);
			pKbdDevExt->EnableFiler = ((PDEVICE_CONTROL_CONTEXT)SystemBuffer)->IsEnable;
			KeReleaseSpinLock(&pKbdDevExt->PendingIrpSpinLock, OldIrql);
			RtlStringCbPrintfA(&OrderStr, sizeof(OrderStr), "Keyboard Filter: %s", (pKbdDevExt->EnableFiler ? "ON" : "OFF"));
			break;
		}
		case IOCTL_ENABLE_LOG: {
			KeAcquireSpinLock(&pKbdDevExt->PendingIrpSpinLock, &OldIrql);
			pKbdDevExt->IsLog = ((PDEVICE_CONTROL_CONTEXT)SystemBuffer)->IsLog;
			KeReleaseSpinLock(&pKbdDevExt->PendingIrpSpinLock, OldIrql);
			RtlStringCbPrintfA(&OrderStr, sizeof(OrderStr), "Keyboard Log: %s", (pKbdDevExt->IsLog ? "ON" : "OFF"));
			break;
		}
		case IOCTL_ORDER_ADDKEY: {
			KeAcquireSpinLock(&pKbdDevExt->PendingIrpSpinLock, &OldIrql);
			pKbdDevExt->BlockedCount = ((PDEVICE_CONTROL_CONTEXT)SystemBuffer)->BlockedCount;
			RtlCopyMemory(pKbdDevExt->BlockedScancodes, ((PDEVICE_CONTROL_CONTEXT)SystemBuffer)->ScanCode, ((PDEVICE_CONTROL_CONTEXT)SystemBuffer)->BlockedCount * sizeof(USHORT));
			KeReleaseSpinLock(&pKbdDevExt->PendingIrpSpinLock, OldIrql);
			RtlInitAnsiString(&OrderStr, "Keyboard Order: Add Key");
			break;
		}
		case IOCTL_ORDER_DELKEY: {
			KeAcquireSpinLock(&pKbdDevExt->PendingIrpSpinLock, &OldIrql);
			pKbdDevExt->BlockedCount = ((PDEVICE_CONTROL_CONTEXT)SystemBuffer)->BlockedCount;
			RtlCopyMemory(pKbdDevExt->BlockedScancodes, ((PDEVICE_CONTROL_CONTEXT)SystemBuffer)->ScanCode, ((PDEVICE_CONTROL_CONTEXT)SystemBuffer)->BlockedCount * sizeof(USHORT));
			KeReleaseSpinLock(&pKbdDevExt->PendingIrpSpinLock, OldIrql);
			RtlInitAnsiString(&OrderStr, "Keyboard Order: Del Key");
			break;
		}
		default: {
			RtlInitAnsiString(&OrderStr, "Not supported IOCTL");
			rtn = STATUS_INVALID_DEVICE_REQUEST;
		}
	}

	DbgPrint("[cnHHHHHcn] Status: 0x%08x  IOCTL: 0x%08x | In: %u | Out: %u\r\n", rtn, ControlCode, InputBufferLen, OutputBufferLen);
	DbgPrint("[cnHHHHHcn] %s\r\n", OrderStr);

	pIrp->IoStatus.Status = rtn;
	pIrp->IoStatus.Information = BytesReturned;
	IoCompleteRequest(pIrp, IO_NO_INCREMENT);
	return rtn;
}

NTSTATUS DispatchPnp(PDEVICE_OBJECT pDeviceObject, PIRP pIrp) {
	NTSTATUS rtn = STATUS_SUCCESS;
	PIO_STACK_LOCATION pIrpStack = NULL;
	PKEYBOARD_DEVICE_EXTENSION pKbdDevExt = NULL;

	if (pDeviceObject == g_GlobalContext.pControlDevice) {
		pIrp->IoStatus.Status = STATUS_SUCCESS;
		pIrp->IoStatus.Information = 0;
		IoCompleteRequest(pIrp, IO_NO_INCREMENT);
		return STATUS_SUCCESS;
	}

	pKbdDevExt = (PKEYBOARD_DEVICE_EXTENSION)pDeviceObject->DeviceExtension;

	pIrpStack = IoGetCurrentIrpStackLocation(pIrp);
	
	switch (pIrpStack->MinorFunction) {
		case IRP_MN_REMOVE_DEVICE:
		{
			IoSkipCurrentIrpStackLocation(pIrp);
			rtn = IoCallDriver(pKbdDevExt->pLowerDeviceObject, pIrp);
			IoDetachDevice(pKbdDevExt->pLowerDeviceObject);
			return rtn;
		}
		default: 
		{
			IoSkipCurrentIrpStackLocation(pIrp);
			rtn = IoCallDriver(pDeviceObject, pIrp);
		}
	}
	return rtn;
	
}

NTSTATUS DispatchPower(PDEVICE_OBJECT pDeviceObject, PIRP pIrp) {
	NTSTATUS rtn = STATUS_SUCCESS;
	PKEYBOARD_DEVICE_EXTENSION pKbdDevExt = NULL;

	if (pDeviceObject == g_GlobalContext.pControlDevice) {
		PoStartNextPowerIrp(pIrp);
		pIrp->IoStatus.Status = STATUS_SUCCESS;
		pIrp->IoStatus.Information = 0;
		IoCompleteRequest(pIrp, IO_NO_INCREMENT);
		return STATUS_SUCCESS;
	}

	pKbdDevExt = (PKEYBOARD_DEVICE_EXTENSION)pDeviceObject->DeviceExtension;

	PoStartNextPowerIrp(pIrp);

	IoSkipCurrentIrpStackLocation(pIrp);

	PoCallDriver(pKbdDevExt->pLowerDeviceObject, pIrp);

	return rtn;
}

NTSTATUS ReadCompleteRoutine(PDEVICE_OBJECT pDeviceObject, PIRP pIrp, PVOID Context) {
	UNREFERENCED_PARAMETER(pDeviceObject);
	UNREFERENCED_PARAMETER(pIrp);
	PDEVICE_OBJECT pFilterDeviceObject = (PDEVICE_OBJECT)Context;
	PKEYBOARD_DEVICE_EXTENSION pKbdDevExt = NULL;
	PIO_STACK_LOCATION pIrpStack = NULL;
	PKEYBOARD_INPUT_DATA pKbdInData = NULL;
	ULONG NumberOfKeys = 0;
	BOOLEAN Modifed = FALSE;
	KIRQL OldIrql = { 0 };
	ULONG i = 0, j = 0;

	pKbdDevExt = (PKEYBOARD_DEVICE_EXTENSION)pFilterDeviceObject->DeviceExtension;
	
	KeAcquireSpinLock(&pKbdDevExt->PendingIrpSpinLock, &OldIrql);

	if (pKbdDevExt->pPendingReadIrp == pIrp) {
		pKbdDevExt->pPendingReadIrp = NULL;
	}

	KeReleaseSpinLock(&pKbdDevExt->PendingIrpSpinLock, OldIrql);

	if (pKbdDevExt->IsDetach) goto Exit;
	
	if (!NT_SUCCESS(pIrp->IoStatus.Status)) goto Exit;
	
	pIrpStack = IoGetCurrentIrpStackLocation(pIrp);
	pKbdInData = (PKEYBOARD_INPUT_DATA)pIrp->AssociatedIrp.SystemBuffer;

	if (!pKbdInData) goto Exit;

	NumberOfKeys = (ULONG)(pIrp->IoStatus.Information / sizeof(KEYBOARD_INPUT_DATA));

	if (!NumberOfKeys) goto Exit;

	j = 0;
	for (i = 0; i < NumberOfKeys; i++) {
		PKEYBOARD_INPUT_DATA keys = &pKbdInData[i];
		
		BOOLEAN ShouldBlock = FALSE;

		pKbdDevExt->TotalKeys++;

		if (pKbdDevExt->IsLog) {
			LogKeyPress(pKbdDevExt, keys, FALSE);
		}

		if (pKbdDevExt->EnableFiler) {
			ShouldBlock = ShouldKeyBlock(pKbdDevExt, keys);
		}

		if (ShouldBlock) {
			pKbdDevExt->BlockedKeys++;
			Modifed = TRUE;
			if (pKbdDevExt->IsLog) {
				LogKeyPress(pKbdDevExt, keys, TRUE);
			}
		}else {
			// 按键未阻止，保留在数据缓冲区中
			if (i != j) {
				// 索引不同，将未被按键数据阻止前移。
				pKbdInData[j] = pKbdInData[i];
			}
			j++;
		}
		if (Modifed) {
			pIrp->IoStatus.Information = j * sizeof(KEYBOARD_INPUT_DATA);
		}

	}

Exit:

	IoReleaseRemoveLock(
		&pKbdDevExt->ioRemoveLock,
		(PVOID)pIrp
	);

	if (pIrp->PendingReturned) {
		IoMarkIrpPending(pIrp);
	}

	return STATUS_SUCCESS;
}

VOID LogKeyPress(PKEYBOARD_DEVICE_EXTENSION pKbdDevExt, PKEYBOARD_INPUT_DATA pKeyIn, BOOLEAN Block) {
	UNREFERENCED_PARAMETER(pKbdDevExt);
	PCSTR KeyName = GetKeyName(pKeyIn->MakeCode, pKeyIn->Flags);
	PCSTR KeyAction = (pKeyIn->Flags & KEY_BREAK) ? "UP" : "DN";
	PCSTR KeyState = Block ? "X" : "O";
	DbgPrint("%s %s | %-10s | ScanCode: 0x%04x | Flags: 0x%04x\r\n", KeyState, KeyAction, KeyName, pKeyIn->MakeCode, pKeyIn->Flags);
}

PCSTR GetKeyName(USHORT ScanCode, USHORT Flags) {
	BOOLEAN IsE0 = (Flags & KEY_E0) != 0;
	if (!IsE0) {
		switch (ScanCode) {
		case 0x02: return "1";
		case 0x03: return "2";
		case 0x04: return "3";
		case 0x05: return "4";
		case 0x06: return "5";
		case 0x07: return "6";
		case 0x08: return "7";
		case 0x09: return "8";
		case 0x0A: return "9";
		case 0x0B: return "0";
		case 0x0C: return "-";
		case 0x0D: return "=";

		case 0x10: return "Q";
		case 0x11: return "W";
		case 0x12: return "E";
		case 0x13: return "R";
		case 0x14: return "T";
		case 0x15: return "Y";
		case 0x16: return "U";
		case 0x17: return "I";
		case 0x18: return "O";
		case 0x19: return "P";
		case 0x1A: return "[";
		case 0x1B: return "]";

		case 0x1E: return "A";
		case 0x1F: return "S";
		case 0x20: return "D";
		case 0x21: return "F";
		case 0x22: return "G";
		case 0x23: return "H";
		case 0x24: return "J";
		case 0x25: return "K";
		case 0x26: return "L";
		case 0x27: return ";";
		case 0x28: return "'";
		case 0x2B: return "\\";

		case 0x2C: return "Z";
		case 0x2D: return "X";
		case 0x2E: return "C";
		case 0x2F: return "V";
		case 0x30: return "B";
		case 0x31: return "N";
		case 0x32: return "M";
		case 0x33: return ",";
		case 0x34: return ".";
		case 0x35: return "/";

		case 0x3B: return "F1";
		case 0x3C: return "F2";
		case 0x3D: return "F3";
		case 0x3E: return "F4";
		case 0x3F: return "F5";
		case 0x40: return "F6";
		case 0x41: return "F7";
		case 0x42: return "F8";
		case 0x43: return "F9";
		case 0x44: return "F10";
		case 0x57: return "F11";
		case 0x58: return "F12";

		case 0x01: return "Esc";
		case 0x0E: return "BackSpace";
		case 0x0F: return "Tab";
		case 0x1C: return "Enter";
		case 0x1D: return "LCtrl";
		case 0x2A: return "LShift";
		case 0x36: return "RShift";
		case 0x38: return "LAlt";
		case 0x39: return "Space";
		case 0x3A: return "CapsLock";
		case 0x29: return "`";

			/* ---- 以下为补充的按键 ---- */
		case 0x37: return "Num*";       // 小键盘 *
		case 0x45: return "NumLock";
		case 0x46: return "ScrollLock";
		case 0x47: return "Num7";
		case 0x48: return "Num8";
		case 0x49: return "Num9";
		case 0x4A: return "Num-";       // 小键盘 -
		case 0x4B: return "Num4";
		case 0x4C: return "Num5";
		case 0x4D: return "Num6";
		case 0x4E: return "Num+";       // 小键盘 +
		case 0x4F: return "Num1";
		case 0x50: return "Num2";
		case 0x51: return "Num3";
		case 0x52: return "Num0";
		case 0x53: return "Num.";       // 小键盘 .
		case 0x54: return "SysRq";      // PrintScreen 的一部分（配合 E0）
		case 0x56: return "OEM_102";    // 欧洲键盘上的 < > 键（美式键盘无）
		case 0x59: return "Num=";       // 部分键盘小键盘 =
		case 0x64: return "F13";
		case 0x65: return "F14";
		case 0x66: return "F15";
		case 0x67: return "F16";
		case 0x68: return "F17";
		case 0x69: return "F18";
		case 0x6A: return "F19";
		case 0x6B: return "F20";
		case 0x6C: return "F21";
		case 0x6D: return "F22";
		case 0x6E: return "F23";
		case 0x6F: return "F24";
		case 0x70: return "Kana";       // 日文假名
		case 0x73: return "OEM_102";    // 部分布局的 \ _ 键
		case 0x79: return "Henkan";     // 日文转换
		case 0x7B: return "Muhenkan";   // 日文无转换
		case 0x7D: return "Yen";        // 日元符号
		case 0x7E: return "OEM_8";      // 部分布局特殊键
		case 0xE0: return "E0_Prefix";  // E0 前缀本身，正常不会单独出现
		case 0xE1: return "E1_Prefix";  // E1 前缀本身
		}
	}
	if (IsE0) {
		switch (ScanCode) {
		case 0x1C: return "NumEnter";
		case 0x1D: return "RCtrl";
		case 0x35: return "Num/";
		case 0x38: return "RAlt";
		case 0x47: return "Home";
		case 0x48: return "Up";
		case 0x49: return "PgUp";
		case 0x4B: return "Left";
		case 0x4D: return "Right";
		case 0x4F: return "End";

			/* ---- 以下为补充的 E0 扩展键 ---- */
		case 0x50: return "Down";
		case 0x51: return "PgDn";
		case 0x52: return "Insert";
		case 0x53: return "Delete";
		case 0x5B: return "LWin";       // 左 Windows 键
		case 0x5C: return "RWin";       // 右 Windows 键
		case 0x5D: return "Menu";       // 菜单键（Application）
		case 0x2A: return "PrtSc";      // E0 2A 是 PrintScreen 前缀
		case 0x37: return "PrtSc";      // E0 37 是 PrintScreen 主体
		case 0x46: return "Break";      // E0 46 (配合 E1) 是 Pause/Break
		case 0x1F: return "LWin";       // 部分键盘
		case 0x27: return "RWin";       // 部分键盘
		case 0x6B: return "Num=";       // 部分键盘小键盘 =
		}
	}

	/* 未知扫描码兜底，避免返回未定义值 */
	return "Unknown";
}

BOOLEAN ShouldKeyBlock(PKEYBOARD_DEVICE_EXTENSION pKbdDevExt, PKEYBOARD_INPUT_DATA pKeyIn) {
	if (!pKbdDevExt->EnableFiler) return FALSE;
	for (size_t i = 0; i < pKbdDevExt->BlockedCount; i++) {
		if (pKbdDevExt->BlockedScancodes[i] == pKeyIn->MakeCode) return TRUE;
	}
	return FALSE;
}
