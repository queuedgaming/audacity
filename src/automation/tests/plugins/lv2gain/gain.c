/*
 * Audacity: A Digital Audio Editor
 * Minimal LV2 test plugin for parameter automation tests: mono gain in dB.
 */
#include <lv2/core/lv2.h>
#include <math.h>
#include <stdlib.h>

#define GAIN_URI "urn:audacity:test:autogain"

enum { PORT_IN = 0, PORT_OUT = 1, PORT_GAIN = 2, PORT_MODE = 3, PORT_MUTE = 4, PORT_LEVEL = 5 };

typedef struct {
    const float* in;
    float* out;
    const float* gain;
    const float* mode;
    const float* mute;
    float* level;
} Gain;

static LV2_Handle instantiate(const LV2_Descriptor* d, double rate, const char* path, const LV2_Feature* const* f)
{
    (void)d; (void)rate; (void)path; (void)f;
    return calloc(1, sizeof(Gain));
}

static void connect_port(LV2_Handle h, uint32_t port, void* data)
{
    Gain* g = (Gain*)h;
    switch (port) {
    case PORT_IN: g->in = (const float*)data; break;
    case PORT_OUT: g->out = (float*)data; break;
    case PORT_GAIN: g->gain = (const float*)data; break;
    case PORT_MODE: g->mode = (const float*)data; break;
    case PORT_MUTE: g->mute = (const float*)data; break;
    case PORT_LEVEL: g->level = (float*)data; break;
    }
}

static void run(LV2_Handle h, uint32_t n)
{
    Gain* g = (Gain*)h;
    const float k = (g->mute && *g->mute > 0.5f) ? 0.0f : powf(10.0f, (g->gain ? *g->gain : 0.0f) / 20.0f);
    for (uint32_t i = 0; i < n; ++i) {
        g->out[i] = g->in[i] * k;
    }
    if (g->level) {
        *g->level = k;
    }
}

static void cleanup(LV2_Handle h) { free(h); }

static const LV2_Descriptor descriptor = { GAIN_URI, instantiate, connect_port, NULL, run, NULL, cleanup, NULL };

LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index)
{
    return index == 0 ? &descriptor : NULL;
}
