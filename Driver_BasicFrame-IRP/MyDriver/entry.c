#include <ntifs.h> 

#define IOCTL_SEND_DATA CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_RECEIVE_DATA CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)


NTSTATUS DispatchDeviceControl(PDEVICE_OBJECT pDeviceObject, PIRP pIrp) {
	UNREFERENCED_PARAMETER(pDeviceObject);
	UNREFERENCED_PARAMETER(pIrp);
	DbgPrint("[cnHHHHHcn] + DispatchDeviceControl called\n");

	NTSTATUS Status = STATUS_SUCCESS;
	ULONG StatusInformation = 0;
	PIO_STACK_LOCATION pIrpStack = NULL;
	pIrpStack = IoGetCurrentIrpStackLocation(pIrp);
	
	PVOID pIoBuffer = pIrp->AssociatedIrp.SystemBuffer;
	ULONG IOCTL_CODE = pIrpStack->Parameters.DeviceIoControl.IoControlCode;
	ULONG InputLength = pIrpStack->Parameters.DeviceIoControl.InputBufferLength;
	ULONG OutputLength = pIrpStack->Parameters.DeviceIoControl.OutputBufferLength;
	
	switch (IOCTL_CODE) {
		case IOCTL_SEND_DATA:
			DbgPrint("[cnHHHHHcn] + Received Data from User Mode: %s\n", (char*)pIoBuffer);
			InputLength = 0;
			break;
		case IOCTL_RECEIVE_DATA:
			DbgPrint("[cnHHHHHcn] + Sending Data to User Mode\n");
			const char* pData = "Driver talk: Hello User Mode!";
			RtlCopyMemory(pIoBuffer, pData, strlen(pData) + 1);
			OutputLength = (ULONG)strlen(pData);
			StatusInformation = OutputLength;
			break;
		default:
			DbgPrint("[cnHHHHHcn] - Unknown IOCTL Code: 0x%08x\n", IOCTL_CODE);
			RtlInitAnsiString(pIoBuffer, "Error: Unknown IOCTL Code!");
			Status = STATUS_INVALID_DEVICE_REQUEST;
			StatusInformation = 0;
	}


	pIrp->IoStatus.Status = Status;
	pIrp->IoStatus.Information = StatusInformation;
	IoCompleteRequest(pIrp, IO_NO_INCREMENT);
	return Status;
}

NTSTATUS DispatchCreate(PDEVICE_OBJECT pDeviceObject, PIRP pIrp)
{
	UNREFERENCED_PARAMETER(pDeviceObject);
	UNREFERENCED_PARAMETER(pIrp);
	DbgPrint("[cnHHHHHcn] + DispatchCreate called\n");



	pIrp->IoStatus.Status = STATUS_SUCCESS;
	pIrp->IoStatus.Information = 0;
	IoCompleteRequest(pIrp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

NTSTATUS DispatchClose(PDEVICE_OBJECT pDeviceObject, PIRP pIrp) 
{
	UNREFERENCED_PARAMETER(pDeviceObject);
	UNREFERENCED_PARAMETER(pIrp);
	DbgPrint("[cnHHHHHcn] + DispatchClose called\n");



	pIrp->IoStatus.Status = STATUS_SUCCESS;
	pIrp->IoStatus.Information = 0;
	IoCompleteRequest(pIrp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

NTSTATUS DispatchRoute(PDEVICE_OBJECT pDeviceObject, PIRP pIrp) 
{
	// 获取当前IRP的堆栈位置和主函数ID
	PIO_STACK_LOCATION pIrpStack = NULL;
	UCHAR MajorFuncID = 0;
	pIrpStack = IoGetCurrentIrpStackLocation(pIrp);
	MajorFuncID = pIrpStack->MajorFunction;

	// 根据主函数ID调用相应的调度函数
	if (MajorFuncID <= IRP_MJ_MAXIMUM_FUNCTION) {
		PDRIVER_DISPATCH pDispatchFunc = pDeviceObject->DriverObject->MajorFunction[MajorFuncID];
		if (pDispatchFunc) {
			return pDispatchFunc(pDeviceObject, pIrp);
		}
	}

	// 如果没有找到对应的调度函数，则返回错误状态
	pIrp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
	pIrp->IoStatus.Information = 0;
	IoCompleteRequest(pIrp, IO_NO_INCREMENT);
	return STATUS_INVALID_DEVICE_REQUEST;
}

VOID DriverUnload(PDRIVER_OBJECT DriverObject)
{
	DbgPrint("[cnHHHHHcn] + DriverUnload called\n");
	if (DriverObject->DeviceObject) {
		UNICODE_STRING SymbolicLinkName;
		RtlInitUnicodeString(&SymbolicLinkName, L"\\??\\cnHHHHHcn");
		IoDeleteSymbolicLink(&SymbolicLinkName);
		IoDeleteDevice(DriverObject->DeviceObject);
		DriverObject->DeviceObject = NULL;
	}
}

NTSTATUS DriverEntry(PDRIVER_OBJECT pDriverObject, PUNICODE_STRING RegistryPath)
{
	UNREFERENCED_PARAMETER(RegistryPath);

	// DbgBreakPoint();
	
	// 创建设备对象和符号链接
	UNICODE_STRING DeviceName, SymbolicLinkName;
	RtlInitUnicodeString(&DeviceName, L"\\Device\\cnHHHHHcn");
	RtlInitUnicodeString(&SymbolicLinkName, L"\\??\\cnHHHHHcn");

	// 创建设备对象
	PDEVICE_OBJECT pDeviceObject = NULL;
	NTSTATUS status = IoCreateDevice(pDriverObject, 0, &DeviceName, FILE_DEVICE_UNKNOWN, 0, FALSE, &pDeviceObject);
	if (!NT_SUCCESS(status)) {
		DbgPrint("[cnHHHHHcn] - Create Device Failed: 0x%08x\n", status);
		return status;
	}

	pDeviceObject->Flags |= DO_BUFFERED_IO;	// 设置设备对象的标志为缓冲I/O

	// 设置调度函数
	pDriverObject->MajorFunction[IRP_MJ_CREATE] = DispatchCreate;
	pDriverObject->MajorFunction[IRP_MJ_CLOSE] = DispatchClose;
	pDriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchDeviceControl;

	// 创建符号链接
	status = IoCreateSymbolicLink(&SymbolicLinkName, &DeviceName);
	if (!NT_SUCCESS(status) && status != STATUS_SUCCESS) {
		DbgPrint("[cnHHHHHcn] - Create Symbolic Link Failed: 0x%08x\n", status);
		IoDeleteDevice(pDeviceObject);
		return status;
	}

	pDriverObject->DriverUnload = DriverUnload;

	pDriverObject->DeviceObject = pDeviceObject;

	DbgPrint("[cnHHHHHcn] + DriverEntry called\n");

	return STATUS_SUCCESS;
}