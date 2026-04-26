#include <ntifs.h> 

typedef struct _POOL_HEADER {
	union {
		struct {
			ULONG PreviousSize:8;
			ULONG PoolIndex:8;
			ULONG BlockSize:8;
			ULONG PoolType:8;
		};
	};
	ULONG PoolTag;
	union {
		PEPROCESS ProcessBilled; // _EPROCESS*
		struct {
			USHORT AllocatorBackTraceIndex;
			USHORT PoolTagHash;
		};
	};
} NON_POOL_HEADER, *PNON_POOL_HEADER;

typedef struct _POOL_TRACKER_BIG_PAGES {
	PVOID Value;
	ULONG Key;
	union {
		ULONG PatternAndType;
		struct {
			ULONG Pattern:8;
			ULONG PoolType:12;
			ULONG SlushSize:12;
		};
	};
	ULONG64 NumberOfBytes;
} BIG_PAGE_POOL_HEADER, *PBIG_PAGE_POOL_HEADER;

PVOID AllocMemory(size_t Size, ULONG Tag) {
	if (0x0 == Size || Size > 0x1000000) {
		DbgPrint("[cnHHHHHcn] - Invalid allocation size: 0x%Ix\n", Size);
		return NULL;
	}
	if(Tag == 0) {
		DbgPrint("[cnHHHHHcn] - Invalid allocation tag\n");
		return NULL;
	}

	// [警告] 自动切换 PagedPool 存在风险：若内存被 PASSIVE_LEVEL 分配，随后在 DISPATCH_LEVEL 被访问，
	// 一旦内存被换出，将引发蓝屏 (0x0000000A)。建议跨 IRQL 使用的内存始终强制使用 NonPagedPool。
	POOL_TYPE PoolType = NonPagedPool;
	if (Size > 0x1000) {
		KIRQL CurrentIRQL = KeGetCurrentIrql();
		if(CurrentIRQL <= APC_LEVEL) PoolType = PagedPool;
	}
	PVOID AllocAddr = NULL;
	AllocAddr = ExAllocatePoolWithTag(PoolType, Size, Tag);
	if (AllocAddr != NULL) {
		RtlZeroMemory(AllocAddr, Size);
		DbgPrint("[cnHHHHHcn] + Allocated memory Type: %s\n", (PoolType == NonPagedPool) ? "NonPagedPool" : "PagedPool");
		DbgPrint("[cnHHHHHcn] + Allocated memory: 0x%p, Size: 0x%Ix, Tag: 0x%08x\n", AllocAddr, Size, Tag);
	}
	else {
		DbgPrint("[cnHHHHHcn] - Allocated memory failed, Size: 0x%Ix, Tag: 0x%08x\n", Size, Tag);
	}

	return AllocAddr;
}

VOID FreeMemory(PVOID* Address) {
	if (Address == NULL || *Address == NULL) {
		DbgPrint("[cnHHHHHcn] - Invalid free address\n");
		return;
	}
	DbgPrint("[cnHHHHHcn] + Freeing memory: 0x%p\n", *Address);
	ExFreePool(*Address);
	*Address = NULL;
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

	pDriverObject->DriverUnload = DriverUnload;

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

	// 创建符号链接
	status = IoCreateSymbolicLink(&SymbolicLinkName, &DeviceName);
	if (!NT_SUCCESS(status) && status != STATUS_SUCCESS) {
		DbgPrint("[cnHHHHHcn] - Create Symbolic Link Failed: 0x%08x\n", status);
		IoDeleteDevice(pDeviceObject);
		return status;
	}

	pDriverObject->DeviceObject = pDeviceObject;

	DbgPrint("[cnHHHHHcn] + DriverEntry called\n");
	DbgPrint("\n\n\n");
	
	PVOID pNonPageAddr = AllocMemory(0x200, 'cHnp');
	PNON_POOL_HEADER pNonPageHeader = (PNON_POOL_HEADER)((ULONG64)pNonPageAddr - sizeof(NON_POOL_HEADER));

	DbgPrint("\n");
	DbgPrint("[cnHHHHHcn] + NON_POOL_HEADER Structure Display\n");
	DbgPrint("[cnHHHHHcn] + NonPageHeader Address: 0x%p\n", pNonPageHeader);
	DbgPrint("[cnHHHHHcn] + Offset:0x0 Previous Size: %d\n", pNonPageHeader->PreviousSize);
	DbgPrint("[cnHHHHHcn] + Offset:0x1 Pool Index: %d\n", pNonPageHeader->PoolIndex);
	DbgPrint("[cnHHHHHcn] + Offset:0x2 Block Size: %d\n", pNonPageHeader->BlockSize);
	DbgPrint("[cnHHHHHcn] + Offset:0x3 Pool Type: %d\n", pNonPageHeader->PoolType);
	DbgPrint("[cnHHHHHcn] + Offset:0x4 Pool Tag: 0x%08x\n", pNonPageHeader->PoolTag);
	DbgPrint("[cnHHHHHcn] + Offset:0x8 Process Billed: 0x%p\n", pNonPageHeader->ProcessBilled);
	DbgPrint("[cnHHHHHcn] + Offset:0x8 Allocator Back Trace Index: %d\n", pNonPageHeader->AllocatorBackTraceIndex);
	DbgPrint("[cnHHHHHcn] + Offset:0xA Pool Tag Hash: 0x%04x\n", pNonPageHeader->PoolTagHash);
	DbgPrint("\n");
	FreeMemory(&pNonPageAddr);
	DbgPrint("\n\n\n");
	

	PVOID pBigPageAddr = AllocMemory(0x200000, 'cHbp');
	/*
	// 不是这个位置，获取这个指针之后，指向的是未知的地址，无法访问，导致蓝屏。
	// Error Code: 0x00000050 PAGE_FAULT_IN_NONPAGED_AREA
	PBIG_PAGE_POOL_HEADER pBigPageHeader = (PBIG_PAGE_POOL_HEADER)((ULONG64)pBigPageAddr - sizeof(BIG_PAGE_POOL_HEADER));
	*/
	PBIG_PAGE_POOL_HEADER pBigPageHeader = NULL;
	DbgBreakPoint();
	DbgPrint("[cnHHHHHcn] + BIG_PAGE_POOL_HEADER Structure Display\n");
	DbgPrint("[cnHHHHHcn] + BigPageHeader Address: 0x%p\n", pBigPageHeader);
	DbgPrint("[cnHHHHHcn] + Offset:0x0			Value: 0x%p\n", pBigPageHeader->Value);
	DbgPrint("[cnHHHHHcn] + Offset:0x8			Key: %d\n", pBigPageHeader->Key);
	DbgPrint("[cnHHHHHcn] + Offset:0xC			PatternAndType: %d\n", pBigPageHeader->PatternAndType);
	DbgPrint("[cnHHHHHcn] + Offset:0xC + 8 Bits Pattern: %d\n", pBigPageHeader->Pattern);
	DbgPrint("[cnHHHHHcn] + Offset:0xD + 0 Bits PoolType: %d\n", pBigPageHeader->PoolType);
	DbgPrint("[cnHHHHHcn] + Offset:0xE + 4 Bits SlushSize: %d\n", pBigPageHeader->SlushSize);
	DbgPrint("[cnHHHHHcn] + Offset:0x10			NumberOfBytes: %lld\n", pBigPageHeader->NumberOfBytes);
	DbgPrint("\n");
	FreeMemory(&pBigPageAddr);

	return STATUS_SUCCESS;
}