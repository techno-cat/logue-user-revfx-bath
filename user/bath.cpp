/*
Copyright 2019 Tomoaki Itoh
This software is released under the MIT License, see LICENSE.txt.
//*/

#include "userrevfx.h"
#include "buffer_ops.h"
#include "LCWCommon.h"

#define LCW_DELAY_BUFFER_DEC(p) ( ((p)->pointer - 1) & (p)->mask )
#define LCW_DELAY_BUFFER_LUT(p, i) ( (p)->buffer[((p)->pointer + (i)) & (p)->mask] )

typedef struct {
    int32_t *buffer;
    uint32_t size;
    uint32_t mask;
    int32_t pointer;
    int32_t gain;
} LCWDelayBuffer;

#define LCW_REVERB_GAIN_TABLE_SIZE (64 + 1)

#define LCW_REVERB_COMB_SIZE (1<<14)
#define LCW_REVERB_COMB_MAX (6)
#define LCW_REVERB_COMB_BUFFER_TOTAL (LCW_REVERB_COMB_SIZE * LCW_REVERB_COMB_MAX)

#define LCW_REVERB_AP_SIZE (1<<12)
#define LCW_REVERB_AP_MAX (6)
#define LCW_REVERB_AP_BUFFER_TOTAL (LCW_REVERB_AP_SIZE * LCW_REVERB_AP_MAX)

static __sdram int32_t s_reverb_ram_comb_buffer[LCW_REVERB_COMB_BUFFER_TOTAL];
static __sdram int32_t s_reverb_ram_ap_buffer[LCW_REVERB_AP_BUFFER_TOTAL];

static LCWDelayBuffer revCombBuffers[LCW_REVERB_COMB_MAX];
static LCWDelayBuffer revApBuffers[LCW_REVERB_AP_MAX];

static float s_inputGain;
static float s_mix;
static float s_depth;
static float s_time;

