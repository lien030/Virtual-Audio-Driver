/*++
Copyright (c) Microsoft Corporation All Rights Reserved
Declaration of the virtual microphone wave miniport tables.
--*/

#ifndef _VIRTUALAUDIODRIVER_MICARRAYWAVTABLE_H_
#define _VIRTUALAUDIODRIVER_MICARRAYWAVTABLE_H_

#define MICARRAY_DEVICE_MAX_CHANNELS 2
#define MICARRAY_MAX_INPUT_STREAMS 1

#define VMIC_WAVE_FORMAT(rate, bits, channels) \
    { { sizeof(KSDATAFORMAT_WAVEFORMATEXTENSIBLE), 0, 0, 0, \
        STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO), STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM), \
        STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX) }, \
      { { WAVE_FORMAT_EXTENSIBLE, channels, rate, rate * channels * (bits / 8), \
          channels * (bits / 8), bits, sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX) }, \
        bits, channels == 1 ? KSAUDIO_SPEAKER_MONO : KSAUDIO_SPEAKER_STEREO, \
        STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM) } }

static KSDATAFORMAT_WAVEFORMATEXTENSIBLE MicArrayPinSupportedDeviceFormats[] =
{
    VMIC_WAVE_FORMAT(48000, 16, 1),
    VMIC_WAVE_FORMAT(44100, 16, 1),
    VMIC_WAVE_FORMAT(96000, 24, 1),
    VMIC_WAVE_FORMAT(192000, 24, 1),
    VMIC_WAVE_FORMAT(48000, 32, 1),
    VMIC_WAVE_FORMAT(48000, 16, 2),
    VMIC_WAVE_FORMAT(44100, 16, 2),
    VMIC_WAVE_FORMAT(96000, 24, 2),
    VMIC_WAVE_FORMAT(192000, 24, 2),
    VMIC_WAVE_FORMAT(48000, 32, 2),
};
#undef VMIC_WAVE_FORMAT

static MODE_AND_DEFAULT_FORMAT MicArrayPinSupportedDeviceModes[] =
{
    // One mode only: Windows opens a pin per active mode, and the injection buffer feeds one stream.
    { STATIC_AUDIO_SIGNALPROCESSINGMODE_RAW, &MicArrayPinSupportedDeviceFormats[0].DataFormat },
};

static PIN_DEVICE_FORMATS_AND_MODES MicArrayPinDeviceFormatsAndModes[] =
{
    { BridgePin, NULL, 0, NULL, 0 },
    { SystemCapturePin,
      MicArrayPinSupportedDeviceFormats, SIZEOF_ARRAY(MicArrayPinSupportedDeviceFormats),
      MicArrayPinSupportedDeviceModes, SIZEOF_ARRAY(MicArrayPinSupportedDeviceModes) }
};

#define VMIC_DATA_RANGE(rate, bits, channels) \
    { { sizeof(KSDATARANGE_AUDIO), KSDATARANGE_ATTRIBUTES, 0, 0, \
        STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO), STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM), \
        STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX) }, channels, bits, bits, rate, rate }

static KSDATARANGE_AUDIO MicArrayPinDataRangesRawStream[] =
{
    VMIC_DATA_RANGE(48000, 16, 1),
    VMIC_DATA_RANGE(44100, 16, 1),
    VMIC_DATA_RANGE(96000, 24, 1),
    VMIC_DATA_RANGE(192000, 24, 1),
    VMIC_DATA_RANGE(48000, 32, 1),
    VMIC_DATA_RANGE(48000, 16, 2),
    VMIC_DATA_RANGE(44100, 16, 2),
    VMIC_DATA_RANGE(96000, 24, 2),
    VMIC_DATA_RANGE(192000, 24, 2),
    VMIC_DATA_RANGE(48000, 32, 2),
};
#undef VMIC_DATA_RANGE

static PKSDATARANGE MicArrayPinDataRangePointersStream[] =
{
    PKSDATARANGE(&MicArrayPinDataRangesRawStream[0]),
    PKSDATARANGE(&PinDataRangeAttributeList),
    PKSDATARANGE(&MicArrayPinDataRangesRawStream[1]),
    PKSDATARANGE(&PinDataRangeAttributeList),
    PKSDATARANGE(&MicArrayPinDataRangesRawStream[2]),
    PKSDATARANGE(&PinDataRangeAttributeList),
    PKSDATARANGE(&MicArrayPinDataRangesRawStream[3]),
    PKSDATARANGE(&PinDataRangeAttributeList),
    PKSDATARANGE(&MicArrayPinDataRangesRawStream[4]),
    PKSDATARANGE(&PinDataRangeAttributeList),
    PKSDATARANGE(&MicArrayPinDataRangesRawStream[5]),
    PKSDATARANGE(&PinDataRangeAttributeList),
    PKSDATARANGE(&MicArrayPinDataRangesRawStream[6]),
    PKSDATARANGE(&PinDataRangeAttributeList),
    PKSDATARANGE(&MicArrayPinDataRangesRawStream[7]),
    PKSDATARANGE(&PinDataRangeAttributeList),
    PKSDATARANGE(&MicArrayPinDataRangesRawStream[8]),
    PKSDATARANGE(&PinDataRangeAttributeList),
    PKSDATARANGE(&MicArrayPinDataRangesRawStream[9]),
    PKSDATARANGE(&PinDataRangeAttributeList),
};

