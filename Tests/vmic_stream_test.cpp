#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winioctl.h>
#include <mmreg.h>
#include <initguid.h>
#include <ks.h>
#include <ksmedia.h>
#include <setupapi.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "../Source/Inc/virtualmic.h"

static void Check(bool Success, const char* Operation, DWORD Error = 0)
{
    if (!Success)
    {
        std::fprintf(stderr, "FAIL: %s (error %lu)\n", Operation, Error);
        std::exit(1);
    }
}

static DWORD Property(HANDLE Device, void* Input, DWORD InputSize, void* Output, DWORD OutputSize)
{
    OVERLAPPED overlapped = {};
    overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    Check(overlapped.hEvent != NULL, "create property event", GetLastError());
    DWORD bytes = 0;
    BOOL success = DeviceIoControl(Device, IOCTL_KS_PROPERTY, Input, InputSize,
        Output, OutputSize, &bytes, &overlapped);
    DWORD error = success ? ERROR_SUCCESS : GetLastError();
    if (error == ERROR_IO_PENDING)
    {
        if (WaitForSingleObject(overlapped.hEvent, 5000) != WAIT_OBJECT_0)
            CancelIoEx(Device, &overlapped);
        success = GetOverlappedResult(Device, &overlapped, &bytes, TRUE);
        error = success ? ERROR_SUCCESS : GetLastError();
    }
    CloseHandle(overlapped.hEvent);
    return error;
}

static void SetState(HANDLE Pin, KSSTATE State)
{
    KSPROPERTY property = {KSPROPSETID_Connection, KSPROPERTY_CONNECTION_STATE, KSPROPERTY_TYPE_SET};
    const DWORD error = Property(Pin, &property, sizeof(property), &State, sizeof(State));
    Check(error == ERROR_SUCCESS, "set stream state", error);
}

static void TestBuffer(const std::wstring& Path, VMIC_FORMAT Format, ULONG Size,
                       ULONG Notifications, bool Accepted)
{
    HANDLE filter = CreateFileW(Path.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    Check(filter != INVALID_HANDLE_VALUE, "open Virtual Mic filter", GetLastError());
    struct
    {
        KSPIN_CONNECT Connect;
        KSDATAFORMAT_WAVEFORMATEXTENSIBLE Format;
        KSMULTIPLE_ITEM Attributes;
        KSATTRIBUTE_AUDIOSIGNALPROCESSING_MODE Mode;
    } request = {};
    request.Connect.Interface = {KSINTERFACESETID_Standard, KSINTERFACE_STANDARD_LOOPED_STREAMING, 0};
    request.Connect.Medium = {KSMEDIUMSETID_Standard, KSMEDIUM_TYPE_ANYINSTANCE, 0};
    request.Connect.PinId = 1;
    request.Connect.Priority = {KSPRIORITY_NORMAL, 1};
    const ULONG frameBytes = VmicFrameBytes(Format);
    auto& format = request.Format;
    format.DataFormat.FormatSize = sizeof(format);
    format.DataFormat.SampleSize = frameBytes;
    format.DataFormat.Flags = KSDATAFORMAT_ATTRIBUTES;
    format.DataFormat.MajorFormat = KSDATAFORMAT_TYPE_AUDIO;
    format.DataFormat.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    format.DataFormat.Specifier = KSDATAFORMAT_SPECIFIER_WAVEFORMATEX;
    format.WaveFormatExt.Format = {WAVE_FORMAT_EXTENSIBLE, Format.Channels, Format.SampleRate,
        Format.SampleRate * frameBytes, static_cast<WORD>(frameBytes), Format.BitsPerSample, 22};
    format.WaveFormatExt.Samples.wValidBitsPerSample = Format.BitsPerSample;
    format.WaveFormatExt.dwChannelMask = Format.Channels == 1 ? KSAUDIO_SPEAKER_MONO : KSAUDIO_SPEAKER_STEREO;
    format.WaveFormatExt.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    request.Attributes.Size = sizeof(request.Attributes) + sizeof(request.Mode);
    request.Attributes.Count = 1;
    request.Mode.AttributeHeader.Size = sizeof(request.Mode);
    request.Mode.AttributeHeader.Attribute = KSATTRIBUTEID_AUDIOSIGNALPROCESSING_MODE;
    request.Mode.SignalProcessingMode = AUDIO_SIGNALPROCESSINGMODE_RAW;
    HANDLE pin = INVALID_HANDLE_VALUE;
    DWORD error = KsCreatePin(filter, &request.Connect, GENERIC_READ | GENERIC_WRITE, &pin);
    Check(error == ERROR_SUCCESS, "create Virtual Mic pin", error);
    CloseHandle(filter);

    KSRTAUDIO_BUFFER_PROPERTY_WITH_NOTIFICATION property = {};
    property.Property = {KSPROPSETID_RtAudio, KSPROPERTY_RTAUDIO_BUFFER_WITH_NOTIFICATION, KSPROPERTY_TYPE_GET};
    property.RequestedBufferSize = Size;
    property.NotificationCount = Notifications;
    KSRTAUDIO_BUFFER buffer = {};
    error = Property(pin, &property, sizeof(property), &buffer, sizeof(buffer));
    if (!Accepted)
    {
        Check(error == ERROR_INVALID_PARAMETER, "reject unsupported buffer", error);
        CloseHandle(pin);
        return;
    }
    Check(error == ERROR_SUCCESS && buffer.ActualBufferSize == Size, "allocate supported buffer", error);
    HANDLE event = CreateEventW(NULL, FALSE, FALSE, NULL);
    Check(event != NULL, "create notification event", GetLastError());
    KSRTAUDIO_NOTIFICATION_EVENT_PROPERTY notification = {};
    notification.Property = {KSPROPSETID_RtAudio, KSPROPERTY_RTAUDIO_REGISTER_NOTIFICATION_EVENT, KSPROPERTY_TYPE_SET};
    notification.NotificationEvent = event;
    error = Property(pin, &notification, sizeof(notification), NULL, 0);
    Check(error == ERROR_SUCCESS, "register notification event", error);
    SetState(pin, KSSTATE_ACQUIRE);
    SetState(pin, KSSTATE_PAUSE);
    LARGE_INTEGER before, after, frequency;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&before);
    SetState(pin, KSSTATE_RUN);
    QueryPerformanceCounter(&after);
    Check(WaitForSingleObject(event, 2000) == WAIT_OBJECT_0, "receive capture notification");
    KSPROPERTY read = {KSPROPSETID_RtAudio, KSPROPERTY_RTAUDIO_GETREADPACKET, KSPROPERTY_TYPE_GET};
    KSRTAUDIO_GETREADPACKET_INFO packet = {};
    error = Property(pin, &read, sizeof(read), &packet, sizeof(packet));
    Check(error == ERROR_SUCCESS, "read completed capture packet", error);
    const ULONGLONG packetFrames = Size / Notifications / frameBytes;
    const LONGLONG offset = packet.PacketNumber * packetFrames * frequency.QuadPart / Format.SampleRate;
    const LONGLONG tolerance = (frequency.QuadPart + Format.SampleRate - 1) / Format.SampleRate + 1;
    const LONGLONG timestamp = static_cast<LONGLONG>(packet.PerformanceCounterValue);
    Check(timestamp >= before.QuadPart + offset - tolerance &&
          timestamp <= after.QuadPart + offset + tolerance, "timestamp identifies packet's first frame");
    SetState(pin, KSSTATE_PAUSE);
    SetState(pin, KSSTATE_ACQUIRE);
    SetState(pin, KSSTATE_STOP);
    notification.Property.Id = KSPROPERTY_RTAUDIO_UNREGISTER_NOTIFICATION_EVENT;
    error = Property(pin, &notification, sizeof(notification), NULL, 0);
    Check(error == ERROR_SUCCESS, "unregister notification event", error);
    CloseHandle(pin);
    CloseHandle(event);
}

