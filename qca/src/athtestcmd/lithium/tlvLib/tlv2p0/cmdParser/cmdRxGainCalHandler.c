/* Copyright (c) 2017-2019 Qualcomm Technologies, Inc.
 * All Rights Reserved.
 * Confidential and Proprietary - Qualcomm Technologies, Inc.
 */
// This is an auto-generated file from input\cmdRxGainCalHandler.s
#include "tlv2Inc.h"
#include "cmdRxGainCalHandler.h"

void* initRXGAINCALOpParms(A_UINT8 *pParmsCommon, PARM_OFFSET_TBL *pParmsOffset, PARM_DICT *pParmDict)
{
    int i, j; 	//for initializing array parameter
    CMD_RXGAINCAL_PARMS  *pRXGAINCALParms = (CMD_RXGAINCAL_PARMS *)pParmsCommon;

    if (pParmsCommon == NULL) return (NULL);

    i = j = 0;	//assign a number to avoid warning in case i and j are not used

    // Populate the parm structure with initial values
    pRXGAINCALParms->phyId = pParmDict[PARM_PHYID].v.valU8;
    pRXGAINCALParms->chainIdx = pParmDict[PARM_CHAINIDX].v.valU8;
    pRXGAINCALParms->band = pParmDict[PARM_BAND].v.valU8;
    pRXGAINCALParms->radioId = pParmDict[PARM_RADIOID].v.valU8;
    for (i = 0; i < 6 ; i++)
    {
        pRXGAINCALParms->bssid[i] = pParmDict[PARM_BSSID].v.ptU8[i];
    }
    for (i = 0; i < 6 ; i++)
    {
        pRXGAINCALParms->staAddr[i] = pParmDict[PARM_STAADDR].v.ptU8[i];
    }
    pRXGAINCALParms->rxMode = pParmDict[PARM_RXMODE].v.valU8;
    pRXGAINCALParms->freq = pParmDict[PARM_FREQ].v.valU16;

    // Make up ParmOffsetTbl
    resetParmOffsetFields();
    fillParmOffsetTbl((A_UINT32)PARM_PHYID, (A_UINT32)(((A_UINT32)&(pRXGAINCALParms->phyId)) - (A_UINT32)pRXGAINCALParms), pParmsOffset);
    fillParmOffsetTbl((A_UINT32)PARM_CHAINIDX, (A_UINT32)(((A_UINT32)&(pRXGAINCALParms->chainIdx)) - (A_UINT32)pRXGAINCALParms), pParmsOffset);
    fillParmOffsetTbl((A_UINT32)PARM_BAND, (A_UINT32)(((A_UINT32)&(pRXGAINCALParms->band)) - (A_UINT32)pRXGAINCALParms), pParmsOffset);
    fillParmOffsetTbl((A_UINT32)PARM_RADIOID, (A_UINT32)(((A_UINT32)&(pRXGAINCALParms->radioId)) - (A_UINT32)pRXGAINCALParms), pParmsOffset);
    fillParmOffsetTbl((A_UINT32)PARM_BSSID, (A_UINT32)(((A_UINT32)&(pRXGAINCALParms->bssid)) - (A_UINT32)pRXGAINCALParms), pParmsOffset);
    fillParmOffsetTbl((A_UINT32)PARM_STAADDR, (A_UINT32)(((A_UINT32)&(pRXGAINCALParms->staAddr)) - (A_UINT32)pRXGAINCALParms), pParmsOffset);
    fillParmOffsetTbl((A_UINT32)PARM_RXMODE, (A_UINT32)(((A_UINT32)&(pRXGAINCALParms->rxMode)) - (A_UINT32)pRXGAINCALParms), pParmsOffset);
    fillParmOffsetTbl((A_UINT32)PARM_FREQ, (A_UINT32)(((A_UINT32)&(pRXGAINCALParms->freq)) - (A_UINT32)pRXGAINCALParms), pParmsOffset);
    return((void*) pRXGAINCALParms);
}

static RXGAINCAL_OP_FUNC RXGAINCALOpFunc = NULL;

TLV2_API void registerRXGAINCALHandler(RXGAINCAL_OP_FUNC fp)
{
    RXGAINCALOpFunc = fp;
}

