#pragma once

#ifndef _KERNEL_MODE
#include <string.h>
#define RtlCopyMemory memcpy
#define RtlZeroMemory(address, length) memset(address, 0, length)
#endif

// The owner serializes access; storage and capacity must contain whole frames.
struct VmicBuffer
{
    unsigned char* Storage;
    unsigned int Capacity;
    unsigned int Head;
    unsigned int Used;

    void Reset(unsigned char* Data, unsigned int Bytes)
    {
        Storage = Data;
        Capacity = Bytes;
        Head = Used = 0;
    }

    unsigned int Write(const unsigned char* Data, unsigned int Bytes)
    {
        const unsigned int count = Bytes < Capacity - Used ? Bytes : Capacity - Used;
        if (!count)
            return 0;
        const unsigned int tail = (Head + Used) % Capacity;
        const unsigned int first = count < Capacity - tail ? count : Capacity - tail;
        RtlCopyMemory(Storage + tail, Data, first);
        RtlCopyMemory(Storage, Data + first, count - first);
        Used += count;
        return count;
    }

    unsigned int Read(unsigned char* Data, unsigned int Bytes)
    {
        const unsigned int count = Bytes < Used ? Bytes : Used;
        if (count)
        {
            const unsigned int first = count < Capacity - Head ? count : Capacity - Head;
            if (Data)
            {
                RtlCopyMemory(Data, Storage + Head, first);
                RtlCopyMemory(Data + first, Storage, count - first);
            }
            Head = (Head + count) % Capacity;
            Used -= count;
        }
        if (Data && count < Bytes)
            RtlZeroMemory(Data + count, Bytes - count);
        return count;
    }
};

inline unsigned long long VmicElapsedFrames(unsigned long long Elapsed100ns, unsigned int SampleRate,
                                            unsigned int* Remainder)
{
    const unsigned long long numerator = Elapsed100ns * SampleRate + *Remainder;
    *Remainder = static_cast<unsigned int>(numerator % 10000000u);
    return numerator / 10000000u;
}
