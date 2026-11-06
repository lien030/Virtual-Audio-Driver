#pragma once

#define VMIC_PROTOCOL_VERSION 1u
#define VMIC_DEVICE_TYPE 0x8000u
#define VMIC_MAX_PACKET_BYTES 16384u
#define VMIC_BUFFER_MILLISECONDS 200u
#define VMIC_BUFFER_BYTES (192000u * 2u * 3u * VMIC_BUFFER_MILLISECONDS / 1000u)
#define VMIC_DEVICE_PATH L"\\\\.\\VirtualMic"

#define IOCTL_VMIC_STATUS ((VMIC_DEVICE_TYPE << 16) | (1u << 14) | (0x800u << 2))
#define IOCTL_VMIC_WRITE ((VMIC_DEVICE_TYPE << 16) | (2u << 14) | (0x801u << 2))
#define IOCTL_VMIC_CLEAR ((VMIC_DEVICE_TYPE << 16) | (2u << 14) | (0x802u << 2))

typedef struct _VMIC_FORMAT
{
    unsigned int SampleRate;
    unsigned short Channels;
    unsigned short BitsPerSample;
} VMIC_FORMAT;

typedef struct _VMIC_STATUS
{
    unsigned int Version;
    unsigned int Size;
    unsigned long long Generation;
    VMIC_FORMAT Format;
    unsigned int Capturing;
    unsigned int QueuedFrames;
    unsigned int CapacityFrames;
    unsigned int MaxPacketBytes;
    unsigned long long WrittenFrames;
    unsigned long long ReadFrames;
    unsigned long long UnderrunFrames;
} VMIC_STATUS;

typedef struct _VMIC_WRITE_HEADER
{
    unsigned int Version;
    unsigned int Size;
    unsigned long long Generation;
    VMIC_FORMAT Format;
} VMIC_WRITE_HEADER;

static_assert(sizeof(VMIC_FORMAT) == 8, "VMIC_FORMAT ABI");
static_assert(sizeof(VMIC_STATUS) == 64, "VMIC_STATUS ABI");
static_assert(sizeof(VMIC_WRITE_HEADER) == 24, "VMIC_WRITE_HEADER ABI");

inline bool VmicFormatSupported(const VMIC_FORMAT& Format)
{
    return (Format.Channels == 1 || Format.Channels == 2) &&
           ((Format.BitsPerSample == 16 && (Format.SampleRate == 44100 || Format.SampleRate == 48000)) ||
            (Format.BitsPerSample == 24 && (Format.SampleRate == 96000 || Format.SampleRate == 192000)) ||
            (Format.BitsPerSample == 32 && Format.SampleRate == 48000));
}

inline unsigned int VmicFrameBytes(const VMIC_FORMAT& Format)
{
    return Format.Channels * (Format.BitsPerSample / 8u);
}
