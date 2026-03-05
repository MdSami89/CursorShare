#pragma once
// =============================================================================
// CursorShare — Keyboard Filter Driver Header
// =============================================================================

#include <kbdmou.h>
#include <ntddk.h>
#include <wdf.h>


// Driver callbacks
DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD CursorShareKbFilterDeviceAdd;
EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL CursorShareKbFilterInternalIoCtl;

// Service callback
VOID CursorShareKbServiceCallback(_In_ PDEVICE_OBJECT DeviceObject,
                                  _In_ PKEYBOARD_INPUT_DATA InputDataStart,
                                  _In_ PKEYBOARD_INPUT_DATA InputDataEnd,
                                  _Inout_ PULONG InputDataConsumed);