int main()
{
    HDEVINFO devices = SetupDiGetClassDevsW(&KSCATEGORY_CAPTURE, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    Check(devices != INVALID_HANDLE_VALUE, "enumerate capture filters", GetLastError());
    std::wstring path;
    for (DWORD index = 0; ; ++index)
    {
        SP_DEVICE_INTERFACE_DATA data = {};
        data.cbSize = sizeof(data);
        if (!SetupDiEnumDeviceInterfaces(devices, NULL, &KSCATEGORY_CAPTURE, index, &data))
        {
            Check(GetLastError() == ERROR_NO_MORE_ITEMS, "enumerate capture interface", GetLastError());
            break;
        }
        DWORD size = 0;
        SetupDiGetDeviceInterfaceDetailW(devices, &data, NULL, 0, &size, NULL);
        Check(size >= sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W), "query capture interface size", GetLastError());
        std::vector<BYTE> storage(size);
        auto detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());
        detail->cbSize = sizeof(*detail);
        Check(SetupDiGetDeviceInterfaceDetailW(devices, &data, detail, size, NULL, NULL) != FALSE,
            "read capture interface", GetLastError());
        if (std::wstring(detail->DevicePath).find(L"\\wavemicarray1") != std::wstring::npos)
        {
            Check(path.empty(), "select a single Virtual Mic filter");
            path = detail->DevicePath;
        }
    }
    SetupDiDestroyDeviceInfoList(devices);
    Check(!path.empty(), "find installed Virtual Mic driver");
    const VMIC_FORMAT formats[] = {
        {44100, 1, 16}, {48000, 1, 16}, {96000, 1, 24}, {192000, 1, 24}, {48000, 1, 32},
        {44100, 2, 16}, {48000, 2, 16}, {96000, 2, 24}, {192000, 2, 24}, {48000, 2, 32}
    };
    for (const auto& format : formats)
    {
        const ULONG frameBytes = VmicFrameBytes(format);
        const ULONG minPacketFrames = (format.SampleRate + 999) / 1000;
        TestBuffer(path, format, (format.SampleRate / 2000) * frameBytes * 2, 2, false);
        TestBuffer(path, format, (format.SampleRate / 50 + 1) * frameBytes, 2, false);
        TestBuffer(path, format, (format.SampleRate / 100) * frameBytes * 2, 2, true);
        TestBuffer(path, format, minPacketFrames * frameBytes, 1, true);
        TestBuffer(path, format, minPacketFrames * frameBytes * 2, 2, true);
        std::printf("PASS: %lu Hz / %u-bit / %u channels: buffer validation, notifications, first-frame timestamps\n",
            format.SampleRate, format.BitsPerSample, format.Channels);
    }
    TestBuffer(path, formats[1], 1920, 0, false);
    TestBuffer(path, formats[1], 1920, 3, false);
    std::puts("PASS: 52 WaveRT stream cases.");
}
