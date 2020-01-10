/*
Copyright 2019 Tomoaki Itoh
This software is released under the MIT License, see LICENSE.txt.
//*/

#include "userrevfx.h"
#include "buffer_ops.h"
#include "LCWCommon.h"

#define LCW_DELAY_BUFFER_DEC(p) ( ((p)->pointer - 1) & (p)->mask )
#define LCW_DELAY_BUFFER_LUT(p, i) ( (p)->buffer[(p)->pointer + (i) & (p)->mask] )

typedef struct {
    int32_t *buffer;
    uint32_t size;
    uint32_t mask;
    int32_t pointer;
    int32_t gain;
} LCWDelayBuffer;

#define LCW_REVERB_GAIN_TABLE_SIZE (64 + 1)

#define AP1_DELAY (241) // = 48000 * 0.005
#define AP2_DELAY (53) // = 48000 * 0.0017

#define LCW_REVERB_DELAY_SIZE (1<<12)
#define LCW_REVERB_AP1_SIZE (1<<12)
#define LCW_REVERB_AP2_SIZE (1<<12)

#define LCW_REVERB_DELAY_PIPE_COUNT (4)
#define LCW_REVERB_DELAY_SIZE_TOTAL (LCW_REVERB_DELAY_SIZE * LCW_REVERB_DELAY_PIPE_COUNT)
static __sdram int32_t s_reverb_ram_delay[LCW_REVERB_DELAY_SIZE_TOTAL];
static __sdram int32_t s_reverb_ram_ap1[LCW_REVERB_AP1_SIZE];
static __sdram int32_t s_reverb_ram_ap2[LCW_REVERB_AP2_SIZE];

static LCWDelayBuffer revDelayBuffers[LCW_REVERB_DELAY_PIPE_COUNT];
static LCWDelayBuffer revAp1Buffer = {
    &(s_reverb_ram_ap1[0]),
    LCW_REVERB_AP1_SIZE,
    LCW_REVERB_AP1_SIZE - 1,
    0,
    0
};
static LCWDelayBuffer revAp2Buffer = {
    &(s_reverb_ram_ap2[0]),
    LCW_REVERB_AP2_SIZE,
    LCW_REVERB_AP2_SIZE - 1,
    0,
    0
};

static float s_inputGain;
static float s_mix;
static float s_depth;
static float s_time;

