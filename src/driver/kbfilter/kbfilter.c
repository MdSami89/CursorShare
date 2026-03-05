// =============================================================================
// CursorShare — KMDF Keyboard Filter Driver
// Upper filter driver for kbdclass — captures keyboard input at kernel level.
// =============================================================================
//
// This driver intercepts keyboard input between the port driver
// (i8042prt/kbdhid) and the class driver (kbdclass). It copies
// KEYBOARD_INPUT_DATA to a shared memory ring buffer accessible by the
// user-mode CursorShare service.
//
// Build requires: WDK, KMDF
// =============================================================================

#include <kbdmou.h>
#include <ntddk.h>
#include <ntddkbd.h>
#include <wdf.h>


#include "kbfilter.h"

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD CursorShareKbFilterDeviceAdd;
EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL CursorShareKbFilterInternalIoCtl;

// Original service callback from the class driver
static CONNECT_DATA g_UpperConnectData;
static PVOID g_SharedMemory = NULL;
static PKEVENT g_NotifyEvent = NULL;
static volatile LONG g_FilterActive = FALSE;

// Shared memory layout offsets
#define SHM_MAGIC_OFFSET 0
#define SHM_WRITE_IDX_OFFSET 16
#define SHM_READ_IDX_OFFSET 20
#define SHM_CAPACITY_OFFSET 24
#define SHM_DATA_OFFSET 64
#define SHM_EVENT_SIZE 32
#define SHM_CAPACITY 4096
#define SHM_TOTAL_SIZE (SHM_DATA_OFFSET + (SHM_CAPACITY * SHM_EVENT_SIZE))

// Magic: "CSHR"
#define SHM_MAGIC 0x52485343

// ---------------------------------------------------------------------------
// Our keyboard service callback — intercepts input data
// ---------------------------------------------------------------------------
VOID CursorShareKbServiceCallback(_In_ PDEVICE_OBJECT DeviceObject,
                                  _In_ PKEYBOARD_INPUT_DATA InputDataStart,
                                  _In_ PKEYBOARD_INPUT_DATA InputDataEnd,
                                  _Inout_ PULONG InputDataConsumed) {
  UNREFERENCED_PARAMETER(DeviceObject);

  // If filtering is active and shared memory is mapped, copy events
  if (InterlockedCompareExchange(&g_FilterActive, TRUE, TRUE) &&
      g_SharedMemory) {
    PUCHAR shm = (PUCHAR)g_SharedMemory;
    volatile ULONG *pWriteIdx = (volatile ULONG *)(shm + SHM_WRITE_IDX_OFFSET);
    volatile ULONG *pReadIdx = (volatile ULONG *)(shm + SHM_READ_IDX_OFFSET);

    for (PKEYBOARD_INPUT_DATA data = InputDataStart; data < InputDataEnd;
         data++) {
      ULONG writeIdx = *pWriteIdx;
      ULONG nextIdx = (writeIdx + 1) & (SHM_CAPACITY - 1);

      // Check if buffer is full
      if (nextIdx == *pReadIdx) {
        break; // Drop events rather than block
      }

      // Write event to ring buffer
      PUCHAR eventSlot = shm + SHM_DATA_OFFSET + (writeIdx * SHM_EVENT_SIZE);

      // Event type: KeyDown=1, KeyUp=2
      eventSlot[0] = (data->Flags & KEY_BREAK) ? 2 : 1; // InputEventType
      eventSlot[1] = 0;                                 // reserved

      // Sequence number (lower 16 bits of write index)
      *(PUSHORT)(eventSlot + 2) = (USHORT)(writeIdx & 0xFFFF);

      // Timestamp (QPC)
      LARGE_INTEGER qpc;
      qpc = KeQueryPerformanceCounter(NULL);
      *(PLONGLONG)(eventSlot + 4) = qpc.QuadPart;

      // Keyboard data
      *(PUSHORT)(eventSlot + 12) = data->MakeCode; // scanCode
      *(PUSHORT)(eventSlot + 14) = 0; // virtualKey (filled by user mode)
      eventSlot[16] = (UCHAR)(data->Flags & (KEY_E0 | KEY_E1)); // flags
      eventSlot[17] = 0;
      eventSlot[18] = 0;
      eventSlot[19] = 0;

      // Zero the rest
      RtlZeroMemory(eventSlot + 20, 12);

      // Advance write index (release semantics via volatile)
      KeMemoryBarrier();
      *pWriteIdx = nextIdx;
    }

    // Signal user-mode that data is available
    if (g_NotifyEvent) {
      KeSetEvent(g_NotifyEvent, IO_NO_INCREMENT, FALSE);
    }
  }

  // ALWAYS forward to the original class driver callback
  // This ensures keyboard input is not blocked even if our filter has issues
  if (g_UpperConnectData.ClassService) {
    ((PSERVICE_CALLBACK_ROUTINE)(g_UpperConnectData.ClassService))(
        g_UpperConnectData.ClassDeviceObject, InputDataStart, InputDataEnd,
        InputDataConsumed);
  }
}

// ---------------------------------------------------------------------------
// DriverEntry
// ---------------------------------------------------------------------------
NTSTATUS
DriverEntry(_In_ PDRIVER_OBJECT DriverObject,
            _In_ PUNICODE_STRING RegistryPath) {
  WDF_DRIVER_CONFIG config;
  WDF_DRIVER_CONFIG_INIT(&config, CursorShareKbFilterDeviceAdd);

  NTSTATUS status =
      WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES,
                      &config, WDF_NO_HANDLE);

  return status;
}

