#include "vp_common.h"

int freq_point[3][FREQNUM]=
{
	{2104, 2213, 2328, 2449, 2577, 2711, 2852, 3000, 3156, 3320, 3493, 3674, 3865, 4066, 4278, 4500},
	{8662, 8827, 8996, 9167, 9342, 9520, 9702, 9887, 10075, 10268, 10463, 10663, 10866, 11074, 11285, 11500,},
    {16673, 16848, 17025, 17204, 17384, 17567, 17751, 17937, 18126, 18316, 18508, 18702, 18899, 19097, 19297, 19500}
};

int freq_delta[3] ={109, 165, 175};
int freq_lag[3] = {4700, 7734, 15336};
int freq_lag_top[3] = {5500, 7734, 15336};
int freq_cutoff[3] = {8000,7000,14000};

filter_type_t filter_type[3] = {FILTER_TYPE_LOWPASS, FILTER_TYPE_HIGHPASS, FILTER_TYPE_HIGHPASS};

/* 
	sinx = x - 1/6*x^3 + 1/120*x^5;
	input Q15, output Q15
*/
int vp_sin(int x)
{
	int x1, x2, x3, x5, s= 0;
	/* we first make sure that x in between [0, 2*pi) */
	x1 = x/(2*PI);
	x1 = x - (2*PI*x1);
	if(x1 < 0)
	{
		x1 += 2*PI;
	}
	if(x1 > PI)
	{
		x1 -= 2*PI;
	}
	if( x1 > PI/2)
	{
		x1 -= PI;
		s = 1;
	}else if (x1 < -PI/2)
	{
		x1 += PI;
		s = 1;
	}

	x2 = VPMUL(x1, x1);
	x3 = VPMUL(x1, x2);
	x5 = VPMUL(x2, x3);
	
	x2 =  (x1 + VPMUL(SINCOEF3, x3) + VPMUL(SINCOEF5, x5));
	if(s)
	{
		x2 = - x2;
	}
	x2 = VPSAT(x2);
	return x2;
}


#ifdef  MEMORYLEAK_DIAGNOSE
#define VP_HEAP_SIZE (8*1024*1024)
#define VP_MAX_ALLOC_ITEM (2*2048)
unsigned char memoryblock[VP_HEAP_SIZE];
unsigned int memorytable[VP_MAX_ALLOC_ITEM][2];
unsigned int size_cur = 0;
unsigned int idx_table = 0;
unsigned int size_occupy = 0;
unsigned int max_size_occupy = 0;
void* vp_alloc(size_t size)
{
	void* ptr =  (void*)(memoryblock+size_cur);
	memset(ptr, 0, size);
	memorytable[idx_table][1] = (unsigned int)ptr;
	memorytable[idx_table][2] = size;
	
	if(size_cur + size > VP_HEAP_SIZE)
	{
		printf("overflow: %d is larger than heapsize: %d!\n",size_cur, VP_HEAP_SIZE);
		return NULL;
	}

	size_cur += size;
	size_occupy += size;
	max_size_occupy = (max_size_occupy < size_occupy) ? size_occupy : max_size_occupy;
	idx_table++;
	return ptr;
}

void vp_free(void* ptr)
{
	unsigned int i;
	for(i = 0; i < idx_table; i++)
	{
		if(memorytable[i][1] == (unsigned int) ptr)
		{
			if(memorytable[i][2] == 0)
			{
				printf("illegal free of %d th alloc, addr: %#X\n",i, memorytable[i][1]);
				return ;
			}
			size_occupy -= memorytable[i][2];
			max_size_occupy = (max_size_occupy < size_occupy) ? size_occupy : max_size_occupy;
			memorytable[i][2] = 0;
			return;
		}
	}
	printf("illegal free %#X\n", (unsigned int)ptr);
}

void vp_memdiagnose()
{
	unsigned int i;
	printf("total allocate %d times, total allocated size %d Bytes, max occupy size %d Bytes !\n", idx_table, size_cur, max_size_occupy);
	for(i = 0; i < idx_table; i++)
	{
		if(memorytable[i][2] != 0)
		{
			printf("memory leak of %d th alloc, addr: %#X, size: %d\n", i, memorytable[i][1],memorytable[i][2]);
		}
	}
	
}

void vp_memdiagnoseinit()
{
	memset(memorytable, 0, sizeof(unsigned int)*VP_MAX_ALLOC_ITEM*2);
	size_cur = 0;
	idx_table = 0;
}

#endif
