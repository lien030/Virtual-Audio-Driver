#pragma once

#include "virtualmic.h"

void VmicInstallDispatch(PDRIVER_OBJECT DriverObject);
NTSTATUS VmicCreateControl(PDEVICE_OBJECT Adapter);
void VmicRemoveControl();
NTSTATUS VmicStartCapture(PVOID Stream, const WAVEFORMATEX* Format);
void VmicStopCapture(PVOID Stream);
void VmicReadCapture(PVOID Stream, BYTE* Buffer, ULONG Bytes);
