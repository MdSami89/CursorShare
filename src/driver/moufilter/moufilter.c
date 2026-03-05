// =============================================================================
// CursorShare — KMDF Mouse Filter Driver
// Upper filter driver for mouclass — captures mouse input at kernel level.
// =============================================================================

#include <kbdmou.h>
#include <ntddk.h>
#include <ntddmou.h>
#include <wdf.h>


// Forward declarations
DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD CursorShareMouFilterDeviceAdd;
EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL CursorShareMouFilterInternalIoCtl;

// Original service callback from the class driver
static CONNECT_DATA g_UpperConnectData;
static PVOID g_SharedMemory = NULL;
static PKEVENT g_NotifyEvent = NULL;
static volatile LONG g_FilterActive = FALSE;

// Shared memory layout (same structure as keyboard driver)
#define SHM_MAGIC_OFFSET 0
#define SHM_WRITE_IDX_OFFSET 16
#define SHM_READ_IDX_OFFSET 20
#define SHM_CAPACITY_OFFSET 24
#define SHM_DATA_OFFSET 64
#define SHM_EVENT_SIZE 32
#define SHM_CAPACITY 4096
#define SHM_TOTAL_SIZE (SHM_DATA_OFFSET + (SHM_CAPACITY * SHM_EVENT_SIZE))
#define SHM_MAGIC 0x52485343

// Mouse button flags from MOUSE_INPUT_DATA
#define MOUSE_LEFT_DOWN 0x0001
#define MOUSE_LEFT_UP 0x0002
#define MOUSE_RIGHT_DOWN 0x0004
#define MOUSE_RIGHT_UP 0x0008
#define MOUSE_MIDDLE_DOWN 0x0010
#define MOUSE_MIDDLE_UP 0x0020
#define MOUSE_BUTTON_4_DOWN 0x0040
#define MOUSE_BUTTON_4_UP 0x0080
#define MOUSE_BUTTON_5_DOWN 0x0100
#define MOUSE_BUTTON_5_UP 0x0200
#define MOUSE_WHEEL_FLAG 0x0400

// ---------------------------------------------------------------------------
// Mouse service callback — intercepts mouse input data
// ---------------------------------------------------------------------------
VOID CursorShareMouServiceCallback(_In_ PDEVICE_OBJECT DeviceObject,
                                   _In_ PMOUSE_INPUT_DATA InputDataStart,
                                   _In_ PMOUSE_INPUT_DATA InputDataEnd,
                                   _Inout_ PULONG InputDataConsumed) {
  UNREFERENCED_PARAMETER(DeviceObject);

  if (InterlockedCompareExchange(&g_FilterActive, TRUE, TRUE) &&
      g_SharedMemory) {
    PUCHAR shm = (PUCHAR)g_SharedMemory;
    volatile ULONG *pWriteIdx = (volatile ULONG *)(shm + SHM_WRITE_IDX_OFFSET);
    volatile ULONG *pReadIdx = (volatile ULONG *)(shm + SHM_READ_IDX_OFFSET);

    for (PMOUSE_INPUT_DATA data = InputDataStart; data < InputDataEnd; data++) {
      ULONG writeIdx = *pWriteIdx;
      ULONG nextIdx = (writeIdx + 1) & (SHM_CAPACITY - 1);

      if (nextIdx == *pReadIdx) {
        break; // Buffer full — drop event
      }

      PUCHAR eventSlot = shm + SHM_DATA_OFFSET + (writeIdx * SHM_EVENT_SIZE);

      // Determine event type
      UCHAR eventType = 3; // MouseMove by default
      if (data->ButtonFlags &
          (MOUSE_LEFT_DOWN | MOUSE_RIGHT_DOWN | MOUSE_MIDDLE_DOWN |
           MOUSE_BUTTON_4_DOWN | MOUSE_BUTTON_5_DOWN)) {
        eventType = 4; // MouseButtonDown
      } else if (data->ButtonFlags &
                 (MOUSE_LEFT_UP | MOUSE_RIGHT_UP | MOUSE_MIDDLE_UP |
                  MOUSE_BUTTON_4_UP | MOUSE_BUTTON_5_UP)) {
        eventType = 5; // MouseButtonUp
      } else if (data->ButtonFlags & MOUSE_WHEEL_FLAG) {
        eventType = 6; // MouseWheel
      }

      eventSlot[0] = eventType;
      eventSlot[1] = 0;

      // Sequence
      *(PUSHORT)(eventSlot + 2) = (USHORT)(writeIdx & 0xFFFF);

      // Timestamp
      LARGE_INTEGER qpc;
      qpc = KeQueryPerformanceCounter(NULL);
      *(PLONGLONG)(eventSlot + 4) = qpc.QuadPart;

      // Mouse data (offset 12 in the event, matching MouseEvent layout)
      *(PSHORT)(eventSlot + 12) = (SHORT)data->LastX;      // dx
      *(PSHORT)(eventSlot + 14) = (SHORT)data->LastY;      // dy
      *(PSHORT)(eventSlot + 16) = (SHORT)data->ButtonData; // wheelDelta
      *(PSHORT)(eventSlot + 18) = 0;                       // hWheelDelta

      // Convert button flags to our bitmask
      UCHAR buttons = 0;
      if (data->ButtonFlags & MOUSE_LEFT_DOWN)
        buttons |= 0x01;
      if (data->ButtonFlags & MOUSE_RIGHT_DOWN)
        buttons |= 0x02;
      if (data->ButtonFlags & MOUSE_MIDDLE_DOWN)
        buttons |= 0x04;
      if (data->ButtonFlags & MOUSE_BUTTON_4_DOWN)
        buttons |= 0x08;
      if (data->ButtonFlags & MOUSE_BUTTON_5_DOWN)
        buttons |= 0x10;
      eventSlot[20] = buttons;

      RtlZeroMemory(eventSlot + 21, 11);

      KeMemoryBarrier();
      *pWriteIdx = nextIdx;
    }

    if (g_NotifyEvent) {
      KeSetEvent(g_NotifyEvent, IO_NO_INCREMENT, FALSE);
    }
  }

  // ALWAYS forward to original class driver
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
  WDF_DRIVER_CONFIG_INIT(&config, CursorShareMouFilterDeviceAdd);

  return WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES,
                         &config, WDF_NO_HANDLE);
}

// ---------------------------------------------------------------------------
// DeviceAdd
// ---------------------------------------------------------------------------
NTSTATUS
CursorShareMouFilterDeviceAdd(_In_ WDFDRIVER Driver,
                              _Inout_ PWDFDEVICE_INIT DeviceInit) {
  UNREFERENCED_PARAMETER(Driver);

  WdfFdoInitSetFilter(DeviceInit);

  WDFDEVICE device;
  WDF_OBJECT_ATTRIBUTES deviceAttributes;
  WDF_OBJECT_ATTRIBUTES_INIT(&deviceAttributes);

  NTSTATUS status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);
  if (!NT_SUCCESS(status))
    return status;

  // Create I/O queue
  WDF_IO_QUEUE_CONFIG queueConfig;
  WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig,
                                         WdfIoQueueDispatchParallel);
  queueConfig.EvtIoInternalDeviceControl = CursorShareMouFilterInternalIoCtl;

  WDFQUEUE queue;
  status =
      WdfIoQueueCreate(device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &queue);
  if (!NT_SUCCESS(status))
    return status;

  // Create shared memory
  UNICODE_STRING shmName;
  RtlInitUnicodeString(&shmName, L"\\BaseNamedObjects\\CursorShareMouShm");

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
      PUCHAR shm = (PUCHAR)g_SharedMemory;
      RtlZeroMemory(shm, SHM_TOTAL_SIZE);
      *(PULONG)(shm + SHM_MAGIC_OFFSET) = SHM_MAGIC;
      *(PULONG)(shm + SHM_CAPACITY_OFFSET) = SHM_CAPACITY;
    }
    ZwClose(sectionHandle);
  }

  // Create notification event
  UNICODE_STRING eventName;
  RtlInitUnicodeString(&eventName, L"\\BaseNamedObjects\\CursorShareMouEvent");

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
// Internal IOCTL handler
// ---------------------------------------------------------------------------
VOID CursorShareMouFilterInternalIoCtl(_In_ WDFQUEUE Queue,
                                       _In_ WDFREQUEST Request,
                                       _In_ size_t OutputBufferLength,
                                       _In_ size_t InputBufferLength,
                                       _In_ ULONG IoControlCode) {
  UNREFERENCED_PARAMETER(OutputBufferLength);
  UNREFERENCED_PARAMETER(InputBufferLength);

  switch (IoControlCode) {
  case IOCTL_INTERNAL_MOUSE_CONNECT: {
    PCONNECT_DATA connectData = NULL;
    size_t length;

    NTSTATUS status = WdfRequestRetrieveInputBuffer(
        Request, sizeof(CONNECT_DATA), (PVOID *)&connectData, &length);

    if (NT_SUCCESS(status)) {
      g_UpperConnectData = *connectData;
      connectData->ClassDeviceObject =
          WdfDeviceWdmGetDeviceObject(WdfIoQueueGetDevice(Queue));
      connectData->ClassService = (PVOID)CursorShareMouServiceCallback;
      InterlockedExchange(&g_FilterActive, TRUE);
    }
    break;
  }

  case IOCTL_INTERNAL_MOUSE_DISCONNECT:
    InterlockedExchange(&g_FilterActive, FALSE);
    break;
  }

  // Forward request
  WdfRequestFormatRequestUsingCurrentType(Request);
  WDF_REQUEST_SEND_OPTIONS sendOptions;
  WDF_REQUEST_SEND_OPTIONS_INIT(&sendOptions,
                                WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);

  if (!WdfRequestSend(Request, WdfDeviceGetIoTarget(WdfIoQueueGetDevice(Queue)),
                      &sendOptions)) {
    NTSTATUS status = WdfRequestGetStatus(Request);
    WdfRequestComplete(Request, status);
  }
}
