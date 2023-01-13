/*
 * Copyright (c) 2021-2022 Michael Lass
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <ladspa.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// PLUGIN_ID needs to be defined via compiler call or here:
// #define PLUGIN_ID xyz
#ifndef PLUGIN_ID
#error "PLUGIN_ID not specified"
#endif

#define IO_INPUT 0
#define IO_OUTPUT 1
#define IO_FREQ 2
#define IO_NHARMONICS 3

#define MAX_STAGES 23

typedef struct {
  LADSPA_Data prevIn[2];
  LADSPA_Data prevOut[2];
  LADSPA_Data a0;
  LADSPA_Data a1;
  LADSPA_Data a2;
  LADSPA_Data b1;
  LADSPA_Data b2;
} FilterStage;

typedef struct {
  LADSPA_Data *inputBuffer;
  LADSPA_Data *outputBuffer;
  LADSPA_Data freq;
  LADSPA_Data sampleRate;
  int nstages;
  FilterStage stages[MAX_STAGES];
} Filter;

static void updateParameters(Filter *filt) {
  for (int i = 0; i < filt->nstages; i++) {
    filt->stages[i].prevIn[0] = 0.0;
    filt->stages[i].prevIn[1] = 0.0;
    filt->stages[i].prevOut[0] = 0.0;
    filt->stages[i].prevOut[1] = 0.0;

    // Following Eq. 19-8 of the DSP Guide by S.W. Smith:
    // http://www.dspguide.com/ch19/3.htm
    LADSPA_Data f = (LADSPA_Data)(i + 1) * filt->freq / filt->sampleRate;
    LADSPA_Data bw = 0.0003;
    LADSPA_Data R = 1.0 - 3 * bw;
    LADSPA_Data K = (1.0 - 2 * R * cos(2 * M_PI * f) + R * R) /
                    (2.0 - 2 * cos(2 * M_PI * f));
    filt->stages[i].a0 = K;
    filt->stages[i].a1 = -2.0 * K * cos(2 * M_PI * f);
    filt->stages[i].a2 = K;
    filt->stages[i].b1 = 2 * R * cos(2 * M_PI * f);
    filt->stages[i].b2 = -R * R;
  }
}

static LADSPA_Handle instantiateFilter(const LADSPA_Descriptor *Descriptor,
                                       unsigned long SampleRate) {
  Filter *filt = malloc(sizeof(Filter));

  filt->freq = 1000;
  filt->sampleRate = SampleRate;
  filt->nstages = 12;
  updateParameters(filt);

  return filt;
}

static void connectPortToFilter(LADSPA_Handle Instance, unsigned long Port,
                                LADSPA_Data *DataLocation) {

  Filter *filt = (Filter *)Instance;

  switch (Port) {
  case IO_INPUT:
    filt->inputBuffer = DataLocation;
    break;
  case IO_OUTPUT:
    filt->outputBuffer = DataLocation;
    break;
  case IO_FREQ:
    filt->freq = *DataLocation;
    updateParameters(filt);
    break;
  case IO_NHARMONICS:
    filt->nstages = (int)*DataLocation;
    if (filt->nstages < 1)
      filt->nstages = 1;
    if (filt->nstages > MAX_STAGES)
      filt->nstages = MAX_STAGES;
    updateParameters(filt);
    break;
  }
}

static void runFilter(LADSPA_Handle Instance, unsigned long SampleCount) {

  Filter *filt = (Filter *)Instance;
  LADSPA_Data *input = filt->inputBuffer;
  LADSPA_Data *output = filt->outputBuffer;

  for (int stageIdx = 0; stageIdx < filt->nstages; stageIdx++) {

    FilterStage *stage = &filt->stages[stageIdx];

    if (filt->freq * (stageIdx + 1) > filt->sampleRate / 2) {
      // frequency outside of valid range
      if (stageIdx == 0) {
        for (unsigned long sampleIdx = 0; sampleIdx < SampleCount;
             sampleIdx++) {
          output[sampleIdx] = input[sampleIdx];
        }
      }
      return;
    }

    // Following Eq. 19-1 of the DSP Guide by S.W. Smith:
    // http://www.dspguide.com/ch19/1.htm
    for (unsigned long sampleIdx = 0; sampleIdx < SampleCount; sampleIdx++) {
      LADSPA_Data v = input[sampleIdx];
      output[sampleIdx] = stage->a0 * v + stage->a1 * stage->prevIn[1] +
                          stage->a2 * stage->prevIn[0] +
                          stage->b1 * stage->prevOut[1] +
                          stage->b2 * stage->prevOut[0];
      stage->prevIn[0] = stage->prevIn[1];
      stage->prevIn[1] = v;
      stage->prevOut[0] = stage->prevOut[1];
      stage->prevOut[1] = output[sampleIdx];
    }
    input = output; // next stage will use the generated output as input
  }
}

static void cleanupFilter(LADSPA_Handle Instance) { free(Instance); }

static LADSPA_Descriptor *descriptor = NULL;

static void __attribute__((constructor)) init() {

  char **portNames;
  LADSPA_PortDescriptor *portDescriptors;
  LADSPA_PortRangeHint *portRangeHints;

  descriptor = (LADSPA_Descriptor *)malloc(sizeof(LADSPA_Descriptor));

  descriptor->UniqueID = PLUGIN_ID;
  descriptor->Label = strdup("notch_harmonics");
  descriptor->Properties = LADSPA_PROPERTY_HARD_RT_CAPABLE;
  descriptor->Name =
      strdup("Multiple notch filters placed at harmonics of a base frequency.");
  descriptor->Maker = strdup("Michael Lass");
  descriptor->Copyright = strdup("2021-2022 Michael Lass, MIT License");
  descriptor->PortCount = 4;

  portDescriptors =
      (LADSPA_PortDescriptor *)calloc(4, sizeof(LADSPA_PortDescriptor));
  descriptor->PortDescriptors = (const LADSPA_PortDescriptor *)portDescriptors;
  portDescriptors[IO_INPUT] = LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO;
  portDescriptors[IO_OUTPUT] = LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO;
  portDescriptors[IO_FREQ] = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
  portDescriptors[IO_NHARMONICS] = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;

  portNames = (char **)calloc(4, sizeof(char *));
  descriptor->PortNames = (const char **)portNames;
  portNames[IO_INPUT] = strdup("Input");
  portNames[IO_OUTPUT] = strdup("Output");
  portNames[IO_FREQ] = strdup("Base frequency");
  portNames[IO_NHARMONICS] = strdup("Number of harmonics");

  portRangeHints =
      ((LADSPA_PortRangeHint *)calloc(4, sizeof(LADSPA_PortRangeHint)));
  descriptor->PortRangeHints = (const LADSPA_PortRangeHint *)portRangeHints;
  portRangeHints[IO_INPUT].HintDescriptor = 0;
  portRangeHints[IO_OUTPUT].HintDescriptor = 0;
  portRangeHints[IO_FREQ].HintDescriptor =
      LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE |
      LADSPA_HINT_LOGARITHMIC | LADSPA_HINT_DEFAULT_MIDDLE;
  portRangeHints[IO_FREQ].LowerBound = 50.0;
  portRangeHints[IO_FREQ].UpperBound = 20000.0;
  portRangeHints[IO_NHARMONICS].HintDescriptor =
      LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE |
      LADSPA_HINT_INTEGER | LADSPA_HINT_DEFAULT_MIDDLE;
  portRangeHints[IO_NHARMONICS].LowerBound = 0.9;
  portRangeHints[IO_NHARMONICS].UpperBound = 0.1 + MAX_STAGES;

  descriptor->instantiate = instantiateFilter;
  descriptor->connect_port = connectPortToFilter;
  descriptor->run = runFilter;
  descriptor->cleanup = cleanupFilter;
  descriptor->activate = NULL;
  descriptor->run_adding = NULL;
  descriptor->set_run_adding_gain = NULL;
  descriptor->deactivate = NULL;
}

static void __attribute__((destructor)) fini() {
  if (descriptor) {
    free((char *)descriptor->Label);
    free((char *)descriptor->Name);
    free((char *)descriptor->Maker);
    free((char *)descriptor->Copyright);
    free((LADSPA_PortDescriptor *)descriptor->PortDescriptors);
    for (unsigned long idx = 0; idx < descriptor->PortCount; idx++)
      free((char *)(descriptor->PortNames[idx]));
    free((char **)descriptor->PortNames);
    free((LADSPA_PortRangeHint *)descriptor->PortRangeHints);
    free(descriptor);
  }
}

const LADSPA_Descriptor *ladspa_descriptor(unsigned long Index) {
  if (Index == 0)
    return descriptor;
  return NULL;
}