#define DELAY_1 (1151)
#define DELAY_2 (1511)
#define DELAY_3 (1693)
#define DELAY_4 (2039)
// q12
static const uint16_t gainTable[LCW_REVERB_GAIN_TABLE_SIZE][LCW_REVERB_DELAY_PIPE_COUNT] = {
  { 0x000, 0x000, 0x000, 0x000 }, // [ 0]
  { 0xBA7, 0xA8D, 0xA09, 0x91F }, // [ 1] 0.522
  { 0xBCF, 0xABD, 0xA3C, 0x957 }, // [ 2] 0.545
  { 0xBF6, 0xAEC, 0xA6E, 0x98E }, // [ 3] 0.569
  { 0xC1C, 0xB19, 0xA9F, 0x9C5 }, // [ 4] 0.595
  { 0xC41, 0xB46, 0xACF, 0x9F9 }, // [ 5] 0.621
  { 0xC65, 0xB71, 0xAFD, 0xA2D }, // [ 6] 0.648
  { 0xC87, 0xB9B, 0xB2A, 0xA60 }, // [ 7] 0.677
  { 0xCA9, 0xBC4, 0xB56, 0xA91 }, // [ 8] 0.707
  { 0xCC9, 0xBEB, 0xB81, 0xAC1 }, // [ 9] 0.738
  { 0xCE8, 0xC12, 0xBAA, 0xAF0 }, // [10] 0.771
  { 0xD06, 0xC37, 0xBD3, 0xB1D }, // [11] 0.805
  { 0xD24, 0xC5B, 0xBFA, 0xB49 }, // [12] 0.841
  { 0xD40, 0xC7E, 0xC20, 0xB74 }, // [13] 0.878
  { 0xD5B, 0xC9F, 0xC44, 0xB9E }, // [14] 0.917
  { 0xD75, 0xCC0, 0xC68, 0xBC7 }, // [15] 0.958
  { 0xD8F, 0xCE0, 0xC8A, 0xBEE }, // [16] 1.000
  { 0xDA7, 0xCFE, 0xCAC, 0xC15 }, // [17] 1.044
  { 0xDBF, 0xD1C, 0xCCC, 0xC3A }, // [18] 1.091
  { 0xDD6, 0xD38, 0xCEB, 0xC5E }, // [19] 1.139
  { 0xDEB, 0xD54, 0xD09, 0xC80 }, // [20] 1.189
  { 0xE01, 0xD6E, 0xD26, 0xCA2 }, // [21] 1.242
  { 0xE15, 0xD88, 0xD42, 0xCC3 }, // [22] 1.297
  { 0xE28, 0xDA0, 0xD5E, 0xCE2 }, // [23] 1.354
  { 0xE3B, 0xDB8, 0xD78, 0xD00 }, // [24] 1.414
  { 0xE4D, 0xDCF, 0xD91, 0xD1E }, // [25] 1.477
  { 0xE5F, 0xDE5, 0xDA9, 0xD3A }, // [26] 1.542
  { 0xE70, 0xDFB, 0xDC1, 0xD56 }, // [27] 1.610
  { 0xE80, 0xE0F, 0xDD8, 0xD70 }, // [28] 1.682
  { 0xE8F, 0xE23, 0xDED, 0xD8A }, // [29] 1.756
  { 0xE9E, 0xE36, 0xE02, 0xDA2 }, // [30] 1.834
  { 0xEAD, 0xE48, 0xE17, 0xDBA }, // [31] 1.915
  { 0xEBA, 0xE5A, 0xE2A, 0xDD1 }, // [32] 2.000
  { 0xEC8, 0xE6B, 0xE3D, 0xDE7 }, // [33] 2.089
  { 0xED4, 0xE7B, 0xE4F, 0xDFC }, // [34] 2.181
  { 0xEE1, 0xE8B, 0xE60, 0xE11 }, // [35] 2.278
  { 0xEEC, 0xE9A, 0xE71, 0xE25 }, // [36] 2.378
  { 0xEF8, 0xEA9, 0xE81, 0xE38 }, // [37] 2.484
  { 0xF03, 0xEB7, 0xE91, 0xE4A }, // [38] 2.594
  { 0xF0D, 0xEC4, 0xEA0, 0xE5B }, // [39] 2.709
  { 0xF17, 0xED1, 0xEAE, 0xE6C }, // [40] 2.828
  { 0xF21, 0xEDD, 0xEBC, 0xE7D }, // [41] 2.954
  { 0xF2A, 0xEE9, 0xEC9, 0xE8C }, // [42] 3.084
  { 0xF33, 0xEF5, 0xED6, 0xE9B }, // [43] 3.221
  { 0xF3B, 0xF00, 0xEE2, 0xEAA }, // [44] 3.364
  { 0xF43, 0xF0A, 0xEEE, 0xEB8 }, // [45] 3.513
  { 0xF4B, 0xF14, 0xEF9, 0xEC5 }, // [46] 3.668
  { 0xF53, 0xF1E, 0xF04, 0xED2 }, // [47] 3.830
  { 0xF5A, 0xF27, 0xF0E, 0xEDE }, // [48] 4.000
  { 0xF61, 0xF30, 0xF18, 0xEEA }, // [49] 4.177
  { 0xF67, 0xF39, 0xF21, 0xEF6 }, // [50] 4.362
  { 0xF6E, 0xF41, 0xF2B, 0xF00 }, // [51] 4.555
  { 0xF74, 0xF49, 0xF33, 0xF0B }, // [52] 4.757
  { 0xF7A, 0xF51, 0xF3C, 0xF15 }, // [53] 4.967
  { 0xF7F, 0xF58, 0xF44, 0xF1F }, // [54] 5.187
  { 0xF85, 0xF5F, 0xF4C, 0xF28 }, // [55] 5.417
  { 0xF8A, 0xF66, 0xF53, 0xF31 }, // [56] 5.657
  { 0xF8F, 0xF6C, 0xF5A, 0xF3A }, // [57] 5.907
  { 0xF93, 0xF72, 0xF61, 0xF42 }, // [58] 6.169
  { 0xF98, 0xF78, 0xF68, 0xF4A }, // [59] 6.442
  { 0xF9C, 0xF7E, 0xF6E, 0xF51 }, // [60] 6.727
  { 0xFA1, 0xF83, 0xF74, 0xF58 }, // [61] 7.025
  { 0xFA5, 0xF88, 0xF7A, 0xF5F }, // [62] 7.336
  { 0xFA8, 0xF8D, 0xF80, 0xF66 }, // [63] 7.661
  { 0xFAC, 0xF92, 0xF85, 0xF6C }  // [64] 8.000
};

__fast_inline float softlimiter(float c, float x)
{
  float xf = si_fabsf(x);
  if ( xf < c ) {
    return x;
  }
  else {
    return si_copysignf( c + fx_softclipf(c, xf - c), x );
  }
}

void REVFX_INIT(uint32_t platform, uint32_t api)
{
  for (int32_t i=0; i<LCW_REVERB_DELAY_PIPE_COUNT; i++) {
    LCWDelayBuffer *buf = &(revDelayBuffers[i]);
    buf->buffer = &(s_reverb_ram_delay[LCW_REVERB_DELAY_SIZE * i]);
    buf->size = LCW_REVERB_DELAY_SIZE;
    buf->mask = LCW_REVERB_DELAY_SIZE - 1;
    buf->pointer = 0;
    buf->gain = 0;
  }

  revAp1Buffer.gain = LCW_SQ15_16( 0.7 );
  revAp2Buffer.gain = LCW_SQ15_16( 0.7 );

  s_mix = 0.5f;
  s_depth = 0.f;
  s_time = 0.f;
  s_inputGain = 0.f;
}

