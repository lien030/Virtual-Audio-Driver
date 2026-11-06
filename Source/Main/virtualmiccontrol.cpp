#include "definitions.h"
#include <wdmsec.h>
#include "virtualmiccontrol.h"
#include "vmicbuffer.h"

static const GUID VmicControlClass = {
    0x9a8ec4c7, 0x6306, 0x41e7, {0xa7, 0x46, 0x54, 0x68, 0x1e, 0x83, 0xb4, 0x1c}};

struct VmicControlExtension
{
    bool Removed;
};

static KSPIN_LOCK g_VmicLock;
static PDEVICE_OBJECT g_VmicDevice;
static PDRIVER_DISPATCH g_VmicOriginal[IRP_MJ_MAXIMUM_FUNCTION + 1];
static BYTE g_VmicStorage[VMIC_BUFFER_BYTES];
static VmicBuffer g_VmicBuffer;
static VMIC_STATUS g_VmicStatus;
static PFILE_OBJECT g_VmicWriter;
static PVOID g_VmicCapture;
static LONG g_VmicDeviceSerial;

static void ClearLocked()
{
    g_VmicBuffer.Head = g_VmicBuffer.Used = 0;
    g_VmicStatus.QueuedFrames = 0;
}

static NTSTATUS Complete(PIRP Irp, NTSTATUS Status, ULONG_PTR Bytes = 0)
{
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Bytes;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static NTSTATUS VmicDispatch(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    const PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    if (DeviceObject->DeviceType != VMIC_DEVICE_TYPE)
        return g_VmicOriginal[stack->MajorFunction](DeviceObject, Irp);

    const auto extension = static_cast<VmicControlExtension*>(DeviceObject->DeviceExtension);
    const PFILE_OBJECT file = stack->FileObject;
    NTSTATUS status = STATUS_SUCCESS;
    ULONG_PTR bytes = 0;
    KIRQL irql;
    KeAcquireSpinLock(&g_VmicLock, &irql);

    switch (stack->MajorFunction)
    {
        case IRP_MJ_CLEANUP:
            if (g_VmicWriter == file)
            {
                g_VmicWriter = NULL;
                ClearLocked();
            }
            break;
        case IRP_MJ_CLOSE:
            break;
        case IRP_MJ_CREATE:
            if (extension->Removed)
                status = STATUS_DEVICE_REMOVED;
            else if (file->FileName.Length)
                status = STATUS_OBJECT_NAME_NOT_FOUND;
            else if (stack->Parameters.Create.SecurityContext->DesiredAccess & FILE_WRITE_DATA)
            {
                if (g_VmicWriter)
                    status = STATUS_SHARING_VIOLATION;
                else
                {
                    g_VmicWriter = file;
                    ClearLocked();
                }
            }
            break;
        case IRP_MJ_DEVICE_CONTROL:
            if (extension->Removed)
            {
                status = STATUS_DEVICE_REMOVED;
                break;
            }
            switch (stack->Parameters.DeviceIoControl.IoControlCode)
            {
                case IOCTL_VMIC_STATUS:
                    if (stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(VMIC_STATUS))
                    {
                        status = STATUS_BUFFER_TOO_SMALL;
                        break;
                    }
                    g_VmicStatus.QueuedFrames = g_VmicBuffer.Used / VmicFrameBytes(g_VmicStatus.Format);
                    RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer, &g_VmicStatus, sizeof(g_VmicStatus));
                    bytes = sizeof(g_VmicStatus);
                    break;
                case IOCTL_VMIC_CLEAR:
                    if (g_VmicWriter != file)
                        status = STATUS_ACCESS_DENIED;
                    else
                        ClearLocked();
                    break;
                case IOCTL_VMIC_WRITE: {
                    const ULONG inputBytes = stack->Parameters.DeviceIoControl.InputBufferLength;
                    if (g_VmicWriter != file)
                    {
                        status = STATUS_ACCESS_DENIED;
                        break;
                    }
                    if (inputBytes < sizeof(VMIC_WRITE_HEADER) ||
                        inputBytes - sizeof(VMIC_WRITE_HEADER) > VMIC_MAX_PACKET_BYTES ||
                        stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(ULONG))
                    {
                        status = STATUS_INVALID_BUFFER_SIZE;
                        break;
                    }
                    const auto header =
                        static_cast<const VMIC_WRITE_HEADER*>(Irp->AssociatedIrp.SystemBuffer);
                    if (header->Version != VMIC_PROTOCOL_VERSION || header->Size != sizeof(*header))
                    {
                        status = STATUS_REVISION_MISMATCH;
                        break;
                    }
                    const ULONG frameBytes = VmicFrameBytes(header->Format);
                    const ULONG dataBytes = inputBytes - sizeof(*header);
                    if (!VmicFormatSupported(header->Format) || dataBytes % frameBytes)
                    {
                        status = STATUS_INVALID_PARAMETER;
                        break;
                    }
                    if (!g_VmicStatus.Capturing)
                    {
                        status = STATUS_DEVICE_NOT_READY;
                        break;
                    }
                    if (header->Generation != g_VmicStatus.Generation ||
                        RtlCompareMemory(&header->Format, &g_VmicStatus.Format, sizeof(VMIC_FORMAT)) !=
                            sizeof(VMIC_FORMAT))
                    {
                        status = STATUS_RETRY;
                        break;
                    }
                    const ULONG accepted =
                        g_VmicBuffer.Write(static_cast<const BYTE*>(Irp->AssociatedIrp.SystemBuffer) +
                                               sizeof(*header),
                                           dataBytes) /
                        frameBytes;
                    if (!accepted && dataBytes)
                    {
                        status = STATUS_DEVICE_BUSY;
                        break;
                    }
                    g_VmicStatus.WrittenFrames += accepted;
                    *static_cast<ULONG*>(Irp->AssociatedIrp.SystemBuffer) = accepted;
                    bytes = sizeof(ULONG);
                    break;
                }
                default:
                    status = STATUS_INVALID_DEVICE_REQUEST;
                    break;
            }
            break;
        default:
            status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }
    KeReleaseSpinLock(&g_VmicLock, irql);
    return Complete(Irp, status, bytes);
}

