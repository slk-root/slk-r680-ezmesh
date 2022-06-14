/* Copyright (c) 2017-2019 Qualcomm Technologies, Inc.
 * All Rights Reserved.
 * Confidential and Proprietary - Qualcomm Technologies, Inc.
 */
// This is an auto-generated file from input\cmdSetPhyRfMode.s
#include "tlv2Inc.h"
#include "cmdSetPhyRfMode.h"

void* initSETPHYRFMODEOpParms(A_UINT8 *pParmsCommon, PARM_OFFSET_TBL *pParmsOffset, PARM_DICT *pParmDict)
{
    int i; 	//for initializing array parameter
    CMD_SETPHYRFMODE_PARMS  *pSETPHYRFMODEParms = (CMD_SETPHYRFMODE_PARMS *)pParmsCommon;

    if (pParmsCommon == NULL) return (NULL);

    i = 0;	//assign a number to avoid warning in case i is not used

    // Populate the parm structure with initial values
    pSETPHYRFMODEParms->phyId = pParmDict[PARM_PHYID].v.valU8;
    pSETPHYRFMODEParms->phyRfMode = pParmDict[PARM_PHYRFMODE].v.valU8;

    // Make up ParmOffsetTbl
    resetParmOffsetFields();
    fillParmOffsetTbl((A_UINT32)PARM_PHYID, (A_UINT32)(((A_UINT32)&(pSETPHYRFMODEParms->phyId)) - (A_UINT32)pSETPHYRFMODEParms), pParmsOffset);
    fillParmOffsetTbl((A_UINT32)PARM_PHYRFMODE, (A_UINT32)(((A_UINT32)&(pSETPHYRFMODEParms->phyRfMode)) - (A_UINT32)pSETPHYRFMODEParms), pParmsOffset);
    return((void*) pSETPHYRFMODEParms);
}

static SETPHYRFMODE_OP_FUNC SETPHYRFMODEOpFunc = NULL;

TLV2_API void registerSETPHYRFMODEHandler(SETPHYRFMODE_OP_FUNC fp)
{
    SETPHYRFMODEOpFunc = fp;
}

A_BOOL SETPHYRFMODEOp(void *pParms)
{
    CMD_SETPHYRFMODE_PARMS *pSETPHYRFMODEParms = (CMD_SETPHYRFMODE_PARMS *)pParms;

#if 0 //for debugging, comment out this line, and uncomment the line below
//#ifdef _DEBUG
    int i; 	//for initializing array parameter
    i = 0;	//assign a number to avoid warning in case i is not used

    A_PRINTF("SETPHYRFMODEOp: phyRfMode %u\n", pSETPHYRFMODEParms->phyRfMode);
#endif //_DEBUG

    if (NULL != SETPHYRFMODEOpFunc) {
        (*SETPHYRFMODEOpFunc)(pSETPHYRFMODEParms);
    }
    return(TRUE);
}
