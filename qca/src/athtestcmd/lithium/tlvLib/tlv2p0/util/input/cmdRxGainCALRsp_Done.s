#===============================================================================
# Copyright (c) 2017-2019 Qualcomm Technologies, Inc.
# All Rights Reserved.
# Confidential and Proprietary - Qualcomm Technologies, Inc.
#===============================================================================

# cmd
CMD= rxgaincal_sigl_done
# cmd parm
PARM_START:
UINT8:phyId:1:u:0
UINT8:status:1:u:0
UINT8:band:1:u:0
UINT8:radioId:1:u:0
PARM_END:

# rsp
RSP= rxgaincalrsp_done
# rsp param
PARM_START:
UINT8:phyId:1:u:0
UINT8:status:1:u:0
UINT8:band:1:u:0
UINT8:radioId:1:u:0
PARM_END:
