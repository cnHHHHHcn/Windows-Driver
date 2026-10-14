#include <ntifs.h>

typedef struct _REGKEYSTATUS {
    HANDLE hKey;
    ULONG Status;
}REGKEYSTATUS, *PREGKEYSTATUS;

VOID DriverUnload(PDRIVER_OBJECT pDriver) {
	UNREFERENCED_PARAMETER(pDriver);
	DbgPrint("[cnHHHHHcn] + Driver Unload Called\n");
}

NTSTATUS DriverEntry(PDRIVER_OBJECT pDriver, PUNICODE_STRING pRegisterPath) {
    UNREFERENCED_PARAMETER(pDriver);
    UNREFERENCED_PARAMETER(pRegisterPath);
	DbgPrint("[cnHHHHHcn] + Driver Entry Called\n");

	NTSTATUS status = STATUS_SUCCESS;
    REGKEYSTATUS RegKey = { 0 };

	OBJECT_ATTRIBUTES KeyAttr = { 0 };
	UNICODE_STRING KeyName = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\SOFTWARE\\cnHHHHHcn");
    InitializeObjectAttributes(&KeyAttr, &KeyName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    /*
    _Out_ PHANDLE KeyHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _Reserved_ ULONG TitleIndex,
    _In_opt_ PUNICODE_STRING Class,
    _In_ ULONG CreateOptions,
    _Out_opt_ PULONG Disposition
    */
    status = ZwCreateKey(&RegKey.hKey, KEY_ALL_ACCESS, &KeyAttr, 0, NULL, REG_OPTION_VOLATILE, &RegKey.Status);
    if (NT_SUCCESS(status)) {
        DbgPrint("[cnHHHHHcn] + Registry Key Created Successfully\n");
        DbgPrint("[cnHHHHHcn] + Registry Key Creation Disposition: %s\n", 
            (RegKey.Status == REG_CREATED_NEW_KEY) ? "New Key Created" : 
			(RegKey.Status == REG_OPENED_EXISTING_KEY) ? "Existing Key Opened" : "Unknown");
    }
    else {
        DbgPrint("[cnHHHHHcn] + Failed to Create Registry Key: 0x%X\n", status);
	}
    /*
    _In_ HANDLE KeyHandle,
    _In_ PUNICODE_STRING ValueName,
    _In_opt_ ULONG TitleIndex,
    _In_ ULONG Type,
    _In_reads_bytes_opt_(DataSize) PVOID Data,
    _In_ ULONG DataSize
    */
    DbgBreakPoint();
    UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"TestValue");
    UNICODE_STRING ValueData = RTL_CONSTANT_STRING(L"cnHHHHHcn");
    if (RegKey.hKey != NULL) {
        status = STATUS_SUCCESS;
        status = ZwSetValueKey(RegKey.hKey, &ValueName, 0, REG_SZ, ValueData.Buffer, ValueData.Length);
        if (NT_SUCCESS(status)) {
            DbgPrint("[cnHHHHHcn] + Registry Value Set Successfully\n");
        }
        else {
            DbgPrint("[cnHHHHHcn] + Failed to Set Registry Value: 0x%X\n", status);
        }
    }

    DbgBreakPoint();
    if (RegKey.hKey != NULL) {
        status = STATUS_SUCCESS;
        PKEY_VALUE_FULL_INFORMATION pKeyValueInfo = NULL;
		ZwQueryValueKey(RegKey.hKey, &ValueName, KeyValueFullInformation, NULL, 0, &RegKey.Status);
		pKeyValueInfo = (PKEY_VALUE_FULL_INFORMATION)ExAllocatePool(NonPagedPool, RegKey.Status);
        if (pKeyValueInfo == NULL) {
            DbgPrint("[cnHHHHHcn] + Failed to Allocate Memory for Registry Value Information\n");
            return STATUS_INSUFFICIENT_RESOURCES;
        }
		status = ZwQueryValueKey(RegKey.hKey, &ValueName, KeyValueFullInformation, pKeyValueInfo, RegKey.Status, &RegKey.Status);
        if (NT_SUCCESS(status)) {
            DbgPrint("[cnHHHHHcn] + Registry Value Queried Successfully\n");
			DbgPrint("[cnHHHHHcn] + Registry Value Query Result Length: %d\n", RegKey.Status);
            DbgPrint("[cnHHHHHcn] + Value Name: %ws\n", ValueName.Buffer);
			DbgPrint("[cnHHHHHcn] + Value Data: %ws\n", (PWCHAR)((PCHAR)pKeyValueInfo + pKeyValueInfo->DataOffset));
            DbgPrint("[cnHHHHHcn] + Value Type: 0x%X\n", pKeyValueInfo->Type);
            DbgPrint("[cnHHHHHcn] + Value Data Length: %d\n", pKeyValueInfo->DataLength);
			ExFreePool(pKeyValueInfo);
			pKeyValueInfo = NULL;
        }
        else {
            DbgPrint("[cnHHHHHcn] + Failed to Query Registry Value: 0x%X\n", status);
		}
    }

    DbgBreakPoint();
    if (RegKey.hKey != NULL) {
		ULONG KeyValueIndex = 0;
        status = STATUS_SUCCESS;
        PKEY_VALUE_FULL_INFORMATION pKeyValueInfo = NULL;

        while (status != STATUS_NO_MORE_ENTRIES){
            ZwEnumerateValueKey(RegKey.hKey, KeyValueIndex, KeyValueFullInformation, NULL, 0, &RegKey.Status);
			pKeyValueInfo = (PKEY_VALUE_FULL_INFORMATION)ExAllocatePool(NonPagedPool, RegKey.Status);
            if(pKeyValueInfo == NULL) {
                DbgPrint("[cnHHHHHcn] + Failed to Allocate Memory for Registry Value Information\n");
                break;
			}
            status = ZwEnumerateValueKey(RegKey.hKey, KeyValueIndex, KeyValueFullInformation, pKeyValueInfo, RegKey.Status, &RegKey.Status);
			DbgPrint("[cnHHHHHcn] + Enumerating Registry Values - Index: %d, Status: 0x%X\n", KeyValueIndex, status);
			DbgPrint("[cnHHHHHcn] + Registry Value Enumeration Result Length: %d\n", RegKey.Status);
            DbgPrint("[cnHHHHHcn] + Registry Value Name: %ws\n", pKeyValueInfo->Name);
            DbgPrint("[cnHHHHHcn] + Registry Value Data: \n");
            PCHAR pRegData = (PCHAR)pKeyValueInfo + pKeyValueInfo->DataOffset;
            ULONG dataLength = pKeyValueInfo->DataLength;

            DbgPrint("[RegDump] Total Length: %d bytes\n", dataLength);

            UCHAR hexLine[16];   // 存放当前行 16 个字节的原始数据
            ULONG lineIndex = 0; // 当前行已经存了多少个字节

            for (ULONG i = 0; i < dataLength; i++) {
                UCHAR byte = ((PUCHAR)pRegData)[i];
                hexLine[lineIndex] = byte; // 存入缓冲区
                lineIndex++;

                // 打印左侧的十六进制
                DbgPrint("%02X ", byte);

                // 每满 16 个字节，或者到了数据的最后一个字节，就打印右侧 ASCII 并换行
                if (lineIndex == 16 || i == dataLength - 1) {
                    // 如果最后一行不满 16 个字节，打印空格补齐左侧的视觉空缺
                    for (ULONG k = lineIndex; k < 16; k++) {
                        DbgPrint("   "); // 3个空格（2个十六进制位+1个空格）
                    }

                    // 打印右侧的分隔符和 ASCII 字符
                    DbgPrint("| ");
                    for (ULONG k = 0; k < lineIndex; k++) {
                        UCHAR ch = hexLine[k];
                        // 如果是可打印的 ASCII 字符（32-126），直接打印，否则打印点
                        DbgPrint("%c", (ch >= 32 && ch <= 126) ? ch : '.');
                    }
                    DbgPrint("\n"); // 换行

                    // 重置行计数器，准备下一行
                    lineIndex = 0;
                }
            }
            DbgPrint("\n");
            DbgPrint("[cnHHHHHcn] + Registry Value Type: 0x%X\n", pKeyValueInfo->Type);
			DbgPrint("[cnHHHHHcn] + Registry Value Data Length: %d\n", pKeyValueInfo->DataLength);
			ExFreePool(pKeyValueInfo);
			pKeyValueInfo = NULL;
            KeyValueIndex++;
        }
    }

    DbgBreakPoint();
    if (RegKey.hKey != NULL) {
		ULONG KeyIndex = 0;
		status = STATUS_SUCCESS;
        PKEY_BASIC_INFORMATION pKeyInfo = NULL;
        while (status != STATUS_NO_MORE_ENTRIES) {
            status = ZwEnumerateKey(RegKey.hKey, KeyIndex, KeyBasicInformation, NULL, 0, &RegKey.Status);
			pKeyInfo = (PKEY_BASIC_INFORMATION)ExAllocatePool(NonPagedPool, RegKey.Status);
            if (pKeyInfo == NULL) {
                DbgPrint("[cnHHHHHcn] + Failed to Allocate Memory for Registry Key Information\n");
                break;
            }
            status = ZwEnumerateKey(RegKey.hKey, KeyIndex, KeyBasicInformation, pKeyInfo, RegKey.Status, &RegKey.Status);
			DbgPrint("[cnHHHHHcn] + Enumerating Registry Keys - Index: %d, Status: 0x%X\n", KeyIndex, status);
			DbgPrint("[cnHHHHHcn] + Registry Key Enumeration Result Length: %d\n", RegKey.Status);
			DbgPrint("[cnHHHHHcn] + Registry Key Name: %ws\n", pKeyInfo->Name);
			ExFreePool(pKeyInfo);
			pKeyInfo = NULL;
            KeyIndex++;
        }
    }

    DbgBreakPoint();
    if (RegKey.hKey != NULL) {
        status = STATUS_SUCCESS;
        status = ZwDeleteValueKey(RegKey.hKey, &ValueName);
        if (NT_SUCCESS(status)) {
            DbgPrint("[cnHHHHHcn] + Registry Value Deleted Successfully\n");
        }
        else {
            DbgPrint("[cnHHHHHcn] + Failed to Delete Registry Value: 0x%X\n", status);
        }
    }

    DbgBreakPoint();
    if(RegKey.hKey != NULL) {
        status = STATUS_SUCCESS;
        status = ZwDeleteKey(RegKey.hKey);
        if (NT_SUCCESS(status)) {
            DbgPrint("[cnHHHHHcn] + Registry Key Deleted Successfully\n");
        }
        else {
            DbgPrint("[cnHHHHHcn] + Failed to Delete Registry Key: 0x%X\n", status);
        }
	}

    DbgBreakPoint();
    if(RegKey.hKey != NULL) {
        ZwClose(RegKey.hKey);
        DbgPrint("[cnHHHHHcn] + Registry Key Handle Closed\n");
	}

    pDriver->DriverUnload = DriverUnload;
    return STATUS_SUCCESS;
}
