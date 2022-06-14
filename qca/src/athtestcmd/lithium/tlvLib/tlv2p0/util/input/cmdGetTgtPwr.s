/*
 * Copyright (c) 2017-2019 Qualcomm Technologies, Inc.
 *
 * All Rights Reserved.
 * Confidential and Proprietary - Qualcomm Technologies, Inc.
 *
 */

# cmd
CMD= getTgtPwr

# cmd parm
PARM_START:
UINT8:phyId:1:u:0
UINT8:userMode:1:u:0
UINT16:freq:1:u:0
UINT32:rateBit:1:u:0
UINT32:nss:1:u:0
UINT8:rate:1:u:0
UINT8:ofdmaPpduType:1:u:0
PARM_END:

# rsp
RSP= getTgtPwrRsp

# rsp parm
PARM_START:
UINT8:phyId:1:u:0
UINT8:tgtPwr:1:u:0
PARM_END:


