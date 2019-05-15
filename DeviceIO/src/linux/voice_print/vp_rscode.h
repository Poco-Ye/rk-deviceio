#ifndef _VP_RSCODE_H_
#define _VP_RSCODE_H_

#define RS_SYMSIZE			5
#define RS_GFPOLY			0x25
#define RS_FCR				1
#define RS_PRIM				1
#define RS_NROOTS			8
#define RS_DATA_LEN			10
#define RS_TOTAL_LEN		(RS_DATA_LEN + RS_NROOTS)
#define RS_PAD				((1<<RS_SYMSIZE) - 1 - RS_TOTAL_LEN)

/*
 * General purpose RS codec, 8-bit symbols.
 */

typedef struct _RS RS;
extern RS *initRsChar(int symsize, int gfpoly, int fcr, int prim, int nroots, int pad);
extern RS *initRs(int symsize, int gfpoly, int fcr, int prim, int nroots, int pad);
extern void encodeRsChar(RS *rs, const unsigned char *data, unsigned char *parity);
extern int  decodeRsChar(RS *rs, unsigned char *data, int *eras_pos, int no_eras);
extern void freeRsChar(RS *rs);
extern void freeRsCache(void);

#endif /* __RSCODE_H__ */
