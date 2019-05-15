#ifndef _VP_COMMON_H_
#define _VP_COMMON_H_

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

//#define MEMORYLEAK_DIAGNOSE
#ifndef LP_64
typedef long long int64_t;
#else
#include <stdint.h>
#endif

#define SYMB_LENGTH_FACTOR    1
#define SYNC_TONE_SYMB_LENGTH 2
#define LOW_FREQ_SIMPLE_NOISE_SHAPING 0
#define QCONST32(x,bits) ((int)(.5+(x)*(((int)1)<<(bits))))

#define PI QCONST32(3.1415926, 15)
#define HAM_COEF1 QCONST32(0.54, 15)
#define HAM_COEF2 QCONST32(0.46, 15)

#define BLACK_COEF1 QCONST32(0.42,15)
#define BLACK_COEF2 QCONST32(0.5,15)
#define BLACK_COEF3 QCONST32(0.08,15)

#define HAN_COEF1 QCONST32(0.5,15)
#define HAN_COEF2 QCONST32(0.5,15)

#define SINCOEF1 QCONST32(1,15)
#define SINCOEF3 QCONST32(-1./6,15)
#define SINCOEF5 QCONST32(1./120,15)

#define VPMUL(a, b) (int)(((((int64_t)(a))*((int64_t)(b))) + (1<<14) )>> 15)
#define VPMULT(a, b) (((int64_t)(a))*((int64_t)(b)))
#define VPMULT16(a, b) (((int)(a))*((int)(b)))
#define VPSHR(a, b)	((a)>>(b))

#define VPSAT(a) (((a)>32767)? 32767 :(((a)<-32768)?-32768:(a)))
#define VPABS(a) (((a) > 0) ? a : (-(a)))

#define SYMBITS 4

#define FREQNUM (1<<SYMBITS)

int vp_sin(int x);

#ifndef MEMORYLEAK_DIAGNOSE
#define vp_alloc(size) calloc(1, size)
#define vp_free(ptr) free(ptr)

#else
void* vp_alloc(size_t size);
void vp_free(void* ptr);
void vp_memdiagnose();
void vp_memdiagnoseinit();
#endif
#define vp_memset(ptr, v, size) memset(ptr, v, size)
#define vp_memcpy(dst, src, size) memcpy(dst, src, size)

/* frequency mapping (HZ), log scale linear */
typedef enum
{
	FILTER_TYPE_LOWPASS,
	FILTER_TYPE_HIGHPASS,
	FILTER_TYPE_BANDPASS,
	FILTER_TYPE_BANDSTOP
} filter_type_t;

extern int freq_point[3][FREQNUM];
extern int freq_delta[3];
extern int freq_lag[3];
extern int freq_lag_top[3];
extern int freq_cutoff[3];
extern filter_type_t filter_type[3];

#endif