void VmicInstallDispatch(PDRIVER_OBJECT DriverObject)
{
    KeInitializeSpinLock(&g_VmicLock);
    g_VmicStatus.Version = VMIC_PROTOCOL_VERSION;
    g_VmicStatus.Size = sizeof(VMIC_STATUS);
    g_VmicStatus.Format = {48000, 1, 16};
    g_VmicStatus.MaxPacketBytes = VMIC_MAX_PACKET_BYTES;
    g_VmicStatus.CapacityFrames = g_VmicStatus.Format.SampleRate * VMIC_BUFFER_MILLISECONDS / 1000u;
    g_VmicBuffer.Reset(g_VmicStorage, g_VmicStatus.CapacityFrames * VmicFrameBytes(g_VmicStatus.Format));
    // Unsupported control-device requests must not reach PortCls with our private extension.
    for (UCHAR function = 0; function <= IRP_MJ_MAXIMUM_FUNCTION; ++function)
    {
        g_VmicOriginal[function] = DriverObject->MajorFunction[function];
        DriverObject->MajorFunction[function] = VmicDispatch;
    }
}

NTSTATUS VmicCreateControl(PDEVICE_OBJECT Adapter)
{
    if (g_VmicDevice)
        return STATUS_SUCCESS;
    WCHAR nameBuffer[64];
    UNICODE_STRING name;
    RtlInitEmptyUnicodeString(&name, nameBuffer, sizeof(nameBuffer));
    NTSTATUS status = RtlUnicodeStringPrintf(&name, L"\\Device\\VirtualMic%lu",
                                             static_cast<ULONG>(InterlockedIncrement(&g_VmicDeviceSerial)));
    if (!NT_SUCCESS(status))
        return status;
    UNICODE_STRING link = RTL_CONSTANT_STRING(L"\\DosDevices\\VirtualMic");
    UNICODE_STRING acl = RTL_CONSTANT_STRING(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;IU)");
    PDEVICE_OBJECT device = NULL;
    status =
        IoCreateDeviceSecure(Adapter->DriverObject, sizeof(VmicControlExtension), &name, VMIC_DEVICE_TYPE,
                             FILE_DEVICE_SECURE_OPEN, FALSE, &acl, &VmicControlClass, &device);
    if (!NT_SUCCESS(status))
        return status;
    status = IoCreateSymbolicLink(&link, &name);
    if (!NT_SUCCESS(status))
    {
        IoDeleteDevice(device);
        return status;
    }
    device->Flags &= ~DO_DEVICE_INITIALIZING;
    g_VmicDevice = device;
    return STATUS_SUCCESS;
}

