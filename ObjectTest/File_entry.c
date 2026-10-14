#include <ntifs.h>

VOID DriverUnload(PDRIVER_OBJECT pDriver) {
	UNREFERENCED_PARAMETER(pDriver);
	DbgPrint("[cnHHHHHcn] + Driver Unload Called\n");
}

NTSTATUS DriverEntry(PDRIVER_OBJECT pDriver, PUNICODE_STRING pRegisterPath) {
    UNREFERENCED_PARAMETER(pDriver);
    UNREFERENCED_PARAMETER(pRegisterPath);
    DbgPrint("[cnHHHHHcn] + Driver Entry Called\n");
    /*
    _Out_ PHANDLE FileHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_opt_ PLARGE_INTEGER AllocationSize,
    _In_ ULONG FileAttributes,
    _In_ ULONG ShareAccess,
    _In_ ULONG CreateDisposition,
    _In_ ULONG CreateOptions,
    _In_reads_bytes_opt_(EaLength) PVOID EaBuffer,
    _In_ ULONG EaLength
    */
    NTSTATUS status = STATUS_SUCCESS;
    HANDLE Src_hFile = NULL;
    HANDLE Dst_hFile = NULL;
    UNICODE_STRING uniSrcName = { 0 };
    UNICODE_STRING uniDstName = { 0 };
    RtlInitUnicodeString(&uniSrcName, L"\\??\\C:\\Users\\test\\Desktop\\InstDrv-master\\readme.md");
    RtlInitUnicodeString(&uniDstName, L"\\??\\C:\\Users\\test\\Desktop\\test.txt");
    OBJECT_ATTRIBUTES SrcObjAttr = { 0 };
    OBJECT_ATTRIBUTES DstObjAttr = { 0 };
    InitializeObjectAttributes(&SrcObjAttr, &uniSrcName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    InitializeObjectAttributes(&DstObjAttr, &uniDstName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    IO_STATUS_BLOCK ioStatusBlock = { 0 };

    status = ZwCreateFile(
        &Src_hFile,
        GENERIC_READ,
        &SrcObjAttr,
        &ioStatusBlock,
        NULL,
        FILE_ATTRIBUTE_NORMAL,
        0,
        FILE_OPEN_IF,
        FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_ALERT,
        NULL,
        0
    );
    if (NT_SUCCESS(status)) {
        DbgPrint("[cnHHHHHcn] + Kernel API ZwCreateFile Success!\n");
        DbgPrint("[cnHHHHHcn] + SrcFile Handle: 0x%p\n", Src_hFile);
        DbgPrint("[cnHHHHHcn] + IO_STATUS_BLOCK Information: 0x%p\n", (PVOID)ioStatusBlock.Information);
    }else {
        DbgPrint("[cnHHHHHcn] + Kernel API ZwCreateFile Failed!\n");
    }

    status = ZwCreateFile(
        &Dst_hFile,
        GENERIC_WRITE,
        &DstObjAttr,
        &ioStatusBlock,
        NULL,
        FILE_ATTRIBUTE_NORMAL,
        0,
        FILE_OPEN_IF,
        FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_ALERT,
        NULL,
        0
    );
    if (NT_SUCCESS(status)) {
        DbgPrint("[cnHHHHHcn] + Kernel API ZwCreateFile Success!\n");
        DbgPrint("[cnHHHHHcn] + DstFile Handle: 0x%p\n", Dst_hFile);
        DbgPrint("[cnHHHHHcn] + IO_STATUS_BLOCK Information: 0x%p\n", (PVOID)ioStatusBlock.Information);
    }
    else {
        DbgPrint("[cnHHHHHcn] + Kernel API ZwCreateFile Failed!\n");
    }

    {
		LONGLONG FileSize = 0;
        FILE_STANDARD_INFORMATION FileInfo = { 0 };
        
		ZwQueryInformationFile(Src_hFile, &ioStatusBlock, &FileInfo, sizeof(FILE_STANDARD_INFORMATION), FileStandardInformation);
        DbgPrint("[cnHHHHHcn] + SrcFile Size: 0x%I64X\n", FileInfo.EndOfFile.QuadPart);
		FileSize = FileInfo.EndOfFile.QuadPart;
        void* Buffer = ExAllocatePool(NonPagedPool, FileSize);
        if (Buffer) {
            status = ZwReadFile(Src_hFile, NULL, NULL, NULL, &ioStatusBlock, Buffer, FileSize, NULL, 0);
            if (NT_SUCCESS(status)) {
                DbgPrint("[cnHHHHHcn] + Kernel API ZwReadFile Success!\n");

            }
            else {
                DbgPrint("[cnHHHHHcn] + Kernel API ZwReadFile Failed!\n");
            }
            DbgPrint("[cnHHHHHcn] + Read Content: %s\n", (char*)Buffer);

            status = ZwWriteFile(Dst_hFile, NULL, NULL, NULL, &ioStatusBlock, Buffer, FileSize, NULL, NULL);
            if (NT_SUCCESS(status)) {
                DbgPrint("[cnHHHHHcn] + Kernel API ZwWriteFile Success!\n");
            }
            else {
                DbgPrint("[cnHHHHHcn] + Kernel API ZwWriteFile Failed!\n");
            }
            ExFreePool(Buffer);
        }
        status = ZwClose(Src_hFile);
        if (NT_SUCCESS(status)) {
            DbgPrint("[cnHHHHHcn] + Kernel API ZwClose Success!\n");
        }
        else {
            DbgPrint("[cnHHHHHcn] + Kernel API ZwClose Failed!\n");
        }

        status = ZwClose(Dst_hFile);
        if (NT_SUCCESS(status)) {
            DbgPrint("[cnHHHHHcn] + Kernel API ZwClose Success!\n");
        }
        else {
            DbgPrint("[cnHHHHHcn] + Kernel API ZwClose Failed!\n");
        }
    };

    pDriver->DriverUnload = DriverUnload;
    return STATUS_SUCCESS;
}
