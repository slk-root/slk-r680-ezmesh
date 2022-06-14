#===============================================================================
# Copyright (c) 2018-2020 Qualcomm Technologies, Inc.
# All Rights Reserved.
# Confidential and Proprietary - Qualcomm Technologies, Inc.
#===============================================================================
# cmd
CMD= EepromRead

# cmd parm
PARM_START:
UINT32:eepromCalDataSize:1:u:0
UINT32:offset:1:u:0
UINT32:size:1:u:0
PARM_END:

# rsp
RSP= EepromReadRsp

# rsp parm
PARM_START:
UINT8:status:1:u:0
UINT8:pad3:3:u
UINT32:offset:1:u:0
UINT32:size:1:u:0
DATA4096:data4k:4096:x
PARM_END:

