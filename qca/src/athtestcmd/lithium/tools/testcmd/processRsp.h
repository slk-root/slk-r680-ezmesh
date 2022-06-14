/*
* Copyright (c) 2017,2020 Qualcomm Technologies, Inc.
*
* All Rights Reserved.
* Confidential and Proprietary - Qualcomm Technologies, Inc.
*/

#ifndef _PROCESS_RSP_H__
#define _PROCESS_RSP_H__

#define A_RATE_NUM      MASK_RATE_MAX //183 
#define G_RATE_NUM      MASK_RATE_MAX //183
#define TCMD_MAX_RATES_11AC_3x3 153

#define RATE_STR_LEN             30

typedef const char RATE_STR[RATE_STR_LEN];

#ifdef USE_NETLINK
extern void cmdReplyFunc_v2(void *buf, int len);
#else
extern void cmdReplyFunc_v2(void *buf);
#endif
extern void cmdReplyFunc(void *buf);
extern void handleTPCCALRsp(void *pParms);
extern void handleTPCCALDATA(void *pParms);
extern void handleREGREADRSP(void *pParms);
extern void handleREGWRITERSP(void *pParms);
extern void handleBASICRSP (void *parms);
extern void handleTXSTATUSRSP (void *parms);
extern void handleRXSTATUSRSP (void *parms);
extern void handleRXRSP (void *parms);
extern void handleMEMREADRSP (void *parms);
extern void handleMEMWRITERSP (void *parms);
extern void handleTestDataRsp (void *parms);
extern void handleMoreSegment (void *parms);
extern void handleMoreSegmentRsp (void *parms);
extern void handleBdGetSizeRsp (void *parms);
extern void handleBdReadRsp (void *parms);
extern void handleLMTxInitRsp(void *parms);
extern void handleLMChannelListRsp (void *parms);
extern void handleLMQueryRsp (void *parms);
extern void handleLMGoRsp(void *pParms);
extern void handleLMRxInitRsp(void *pParms);
extern void handleLMHWCALRsp(void *pParms);
extern void handleEepromGetSizeRsp (void *parms);
extern void handleEepromReadRsp(void *parms);

#endif //_PROCESS_RSP_H__