//=============================================================================
static
KSDATARANGE MicArrayPinDataRangesBridge[] =
{
    {
        sizeof(KSDATARANGE),
        0,
        0,
        0,
        STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
        STATICGUIDOF(KSDATAFORMAT_SUBTYPE_ANALOG),
        STATICGUIDOF(KSDATAFORMAT_SPECIFIER_NONE)
    }
};

static
PKSDATARANGE MicArrayPinDataRangePointersBridge[] =
{
    &MicArrayPinDataRangesBridge[0]
};

//=============================================================================
static
PCPIN_DESCRIPTOR MicArrayWaveMiniportPins[] =
{
    // Wave In Bridge Pin (Capture - From Topology) KSPIN_WAVE_BRIDGE
    {
        0,
        0,
        0,
        NULL,
        {
            0,
            NULL,
            0,
            NULL,
            SIZEOF_ARRAY(MicArrayPinDataRangePointersBridge),
            MicArrayPinDataRangePointersBridge,
            KSPIN_DATAFLOW_IN,
            KSPIN_COMMUNICATION_NONE,
            &KSCATEGORY_AUDIO,
            NULL,
            0
        }
    },
    // Wave In Streaming Pin (Capture) KSPIN_WAVE_HOST
    {
        MICARRAY_MAX_INPUT_STREAMS,
        MICARRAY_MAX_INPUT_STREAMS,
        0,
        NULL,
        {
            0,
            NULL,
            0,
            NULL,
            SIZEOF_ARRAY(MicArrayPinDataRangePointersStream),
            MicArrayPinDataRangePointersStream,
            KSPIN_DATAFLOW_OUT,
            KSPIN_COMMUNICATION_SINK,
            &KSCATEGORY_AUDIO,
            &KSAUDFNAME_RECORDING_CONTROL,
            0
        }
    }
};

//=============================================================================
static
PCNODE_DESCRIPTOR MicArrayWaveMiniportNodes[] =
{
    // KSNODE_WAVE_ADC
    {
        0,                      // Flags
        NULL,                   // AutomationTable
        &KSNODETYPE_ADC,        // Type
        NULL                    // Name
    }
};

//=============================================================================
static
PCCONNECTION_DESCRIPTOR MicArrayWaveMiniportConnections[] =
{
    { PCFILTER_NODE,        KSPIN_WAVE_BRIDGE,      KSNODE_WAVE_ADC,     1 },
    { KSNODE_WAVE_ADC,      0,                      PCFILTER_NODE,       KSPIN_WAVEIN_HOST },
};

//=============================================================================

static
VIRTUALAUDIODRIVER_PROPERTY_ITEM PropertiesMicArrayWaveFilter[] =
{
    {
        {
            &KSPROPSETID_General,
            KSPROPERTY_GENERAL_COMPONENTID,
            KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_BASICSUPPORT,
            PropertyHandler_WaveFilter,
        },
        0,
        0,
        NULL,
        NULL,
        NULL,
        NULL,
        0
    },
    {
        {
            &KSPROPSETID_Pin,
            KSPROPERTY_PIN_PROPOSEDATAFORMAT,
            KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_BASICSUPPORT,
            PropertyHandler_WaveFilter,
        },
        0,
        0,
        NULL,
        NULL,
        NULL,
        NULL,
        0
    },
    {
        {
            &KSPROPSETID_Pin,
            KSPROPERTY_PIN_PROPOSEDATAFORMAT2,
            KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_BASICSUPPORT,
            PropertyHandler_WaveFilter,
        },
        0,
        0,
        NULL,
        NULL,
        NULL,
        NULL,
        0
    },
};

DEFINE_PCAUTOMATION_TABLE_PROP(AutomationMicArrayWaveFilter, PropertiesMicArrayWaveFilter);

//=============================================================================
static
PCFILTER_DESCRIPTOR MicArrayWaveMiniportFilterDescriptor =
{
    0,                                              // Version
    &AutomationMicArrayWaveFilter,                  // AutomationTable
    sizeof(PCPIN_DESCRIPTOR),                       // PinSize
    SIZEOF_ARRAY(MicArrayWaveMiniportPins),         // PinCount
    MicArrayWaveMiniportPins,                       // Pins
    sizeof(PCNODE_DESCRIPTOR),                      // NodeSize
    SIZEOF_ARRAY(MicArrayWaveMiniportNodes),        // NodeCount
    MicArrayWaveMiniportNodes,                      // Nodes
    SIZEOF_ARRAY(MicArrayWaveMiniportConnections),  // ConnectionCount
    MicArrayWaveMiniportConnections,                // Connections
    0,                                              // CategoryCount
    NULL                                            // Categories  - use defaults (audio, render, capture)
};

#endif // _VIRTUALAUDIODRIVER_MICARRAYWAVTABLE_H_
