// Installed-driver check, run in the test VM: Windows must apply the Virtual Mic mute to injected PCM,
// and a communications client must be able to record next to a default client.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winioctl.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <vector>
#include "../Source/Inc/virtualmic.h"

static void Check(bool Success, const char* Operation, unsigned long Code = 0)
{
    if (!Success)
    {
        std::fprintf(stderr, "FAIL: %s (0x%08lx)\n", Operation, Code);
        std::exit(1);
    }
}

static void Ok(HRESULT Result, const char* Operation)
{
    Check(SUCCEEDED(Result), Operation, static_cast<unsigned long>(Result));
}

static IMMDevice* FindMic()
{
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDeviceCollection* devices = nullptr;
    UINT count = 0;
    Ok(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&enumerator)),
        "create device enumerator");
    Ok(enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &devices), "enumerate capture endpoints");
    Ok(devices->GetCount(&count), "count capture endpoints");
    IMMDevice* mic = nullptr;
    for (UINT index = 0; index < count; ++index)
    {
        IMMDevice* device = nullptr;
        IPropertyStore* properties = nullptr;
        PROPVARIANT name;
        PropVariantInit(&name);
        Ok(devices->Item(index, &device), "open capture endpoint");
        Ok(device->OpenPropertyStore(STGM_READ, &properties), "open endpoint properties");
        Ok(properties->GetValue(PKEY_Device_FriendlyName, &name), "read endpoint name");
        const bool match = name.vt == VT_LPWSTR && std::wcsstr(name.pwszVal, L"Virtual Mic") != nullptr;
        PropVariantClear(&name);
        properties->Release();
        Check(!(match && mic), "select a single Virtual Mic endpoint");
        if (match)
            mic = device;
        else
            device->Release();
    }
    Check(mic != nullptr, "find Virtual Mic endpoint");
    return mic;
}

struct Capture
{
    IAudioClient2* Client;
    IAudioCaptureClient* Reader;
    UINT32 FrameBytes;
    UINT64 Frames;
    bool Signal;
};

static HRESULT Start(IMMDevice* Mic, AUDIO_STREAM_CATEGORY Category, Capture& Stream)
{
    Stream = {};
    WAVEFORMATEX* format = nullptr;
    AudioClientProperties properties = {sizeof(properties), FALSE, Category, AUDCLNT_STREAMOPTIONS_NONE};
    HRESULT result = Mic->Activate(__uuidof(IAudioClient2), CLSCTX_INPROC_SERVER, nullptr,
        reinterpret_cast<void**>(&Stream.Client));
    if (SUCCEEDED(result))
        result = Stream.Client->SetClientProperties(&properties);
    if (SUCCEEDED(result))
        result = Stream.Client->GetMixFormat(&format);
    if (SUCCEEDED(result))
    {
        Stream.FrameBytes = format->nBlockAlign;
        result = Stream.Client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 2000000, 0, format, nullptr);
        CoTaskMemFree(format);
    }
    if (SUCCEEDED(result))
        result = Stream.Client->GetService(IID_PPV_ARGS(&Stream.Reader));
    if (SUCCEEDED(result))
        result = Stream.Client->Start();
    return result;
}

struct Writer
{
    HANDLE Device;
    std::vector<BYTE> Packet;
};

// Follows the running capture stream and fills each packet with non-zero PCM.
static void Refresh(Writer& Out)
{
    VMIC_STATUS status = {};
    DWORD bytes = 0;
    Check(DeviceIoControl(Out.Device, IOCTL_VMIC_STATUS, nullptr, 0, &status, sizeof(status), &bytes, nullptr) != FALSE,
        "query Virtual Mic status", GetLastError());
    const VMIC_WRITE_HEADER header = {VMIC_PROTOCOL_VERSION, sizeof(header), status.Generation, status.Format};
    const unsigned int frameBytes = VmicFrameBytes(status.Format);
    Out.Packet.assign(sizeof(header) + VMIC_MAX_PACKET_BYTES / frameBytes * frameBytes, static_cast<BYTE>(0x10));
    std::memcpy(Out.Packet.data(), &header, sizeof(header));
}

static void Feed(Writer& Out)
{
    ULONG accepted = 0;
    DWORD bytes = 0;
    while (DeviceIoControl(Out.Device, IOCTL_VMIC_WRITE, Out.Packet.data(), static_cast<DWORD>(Out.Packet.size()),
        &accepted, sizeof(accepted), &bytes, nullptr))
    {
    }
    const DWORD error = GetLastError();
    if (error == ERROR_BUSY)
        return; // The 200 ms queue is full.
    Check(error == ERROR_RETRY || error == ERROR_NOT_READY, "write injected PCM", error);
    Refresh(Out);
}

// Keeps the queue full while recording, and notes whether any non-silent frame arrived.
static void Pump(Writer& Out, Capture* Streams, int Count, DWORD Milliseconds)
{
    for (int index = 0; index < Count; ++index)
    {
        Streams[index].Frames = 0;
        Streams[index].Signal = false;
    }
    const ULONGLONG end = GetTickCount64() + Milliseconds;
    while (GetTickCount64() < end)
    {
        Feed(Out);
        Sleep(5);
        for (int index = 0; index < Count; ++index)
        {
            Capture& stream = Streams[index];
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            while (SUCCEEDED(stream.Reader->GetBuffer(&data, &frames, &flags, nullptr, nullptr)) && frames)
            {
                if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT))
                    for (UINT32 offset = 0; offset < frames * stream.FrameBytes && !stream.Signal; ++offset)
                        stream.Signal = data[offset] != 0;
                stream.Frames += frames;
                Ok(stream.Reader->ReleaseBuffer(frames), "release capture packet");
            }
        }
    }
}

int main()
{
    Ok(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "initialize COM");
    IMMDevice* mic = FindMic();
    IAudioEndpointVolume* volume = nullptr;
    Ok(mic->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, nullptr, reinterpret_cast<void**>(&volume)),
        "open endpoint volume");
    DWORD hardware = 0;
    BOOL wasMuted = FALSE;
    float level = 0;
    Ok(volume->QueryHardwareSupport(&hardware), "query hardware controls");
    Ok(volume->GetMute(&wasMuted), "read mute");
    Ok(volume->GetMasterVolumeLevelScalar(&level), "read level");
    Ok(volume->SetMute(FALSE, nullptr), "unmute");
    Ok(volume->SetMasterVolumeLevelScalar(1.0f, nullptr), "set full level");

    Capture streams[2] = {};
    Ok(Start(mic, AudioCategory_Other, streams[0]), "start default capture");
    Writer writer = {CreateFileW(VMIC_DEVICE_PATH, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr), {}};
    Check(writer.Device != INVALID_HANDLE_VALUE, "open Virtual Mic writer", GetLastError());
    Refresh(writer);
    Pump(writer, streams, 1, 800);
    Check(streams[0].Frames > 0 && streams[0].Signal, "inject PCM into shared capture");

    int failures = 0;
    Ok(volume->SetMute(TRUE, nullptr), "mute");
    Pump(writer, streams, 1, 200); // Drop audio captured before the mute.
    Pump(writer, streams, 1, 500);
    const bool muted = streams[0].Frames > 0 && !streams[0].Signal;
    std::printf("%s: muting Virtual Mic in Windows silences injected PCM\n", muted ? "PASS" : "FAIL");
    failures += !muted;
    Ok(volume->SetMute(FALSE, nullptr), "unmute");

    const HRESULT second = Start(mic, AudioCategory_Communications, streams[1]);
    if (SUCCEEDED(second))
        Pump(writer, streams, 2, 500);
    const bool together = SUCCEEDED(second) && streams[0].Frames > 0 && streams[1].Frames > 0;
    std::printf("%s: communications and default clients record together (0x%08lx)\n", together ? "PASS" : "FAIL",
        static_cast<unsigned long>(second));
    failures += !together;

    const bool software = !(hardware & (ENDPOINT_HARDWARE_SUPPORT_VOLUME | ENDPOINT_HARDWARE_SUPPORT_MUTE));
    std::printf("%s: Windows owns the level and mute (hardware mask 0x%lx)\n", software ? "PASS" : "FAIL", hardware);
    failures += !software;

    volume->SetMasterVolumeLevelScalar(level, nullptr);
    volume->SetMute(wasMuted, nullptr);
    return failures;
}
