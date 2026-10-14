#include <ntifs.h>


typedef struct _GDTR32 {
	USHORT Limit;
	ULONG Base;
} GDTR32, * PGDTR32;

typedef struct _GDTR64 {
	USHORT Limit;
	ULONGLONG Base;
} GDTR64, * PGDTR64;

#if defined(_M_IX86) || defined(_M_ARM)
typedef GDTR32	GDTR;
typedef PGDTR32 PGDTR;
#else
typedef GDTR64	GDTR;
typedef PGDTR64	PGDTR;
#endif 

typedef struct _SEGMENT_SELECTORS {
	union {
		USHORT Data;
		struct Descriptor {
			USHORT RPL : 2;
			USHORT TI : 1;
			USHORT Index : 13;
		} DescriptorData;
	};
} SEGMENT_SELECTORS, *PSEGMENT_SELECTORS;

typedef struct _SEGMENT_CONTEXT {
	SEGMENT_SELECTORS cs;
	SEGMENT_SELECTORS ss;
	SEGMENT_SELECTORS ds;
	SEGMENT_SELECTORS es;
	SEGMENT_SELECTORS fs;
	SEGMENT_SELECTORS gs;
}SEGMENT_CONTEXT, * PSEGMENT_CONTEXT;

typedef struct _CALLGATE_DESCRIPTOR {
	union {
		ULONGLONG Data;
		struct Descriptor {
			ULONGLONG SegmentOffsetLow : 16;
			ULONGLONG SegmentSelector : 16;
			ULONGLONG ParameterCount : 5;
			ULONGLONG Reserved : 3;
			ULONGLONG Type : 4;
			ULONGLONG S : 1;
			ULONGLONG DPL : 2;
			ULONGLONG P : 1;
			ULONGLONG SegmentOffsetHigh : 16;
		} DescriptorData;
	};
} CALLGATE_DESCRIPTOR, * PCALLGATE_DESCRIPTOR;


typedef struct _SEGMENT_DESCRIPTOR {
	union {
		ULONGLONG Data;
		struct Descriptor {
			ULONGLONG SegmentLimitLow : 16;
			ULONGLONG BaseAddressLow : 16;
			ULONGLONG BaseAddressMid : 8;
			ULONGLONG Type : 4;
			ULONGLONG S : 1;
			ULONGLONG DPL : 2;
			ULONGLONG P : 1;
			ULONGLONG SegmentLimitHigh : 4;
			ULONGLONG AVL : 1;
			ULONGLONG L : 1;
			ULONGLONG D_B : 1;
			ULONGLONG G : 1;
			ULONGLONG BaseAddressHigh : 8;
		} DescriptorData;
	};
} SEGMENT_DESCRIPTOR, * P_SEGMENT_DESCRIPTOR;

extern "C" void GetSegment(PSEGMENT_CONTEXT pSegContext);



NTSTATUS DriverUnload(PDRIVER_OBJECT pDriver) {
	UNREFERENCED_PARAMETER(pDriver);
	DbgPrint("[cnHHHHHcn] + Driver Unload Called\n");
}

NTSTATUS DriverEntry(PDRIVER_OBJECT pDriver, PUNICODE_STRING pRegisterPath) {
	UNREFERENCED_PARAMETER(pDriver);
	UNREFERENCED_PARAMETER(pRegisterPath);
	DbgPrint("[cnHHHHHcn] + Driver Entry Called\n");
	pDriver->DriverUnload = DriverUnload;

	SEGMENT_CONTEXT SegContext;
	GetSegment(&SegContext);

	DbgPrint("Segment Overview\r\n");
	DbgPrint("Code Segment\tValue:%04x\tRPL:%d\tTI:%d\tIndex:%d\r\n", SegContext.cs.Data, SegContext.cs.DescriptorData.RPL, SegContext.cs.DescriptorData.TI, SegContext.cs.DescriptorData.Index);
	DbgPrint("Stack Segment\tValue:%04x\tRPL:%d\tTI:%d\tIndex:%d\r\n", SegContext.ss.Data, SegContext.ss.DescriptorData.RPL, SegContext.ss.DescriptorData.TI, SegContext.ss.DescriptorData.Index);
	DbgPrint("Data Segment\tValue:%04x\tRPL:%d\tTI:%d\tIndex:%d\r\n", SegContext.ds.Data, SegContext.ds.DescriptorData.RPL, SegContext.ds.DescriptorData.TI, SegContext.ds.DescriptorData.Index);
	DbgPrint("Extra Segment\tValue:%04x\tRPL:%d\tTI:%d\tIndex:%d\r\n", SegContext.es.Data, SegContext.es.DescriptorData.RPL, SegContext.es.DescriptorData.TI, SegContext.es.DescriptorData.Index);
	DbgPrint("FS Segment\tValue:%04x\tRPL:%d\tTI:%d\tIndex:%d\r\n", SegContext.fs.Data, SegContext.fs.DescriptorData.RPL, SegContext.fs.DescriptorData.TI, SegContext.fs.DescriptorData.Index);
	DbgPrint("GS Segment\tValue:%04x\tRPL:%d\tTI:%d\tIndex:%d\r\n", SegContext.gs.Data, SegContext.gs.DescriptorData.RPL, SegContext.gs.DescriptorData.TI, SegContext.gs.DescriptorData.Index);

	GDTR GDT = { 0 };


	DbgPrint("\r\nGlobal Descriptor Table Overview\r\n");
	DbgPrint("Global Descriptor Table\tlimit:%d\tBase:%p\r\n", GDT.Limit, GDT.Base);
	
	DbgBreakPoint();
	return STATUS_SUCCESS;
}
