#include "../Source/Inc/virtualmic.h"
#include "../Source/Inc/vmicbuffer.h"
#include <assert.h>

int main()
{
    static_assert(VMIC_BUFFER_BYTES == 230400, "200 ms of 192 kHz stereo 24-bit PCM");
    unsigned char storage[12] = {}, output[16] = {};
    const unsigned char source[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    VmicBuffer ring = {};
    ring.Reset(storage, sizeof(storage));
    assert(ring.Write(source, 8) == 8);
    assert(ring.Read(output, 6) == 6 && memcmp(output, source, 6) == 0);
    assert(ring.Write(source + 8, 8) == 8);
    assert(ring.Write(source, 4) == 2);
    assert(ring.Write(source, 2) == 0);
    assert(ring.Read(output, 16) == 12);
    assert(memcmp(output, source + 6, 10) == 0 && output[10] == 1 && output[11] == 2);
    for (int i = 12; i < 16; ++i)
        assert(output[i] == 0);
    assert(ring.Read(NULL, 100) == 0);
    ring.Reset(storage, 12);
    assert(ring.Write(source, 12) == 12 && ring.Read(NULL, 6) == 6);
    assert(ring.Read(output, 6) == 6 && memcmp(output, source + 6, 6) == 0);

    const unsigned int rates[] = {44100, 48000, 96000, 192000};
    for (unsigned int rate : rates)
    {
        unsigned int remainder = 0;
        unsigned long long frames = 0;
        for (int i = 0; i < 600000; ++i)
            frames += VmicElapsedFrames(10000, rate, &remainder);
        assert(frames == static_cast<unsigned long long>(rate) * 600 && remainder == 0);
    }
    assert(VmicFormatSupported({44100, 1, 16}));
    assert(VmicFormatSupported({192000, 2, 24}));
    assert(!VmicFormatSupported({44100, 2, 32}));
    assert(!VmicFormatSupported({48000, 0, 16}));
}