void REVFX_PROCESS(float *xn, uint32_t frames)
{
  float * __restrict x = xn;
  const float * x_e = x + 2*frames;

  const float dry = 1.f - s_mix;
  const float wet = s_mix;
  const int32_t depth = (int32_t)(s_depth * 0x400);

  int32_t time = (int32_t)((LCW_REVERB_GAIN_TABLE_SIZE - 1) * s_time);
  revDelayBuffers[0].gain = gainTable[time][0];
  revDelayBuffers[1].gain = gainTable[time][1];
  revDelayBuffers[2].gain = gainTable[time][2];
  revDelayBuffers[3].gain = gainTable[time][3];

  LCWDelayBuffer *delay = &(revDelayBuffers[0]);
  LCWDelayBuffer *ap1 = &revAp1Buffer;
  LCWDelayBuffer *ap2 = &revAp2Buffer;

  for (; x != x_e; ) {
    float xL = *x;
    // float xR = *(x + 1);

    int32_t inL = (int32_t)( s_inputGain * xL * (1 << 20) );
    int32_t zDelay[] = {
      LCW_DELAY_BUFFER_LUT((delay+0), DELAY_1),
      LCW_DELAY_BUFFER_LUT((delay+1), DELAY_2),
      LCW_DELAY_BUFFER_LUT((delay+2), DELAY_3),
      LCW_DELAY_BUFFER_LUT((delay+3), DELAY_4)
    };
    int32_t zAp1 = LCW_DELAY_BUFFER_LUT(ap1, AP1_DELAY);
    int32_t zAp2 = LCW_DELAY_BUFFER_LUT(ap2, AP2_DELAY);

    int64_t sumZDelay = 0;
    for (int j=0; j<LCW_REVERB_DELAY_PIPE_COUNT; j++) {
      sumZDelay += zDelay[j];
      delay[j].pointer = LCW_DELAY_BUFFER_DEC(delay+j);
      delay[j].buffer[delay[j].pointer] =
        inL + (int32_t)( ((int64_t)zDelay[j] * delay[j].gain) >> 12 );
    }

    int64_t outMtap = (sumZDelay * depth) >> 12;
    int64_t inAp1 = outMtap + (((int64_t)zAp1 * ap1->gain) >> 16);
    int64_t outAp1 = outMtap - ((inAp1 * ap1->gain) >> 16);
    ap1->pointer = LCW_DELAY_BUFFER_DEC(ap1);
    ap1->buffer[ap1->pointer] = (int32_t)inAp1;

    int64_t inAp2 = outAp1 + (((int64_t)zAp2 * ap2->gain) >> 16);
    int64_t outAp2 = outAp1 - ((inAp2 * ap2->gain) >> 16);
    ap2->pointer = LCW_DELAY_BUFFER_DEC(ap2);
    ap2->buffer[ap2->pointer] = (int32_t)inAp2;

    float outL = outAp2 / (float)(1 << 20);
    float yL = softlimiter( 0.1f, (dry * xL) + (wet * outL) );

    *(x++) = yL;
    *(x++) = yL;

    if ( s_inputGain < 0.99998f ) {
      s_inputGain += ( (1.f - s_inputGain) * 0.0625f );
    }
    else { s_inputGain = 1.f; }
  }
}

void REVFX_RESUME(void)
{
  buf_clr_u32(
    (uint32_t * __restrict__)s_reverb_ram_delay,
    LCW_REVERB_DELAY_SIZE * LCW_REVERB_DELAY_PIPE_COUNT );
  buf_clr_u32(
    (uint32_t * __restrict__)s_reverb_ram_ap1,
    LCW_REVERB_AP1_SIZE );
  buf_clr_u32(
    (uint32_t * __restrict__)s_reverb_ram_ap2,
    LCW_REVERB_AP2_SIZE );
  s_inputGain = 0.f;
}

void REVFX_PARAM(uint8_t index, int32_t value)
{
  const float valf = q31_to_f32(value);
  switch (index) {
  case k_user_revfx_param_time:
    s_time = clip01f(valf);
    break;
  case k_user_revfx_param_depth:
    s_depth = clip01f(valf);
    break;
  case k_user_revfx_param_shift_depth:
    // Rescale to add notch around 0.5f
    s_mix = (valf <= 0.49f) ? 1.02040816326530612244f * valf : (valf >= 0.51f) ? 0.5f + 1.02f * (valf-0.51f) : 0.5f;
    break;
  default:
    break;
  }
}