void VmicRemoveControl()
{
    PDEVICE_OBJECT device = g_VmicDevice;
    if (!device)
        return;
    KIRQL irql;
    KeAcquireSpinLock(&g_VmicLock, &irql);
    static_cast<VmicControlExtension*>(device->DeviceExtension)->Removed = true;
    g_VmicStatus.Capturing = 0;
    ++g_VmicStatus.Generation;
    g_VmicWriter = NULL;
    g_VmicCapture = NULL;
    g_VmicDevice = NULL;
    ClearLocked();
    KeReleaseSpinLock(&g_VmicLock, irql);
    UNICODE_STRING link = RTL_CONSTANT_STRING(L"\\DosDevices\\VirtualMic");
    IoDeleteSymbolicLink(&link);
    IoDeleteDevice(device);
}

NTSTATUS VmicStartCapture(PVOID Stream, const WAVEFORMATEX* Format)
{
    const VMIC_FORMAT format = {Format->nSamplesPerSec, Format->nChannels, Format->wBitsPerSample};
    if (!VmicFormatSupported(format))
        return STATUS_NOT_SUPPORTED;
    KIRQL irql;
    KeAcquireSpinLock(&g_VmicLock, &irql);
    NTSTATUS status = STATUS_SUCCESS;
    if (!g_VmicDevice)
        status = STATUS_DEVICE_REMOVED;
    else if (g_VmicCapture && g_VmicCapture != Stream)
        status = STATUS_DEVICE_BUSY;
    else if (!g_VmicStatus.Capturing)
    {
        g_VmicCapture = Stream;
        g_VmicStatus.Format = format;
        ++g_VmicStatus.Generation;
        g_VmicStatus.Capturing = 1;
        g_VmicStatus.CapacityFrames = format.SampleRate * VMIC_BUFFER_MILLISECONDS / 1000u;
        g_VmicBuffer.Reset(g_VmicStorage, g_VmicStatus.CapacityFrames * VmicFrameBytes(format));
    }
    KeReleaseSpinLock(&g_VmicLock, irql);
    return status;
}

void VmicStopCapture(PVOID Stream)
{
    KIRQL irql;
    KeAcquireSpinLock(&g_VmicLock, &irql);
    if (g_VmicCapture == Stream)
    {
        g_VmicCapture = NULL;
        g_VmicStatus.Capturing = 0;
        ++g_VmicStatus.Generation;
        ClearLocked();
    }
    KeReleaseSpinLock(&g_VmicLock, irql);
}

void VmicReadCapture(PVOID Stream, BYTE* Buffer, ULONG Bytes)
{
    KIRQL irql;
    KeAcquireSpinLock(&g_VmicLock, &irql);
    if (g_VmicCapture == Stream)
    {
        const ULONG frameBytes = VmicFrameBytes(g_VmicStatus.Format);
        const ULONG read = g_VmicBuffer.Read(Buffer, Bytes);
        g_VmicStatus.ReadFrames += read / frameBytes;
        g_VmicStatus.UnderrunFrames += (Bytes - read) / frameBytes;
    }
    else if (Buffer)
        RtlZeroMemory(Buffer, Bytes);
    KeReleaseSpinLock(&g_VmicLock, irql);
}