A_BOOL RXGAINCALOp(void *pParms)
{
    CMD_RXGAINCAL_PARMS *pRXGAINCALParms = (CMD_RXGAINCAL_PARMS *)pParms;

#if 0 //for debugging, comment out this line, and uncomment the line below
//#ifdef _DEBUG
    int i; 	//for initializing array parameter
    i = 0;	//assign a number to avoid warning in case i is not used

    A_PRINTF("RXGAINCALOp: phyId %u\n", pRXGAINCALParms->phyId);
    A_PRINTF("RXGAINCALOp: chainIdx %u\n", pRXGAINCALParms->chainIdx);
    A_PRINTF("RXGAINCALOp: band %u\n", pRXGAINCALParms->band);
    A_PRINTF("RXGAINCALOp: radioId %u\n", pRXGAINCALParms->radioId);
    for (i = 0; i < 6 ; i++)
    {
        A_PRINTF("RXGAINCALOp: bssid 0x%x\n", pRXGAINCALParms->bssid[i]);
    }
    for (i = 0; i < 6 ; i++)
    {
        A_PRINTF("RXGAINCALOp: staAddr 0x%x\n", pRXGAINCALParms->staAddr[i]);
    }
    A_PRINTF("RXGAINCALOp: rxMode %u\n", pRXGAINCALParms->rxMode);
    A_PRINTF("RXGAINCALOp: freq %u\n", pRXGAINCALParms->freq);
#endif //_DEBUG

    if (NULL != RXGAINCALOpFunc) {
        (*RXGAINCALOpFunc)(pRXGAINCALParms);
    }
    return(TRUE);
}

void* initRXGAINCALRSPOpParms(A_UINT8 *pParmsCommon, PARM_OFFSET_TBL *pParmsOffset, PARM_DICT *pParmDict)
{
    int i, j; 	//for initializing array parameter
    CMD_RXGAINCALRSP_PARMS  *pRXGAINCALRSPParms = (CMD_RXGAINCALRSP_PARMS *)pParmsCommon;

    if (pParmsCommon == NULL) return (NULL);

    i = j = 0;	//assign a number to avoid warning in case i and j are not used

    // Populate the parm structure with initial values
    pRXGAINCALRSPParms->phyId = pParmDict[PARM_PHYID].v.valU8;
    pRXGAINCALRSPParms->status = pParmDict[PARM_STATUS].v.valU8;
    pRXGAINCALRSPParms->band = pParmDict[PARM_BAND].v.valU8;
    pRXGAINCALRSPParms->refISS = pParmDict[PARM_REFISS].v.valS8;
    pRXGAINCALRSPParms->rate = pParmDict[PARM_RATE].v.valU8;
    pRXGAINCALRSPParms->bandWidth = pParmDict[PARM_BANDWIDTH].v.valU8;
    pRXGAINCALRSPParms->chanIdx = pParmDict[PARM_CHANIDX].v.valU8;
    pRXGAINCALRSPParms->chainIdx = pParmDict[PARM_CHAINIDX].v.valU8;
    pRXGAINCALRSPParms->numPackets = pParmDict[PARM_NUMPACKETS].v.valU16;
    pRXGAINCALRSPParms->freq = pParmDict[PARM_FREQ].v.valU16;
    pRXGAINCALRSPParms->radioId = pParmDict[PARM_RADIOID].v.valU8;

    // Make up ParmOffsetTbl
    resetParmOffsetFields();
    fillParmOffsetTbl((A_UINT32)PARM_PHYID, (A_UINT32)(((A_UINT32)&(pRXGAINCALRSPParms->phyId)) - (A_UINT32)pRXGAINCALRSPParms), pParmsOffset);
    fillParmOffsetTbl((A_UINT32)PARM_STATUS, (A_UINT32)(((A_UINT32)&(pRXGAINCALRSPParms->status)) - (A_UINT32)pRXGAINCALRSPParms), pParmsOffset);
    fillParmOffsetTbl((A_UINT32)PARM_BAND, (A_UINT32)(((A_UINT32)&(pRXGAINCALRSPParms->band)) - (A_UINT32)pRXGAINCALRSPParms), pParmsOffset);
    fillParmOffsetTbl((A_UINT32)PARM_REFISS, (A_UINT32)(((A_UINT32)&(pRXGAINCALRSPParms->refISS)) - (A_UINT32)pRXGAINCALRSPParms), pParmsOffset);
    fillParmOffsetTbl((A_UINT32)PARM_RATE, (A_UINT32)(((A_UINT32)&(pRXGAINCALRSPParms->rate)) - (A_UINT32)pRXGAINCALRSPParms), pParmsOffset);
    fillParmOffsetTbl((A_UINT32)PARM_BANDWIDTH, (A_UINT32)(((A_UINT32)&(pRXGAINCALRSPParms->bandWidth)) - (A_UINT32)pRXGAINCALRSPParms), pParmsOffset);
    fillParmOffsetTbl((A_UINT32)PARM_CHANIDX, (A_UINT32)(((A_UINT32)&(pRXGAINCALRSPParms->chanIdx)) - (A_UINT32)pRXGAINCALRSPParms), pParmsOffset);
    fillParmOffsetTbl((A_UINT32)PARM_CHAINIDX, (A_UINT32)(((A_UINT32)&(pRXGAINCALRSPParms->chainIdx)) - (A_UINT32)pRXGAINCALRSPParms), pParmsOffset);
    fillParmOffsetTbl((A_UINT32)PARM_NUMPACKETS, (A_UINT32)(((A_UINT32)&(pRXGAINCALRSPParms->numPackets)) - (A_UINT32)pRXGAINCALRSPParms), pParmsOffset);
    fillParmOffsetTbl((A_UINT32)PARM_FREQ, (A_UINT32)(((A_UINT32)&(pRXGAINCALRSPParms->freq)) - (A_UINT32)pRXGAINCALRSPParms), pParmsOffset);
    fillParmOffsetTbl((A_UINT32)PARM_RADIOID, (A_UINT32)(((A_UINT32)&(pRXGAINCALRSPParms->radioId)) - (A_UINT32)pRXGAINCALRSPParms), pParmsOffset);
    return((void*) pRXGAINCALRSPParms);
}