// ---------------------------------------------------------------------------
// DeviceAdd callback
// ---------------------------------------------------------------------------
NTSTATUS
CursorShareKbFilterDeviceAdd(_In_ WDFDRIVER Driver,
                             _Inout_ PWDFDEVICE_INIT DeviceInit) {
  UNREFERENCED_PARAMETER(Driver);

  // Indicate this is a filter driver
  WdfFdoInitSetFilter(DeviceInit);

  // Create the device
  WDFDEVICE device;
  WDF_OBJECT_ATTRIBUTES deviceAttributes;
  WDF_OBJECT_ATTRIBUTES_INIT(&deviceAttributes);

  NTSTATUS status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);
  if (!NT_SUCCESS(status)) {
    return status;
  }

  // Create default I/O queue for internal IOCTLs
  WDF_IO_QUEUE_CONFIG queueConfig;
  WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig,
                                         WdfIoQueueDispatchParallel);
  queueConfig.EvtIoInternalDeviceControl = CursorShareKbFilterInternalIoCtl;

  WDFQUEUE queue;
  status =
      WdfIoQueueCreate(device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &queue);
  if (!NT_SUCCESS(status)) {
    return status;
  }

  // Create shared memory section
  UNICODE_STRING shmName;
  RtlInitUnicodeString(&shmName, L"\\BaseNamedObjects\\CursorShareKbdShm");

  OBJECT_ATTRIBUTES objAttrs;
  InitializeObjectAttributes(&objAttrs, &shmName,
                             OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL,
                             NULL);

  HANDLE sectionHandle;
  LARGE_INTEGER sectionSize;
  sectionSize.QuadPart = SHM_TOTAL_SIZE;

  status = ZwCreateSection(&sectionHandle, SECTION_ALL_ACCESS, &objAttrs,
                           &sectionSize, PAGE_READWRITE, SEC_COMMIT, NULL);

  if (NT_SUCCESS(status)) {
    SIZE_T viewSize = 0;
    status =
        ZwMapViewOfSection(sectionHandle, ZwCurrentProcess(), &g_SharedMemory,
                           0, 0, NULL, &viewSize, ViewUnmap, 0, PAGE_READWRITE);

    if (NT_SUCCESS(status)) {
      // Initialize shared memory header
      PUCHAR shm = (PUCHAR)g_SharedMemory;
      RtlZeroMemory(shm, SHM_TOTAL_SIZE);
      *(PULONG)(shm + SHM_MAGIC_OFFSET) = SHM_MAGIC;
      *(PULONG)(shm + SHM_CAPACITY_OFFSET) = SHM_CAPACITY;
    }

    ZwClose(sectionHandle);
  }

  // Create notification event
  UNICODE_STRING eventName;
  RtlInitUnicodeString(&eventName, L"\\BaseNamedObjects\\CursorShareKbdEvent");

  OBJECT_ATTRIBUTES eventAttrs;
  InitializeObjectAttributes(&eventAttrs, &eventName,
                             OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL,
                             NULL);

  HANDLE eventHandle;
  status = ZwCreateEvent(&eventHandle, EVENT_ALL_ACCESS, &eventAttrs,
                         SynchronizationEvent, FALSE);

  if (NT_SUCCESS(status)) {
    status = ObReferenceObjectByHandle(eventHandle, EVENT_ALL_ACCESS,
                                       *ExEventObjectType, KernelMode,
                                       (PVOID *)&g_NotifyEvent, NULL);
    ZwClose(eventHandle);
  }

  return STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------
// Internal IOCTL handler — hook IOCTL_INTERNAL_KEYBOARD_CONNECT
// ---------------------------------------------------------------------------
VOID CursorShareKbFilterInternalIoCtl(_In_ WDFQUEUE Queue,
                                      _In_ WDFREQUEST Request,
                                      _In_ size_t OutputBufferLength,
                                      _In_ size_t InputBufferLength,
                                      _In_ ULONG IoControlCode) {
  UNREFERENCED_PARAMETER(OutputBufferLength);
  UNREFERENCED_PARAMETER(InputBufferLength);

  NTSTATUS status = STATUS_SUCCESS;
  BOOLEAN forwardRequest = TRUE;

  switch (IoControlCode) {
  case IOCTL_INTERNAL_KEYBOARD_CONNECT: {
    // Intercept the connect IOCTL to hook the service callback
    PCONNECT_DATA connectData = NULL;
    size_t length;

    status = WdfRequestRetrieveInputBuffer(Request, sizeof(CONNECT_DATA),
                                           (PVOID *)&connectData, &length);

    if (NT_SUCCESS(status)) {
      // Save the original callback
      g_UpperConnectData = *connectData;

      // Replace with our callback
      connectData->ClassDeviceObject =
          WdfDeviceWdmGetDeviceObject(WdfIoQueueGetDevice(Queue));
      connectData->ClassService = (PVOID)CursorShareKbServiceCallback;

      InterlockedExchange(&g_FilterActive, TRUE);
    }
    break;
  }

  case IOCTL_INTERNAL_KEYBOARD_DISCONNECT:
    // Restore original callback
    InterlockedExchange(&g_FilterActive, FALSE);
    break;

  default:
    break;
  }

  // Forward the request down the stack
  if (forwardRequest) {
    WdfRequestFormatRequestUsingCurrentType(Request);
    WDF_REQUEST_SEND_OPTIONS sendOptions;
    WDF_REQUEST_SEND_OPTIONS_INIT(&sendOptions,
                                  WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);

    if (!WdfRequestSend(Request,
                        WdfDeviceGetIoTarget(WdfIoQueueGetDevice(Queue)),
                        &sendOptions)) {
      status = WdfRequestGetStatus(Request);
      WdfRequestComplete(Request, status);
    }
  }
}