// q12
static const uint16_t gainTable[LCW_REVERB_GAIN_TABLE_SIZE][LCW_REVERB_COMB_MAX] = {
  { 0x000, 0x000, 0x000, 0x000, 0x000 }, // [ 0]
// 6139, 4507, 2609, 2999, 2579, 1559
//  547,  967, 1559
  { 0x300, 0x4AF, 0x7DB, 0x710, 0x7EC, 0xA76 }, // [ 1] 0.528
  { 0x347, 0x4FF, 0x828, 0x760, 0x838, 0xAB2 }, // [ 2] 0.557
  { 0x390, 0x550, 0x873, 0x7AE, 0x883, 0xAED }, // [ 3] 0.588
  { 0x3DB, 0x5A1, 0x8BD, 0x7FC, 0x8CD, 0xB26 }, // [ 4] 0.621
  { 0x428, 0x5F3, 0x906, 0x848, 0x915, 0xB5D }, // [ 5] 0.655
  { 0x476, 0x644, 0x94D, 0x893, 0x95C, 0xB92 }, // [ 6] 0.692
  { 0x4C6, 0x696, 0x992, 0x8DD, 0x9A0, 0xBC5 }, // [ 7] 0.730
  { 0x517, 0x6E6, 0x9D5, 0x924, 0x9E3, 0xBF6 }, // [ 8] 0.771
  { 0x568, 0x736, 0xA16, 0x96A, 0xA24, 0xC25 }, // [ 9] 0.814
  { 0x5B9, 0x786, 0xA56, 0x9AF, 0xA63, 0xC53 }, // [10] 0.859
  { 0x60B, 0x7D4, 0xA94, 0x9F1, 0xAA1, 0xC7F }, // [11] 0.907
  { 0x65C, 0x821, 0xACF, 0xA32, 0xADC, 0xCA8 }, // [12] 0.958
  { 0x6AD, 0x86C, 0xB09, 0xA71, 0xB15, 0xCD1 }, // [13] 1.011
  { 0x6FE, 0x8B6, 0xB41, 0xAAD, 0xB4D, 0xCF7 }, // [14] 1.067
  { 0x74E, 0x8FF, 0xB77, 0xAE8, 0xB82, 0xD1C }, // [15] 1.127
  { 0x79D, 0x946, 0xBAB, 0xB21, 0xBB6, 0xD40 }, // [16] 1.189
  { 0x7EA, 0x98B, 0xBDD, 0xB58, 0xBE8, 0xD62 }, // [17] 1.255
  { 0x837, 0x9CF, 0xC0D, 0xB8D, 0xC17, 0xD82 }, // [18] 1.325
  { 0x882, 0xA10, 0xC3C, 0xBC1, 0xC46, 0xDA1 }, // [19] 1.399
  { 0x8CC, 0xA50, 0xC68, 0xBF2, 0xC72, 0xDBF }, // [20] 1.477
  { 0x914, 0xA8E, 0xC93, 0xC21, 0xC9C, 0xDDB }, // [21] 1.559
  { 0x95B, 0xACA, 0xCBC, 0xC4F, 0xCC5, 0xDF6 }, // [22] 1.646
  { 0x99F, 0xB04, 0xCE4, 0xC7B, 0xCEC, 0xE10 }, // [23] 1.737
  { 0x9E2, 0xB3C, 0xD0A, 0xCA5, 0xD12, 0xE28 }, // [24] 1.834
  { 0xA23, 0xB72, 0xD2E, 0xCCE, 0xD35, 0xE40 }, // [25] 1.936
  { 0xA62, 0xBA6, 0xD51, 0xCF4, 0xD58, 0xE56 }, // [26] 2.044
  { 0xAA0, 0xBD8, 0xD72, 0xD19, 0xD79, 0xE6B }, // [27] 2.158
  { 0xADB, 0xC09, 0xD91, 0xD3D, 0xD98, 0xE80 }, // [28] 2.278
  { 0xB14, 0xC38, 0xDB0, 0xD5F, 0xDB6, 0xE93 }, // [29] 2.404
  { 0xB4C, 0xC64, 0xDCD, 0xD7F, 0xDD3, 0xEA5 }, // [30] 2.538
  { 0xB81, 0xC8F, 0xDE8, 0xD9F, 0xDEE, 0xEB7 }, // [31] 2.679
  { 0xBB5, 0xCB9, 0xE03, 0xDBC, 0xE08, 0xEC8 }, // [32] 2.828
  { 0xBE7, 0xCE0, 0xE1C, 0xDD9, 0xE21, 0xED8 }, // [33] 2.986
  { 0xC17, 0xD06, 0xE34, 0xDF4, 0xE39, 0xEE7 }, // [34] 3.152
  { 0xC45, 0xD2B, 0xE4B, 0xE0E, 0xE50, 0xEF5 }, // [35] 3.327
  { 0xC71, 0xD4D, 0xE61, 0xE26, 0xE65, 0xF03 }, // [36] 3.513
  { 0xC9C, 0xD6F, 0xE76, 0xE3E, 0xE7A, 0xF10 }, // [37] 3.708
  { 0xCC4, 0xD8F, 0xE89, 0xE54, 0xE8D, 0xF1C }, // [38] 3.914
  { 0xCEC, 0xDAD, 0xE9C, 0xE6A, 0xEA0, 0xF28 }, // [39] 4.132
  { 0xD11, 0xDCA, 0xEAE, 0xE7E, 0xEB2, 0xF33 }, // [40] 4.362
  { 0xD35, 0xDE6, 0xEBF, 0xE92, 0xEC3, 0xF3D }, // [41] 4.605
  { 0xD57, 0xE00, 0xED0, 0xEA4, 0xED3, 0xF47 }, // [42] 4.861
  { 0xD78, 0xE1A, 0xEDF, 0xEB6, 0xEE2, 0xF51 }, // [43] 5.131
  { 0xD98, 0xE32, 0xEEE, 0xEC6, 0xEF1, 0xF5A }, // [44] 5.417
  { 0xDB6, 0xE49, 0xEFC, 0xED6, 0xEFF, 0xF62 }, // [45] 5.718
  { 0xDD2, 0xE5F, 0xF09, 0xEE5, 0xF0C, 0xF6B }, // [46] 6.037
  { 0xDEE, 0xE74, 0xF16, 0xEF4, 0xF18, 0xF72 }, // [47] 6.373
  { 0xE08, 0xE88, 0xF22, 0xF01, 0xF24, 0xF7A }, // [48] 6.727
  { 0xE21, 0xE9A, 0xF2D, 0xF0E, 0xF2F, 0xF81 }, // [49] 7.102
  { 0xE39, 0xEAD, 0xF38, 0xF1B, 0xF3A, 0xF87 }, // [50] 7.497
  { 0xE4F, 0xEBE, 0xF42, 0xF27, 0xF44, 0xF8E }, // [51] 7.914
  { 0xE65, 0xECE, 0xF4C, 0xF32, 0xF4E, 0xF93 }, // [52] 8.354
  { 0xE7A, 0xEDE, 0xF55, 0xF3C, 0xF57, 0xF99 }, // [53] 8.819
  { 0xE8D, 0xEEC, 0xF5E, 0xF46, 0xF60, 0xF9E }, // [54] 9.310
  { 0xEA0, 0xEFA, 0xF66, 0xF50, 0xF68, 0xFA4 }, // [55] 9.828
  { 0xEB2, 0xF08, 0xF6E, 0xF59, 0xF70, 0xFA8 }, // [56] 10.375
  { 0xEC3, 0xF14, 0xF76, 0xF62, 0xF78, 0xFAD }, // [57] 10.952
  { 0xED3, 0xF21, 0xF7D, 0xF6A, 0xF7F, 0xFB1 }, // [58] 11.561
  { 0xEE2, 0xF2C, 0xF84, 0xF72, 0xF85, 0xFB5 }, // [59] 12.205
  { 0xEF1, 0xF37, 0xF8A, 0xF79, 0xF8C, 0xFB9 }, // [60] 12.884
  { 0xEFE, 0xF41, 0xF90, 0xF80, 0xF92, 0xFBD }, // [61] 13.601
  { 0xF0C, 0xF4B, 0xF96, 0xF87, 0xF97, 0xFC0 }, // [62] 14.358
  { 0xF18, 0xF54, 0xF9C, 0xF8D, 0xF9D, 0xFC4 }, // [63] 15.157
  { 0xF24, 0xF5D, 0xFA1, 0xF93, 0xFA2, 0xFC7 }  // [64] 16.000
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

#define FIR_TAP (5)
__fast_inline int32_t lut_with_lpf(LCWDelayBuffer *p, int32_t i)
{
#if(1)
  int32_t fir[] = { 21,  236,  505,  236,   21 };
  int64_t sum = 0;
  for (int32_t j=0; j<FIR_TAP; j++) {
    int64_t tmp = LCW_DELAY_BUFFER_LUT(p, i + j - (FIR_TAP/2));
    sum += (tmp * fir[j]);
  }

  return (int32_t)(sum >> 10);
#else
  return LCW_DELAY_BUFFER_LUT(p, i);
#endif
//   int32_t j = i - (FIR_TAP >> 1);
//   int64_t sum = 0;
//   for (int32_t k=0; k<FIR_TAP; k++) {
//     sum += ...
//   }

//   return sum >> foo;
// #define LCW_DELAY_BUFFER_LUT(p, i) ( (p)->buffer[((p)->pointer + (i)) & (p)->mask] )

  
}

void REVFX_INIT(uint32_t platform, uint32_t api)
{
  for (int32_t i=0; i<LCW_REVERB_COMB_MAX; i++) {
    LCWDelayBuffer *buf = &(revCombBuffers[i]);
    buf->buffer = &(s_reverb_ram_comb_buffer[LCW_REVERB_COMB_SIZE * i]);
    buf->size = LCW_REVERB_COMB_SIZE;
    buf->mask = LCW_REVERB_COMB_SIZE - 1;
    buf->pointer = 0;
    buf->gain = LCW_SQ15_16( 0.7 ) >> 4;
  }

  for (int32_t i=0; i<LCW_REVERB_AP_MAX; i++) {
    LCWDelayBuffer *buf = &(revApBuffers[i]);
    buf->buffer = &(s_reverb_ram_ap_buffer[LCW_REVERB_AP_SIZE * i]);
    buf->size = LCW_REVERB_AP_SIZE;
    buf->mask = LCW_REVERB_AP_SIZE - 1;
    buf->pointer = 0;
    buf->gain = LCW_SQ15_16( 0.7 ) >> 4;
  }

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

  for (int32_t i=0; i<3; i++) {
    revCombBuffers[i].gain = gainTable[time][i];
  }
  for (int32_t i=3; i<LCW_REVERB_COMB_MAX; i++) {
    revCombBuffers[i].gain = gainTable[1][i];
  }

  LCWDelayBuffer *comb = &(revCombBuffers[0]);
  LCWDelayBuffer *ap = &revApBuffers[0];  

  const int32_t combDelay[] = { 6139, 4507, 2609, 2999, 2579, 1559 };
  const int32_t preDelay[] = { 547,  967, 1559 };
  const int32_t apDelay[] = {
    337, // = 48000 * 0.007
    //523, // = 48000 * 0.011
    241, // = 48000 * 0.005
    109, // = 48000 * 0.0023
    53  // = 48000 * 0.0017
  };
  
  for (; x != x_e; ) {
    float xL = *x;
    // float xR = *(x + 1);
    int32_t inL = (int32_t)( s_inputGain * xL * (1 << 20) );

    int64_t preOut = 0;
    for (int32_t j=0; j<3; j++) {
      LCWDelayBuffer *p = comb + j;
      int32_t z = lut_with_lpf(p, combDelay[j]);
      //int32_t z = LCW_DELAY_BUFFER_LUT(p, combDelay[j]);
      preOut += LCW_DELAY_BUFFER_LUT(p, preDelay[j]);

      p->pointer = LCW_DELAY_BUFFER_DEC(p);
      p->buffer[p->pointer] =
        (int32_t)(inL + (((int64_t)z * p->gain) >> 12));
    }

    preOut >>= 1;

    int64_t combSum = 0;
    for (int32_t j=3, k=0; j<LCW_REVERB_COMB_MAX; j++, k++) {
      LCWDelayBuffer *p = comb + j;
      //int32_t z = lut_with_lpf(p, combDelay[j]);
      int32_t z = LCW_DELAY_BUFFER_LUT(p, combDelay[j]);
      combSum += z;
      
      p->pointer = LCW_DELAY_BUFFER_DEC(p);
      p->buffer[p->pointer] =
        (int32_t)(preOut + (((int64_t)z * p->gain) >> 12));
    }

    /* comb[] -> AP */
    int64_t out = (combSum * depth) >> 12;
    for (int32_t j=0; j<4; j++) {
      LCWDelayBuffer *p = ap + j;

      int32_t z = LCW_DELAY_BUFFER_LUT(p, apDelay[j]);
      int64_t in = out + (((int64_t)z * p->gain) >> 12);
      out = (int64_t)z - ((in * p->gain) >> 12);

      p->pointer = LCW_DELAY_BUFFER_DEC(p);
      p->buffer[p->pointer] = in;
    }
 
    float outL = out / (float)(1 << 20);
    //float outL = (inComb2[0] >> 2) / (float)(1 << 20);
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
    (uint32_t * __restrict__)s_reverb_ram_comb_buffer,
    LCW_REVERB_COMB_BUFFER_TOTAL );
  buf_clr_u32(
    (uint32_t * __restrict__)s_reverb_ram_ap_buffer,
    LCW_REVERB_AP_BUFFER_TOTAL );
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