static RXGAINCALRSP_OP_FUNC RXGAINCALRSPOpFunc = NULL;

TLV2_API void registerRXGAINCALRSPHandler(RXGAINCALRSP_OP_FUNC fp)
{
    RXGAINCALRSPOpFunc = fp;
}

A_BOOL RXGAINCALRSPOp(void *pParms)
{
    CMD_RXGAINCALRSP_PARMS *pRXGAINCALRSPParms = (CMD_RXGAINCALRSP_PARMS *)pParms;

#if 0 //for debugging, comment out this line, and uncomment the line below
//#ifdef _DEBUG
    int i; 	//for initializing array parameter
    i = 0;	//assign a number to avoid warning in case i is not used

    A_PRINTF("RXGAINCALRSPOp: phyId %u\n", pRXGAINCALRSPParms->phyId);
    A_PRINTF("RXGAINCALRSPOp: status %u\n", pRXGAINCALRSPParms->status);
    A_PRINTF("RXGAINCALRSPOp: band %u\n", pRXGAINCALRSPParms->band);
    A_PRINTF("RXGAINCALRSPOp: refISS %u\n", pRXGAINCALRSPParms->refISS);
    A_PRINTF("RXGAINCALRSPOp: rate %u\n", pRXGAINCALRSPParms->rate);
    A_PRINTF("RXGAINCALRSPOp: bandWidth %u\n", pRXGAINCALRSPParms->bandWidth);
    A_PRINTF("RXGAINCALRSPOp: chanIdx %u\n", pRXGAINCALRSPParms->chanIdx);
    A_PRINTF("RXGAINCALRSPOp: chainIdx %u\n", pRXGAINCALRSPParms->chainIdx);
    A_PRINTF("RXGAINCALRSPOp: numPackets %u\n", pRXGAINCALRSPParms->numPackets);
    A_PRINTF("RXGAINCALRSPOp: freq %u\n", pRXGAINCALRSPParms->freq);
    A_PRINTF("RXGAINCALRSPOp: radioId %u\n", pRXGAINCALRSPParms->radioId);
#endif //_DEBUG

    if (NULL != RXGAINCALRSPOpFunc) {
        (*RXGAINCALRSPOpFunc)(pRXGAINCALRSPParms);
    }
    return(TRUE);
}
