/*
 * Copyright (c) 2013-2017,2020-2021 Qualcomm Technologies, Inc.
 *
 * All Rights Reserved.
 * Confidential and Proprietary - Qualcomm Technologies, Inc.
 * 2013-2017 Qualcomm Atheros, Inc.
 * All Rights Reserved.
 * Qualcomm Atheros Confidential and Proprietary.
 */
/*
 * Copyright (c) 2006 Atheros communications Inc.
 * All rights reserved.
 *
 * $ATH_LICENSE_HOSTSDK0_C$
 *
 */

#include <sys/types.h>
#ifdef LINUX
#include <sys/file.h>
#include <sys/socket.h>
#ifndef USE_NETLINK
#include <sys/ioctl.h>
#include <linux/if.h>
#include <linux/wireless.h>
#endif
#include <linux/types.h>
#include <unistd.h>
#include <err.h>
#else //WIN32
#include <stdarg.h>
#include <wtypes.h>
#endif //LiNUX

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef LINUX
#include <athdefs.h>
#include <athtypes_linux.h>
#else //WIN32
#include "wlantype.h"
#endif //LINUX
/*
 * These frequency values are as per channel tags and regulatory domain
 * info. Please update them as database is updated.
 */
#define A_FREQ_MIN              4920
#define A_FREQ_MAX              5925

#define A_CHAN0_FREQ            5000
#define A_CHAN_MAX              ((A_FREQ_MAX - A_CHAN0_FREQ)/5)

#define BG_FREQ_MIN             2412
#define BG_FREQ_MAX             2484

#define BG_CHAN0_FREQ           2407
#define BG_CHAN_MIN             ((BG_FREQ_MIN - BG_CHAN0_FREQ)/5)
#define BG_CHAN_MAX             14      /* corresponding to 2484 MHz */

#define A_20MHZ_BAND_FREQ_MAX   5000
#include <testcmd.h>
#include "athtestcmd.h"
#include <os_if.h>

#include "cmdOpcodes.h"
#include "tcmdHostInternal.h"
#include "art_utf_common.h"
#include "cmdStream.h"
#include "dkCmdIds.h"
#include "cmdRspParmsInternal.h"
#include "cmdRspParmsDict.h"
#include "parseBinCmdStream.h"
#include "cmdTPCCALHandler.h"
#include "cmdTPCCALPWRHandler.h"
#include "cmdRxGainCalHandler.h"
#include "cmdXtalCalHandler.h"
#include "tlvCmdEncoder.h"
#include "tlv2Api.h"
#include "tlv2SysApi.h"
#include "processRsp.h"

#include "cmdLMTxHandler.h"
#include "cmdLMChannelListAndRspHandler.h"
#include "cmdLMTxInitHandler.h"
#include "cmdLMGoHandler.h"
#include "cmdLMQueryHandler.h"
#include "cmdLMRxInitHandler.h"

#ifdef EEPROM_HEADER
#include "qc900b_eeprom.h"
#endif //EEPROM_HEADER

#ifdef ATHTESTCMD_LIB
#include <setjmp.h>
extern void testcmd_error(int code, const char *fmt, ...);
#define A_ERR testcmd_error
#elif !defined(USE_NETLINK)
#define A_ERR err
#endif //ATHTESTCMD_LIB
//A_UINT16 lm_freq[] = {5180, 5190, 5210, 5775, 5795, 5805};

#define USE_TLV2

A_UINT16 lm_freq[] = {5180, 5180, 5180, 5180, 5220, 5180};
A_UINT16 lm_freq2[] = {0, 0, 0, 0, 0, 0};
A_UINT8 lm_phyId[] = {0, 0, 0, 0, 0, 0};
A_UINT8 lm_chainMask[] = {1, 2, 4, 8, 15, 15};
//A_UINT8 lm_chainMask[] = {1, 2, 4, 8 , 15, 1};

A_UINT8 lm_cbState[] = {1, 1, 1, 6, 8, 13};//6:vht40plus, 8: vht80_0, 13: vht160
//A_UINT8 lm_cbState[] = {0, 1, 2, 3, 0, 0};
A_UINT8 lm_txPower[] = {22, 22, 22 , 22, 22, 22};
//A_UINT16 lm_rateIdx[] = {4, 6, 8 , 10, 11, 12};
A_UINT16 lm_rateBitIdx[] = {8, 9, 10 , 203, 126, 224};

A_BOOL is4x4Rate = FALSE;

typedef struct
{
	A_UINT8 sec_id;
	A_UINT8 length;
	A_UINT8 mac_addr[6];
} __ATTRIB_PACK tSectionMac;

typedef struct
{
	A_UINT8 appl_id;
	A_UINT8 version;
	tSectionMac sec_mac;
	A_UINT8 crc;
} __ATTRIB_PACK tOptStream;

unsigned char computeCrc8(unsigned char *buf, unsigned char len)
{
    unsigned char sum = 0, i;

    for (i = 0; i < len; i++)
    {
        sum ^= *buf++;
    }
    sum = ~sum;
    return(sum);
}

const char *progname;
const char commands[] =
"commands:\n\
--tx <frame/tx99/tx100/sine/off> \n\
      --phyId <0/1> \n\
      --txfreq <Tx channel or freq(default 2412)> \n\
      --txfreq2 <Tx channel or freq(default 2412)> \n\
      --txrate < 0 - 1L / 1 - 2L / 2 - 5L / 3 - 11L / 4 - 2S / 5 - 5S / 6 - 11S / 10 - 6Mbps / 11 - 9 Mbps / 12 - 12 Mbps / 13 - 18 Mbps / 14 - 24 Mbps / 15 - 36 Mbps / 16 - 48 Mbps / 17 - 54 Mbps / 20 - MCS0 / 21 - MCS1 / 22 - MCS2 / 23 - MCS3 / 24 - MCS4 / 25 -MCS5 / 26 - MCS6 / 27 - MCS7 / 28 - MCS8 / 29 - MCS9 / 30 - MCS / 31 - MCS11 / 32 - MCS12 / 33 - MCS13 / -1 - CW> \n\
      --rateBw < -1 - CW / 0 - CCK / 1 - LegacyOFDM / 2 - HT20 / 3- HT40 / 4 - VHT20 / 5 - VHT40 / 6 - VHT80 / 7 - VHT80P80 / 8 - HE20 / 9 - HE40 / 10 - HE80 / 11 - HE80P80 / 12 - OFDMA_HE20 / 13 - OFDMA_HE40 / 14 - OFDMA_HE80 / 15 - OFDMA_HE80P80 / 16 - HE160 / 17 - OFDMA_HE160 / 18 - HE165 / 19 - OFDMA_HE165 / 20 - VHT160 / 21 - VHT165 / -1 - CW> \n\
      --phy165Mode <0/1/2> \n\
      --txpwr <0-30dBm (tx99/txframe: 0-60 in 0.5 dBm resolution, i.e 1=0.5dbm, 2=1dbm.....; sine: 0-60, PCDAC vaule)>\n\
      --tgtpwr <target power>\n\
      --pcdac <power control DAC>\n\
      --gainIdx <tx gain table index>\n\
      --dacGain <DAC gain>\n\
      --forcedGain <power control DAC>\n\
      --txantenna <1/2/0 (auto)>\n\
      --txpktsz <pkt size, [32-1500](default 1500)>\n\
      --txpattern <tx data pattern, 0: all zeros; 1: all ones; 2: repeating 10; 3: PN7; 4: PN9; 5: PN15\n\
      --txchain (1:on chain 0, 2:on chain 1, 4: on chain2, 8: on chain3, 15 on all 4 chains  )\n\
      --tpcm <forcedTgtPwr/forcedGainIdx/forcedGain/txpwr/tgtpwr/forcedGlutIdx> : Set transmit power control mode, by default tpcm is set to txpwr \n\
      --ani (Enable ANI. The ANI is disabled if this option is not specified)\n\
      --gI <Guard Interval. 0 - GI_0 / 1 - LTF_Mode0_GI_400 / 2 - LTF_Mode0_GI_800 / 3 - LTF_Mode0_GI_1600 / 4 - LTF_Mode0_GI_3200 / 17 - LTF_Mode1_GI_400 / 18 - LTF_Mode1_GI_800 / 19 - LTF_Mode1_GI_1600 / 20 - LTF_Mode1_GI_3200 / 33 - LTF_Mode2_GI_400 / 34 - LTF_Mode2_GI_800 / 35 - LTF_Mode2_GI_1600 / 36 - LTF_Mode2_GI_3200 / 49 - LTF_Mode4_GI_400 / 50 - LTF_Mode4_GI_800 / 51 - LTF_Mode4_GI_1600 / 52 - LTF_Mode4_GI_3200>\n\
      --paprd (Enable PAPRD. The PAPRD is disabled if this option is not specified)\n\
      --stbc (Enable STBC. STBC is disabled if this option is not specified)\n\
      --ldpc (Enable LDPC. LDPC is disabled if this option is not specified)\n\
      --scrambleroff (Disable scrambler. The scrambler is enabled by default)\n\
      --aifsn <AIFS slots num,[0-252](Used only under '--tx frame' mode)>\n\
      --shortguard (use short guard)\n\
      --mode <cbstate->legacy mapping - bw20->ht20/vht20 / bw40_low->ht40minus/vht40minus / bw40_high->ht40plus/vht40plus / bw80_x->vht80_x:x=0:3 \n\
                                        bw80p80->vht80p80 / bw160->vht160 / bw165  / bw80p80_x->vht80p80_x / bw160_x->vht160_x / bw165_x:x=0:7\n\
	* bw20 ==> primary 20\n\
	* bw40_low ==> primary low\n\
	* bw40_high ==> primary high\n\
      >\n\
      --bw <full/half/quarter>\n\
      --nss <1/2/3/4/5/6/7/8>\n\
      --setlongpreamble <1/0>\n\
      --numpackets <number of packets to send 0-65535>\n\
	  --setup <setup tx parameters without transmitting>\n\
	  --go <start transmitting>\n\
--tx sine --txfreq <Tx channel or freq(default 2412)>\n\
      --agg <number of aggregate packets>\n\
      --bssid\n\
      --srcmac <source MAC address>\n\
      --dstmac <destination MAC address>\n\
--tx sine --txfreq <Tx channel or freq(default 2412)>\n\
--rx <promis/filter(default MacAddress: 01:00:00:C0:FF:EE)/report> \n\
      --phyId <0/1> \n\
      --rxfreq <Rx channel or freq(default 2412)> \n\
      --rxfreq2 <Rx channel or freq(default 2412)> \n\
      --rxantenna <1/2/0 (auto)> \n\
      --rxchain <1:chain 0, 2:chain 1, 3:both>\n\
      --mode <cbstate->legacy mapping - bw20->ht20/vht20 / bw40_low->ht40minus/vht40minus / bw40_high->ht40plus/vht40plus / bw80_x->vht80_x:x=0:3 \n\
                                        bw80p80->vht80p80 / bw160->vht160 / bw165  / bw80p80_x->vht80p80_x / bw160_x->vht160_x / bw165_x:x=0:7\n\
	* bw20 ==> primary 20\n\
	* bw40_low ==> primary low\n\
	* bw40_high ==> primary high\n\
      >\n\
	  --rxMac <mac addr like 00:03:7f:be:ef:11>\n\
      --ack <Enable/Disable ACK, 0:disable; 1:enable, by default ACK is enabled>\n\
      --bc <0:receive unicast frame (default), 1:receive broadcast frame>\n\
      --lpl <0:LPL off(default), 1:reduced receive, 2:reduced search>\n\
	  --setup <setup rx parameters without receiving>\n\
	  --go <start receiving>\n\
--pm <wakeup/sleep/deepsleep>\n\
--setmac <mac addr like 00:03:7f:be:ef:11>\n\
--setbssid <mac addr like 00:03:7f:be:ef:11>\n\
--SetAntSwitchTable <table1 in hex value> <table2 in hex value>  (Set table1=0 and table2=0 will restore the default AntSwitchTable)\n\
--efusedump --start <start address> --end <end address>\n\
--efusewrite <start_offsetxx lengthxx contentsxx xx> (all in hex)\n\
--otpwrite filename or sectionIdxx lengthxx dataxx(xx xx)  (could be one or multiple data in quotation marks)\n\
--otpdump\n\
--otpreset xx: xx=1-> OTPSTREAM_READ, xx=2->OTPSTREAM_WRITE\n\
--btmode <put DUT into BT mode>\n\
--eepromdump \n\
--eepromwrite --bdfile fakeBoardData.bin\n\
--eepromerase \n\
--eepromcheck --totalsize xx --offset xx --blocksize xx\n\
--hwcal <fullresetcal/reset1/reset2/reset3/hwreset/initbl/fullcal/rxdco/rxfilt/rxiq/txiq/txcl/pkdet/noisefloor/dpd/lna>\n\
    --loop <loop count>\n\
	  --chmask <1:on chain 0, 2:on chain 1, 4: on chain2, 8: on chain3, 15 on all 4 chains>\n\
	  --loopback <0 or 1: loopBack parameter for rxdco cal>\n\
    --savecal <0 or 1: save the calibration. Valid only when rxdco/rxfilt/rxiq/txiq/txcl/pkdet is specified>\n\
    --nfcal <0 or 1: do noise floor cal after the calibration. Valid only when rxdco/rxfilt/rxiq/txiq/txcl/pkdet is specified>\n\
--rr --addr xx\n\
--rw --addr xx --value xx\n\
--getnvmac \n\
--setnvmac <mac addr like 00:03:7f:be:ef:11>\n\
--nvbdcommit --nvtype xx --nvsegment xx  --nvcompressed xx\n\
--getcalbdata --totalsize xx --offset xx --blocksize xx\n\
--getbdatasize \n\
--gettxpwr txfreq mode bflag txchain \n\
--calLm \n\
--calPwr \n\
--mr --addr xx --numbytes xx\n\
--mw --addr xx --numbytes xx --value\n\
--dpdtimingidx\n\
--dpdtrainingquality\n\
--dpdatten\n\
--dpdagc2power\n\
--lmtxinit txpacketNum(0 or 1000)\n\
--lmchannellist lm_flag (1) itemNum(no more than 6 for testing)\n\
--lmquery cmdId (0 or 1)\n\
--lmgo cmdId (0)\n\
--lmrxinit promis\n\
--norssiavg < disable rssi averaging in Rx and report last packet rssi >\n\
 ";

void setPowerPerRateIndex(A_BOOL is11ACRate,A_UINT32 value);
static A_BOOL fetchTxCALPwr(A_UINT32 *numMeasuredPwr, A_INT16 *measuredPwr);
A_UINT8 eepromWrite(A_UINT32 offset, A_UINT16 blksize, A_INT8 *data, A_UINT8 enablePrint);
A_UINT8 eepromRead(A_UINT32 bdsize,A_UINT32 offset,A_UINT16 blksize);
A_UINT8 checkEepromSize();
//static void prtCmdStream(A_UINT8 *stream, A_UINT32 streamLen);

#define INVALID_FREQ    0
#define AR6002_REV6     1/* temporary define here */

#define MAX_FRAME_LENGTH         1500
#define TX_FRAME_DURATION        340  // the following 4 values are in micro seconds
#define PREAMBLE_TIME_OFDM       20
#define PREAMBLE_TIME_CCK_LONG   192
#define PREAMBLE_TIME_CCK_SHORT  96
#define SLOT_TIME                9
#define SLOT_TIME_LONG           20
#define MAX_NUM_SLOTS            255

#define _MAX_LENGTH_2_READ       1024

extern RATE_STR  bgRateStrTbl[G_RATE_NUM];

const float ActualDataRate[] = {
/* CCK  */            1.0,  2.0,  5.5,  11.0,                              //0 - 3
/* OFDM */            6.0,  9.0,  12.0, 18.0, 24.0, 36.0, 48.0, 54.0,      // 4 - 11
/* MCS 20 1S */       6.5,  13.0, 19.5, 26.0, 39.0, 52.0, 58.5, 65.0,      // 12 - 19
#ifdef AR6002_REV6
/* MCS 20 2S */       13.0, 26.0, 39.0, 52.0, 78.0, 104.0,117.0,130.0,     // 20 - 27
#endif
/* MCS 40 1S */       13.5, 27.0, 40.5, 54.0, 81.0, 108.0,121.5,135.0,     // 28 - 35
#ifdef AR6002_REV6
/* MCS 40 2S */       27.0, 54.0, 81.0, 108.0, 162.0, 216.0, 243.0, 270.0 ,// 36 - 43
#endif
/* CCK       */       2.0,  5.5,  11,0                                     // 44 - 45
};

extern A_BOOL replyReceived;

static A_UINT32 freqValid(A_UINT32 val);
static A_UINT16 wmic_ieee2freq(A_UINT32 chan);
//static void prtRateTbl(A_UINT32 freq);
//static A_UINT32 rateValid(A_UINT32 val, A_UINT32 freq);
static A_UINT32 antValid(A_UINT32 val);
static A_STATUS ath_ether_aton(const char *orig, A_UINT8 *eth);
static A_UINT32 pktSzValid(A_UINT32 val);
static A_INT32 computeNumSlots(A_UINT32 dutyCycle, A_UINT32 dataRateIndex, float txFrameDurationPercent);
static A_BOOL readBinFile(char *fileName, void *buf, A_UINT32 *length, A_UINT32 length2Read);
static A_BOOL writeBinFile(char *fileName, void *buf, A_UINT32 *length, A_UINT32 length2Read);

#ifdef WIN32
#define IFNAMSIZ        16
#define MBUFFER 2024

void err(int eval, const char *format, ...)
{
    va_list ap;
    char buffer[MBUFFER];

    va_start(ap, format);
	_vsnprintf(buffer,MBUFFER,format,ap);
	va_end(ap);
	printf("%s",buffer);

    exit(eval);
}
#endif //WIN32

static void
usage(void)
{
    fprintf(stderr, "usage:\n%s [-i device] commands\n", progname);
    fprintf(stderr, "%s\n", commands);
//    prtRateTbl(INVALID_FREQ);
    A_ERR(-1, "Incorrect usage");
}

// 1 byte per rate (power value)
// For now.. let's make powerGainStart same for all rates..  (0-15)
void getPowerGain(unsigned int *pwrGain,unsigned int value)
{
    int i=0;

    value = value&0xff;
    *pwrGain = 0;
    for(i=0;i<4;i++)
    {
        *pwrGain = *pwrGain | (value << 8*i);
        //value = (value&0xff) << 8;
    }

}

void rateIndexToArrayMapping (A_UINT32 rateIndex, A_UINT32 *rowIndex, A_UINT32 *bitmask, A_BOOL *is11AcRate )
{
    *is11AcRate = FALSE;

    if ((rateIndex == 150) || (rateIndex==151) || (rateIndex==152) ) {
        *is11AcRate = FALSE;
    }
    else if (rateIndex >= 60)
        *is11AcRate = TRUE;

    printf("Is11AcRate %d\n",*is11AcRate);

    switch(rateIndex)
    {
        case 0: // 1 Mb
            *rowIndex = 0;
            *bitmask = 0x00000001; // set Bit 1
             break;
        case 1: // 2 Mb Long CCK
            *rowIndex = 0;
            *bitmask = 0x00000002; // set Bit 2
             break;
        case 150: // 2 Mb Short CCK
            *rowIndex = 0;
            *bitmask = 0x00000004; // set Bit 3 (SHORT CCK)
             break;
        case 2: // 5.5 Mb Long CCK
            *rowIndex = 0;
            *bitmask = 0x00000008; // set Bit 4
             break;
        case 151: // 5.5 Mb Short CCK
            *rowIndex = 0;
            *bitmask = 0x00000010; // set Bit 5 (SHORT CCK)
             break;
        case 3: // 11 Mb Long CCK
            *rowIndex = 0;
            *bitmask = 0x00000020; // set Bit 6
             break;
        case 152: // 11 Mb Short CCK
            *rowIndex = 0;
            *bitmask = 0x00000040; // set Bit 7 (SHORT CCK)
             break;

        case 4: // 6 Mb
            *rowIndex = 0;
            *bitmask = 0x00000100;
             break;
        case 5: // 9 Mb
            *rowIndex = 0;
            *bitmask = 0x00000200;
             break;
        case 6: // 12 Mb
            *rowIndex = 0;
            *bitmask = 0x00000400;
             break;
        case 7: // 18 Mb
            *rowIndex = 0;
            *bitmask = 0x00000800;
             break;
        case 8: // 24 Mb
            *rowIndex = 0;
            *bitmask = 0x00001000;
             break;
        case 9: // 36 Mb
            *rowIndex = 0;
            *bitmask = 0x00002000;
             break;
        case 10: // 48 Mb
            *rowIndex = 0;
            *bitmask = 0x00004000;
             break;
        case 11: // 54 Mb
            *rowIndex = 0;
            *bitmask = 0x00008000;
             break;

        case 12: // HT20 MCS0
            *rowIndex = 0;
            *bitmask = 0x00010000;
             break;
        case 13: // HT20 MCS1
            *rowIndex = 0;
            *bitmask = 0x00020000;
             break;
        case 14: // HT20 MCS2
            *rowIndex = 0;
            *bitmask = 0x00040000;
             break;
        case 15: // HT20 MCS3
            *rowIndex = 0;
            *bitmask = 0x00080000;
             break;
        case 16: // HT20 MCS4
            *rowIndex = 0;
            *bitmask = 0x00100000;
             break;
        case 17: // HT20 MCS5
            *rowIndex = 0;
            *bitmask = 0x00200000;
             break;
        case 18: // HT20 MCS6
            *rowIndex = 0;
            *bitmask = 0x00400000;
             break;
        case 19: // HT20 MCS7
            *rowIndex = 0;
            *bitmask = 0x00800000;
             break;

        case 20: // HT20 MCS8
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00000001; // set Bit 1
             break;
        case 21: // HT20 MCS9
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00000002; // set Bit 2
             break;
        case 22: // HT20 MCS10
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00000004; // set Bit
             break;
        case 23: // HT20 MCS11
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00000008; // set Bit
             break;
        case 24: // HT20 MCS12
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00000010; // set Bit
             break;
        case 25: // HT20 MCS13
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00000020; // set Bit
             break;
        case 26: // HT20 MCS14
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00000040; // set Bit
             break;
        case 27: // HT20 MCS15
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00000080; // set Bit
             break;

        case 28: // HT20 MCS16
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00010000; // set Bit 1
             break;
        case 29: // HT20 MCS17
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00020000; // set Bit 2
             break;
        case 30: // HT20 MCS18
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00040000; // set Bit
             break;
        case 31: // HT20 MCS19
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00080000; // set Bit
             break;
        case 32: // HT20 MCS20
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00100000; // set Bit
             break;
        case 33: // HT20 MCS21
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00200000; // set Bit
             break;
        case 34: // HT20 MCS22
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00400000; // set Bit
             break;
        case 35: // HT20 MCS23
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00800000; // set Bit
             break;

        case 36: // HT40 MCS0
            *rowIndex = 0;
            *bitmask = 0x01000000;
             break;
        case 37: // HT40 MCS1
            *rowIndex = 0;
            *bitmask = 0x02000000;
             break;
        case 38: // HT40 MCS2
            *rowIndex = 0;
            *bitmask = 0x04000000;
             break;
        case 39: // HT40 MCS3
            *rowIndex = 0;
            *bitmask = 0x08000000;
             break;
        case 40: // HT40 MCS4
            *rowIndex = 0;
            *bitmask = 0x10000000;
             break;
        case 41: // HT40 MCS5
            *rowIndex = 0;
            *bitmask = 0x20000000;
             break;
        case 42: // HT40 MCS6
            *rowIndex = 0;
            *bitmask = 0x40000000;
             break;
        case 43: // HT40 MCS7 135
            *rowIndex = 0;
            *bitmask = 0x80000000; // set Bit 32
             break;

        case 44: // HT40 MCS8
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00000100; // set Bit
             break;
        case 45: // HT40 MCS9
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00000200; // set Bit
             break;
        case 46: // HT40 MCS10
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00000400; // set Bit
             break;
        case 47: // HT40 MCS11
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00000800; // set Bit
             break;
        case 48: // HT40 MCS12
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00001000; // set Bit
             break;
        case 49: // HT40 MCS13
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00002000; // set Bit
             break;
        case 50: // HT40 MCS14
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00004000; // set Bit
             break;
        case 51: // HT40 MCS15
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00008000; // set Bit
             break;

        case 52: // HT40 MCS16
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x01000000; // set Bit
             break;
        case 53: // HT40 MCS17
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x02000000; // set Bit
             break;
        case 54: // HT40 MCS18
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x04000000; // set Bit
             break;
        case 55: // HT40 MCS19
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x08000000; // set Bit
             break;
        case 56: // HT40 MCS20
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x10000000; // set Bit
             break;
        case 57: // HT40 MCS21
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x20000000; // set Bit
             break;
        case 58: // HT40 MCS22
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x40000000; // set Bit
             break;
        case 59: // HT40 MCS23
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x80000000; // set Bit
             break;

        case 60: // VHT20 MCS0 S1
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00000001; // set Bit
             break;
        case 61: // VHT20 MCS1 S1
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00000002; // set Bit
             break;
        case 62: // VHT20 MCS2 S1
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00000004; // set Bit
             break;
        case 63: // VHT20 MCS3 S1
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00000008; // set Bit
             break;
        case 64: // VHT20 MCS4 S1
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00000010; // set Bit
             break;
        case 65: // VHT20 MCS5 S1
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00000020; // set Bit
             break;
        case 66: // VHT20 MCS6 S1
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00000040; // set Bit
             break;
        case 67: // VHT20 MCS7 S1
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00000080; // set Bit
             break;
        case 68: // VHT20 MCS8 S1
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00000100; // set Bit
             break;
        case 69: // VHT20 MCS9 S1
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00000200; // set Bit
             break;

        case 70: // VHT20 MCS0 S2
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00000010; // set Bit
             break;
        case 71: // VHT20 MCS1 S2
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00000020; // set Bit
             break;
        case 72: // VHT20 MCS2 S2
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00000040; // set Bit
             break;
        case 73: // VHT20 MCS3 S2
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00000080; // set Bit
             break;
        case 74: // VHT20 MCS4 S2
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00000100; // set Bit
             break;
        case 75: // VHT20 MCS5 S2
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00000200; // set Bit
             break;
        case 76: // VHT20 MCS6 S2
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00000400; // set Bit
             break;
        case 77: // VHT20 MCS7 S2
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00000800; // set Bit
             break;
        case 78: // VHT20 MCS8 S2
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00001000; // set Bit
             break;
        case 79: // VHT20 MCS9 S2
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00002000; // set Bit
             break;

        case 80: // VHT20 MCS0 S3
            *rowIndex = 2;         // row Index 2
            *bitmask = 0x00000100; // set Bit
             break;
        case 81: // VHT20 MCS1 S3
            *rowIndex = 2;         // row Index 2
            *bitmask = 0x00000200; // set Bit
             break;
        case 82: // VHT20 MCS2 S3
            *rowIndex = 2;         // row Index 2
            *bitmask = 0x00000400; // set Bit
             break;
        case 83: // VHT20 MCS3 S3
            *rowIndex = 2;         // row Index 2
            *bitmask = 0x00000800; // set Bit
             break;
        case 84: // VHT20 MCS4 S3
            *rowIndex = 2;         // row Index 2
            *bitmask = 0x00001000; // set Bit
             break;
        case 85: // VHT20 MCS5 S3
            *rowIndex = 2;         // row Index 2
            *bitmask = 0x00002000; // set Bit
             break;
        case 86: // VHT20 MCS6 S3
            *rowIndex = 2;         // row Index 2
            *bitmask = 0x00004000; // set Bit
             break;
        case 87: // VHT20 MCS7 S3
            *rowIndex = 2;         // row Index 2
            *bitmask = 0x00008000; // set Bit
             break;
        case 88: // VHT20 MCS8 S3
            *rowIndex = 2;         // row Index 2
            *bitmask = 0x00010000; // set Bit
             break;
        case 89: // VHT20 MCS9 S3
            *rowIndex = 2;         // row Index 2
            *bitmask = 0x00020000; // set Bit
             break;

        case 90: // VHT40 MCS0 S1
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00001000; // set Bit
             break;
        case 91: // VHT40 MCS1 S1
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00002000; // set Bit
             break;
        case 92: // VHT40 MCS2 S1
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00004000; // set Bit
             break;
        case 93: // VHT40 MCS3 S1
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00008000; // set Bit
             break;
        case 94: // VHT40 MCS4 S1
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00010000; // set Bit
             break;
        case 95: // VHT40 MCS5 S1
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00020000; // set Bit
             break;
        case 96: // VHT40 MCS6 S1
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00040000; // set Bit
             break;
        case 97: // VHT40 MCS7 S1
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00080000; // set Bit
             break;
        case 98: // VHT40 MCS8 S1
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00100000; // set Bit
             break;
        case 99: // VHT40 MCS9 S1
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00200000; // set Bit
             break;

        case 100: // VHT40 MCS0 S2
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00010000; // set Bit
             break;
        case 101: // VHT40 MCS1 S2
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00020000; // set Bit
             break;
        case 102: // VHT40 MCS2 S2
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00040000; // set Bit
             break;
        case 103: // VHT40 MCS3 S2
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00080000; // set Bit
             break;
        case 104: // VHT40 MCS4 S2
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00100000; // set Bit
             break;
        case 105: // VHT40 MCS5 S2
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00200000; // set Bit
             break;
        case 106: // VHT40 MCS6 S2
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00400000; // set Bit
             break;
        case 107: // VHT40 MCS7 S2
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00800000; // set Bit
             break;
        case 108: // VHT40 MCS8 S2
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x01000000; // set Bit
             break;
        case 109: // VHT40 MCS9 S2
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x02000000; // set Bit
             break;

        case 110: // VHT40 MCS0 S3
            *rowIndex = 2;         // row Index 2
            *bitmask = 0x00100000; // set Bit
             break;
        case 111: // VHT40 MCS1 S3
            *rowIndex = 2;         // row Index 2
            *bitmask = 0x00200000; // set Bit
             break;
        case 112: // VHT40 MCS2 S3
            *rowIndex = 2;         // row Index 2
            *bitmask = 0x00400000; // set Bit
             break;
        case 113: // VHT40 MCS3 S3
            *rowIndex = 2;         // row Index 2
            *bitmask = 0x00800000; // set Bit
             break;
        case 114: // VHT40 MCS4 S3
            *rowIndex = 2;         // row Index 2
            *bitmask = 0x01000000; // set Bit
             break;
        case 115: // VHT40 MCS5 S3
            *rowIndex = 2;         // row Index 2
            *bitmask = 0x02000000; // set Bit
             break;
        case 116: // VHT40 MCS6 S3
            *rowIndex = 2;         // row Index 2
            *bitmask = 0x04000000; // set Bit
             break;
        case 117: // VHT40 MCS7 S3
            *rowIndex = 2;         // row Index 2
            *bitmask = 0x08000000; // set Bit
             break;
        case 118: // VHT40 MCS8 S3
            *rowIndex = 2;         // row Index 2
            *bitmask = 0x10000000; // set Bit
             break;
        case 119: // VHT40 MCS9 S3
            *rowIndex = 2;         // row Index 2
            *bitmask = 0x20000000; // set Bit
             break;

        case 120: // VHT80 MCS0 S0
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x01000000; // set Bit
             break;
        case 121: // VHT80 MCS1 S0
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x02000000; // set Bit
             break;
        case 122: // VHT80 MCS2 S0
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x04000000; // set Bit
             break;
        case 123: // VHT80 MCS3 S0
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x08000000; // set Bit
             break;
        case 124: // VHT80 MCS4 S0
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x10000000; // set Bit
             break;
        case 125: // VHT80 MCS5 S0
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x20000000; // set Bit
             break;
        case 126: // VHT80 MCS6 S0
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x40000000; // set Bit
             break;
        case 127: // VHT80 MCS7 S0
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x80000000; // set Bit
             break;
        case 128: // VHT80 MCS8 S0
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00000001; // set Bit
             break;
        case 129: // VHT80 MCS9 S0
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x00000002; // set Bit
             break;

        case 130: // VHT80 MCS0 S1
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x10000000; // set Bit
             break;
        case 131: // VHT80 MCS1 S1
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x20000000; // set Bit
             break;
        case 132: // VHT80 MCS2 S1
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x40000000; // set Bit
             break;
        case 133: // VHT80 MCS3 S1
            *rowIndex = 1;         // row Index 1
            *bitmask = 0x80000000; // set Bit
             break;
        case 134: // VHT80 MCS4 S1
            *rowIndex = 2;         // row Index 2
            *bitmask = 0x00000001; // set Bit
             break;
        case 135: // VHT80 MCS5 S1
            *rowIndex = 2;         // row Index 2
            *bitmask = 0x00000002; // set Bit
             break;
        case 136: // VHT80 MCS6 S1
            *rowIndex = 2;         // row Index 2
            *bitmask = 0x00000004; // set Bit
             break;
        case 137: // VHT80 MCS7 S1
            *rowIndex = 2;         // row Index 2
            *bitmask = 0x00000008; // set Bit
             break;
        case 138: // VHT80 MCS8 S1
            *rowIndex = 2;         // row Index 2
            *bitmask = 0x00000010; // set Bit
             break;
        case 139: // VHT80 MCS9 S1
            *rowIndex = 2;         // row Index 2
            *bitmask = 0x00000020; // set Bit
             break;

        case 140: // VHT80 MCS0 S3
            *rowIndex = 3;         // row Index 3
            *bitmask = 0x00000001; // set Bit
             break;
        case 141: // VHT80 MCS1 S3
            *rowIndex = 3;         // row Index 3
            *bitmask = 0x00000002; // set Bit
             break;
        case 142: // VHT80 MCS2 S3
            *rowIndex = 3;         // row Index 3
            *bitmask = 0x00000004; // set Bit
             break;
        case 143: // VHT80 MCS3 S3
            *rowIndex = 3;         // row Index 3
            *bitmask = 0x00000008; // set Bit
             break;
        case 144: // VHT80 MCS4 S3
            *rowIndex = 3;         // row Index 3
            *bitmask = 0x00000010; // set Bit
             break;
        case 145: // VHT80 MCS5 S3
            *rowIndex = 3;         // row Index 3
            *bitmask = 0x00000020; // set Bit
             break;
        case 146: // VHT80 MCS6 S3
            *rowIndex = 3;         // row Index 3
            *bitmask = 0x00000040; // set Bit
             break;
        case 147: // VHT80 MCS7 S3
            *rowIndex = 3;         // row Index 3
            *bitmask = 0x00000080; // set Bit
             break;
        case 148: // VHT80 MCS8 S3
            *rowIndex = 3;         // row Index 3
            *bitmask = 0x00000100; // set Bit
             break;
        case 149: // VHT80 MCS9 S3
            *rowIndex = 3;         // row Index 3
            *bitmask = 0x00000200; // set Bit
             break;

        case 153: // VHT20 MCS0 S4
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00000001; // set Bit
             break;
        case 154: // VHT20 MCS1 S4
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00000002; // set Bit
             break;
        case 155: // VHT20 MCS2 S4
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00000004; // set Bit
             break;
        case 156: // VHT20 MCS3 S4
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00000008; // set Bit
             break;
        case 157: // VHT20 MCS4 S4
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00000010; // set Bit
             break;
        case 158: // VHT20 MCS5 S4
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00000020; // set Bit
             break;
        case 159: // VHT20 MCS6 S4
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00000040; // set Bit
             break;
        case 160: // VHT20 MCS7 S4
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00000080; // set Bit
             break;
        case 161: // VHT20 MCS8 S4
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00000100; // set Bit
             break;
        case 162: // VHT20 MCS9 S4
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00000200; // set Bit
             break;

        case 163: // VHT40 MCS0 S4
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00000400; // set Bit
             break;
        case 164: // VHT40 MCS1 S4
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00000800; // set Bit
             break;
        case 165: // VHT40 MCS2 S4
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00001000; // set Bit
             break;
        case 166: // VHT40 MCS3 S4
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00002000; // set Bit
             break;
        case 167: // VHT40 MCS4 S4
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00004000; // set Bit
             break;
        case 168: // VHT40 MCS5 S4
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00008000; // set Bit
             break;
        case 169: // VHT40 MCS6 S4
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00010000; // set Bit
             break;
        case 170: // VHT40 MCS7 S4
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00020000; // set Bit
             break;
        case 171: // VHT40 MCS8 S4
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00040000; // set Bit
             break;
        case 172: // VHT40 MCS9 S4
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00080000; // set Bit
             break;

        case 173: // VHT80 MCS0 S4
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00100000; // set Bit
             break;
        case 174: // VHT80 MCS1 S4
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00200000; // set Bit
             break;
        case 175: // VHT80 MCS2 S4
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00400000; // set Bit
             break;
        case 176: // VHT80 MCS3 S4
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x00800000; // set Bit
             break;
        case 177: // VHT80 MCS4 S4
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x01000000; // set Bit
             break;
        case 178: // VHT80 MCS5 S4
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x02000000; // set Bit
             break;
        case 179: // VHT80 MCS6 S4
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x04000000; // set Bit
             break;
        case 180: // VHT80 MCS7 S4
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x08000000; // set Bit
             break;
        case 181: // VHT80 MCS8 S4
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x10000000; // set Bit
             break;
        case 182: // VHT80 MCS9 S4
            *rowIndex = 0;         // row Index 0
            *bitmask = 0x20000000; // set Bit
             break;
        // cascade
	    case 183: // VHT160 MCS0 S1
		     *rowIndex = 7;		   // row Index 0
		     *bitmask = 0x00000001; // set Bit
		     break;
	    case 184: // VHT160 MCS1 S1
		     *rowIndex = 7;		   // row Index 0
		     *bitmask = 0x00000002; // set Bit
		     break;
	    case 185: // VHT160 MCS2 S1
		     *rowIndex = 7;		   // row Index 0
		     *bitmask = 0x00000004; // set Bit
		     break;
	    case 186: // VHT160 MCS3 S1
		     *rowIndex = 7;		   // row Index 0
		     *bitmask = 0x00000008; // set Bit
		     break;

	    case 187: // VHT160 MCS4 S1
		     *rowIndex = 7;		   // row Index 0
		     *bitmask = 0x00000010; // set Bit
		     break;
	    case 188: // VHT160 MCS5 S1
		     *rowIndex = 7;		   // row Index 0
		     *bitmask = 0x00000020; // set Bit
		     break;
	    case 189: // VHT160 MCS6 S1
		     *rowIndex = 7;		   // row Index 0
		     *bitmask = 0x00000040; // set Bit
		     break;
	    case 190: // VHT160 MCS7 S1
		     *rowIndex = 7;		   // row Index 0
		     *bitmask = 0x00000080; // set Bit
		     break;
        case 191: // VHT160 MCS8 S1
		     *rowIndex = 7;		   // row Index 0
		     *bitmask = 0x00000100; // set Bit
		     break;
	    case 192: // VHT160 MCS9 S1
		     *rowIndex = 7;		   // row Index 0
		     *bitmask = 0x000000200; // set Bit
		     break;
	    case 193: // VHT160 MCS0 S2
		     *rowIndex = 7;		   // row Index 0
		     *bitmask = 0x00000400; // set Bit
		     break;
	    case 194: // VHT160 MCS1 S2
		     *rowIndex = 7;		   // row Index 0
		     *bitmask = 0x00000800; // set Bit
		     break;

	    case 195: // VHT160 MCS2 S2
		     *rowIndex = 7;		   // row Index 0
		     *bitmask = 0x00001000; // set Bit
		     break;
	    case 196: // VHT160 MCS3 S1
		     *rowIndex = 7;		   // row Index 0
		     *bitmask = 0x00002000; // set Bit
		     break;
	    case 197: // VHT160 MCS4 S2
		     *rowIndex = 7;		   // row Index 0
		     *bitmask = 0x00004000; // set Bit
		     break;
	    case 198: // VHT160 MCS5 S2
		     *rowIndex = 7;		   // row Index 0
		     *bitmask = 0x00008000; // set Bit
		     break;
        case 199: // VHT160 MCS6 S2
		     *rowIndex = 7;		   // row Index 0
		     *bitmask = 0x00010000; // set Bit
		     break;
	    case 200: // VHT160 MCS7 S2
		     *rowIndex = 7;		   // row Index 0
		     *bitmask = 0x00020000; // set Bit
		     break;
	    case 201: // VHT160 MCS8 S2
		     *rowIndex = 7;		   // row Index 0
		     *bitmask = 0x00040000; // set Bit
		     break;
	    case 202: // VHT160 MCS9 S2
		     *rowIndex = 7;		   // row Index 0
		     *bitmask = 0x00080000; // set Bit
		     break;
    }

}

//#define USE_TLV2

void setRateMask (A_UINT32 *tlv2RateMaskArray, A_UINT8 index, A_UINT8 *tlv1KeyWord, A_UINT32 rateBitMask)
{
#ifdef USE_TLV2
    tlv2RateMaskArray[index] = rateBitMask;
#else
    //addParameterToCommand((A_UINT8 *)tlv1KeyWord, (A_UINT8 *)&rateBitMask);
#endif
}

void addParm (PARM_CODE parmCode, char *tlv1KeyWord, A_UINT32 value)
{
#ifdef USE_TLV2
	tlv2AddParms(2, parmCode, value);
#else
    //addParameterToCommand((A_UINT8 *)tlv1KeyWord, (A_UINT8 *)&value);
#endif //USE_TLV2
}

void addArrayParm (PARM_CODE parmCode, char *tlv1KeyWord, void *array, A_UINT32 size)
{
#ifdef USE_TLV2
    tlv2AddParms(4, parmCode, size, 0, array);
#else
    //addParameterToCommand((A_UINT8 *)tlv1KeyWord, (A_UINT8 *)array);
#endif //USE_TLV2
}

A_UINT8 bdata[BOARD_DATA_SZ_MAX];
A_UINT32 bdata_block_size = 0;
char param_addr_mode = 0;
char param_value_mode = 0;
A_UINT8 param_valuetype = 1;
A_UINT16 param_value_offset = 0;
A_UINT16 param_value_array_size = 0;
A_UINT8 param_value_array[256];
A_UINT32 g_eeprom_size = 0;
A_UINT8 g_eeprom_read_done = 0;
A_UINT8 responseStatus = 0;

int main (int argc, char **argv)
{
    int c, error;
    char ifname[IFNAMSIZ];
    char buf[2048 + 8];
    int packet_size = 1500; //added packet_size with default value 1500
    int rate_Bw = 1;        //added rate bandwidth with default value 1
    int phy165Mode = 0;
    A_UINT8 *rCmdStream = NULL;
    A_UINT32 cmdStreamLen=0,opCode=_OP_TX;
    A_UINT32 value = 0, dataRate = 0, miscFlags = 0;//bdata_offset = 0;
    A_UINT16 regDmn[2];
    //A_BOOL tlvConstruct = FALSE, ret=FALSE;
    A_BOOL is11ACRate = FALSE;
    A_BOOL respNeeded = TRUE;
    A_BOOL rxSetMac = FALSE;
    //A_UINT32 efuseStart=0,efuseEnd=0;
    TESTFLOW_CMD_STREAM_V2 *pCmdStream;
    A_BOOL useTLV1p0=TRUE;
    A_INT16 measuredPwr[512];
    A_UINT32 numMeasuredPwr=0;
    A_BOOL tlv2Construct = FALSE;
    /* Default srcmac, dstmac and bssid for 5G and 2G */
    A_UINT8 srcMac[2][ATH_MAC_LEN] = {{0x00, 0x03, 0x7F, 0x03, 0x40, 0x33}, {0x00, 0x03, 0x7F, 0x03, 0x40, 0x34}};
    A_UINT8 dstMac[2][ATH_MAC_LEN] = {{0x00, 0x03, 0x7F, 0x11, 0x12, 0x13}, {0x00, 0x03, 0x7F, 0x11, 0x12, 0x14}};
    A_UINT8 bssid[2][ATH_MAC_LEN] = {{0x00, 0x00, 0x00, 0xCA, 0xFF, 0xEE}, {0x00, 0x00, 0x00, 0xCA, 0xFF, 0xEF}};
    A_UINT8 is2GHz = 1;
    A_UINT32 rtMask[2] = {20,0};

    progname = argv[0];
    memset(buf, 0, sizeof(buf));

    if (argc == 1) {
        usage();
    }

    memset(ifname, '\0', IFNAMSIZ);
    strlcpy(ifname, "wifi0", IFNAMSIZ);

    // add TLV2p0
    // TBD?? should be based on a package config file
    addTLV2p0BinCmdParser();
    addTLV2p0Encoder();
    registerTPCCALRSPHandler(handleTPCCALRsp);
    registerTPCCALDATAHandler(handleTPCCALDATA);
    registerREGREADRSPHandler(handleREGREADRSP);
    registerREGWRITERSPHandler(handleREGWRITERSP);
    registerBASICRSPHandler(handleBASICRSP);
    registerTXSTATUSRSPHandler(handleTXSTATUSRSP);
    registerRXSTATUSRSPHandler(handleRXSTATUSRSP);
    registerRXRSPHandler(handleRXRSP);
    registerMEMREADRSPHandler(handleMEMREADRSP);
    registerMEMWRITERSPHandler(handleMEMWRITERSP);
    registerLMGORSPHandler(handleLMGoRsp);
    registerLMQUERYRSPHandler(handleLMQueryRsp);
    registerLMTXINITRSPHandler(handleLMTxInitRsp);
    registerLMRXINITRSPHandler(handleLMRxInitRsp);
    registerLMCHANNELLISTRSPHandler(handleLMChannelListRsp);
    registerEEPROMGETSIZERSPHandler(handleEepromGetSizeRsp);
    registerMORESEGMENTHandler(handleMoreSegment);
    registerEEPROMREADRSPHandler(handleEepromReadRsp);
#ifdef USE_TLV2
    useTLV1p0 = FALSE;



 #endif //USE_TLV2
    while (1) {
        int option_index = 0;
        static struct option long_options[] = {
            {"version", 0, NULL, 'v'},
            {"interface", 1, NULL, 'i'},
            {"tx", 1, NULL, 't'},
            {"phyId", 1, NULL, 'y'},
            {"txfreq", 1, NULL, 'f'},
            {"txfreq2", 1, NULL, 'F'},
            {"txrate", 1, NULL, 'g'},
            {"rateBw", 1, NULL, 'Q'},
            {"phy165Mode", 1, NULL, 'j'},
            {"txpwr", 1, NULL, 'h'},
            {"tgtpwr", 1, NULL, 'H'},
            {"pcdac", 1, NULL, 'I'},
            {"forcedGain", 1, NULL, TCMD_TX_FORCED_GAIN},
            {"gainIdx", 1, NULL, TCMD_TX_FORCED_GAINIDX},
            {"dacGain", 1, NULL, TCMD_TX_DAC_GAIN},
            {"paConfig", 1, NULL, TCMD_TX_PA_CONFIG},
            {"txantenna", 1, NULL, 'q'},
            {"txpktsz", 1, NULL, 'z'},
            {"txpattern", 1, NULL, 'e'},
            {"txchain", 1, NULL, 'c'},
            {"rxchain", 1, NULL, 'C'},
            {"bssid", 1, NULL, 'b'},
            {"srcmac", 1, NULL, 'd'},
            {"dstmac", 1, NULL, 'D'},
            {"rx", 1, NULL, 'r'},
            {"rxfreq", 1, NULL, 'p'},
            {"rxfreq2", 1, NULL, 'L'},
            {"rxantenna", 1, NULL, 'q'},
            {"ack", 1, NULL, 'k'},
            {"bc", 1, NULL, 'K'},
            {"pm", 1, NULL, 'x'},
            {"rxMac", 1, NULL, 'T'},
            {"setmac", 1, NULL, 's'},
            {"setbssid", 1, NULL, 'X'},
            {"ani", 0, NULL, 'a'},
            {"gI", 1, NULL, 'W'},
            {"paprd", 0, NULL, 'A'},
            {"stbc", 0, NULL, 'Y'},
            {"ldpc", 0, NULL, 'Z'},
            {"scrambleroff", 0, NULL, 'o'},
            {"aifsn", 1, NULL, 'u'},
            {"dutycyc", 1, NULL, 'U'},
            {"SetAntSwitchTable", 1, NULL, 'S'},
            {"shortguard", 0, NULL, 'G'},
            {"numpackets", 1, NULL, 'n'},
            {"agg", 1, NULL, 'N'},
            {"mode", 1, NULL, 'M'},
            {"bw", 1, NULL, 'm'},
            {"nss", 1, NULL, 'J'},
            {"setlongpreamble", 1, NULL, 'l'},
            {"setreg", 1, NULL, 'R'},
            {"regval", 1, NULL, 'V'},
            {"writeotp", 0, NULL, 'w'},
            {"otpregdmn", 1, NULL, 'E'},
            {"btaddr", 1, NULL, 'B'},
            {"therm", 0, NULL, TCMD_READ_THERMAL},
            {"tpcm", 1, NULL, 'P'},
            {"lpl", 1, NULL, TCMD_SET_RX_LPL},
            {"cal", 1, NULL, TCMD_CAL},
            {"calLm", 0, NULL, TCMD_CAL_LM},
            {"calPwr", 0, NULL, TCMD_CAL_PWR},
            {"otpdump", 0, NULL, TCMD_OTP_DUMP},
            {"otpreset", 1, NULL, TCMD_OTP_RESET},
            {"otpwrite", 1, NULL, TCMD_OTP_WRITE},
            {"efusedump", 0, NULL, TCMD_EFUSE_DUMP},
            {"start", 1, NULL, TCMD_EFUSE_START},
            {"end", 1, NULL, TCMD_EFUSE_END},
            {"efusewrite", 1, NULL, TCMD_EFUSE_WRITE},
            {"txstatus", 0, NULL, TCMD_TX_STATUS},
            {"eepromdump", 0, NULL, TCMD_EEPROM_DUMP},
            {"eepromwrite", 0, NULL, TCMD_EEPROM_WRITE},
            {"totalsize", 1, NULL, TCMD_EEPROM_TOTAL_SIZE},
            {"offset", 1, NULL, TCMD_EEPROM_OFFSET},
            {"blocksize", 1, NULL, TCMD_EEPROM_BLOCK_SIZE},
            {"bdfile", 1, NULL, TCMD_EEPROM_BLOCK_DATA},
            {"eepromerase", 0, NULL, TCMD_EEPROM_ERASE},
            {"eepromcheck", 0, NULL, TCMD_EEPROM_CHECK},
            {"hwcal", 1, NULL, TCMD_HWCAL},
            {"loop", 1, NULL, TCMD_LOOP},
            {"chmask", 1, NULL, TCMD_CHAIN_MASK},
            {"loopback", 1, NULL, TCMD_LOOPBACK},
            {"savecal", 1, NULL, TCMD_SAVE_CAL},
            {"nfcal", 1, NULL, TCMD_NF_CAL},
            {"setup", 0, NULL, TCMD_SETUP},
            {"go", 0, NULL, TCMD_GO},
            {"rr", 0, NULL, TCMD_REG_READ},
            {"rw", 0, NULL, TCMD_REG_WRITE},
            {"addr", 1, NULL, TCMD_ADDR},
            {"value", 1, NULL, TCMD_VALUE},
            {"getnvmac", 0, NULL, TCMD_GET_NV_MAC_ADDRESS},
            {"setnvmac", 1, NULL, TCMD_SET_NV_MAC_ADDRESS},
            {"nvbdcommit", 0, NULL, TCMD_COMMIT_NV_BDATA},
            {"nvtype", 1, NULL, TCMD_COMMIT_NV_BDATA_TYPE},
            {"nvsegment", 1, NULL, TCMD_COMMIT_NV_BDATA_SEGMENT},
            {"nvcompressed", 1, NULL, TCMD_COMMIT_NV_BDATA_COMPRESSED},
            {"getcalbdata", 0, NULL, TCMD_GET_CAL_BDATA},
            {"getbdatasize", 0, NULL, TCMD_GET_CAL_BDATA_SIZE},
            {"mr", 0, NULL, TCMD_MEM_READ},
            {"mw", 0, NULL, TCMD_MEM_WRITE},
            {"numbytes", 1, NULL, TCMD_NUMBYTES},
            {"valuetype", 1, NULL, TCMD_VALUETYPE},
            {"calXtal", 1, NULL, TCMD_CAL_XTAL},
            {"gettxpwr", 0, NULL, TCMD_GET_TX_OFFLINE_POWER},
            {"norssiavg", 0, NULL, TCMD_RSSI_AVG_DISABLE},
            /*{"dpdtimingidx", 0, NULL, TCMD_DPD_TUNING_LOOPBK_TIMINGIDX},
            {"dpdtrainingquality", 0, NULL, TCMD_DPD_TUNING_TRAINING_QUALITY},
            {"dpdatten", 0, NULL, TCMD_DPD_TUNING_LOOPBK_ATTEN},
            {"dpdagc2power", 0, NULL, TCMD_DPD_TUNING_AGC2PWR},
            {"lmtxinit", 1, NULL, TCMD_LM_TXINIT},
            {"lmchannellist", 1, NULL, TCMD_LM_CHANNEL_LIST},
            {"lmquery", 1, NULL, TCMD_LM_QUERY},
            {"lmgo", 1, NULL, TCMD_LM_GO},
            {"lmrxinit", 1, NULL, TCMD_LM_RXINIT},*/
            {0, 0, 0, 0}
        };


        c = getopt_long(argc, argv, "vi:t:f:F:g:h:HI:j:z:e:c:C:b:d:D:r:p:q:k:K:x:s:X:aAYZouU:S:Gn:N:M:m:l:P:L:",
                         long_options, &option_index);

        if (c == -1)
            break;

        switch (c) {
        case 'i':
#ifdef UTFSIM
            if (!strncmp(optarg, "sim", 3)) {
                UTF_RFSIM_INIT(optarg + 4);
            } else
#else
            {
                memset(ifname, '\0', 8);
                strlcpy(ifname, optarg, IFNAMSIZ);
            }
#endif
            break;
        case 'y':
#ifdef USE_TLV2
                value = atoi(optarg);
                addParm(PARM_PHYID, "phyId", value);
#else
                printf("Invalid option!\n");
                return 0;
#endif
            break;
        case 'J':
#ifdef USE_TLV2
            value = atoi(optarg);
            addParm(PARM_NSS, "nss", value);
#endif
            break;
        case 't':
            if (!strcmp(optarg, "sine")) {
                value = TCMD_CONT_TX_SINE;
            } else if (!strcmp(optarg, "frame")) {
                value = TCMD_CONT_TX_FRAME;
            } else if (!strcmp(optarg, "tx99")) {
                value = TCMD_CONT_TX_TX99;
            } else if (!strcmp(optarg, "tx100")) {
                value = TCMD_CONT_TX_TX100;
            } else if (!strcmp(optarg, "cwtone")) {
                value = TCMD_CONT_TX_CWTONE;
            } else if (!strcmp(optarg, "clpcpkt")) {
                value = TCMD_CONT_TX_CLPCPKT;
            } else if (!strcmp(optarg, "off")) {
                value = TCMD_CONT_TX_OFF;
            } else {
                printf("Error \n");
                usage();
                return 0;
            }
#ifdef USE_TLV2
            if (value ==TCMD_CONT_TX_OFF)
            {
            	tlv2CreateCmdHeader(CMD_TXSTATUS); // stop Tx and need report
            }
            else
            {
				tlv2CreateCmdHeader(CMD_TX);
				tlv2AddParms(2, PARM_TXMODE, value);
            }
            tlv2Construct = TRUE;
            miscFlags |= (TX_STATUS_PER_RATE_MASK | PROCESS_RATE_IN_ORDER_MASK);
#else
            tlvConstruct = TRUE;
            //createCommand(_OP_TX);
            //addParameterToCommand((A_UINT8 *)"txMode",(A_UINT8 *)&value);
#endif //USE_TLV2
            opCode = _OP_TX;
            break;
        case 'f':
        case 'p':
            value = freqValid(atoi(optarg));
#ifdef USE_TLV2
            if (value < 3000)
            {
                is2GHz = 1;
                rate_Bw = 0;
            }
            else
                is2GHz = 0;
#endif
            printf("channel=%d, ", value);
            addParm(PARM_FREQ, "channel", value);
            break;
        case 'F':
        case 'L':
            value = freqValid(atoi(optarg));
            addParm(PARM_FREQ2, "channel2", value);
			printf("channel2: %d\n", value);
            break;

        case 'G':
            value = 1;
            addParm(PARM_SHORTGUARD, "shortGuard", value);
            break;
        case 'P':
            {
                if (!strcmp(optarg, "forcedGain")) {
                    value = TPC_FORCED_GAIN;
                } else if (!strcmp(optarg, "txpwr")) {
                    value = TPC_TX_PWR;
                } else if (!strcmp(optarg, "tgtpwr")) {
                    value = TPC_TGT_PWR;
                } else if (!strcmp(optarg, "forcedGainIdx")) {
                    value = TPC_FORCED_GAINIDX;
                } else if (!strcmp(optarg, "forcedTgtPwr")) {
                    value = TPC_FORCED_TGTPWR;
                } else if (!strcmp(optarg, "forcedGlutIdx")) {
                    value = TPC_TX_PWR;//TPC_FORCED_GLUTIDX;
                }
                else {
                    value = TPC_TX_PWR;
                }
                addParm(PARM_TPCM, "tpcm", value);
            }
            break;
        case 'M':
              if (!strcmp(optarg, "bw20") || (!strcmp(optarg, "ht20")) || (!strcmp(optarg, "vht20"))) {
                  value  = BW20;
              } else if (!strcmp(optarg, "bw40_low") || (!strcmp(optarg, "ht40minus")) || (!strcmp(optarg, "vht40minus"))) {
                  value  = BW40_primaryLow;
              } else if (!strcmp(optarg, "bw40_high") || (!strcmp(optarg, "ht40plus")) || (!strcmp(optarg, "vht40plus"))) {
                  value  = BW40_primaryHigh;
              } else if (!strcmp(optarg, "bw80_0") || (!strcmp(optarg, "vht80_0"))) {
                  value  = BW80_20_40Low_40_80Low;
              } else if (!strcmp(optarg, "bw80_1") || (!strcmp(optarg, "vht80_1"))) {
                  value = BW80_20_40High_40_80Low;
              } else if (!strcmp(optarg, "bw80_2") || (!strcmp(optarg, "vht80_2"))) {
                  value = BW80_20_40Low_40_80High;
              } else if (!strcmp(optarg, "bw80_3") || (!strcmp(optarg, "vht80_3"))) {
                  value = BW80_20_40GHigh_40_80High;
              } else if (!strcmp(optarg, "bw80p80") || (!strcmp(optarg, "vht80p80"))) {
                  value = BW80p80;
              } else if (!strcmp(optarg, "bw160") || (!strcmp(optarg, "vht160"))) {
                  value  = BW160;
              } else if (!strcmp(optarg, "bw165")) {
                  value  = BW165;
              } else if (!strcmp(optarg, "bw80p80_0") || (!strcmp(optarg, "vht80p80_0"))) {
                  value = Primary_1st_20_in_BW80p80;
              } else if (!strcmp(optarg, "bw80p80_1") || (!strcmp(optarg, "vht80p80_1"))) {
                  value = Primary_2nd_20_in_BW80p80;
              } else if (!strcmp(optarg, "bw80p80_2") || (!strcmp(optarg, "vht80p80_2"))) {
                  value = Primary_3rd_20_in_BW80p80;
              } else if (!strcmp(optarg, "bw80p80_3") || (!strcmp(optarg, "vht80p80_3"))) {
                  value  = Primary_4th_20_in_BW80p80;
              } else if (!strcmp(optarg, "bw80p80_4") || (!strcmp(optarg, "vht80p80_4"))) {
                 value	= Primary_5th_20_in_BW80p80;
              } else if (!strcmp(optarg, "bw80p80_5") || (!strcmp(optarg, "vht80p80_5"))) {
                 value  = Primary_6th_20_in_BW80p80;
              } else if (!strcmp(optarg, "bw80p80_6") || (!strcmp(optarg, "vht80p80_6"))) {
                 value  = Primary_7th_20_in_BW80p80;
              } else if (!strcmp(optarg, "bw80p80_7") || (!strcmp(optarg, "vht80p80_7"))) {
                 value  = Primary_8th_20_in_BW80p80;
              } else if (!strcmp(optarg, "bw160_0") || (!strcmp(optarg, "vht160_0"))) {
                 value	= Primary_1st_20_in_BW160;
              } else if (!strcmp(optarg, "bw160_1") || (!strcmp(optarg, "vht160_1"))) {
                  value  = Primary_2nd_20_in_BW160;
              } else if (!strcmp(optarg, "bw160_2") || (!strcmp(optarg, "vht160_2"))) {
                  value  = Primary_3rd_20_in_BW160;
              } else if (!strcmp(optarg, "bw160_3") || (!strcmp(optarg, "vht160_3"))) {
                 value  = Primary_4th_20_in_BW160;
              } else if (!strcmp(optarg, "bw160_4") || (!strcmp(optarg, "vht160_4"))) {
                 value	= Primary_5th_20_in_BW160;
              } else if (!strcmp(optarg, "bw160_5") || (!strcmp(optarg, "vht160_5"))) {
                 value  = Primary_6th_20_in_BW160;
              } else if (!strcmp(optarg, "bw160_6") || (!strcmp(optarg, "vht160_6"))) {
                 value  = Primary_7th_20_in_BW160;
              } else if (!strcmp(optarg, "bw160_7") || (!strcmp(optarg, "vht160_7"))) {
                 value  = Primary_8th_20_in_BW160;
              } else if (!strcmp(optarg, "bw165_0")) {
                 value	= Primary_1st_20_in_BW165;
              } else if (!strcmp(optarg, "bw165_1")) {
                  value  = Primary_2nd_20_in_BW165;
              } else if (!strcmp(optarg, "bw165_2")) {
                  value  = Primary_3rd_20_in_BW165;
              } else if (!strcmp(optarg, "bw165_3")) {
                 value  = Primary_4th_20_in_BW165;
              } else if (!strcmp(optarg, "bw165_4")) {
                 value	= Primary_5th_20_in_BW165;
              } else if (!strcmp(optarg, "bw165_5")) {
                 value  = Primary_6th_20_in_BW165;
              } else if (!strcmp(optarg, "bw165_6")) {
                 value  = Primary_7th_20_in_BW165;
              } else if (!strcmp(optarg, "bw165_7")) {
                 value  = Primary_8th_20_in_BW165;
              }
              else {
                  printf("Error\n");
                  usage();
                  return 0;
              }
              addParm(PARM_WLANMODE, "wlanMode", value);
            break;
        case 'm':
            if (!strcmp(optarg, "half")) {
                value = HALF_SPEED_MODE;
            } else if (!strcmp(optarg, "quarter")) {
                value = QUARTER_SPEED_MODE;
            }
            else {
                printf("Error\n");
                usage();
                return 0;
            }

            addParm(PARM_BANDWIDTH, "bandwidth", value);
            break;
        case TCMD_TX_FORCED_GAINIDX:
            value = atoi(optarg);
            addParm(PARM_GAINIDX, "gainIdx", value);
            break;
        case TCMD_TX_DAC_GAIN:
            value = atoi(optarg);
            addParm(PARM_DACGAIN, "dacGain", value);
            break;
        case TCMD_TX_PA_CONFIG:
            value = atoi(optarg);
            addParm(PARM_PACONFIG, "paConfig", value);
            break;
        case 'n':
            value = atoi(optarg);
            addParm(PARM_TXNUMPACKETS, "numPackets", value);
            break;
        case 'N':
            value = atoi(optarg);
            addParm(PARM_AGG, "agg", value);
            break;
        case 'g':
        {
            //A_UINT32 rowIndex=0,rateBitMask=0;
            //A_UINT8 rateMask[25];
            //A_UINT32 rMask[2] = {0};
            //A_UINT32 rMask11AC[6] = {0};

            /* let user input index of rateTable instead of string parse */
            //dataRate = value = rateValid(atoi(optarg), freq);
            dataRate = value = atoi(optarg);

#ifdef USE_TLV2
            tlv2AddParms(2, PARM_RATEBITINDEX, value);
#else

            if ( value >= MASK_RATE_MAX ) {   //183
                printf("Invalid Index\n");
                return 0;
            }

            if (( value == MASK_RATE_MAX /*183 */) && (sizeof(rateBitMask) <= sizeof(rateMask)))  // Sweep all rates
            {
                memset(rateMask,0,sizeof(rateMask));
                snprintf((char*)rateMask,sizeof(rateMask),"%s%d","rateMask",0);
                rateBitMask = 0xFFFFFF00;
                setRateMask(rMask, 0, rateMask, rateBitMask);

                memset(rateMask,0,sizeof(rateMask));
                snprintf((char*)rateMask,sizeof(rateMask),"%s%d","rateMask",1);
                rateBitMask = 0xFFFFFFFF;
                setRateMask(rMask, 1, rateMask, rateBitMask);

                memset(rateMask,0,sizeof(rateMask));
                snprintf((char*)rateMask,sizeof(rateMask),"%s%d","rateMask11AC",0);
                rateBitMask = 0xFF3FF1FF;
                setRateMask(rMask11AC, 0, rateMask, rateBitMask);

                memset(rateMask,0,sizeof(rateMask));
                snprintf((char*)rateMask,sizeof(rateMask),"%s%d","rateMask11AC",1);
                rateBitMask = 0xF3FF1FF3;
                setRateMask(rMask11AC, 1, rateMask, rateBitMask);

                memset(rateMask,0,sizeof(rateMask));
                snprintf((char*)rateMask,sizeof(rateMask),"%s%d","rateMask11AC",2);
                rateBitMask = 0x3FF3FF3F;
                setRateMask(rMask11AC, 2, rateMask, rateBitMask);

                memset(rateMask,0,sizeof(rateMask));
                snprintf((char*)rateMask,sizeof(rateMask),"%s%d","rateMask11AC",3);
                rateBitMask = 0x3BF;
                setRateMask(rMask11AC, 3, rateMask, rateBitMask);

                rateBitMask = 0x3FFFFFFF;
                setRateMask(rMask11AC, 4, (A_UINT8 *)"rtMaskAC4x4", rateBitMask);

                // cascade
                rateBitMask = 0x000FFFFF;
                setRateMask(rMask11AC, 5, (A_UINT8 *)"rtMaskAC160", rateBitMask);

#ifdef USE_TLV2
                //tlv2AddParms(4, PARM_RATEMASK, 2, 0, rMask);
                //tlv2AddParms(4, PARM_RATEMASK11AC, 5, 0, rMask11AC);
#endif //USE_TLV2
           }
            else {
            	// Map ratemask to the bitmask value..
            	rateIndexToArrayMapping (value, &rowIndex, &rateBitMask, &is11ACRate);

            	printf("rowIndex %d bitMask %d\n",rowIndex,rateBitMask);

            	memset(rateMask,0,sizeof(rateMask));

            	if (is11ACRate == FALSE ) {
            		snprintf((char*)rateMask,sizeof(rateMask),"%s%d","rateMask",rowIndex);
                        setRateMask(rMask, rowIndex, rateMask, rateBitMask);

            		if (( rowIndex != 0) && (sizeof(rateBitMask) <= sizeof(rateMask))) {
            			//NOTE: This is required as on the firmware default rateMask0 is set to rate 11
            		    memset(rateMask,0,sizeof(rateMask));
            		    snprintf((char*)rateMask,sizeof(rateMask),"%s%d","rateMask",0);
            		    value = 0;
                            setRateMask(rMask, 0, rateMask, value);
            		}
#ifdef USE_TLV2
            		//tlv2AddParms(4, PARM_RATEMASK, 2, 0, rMask);
#endif //USE_TLV2
            	}
            	else {
            		if (( value <= 152) && (sizeof(rateBitMask) <= sizeof(rateMask))) {
            			snprintf((char*)rateMask,sizeof(rateMask),"%s%d","rateMask11AC",rowIndex);
                        setRateMask(rMask11AC, rowIndex, rateMask, rateBitMask);
            		}
            		else if ( value <= ATH_RATE_VHT80_NSS4_MCS9 ) {
                        is4x4Rate = TRUE;
                        setRateMask(rMask11AC, 4, (A_UINT8 *)"rtMaskAC4x4", rateBitMask);
            		}
            		else {
                        setRateMask(rMask11AC, 4, (A_UINT8 *)"rtMaskAC160", rateBitMask);
            		}
            		//NOTE: This is required as on the firmware default rateMask0 is set to rate 11
            		memset(rateMask,0,sizeof(rateMask));
                	snprintf((char*)rateMask,sizeof(rateMask),"%s%d","rateMask",0);
                	value = 0;
                        setRateMask(rMask, 0, rateMask, value);
#ifdef USE_TLV2
                        //tlv2AddParms(4, PARM_RATEMASK, 2, 0, rMask);
                        //tlv2AddParms(4, PARM_RATEMASK11AC, 5, 0, rMask11AC);
#endif //USE_TLV2
            	}
            }
#endif
        }
            break;
        case 'Q':
#ifdef USE_TLV2
            value = atoi(optarg);
            rate_Bw = value;
            //addParm(PARM_RATE, "rateBw", value);
#endif
            break;

        case 'j':
#ifdef USE_TLV2
            value = atoi(optarg);
            phy165Mode = value;
#endif
            break;


        case 'h':
        {
#ifdef USE_TLV2
            value = atoi(optarg);
            if (value < 0)
            {
                printf("Negative power not supported!\n");
                return 0;
            }
            addParm(PARM_TXPOWER, "txpwr", value);
#else
            //value = TPC_TX_PWR;
            goto _txpwrProcessing;
#endif
        }
#ifdef USE_TLV2
        break;
#endif
        case 'H':
        {
            //value = TPC_TGT_PWR;
            goto _txpwrProcessing;
        }

        _txpwrProcessing:
        {
            int txPowerAsInt = atoi(optarg);
           /*
            *Get tx power from user.  This is given in the form of a number
            *that's an integer and in 0.5dbm unit
            */
            value = (A_UINT32)(txPowerAsInt);
           /*
            *Let's set all the pwrGainStart,pwrGainEnd to same txPower values
            *Currently we only get one power value
            */
            setPowerPerRateIndex(is11ACRate,value);
	}
            break;
        case 'I':
        {
            value = TPC_FORCED_GAIN;
            addParm(PARM_TPCM, "tpcm", value);
            value = (A_UINT32)(atoi(optarg)); //txPwr

            //Let's set all the pwrGainStart,pwrGainEnd to same txPower values
            //Currently we only get one power value
            setPowerPerRateIndex(is11ACRate,value);
        }
            break;
        case TCMD_TX_FORCED_GAIN:
        {
            value = TPC_TX_FORCED_GAIN;
            addParm(PARM_TPCM, "tpcm", value);

            value = (A_UINT32)(atoi(optarg)); //txPwr
            //printf("TPC_TX_FORCED_GAIN=%d, value=%d ==>0x%x\n", TPC_TX_FORCED_GAIN, value, value);
            //Let's set all the pwrGainStart,pwrGainEnd to same txPower values
            //Currently we only get one power value
            setPowerPerRateIndex(is11ACRate,value);
        }
            break;
        case 'q':
            value = antValid(atoi(optarg));
            addParm(PARM_ANTENNA, "antenna", value);
            break;
        case 'z':
            value = pktSzValid(atoi(optarg));
            packet_size = value;
            //addParm(PARM_PKTSZ, "pktSz", value);
            break;
        case 'e':
            value = atoi(optarg);
            addParm(PARM_TXPATTERN, "txPattern", value);
            break;
        case 'c':
            value = atoi(optarg);
            addParm(PARM_CHAINMASK, "txChain0", value);
            break;
        case 'C':
            value = atoi(optarg);
            addParm(PARM_CHAINMASK, "rxChain", value);
            break;
        case 'b':
            {
                //A_UINT8 mac[ATH_MAC_LEN]; // Created "bssid" above

                if (ath_ether_aton(optarg, bssid[is2GHz]) != A_OK) {
                    A_ERR(-1, "Invalid bssid address format! \n");
                }
                //addArrayParm(PARM_BSSID, "bssid", bssid, ATH_MAC_LEN);
                printf("JLU: tcmd: set bssid 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x\n",
                        bssid[is2GHz][0], bssid[is2GHz][1], bssid[is2GHz][2], bssid[is2GHz][3], bssid[is2GHz][4], bssid[is2GHz][5]);
                break;
            }
        case 'd':
            {
                //A_UINT8 mac[ATH_MAC_LEN]; //Created "srcMac" above

                if (ath_ether_aton(optarg, srcMac[is2GHz]) != A_OK) {
                    A_ERR(-1, "Invalid srcmac address format! \n");
                }
                //addArrayParm(PARM_TXSTATION, "txStation", srcMac, ATH_MAC_LEN);
                printf("JLU: tcmd: set srcmac 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x\n",
                        srcMac[is2GHz][0], srcMac[is2GHz][1], srcMac[is2GHz][2], srcMac[is2GHz][3], srcMac[is2GHz][4], srcMac[is2GHz][5]);
                break;
            }
        case 'D':
            {
                //A_UINT8 mac[ATH_MAC_LEN]; // Created "dstMac" above

                if (ath_ether_aton(optarg, dstMac[is2GHz]) != A_OK) {
                    A_ERR(-1, "Invalid dstmac address format! \n");
                }
                //addArrayParm(PARM_RXSTATION, "rxStation", dstMac, ATH_MAC_LEN);
                printf("JLU: tcmd: set dstmac 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x\n",
                        dstMac[is2GHz][0], dstMac[is2GHz][1], dstMac[is2GHz][2], dstMac[is2GHz][3], dstMac[is2GHz][4], dstMac[is2GHz][5]);

                /* if destination MAC address is present then set broadcast to off */
                //value = 0;
                //addParm(PARM_BROADCAST, "broadcast", value);
                break;
            }
        case 'r':
            if (!strcmp(optarg, "promis")) {
                value = TCMD_CONT_RX_PROMIS;
                printf(" Its cont Rx promis mode \n");
            } else if (!strcmp(optarg, "filter")) {
                value = TCMD_CONT_RX_FILTER;
                printf(" Its cont Rx  filter  mode \n");
            } else if (!strcmp(optarg, "report")) {
                printf(" Its cont Rx report  mode \n");
                value = TCMD_CONT_RX_REPORT;
            } else {
                printf("Error\n");
                usage();
                return 0;
            }
#ifdef USE_TLV2
		if (value == TCMD_CONT_RX_REPORT){
			tlv2CreateCmdHeader(CMD_RXSTATUS);
			tlv2AddParms(2, PARM_STOPRX,1);
		}
	    else{
            tlv2CreateCmdHeader(CMD_RX);
            tlv2AddParms(2, PARM_RXMODE, value);
		}
            tlv2Construct = TRUE;
            miscFlags |= (RX_STATUS_PER_RATE_MASK | PROCESS_RATE_IN_ORDER_MASK);
#else
            tlvConstruct = TRUE;
            //createCommand(_OP_RX);
            //addParameterToCommand((A_UINT8 *)"rxMode",(A_UINT8 *)&value);
#endif //USE_TLV2
			if(value == TCMD_CONT_RX_PROMIS){
			A_UINT8 mac[ATH_MAC_LEN]={0};
			addArrayParm(PARM_STAADDR, "addr", mac, ATH_MAC_LEN);
		}
            opCode = _OP_RX;
            break;
        case 'k':
            value = atoi(optarg);
            addParm(PARM_ACK, "ack", value);
            break;
        case 'K':
            value = atoi(optarg);
#ifdef USE_TLV2
            tlv2AddParms(2, PARM_BROADCAST, value);
#else
            /*if ( opCode == _OP_TX )
                addParameterToCommand((A_UINT8 *)"broadcast",(A_UINT8 *)&value);
            else
                addParameterToCommand((A_UINT8 *)"bc",(A_UINT8 *)&value);*/
#endif //USE_TLV2
           break;
        case TCMD_SET_RX_LPL:
            value = atoi(optarg);
            addParm(PARM_LPL, "lpl", value);
            break;
        case 'l':
            printf("NOT supported\n");
            break;
        /*case 'x':
            printf("In pm..\n");
            tlvConstruct = TRUE;
            ret = createCommand(_OP_PM);
            opCode = _OP_PM;

            if (ret == FALSE)
               printf("Error in creating command _OP_PM\n");

            if (!strcmp(optarg, "wakeup")) {
                value = TCMD_PM_WAKEUP;
            } else if (!strcmp(optarg, "sleep")) {
                value = TCMD_PM_SLEEP;
            } else if (!strcmp(optarg, "deepsleep")) {
                value = TCMD_PM_DEEPSLEEP;
            } else {
                printf("Error\n");
                usage();
                return 0;
            }

            ret = addParameterToCommand((A_UINT8 *)"mode",(A_UINT8 *)&value);
            if (ret == FALSE)
               printf("Error in adding param mode to command _OP_PM\n");

            break;*/
        case 'R':
            printf("NOT supported\n");
            break;
        case 'V':
            printf("NOT supported\n");
            break;
		case 'T':
			{	
#ifdef USE_TLV2
				A_UINT8 mac[ATH_MAC_LEN]={};
                if (ath_ether_aton(optarg, mac) != A_OK) {
                    A_ERR(-1, "Invalid mac address format! \n");
                }
                addArrayParm(PARM_STAADDR, "addr", mac, ATH_MAC_LEN);
                printf("JLU: tcmd: FilterMacAddress 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x\n",
                        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
#endif
				break;
            }
		
		
        case 's':
            {
                A_UINT8 mac[ATH_MAC_LEN];

#ifdef USE_TLV2
                if(tlv2Construct == FALSE)
                {
               	    tlv2CreateCmdHeader(CMD_RX);
                    tlv2Construct = TRUE;
                    opCode = _OP_RX;
                }
#else
                /*if ( tlvConstruct == FALSE )
                {
                    createCommand(_OP_RX);
                    opCode = _OP_RX;
                    tlvConstruct = TRUE;
                }*/
#endif //USE_TLV2
                if ( rxSetMac == FALSE ) {
                    value = TCMD_CONT_RX_SETMAC;
                    addParm(PARM_RXMODE, "rxMode", value);
                    rxSetMac = TRUE;
                }

                if (ath_ether_aton(optarg, mac) != A_OK) {
                    A_ERR(-1, "Invalid mac address format! \n");
                }
                addArrayParm(PARM_STAADDR, "addr", mac, ATH_MAC_LEN);

                printf("JLU: tcmd: setmac 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x\n",
                        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                break;
            }

	/*case TCMD_SET_NV_MAC_ADDRESS:
            {
	        A_UINT8 mac[ATH_MAC_LEN];
		if (ath_ether_aton(optarg, mac) != A_OK) {
                    A_ERR(-1, "Invalid mac address format! \n");
                }
		printf("JLU: tcmd: setmac 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x\n",
                        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

                printf("TCMD_SET_NV_MAC_ADDRESS TLV ..\n");
		createCommand(_OP_GENERIC_NART_CMD);
		opCode = _OP_GENERIC_NART_CMD;
		value = NV_SET_MAC_ADDR_CMD_ID;
		addParameterToCommand((A_UINT8 *)"commandId",(A_UINT8 *)&value);
		value = 11;
		addParameterToCommand((A_UINT8 *)"param1",(A_UINT8 *)&value);
		value = 0;
		addParameterToCommand((A_UINT8 *)"param2",(A_UINT8 *)&value);
		buf[0] = 0x01;
		buf[1] = 0x08;
		buf[2] = 0x04;
		buf[3] = 0x06;
		buf[4] = 1;
		memcpy(&buf[5],  mac, 6);
		addParameterToCommandWithLen((A_UINT8 *)"data",(A_UINT8 *)buf, 11);
		respNeeded = TRUE;
		break;
            }
	case  TCMD_GET_TX_OFFLINE_POWER:
		//./attestcmd --gettxpwr txfreq mode bflag txchain nss
		value = atoi(argv[2]);
		buf[0] = value &0xff;
		buf[1] = (value >> 8) &0xff;
		buf[2] = atoi(argv[3])&0xff;
	        value = 0x8100;//atoi(argv[4]);
		memcpy(&buf[3], (char *)&value, 4);

		buf[7] = atoi(argv[5])&0xf;
		buf[8] = atoi(argv[6])&0xf;
		createCommand(_OP_GENERIC_NART_CMD);
		opCode = _OP_GENERIC_NART_CMD;
		printf("TCMD_GET_TX_OFFLINE_POWER, freq=%d, mode=%d, rcflags=%x, txmask=%d, nss=%d\n", value, buf[2], atoi(argv[4]), buf[7], buf[8]);
		value = GET_TX_OFFLINE_POWER_ID;
	        addParameterToCommand((A_UINT8 *)"commandId",(A_UINT8 *)&value);
		addParameterToCommandWithLen((A_UINT8 *)"data",(A_UINT8 *)buf, 9);
		respNeeded = TRUE;
		break;

	case TCMD_GET_NV_MAC_ADDRESS:
            {
		printf("TCMD_GET_NV_MAC_ADDRESS TLV ..\n");
		createCommand(_OP_GENERIC_NART_CMD);
		opCode = _OP_GENERIC_NART_CMD;
		value = NV_GET_MAC_ADDR_CMD_ID;
		addParameterToCommand((A_UINT8 *)"commandId",(A_UINT8 *)&value);
		respNeeded = TRUE;
		break;
            }

	
	//    {"nvbdcommit", 0, NULL, TCMD_COMMIT_NV_BDATA},
        //    {"nvtype", 1, NULL, TCMD_COMMIT_NV_BDATA_TYPE},
	//    {"nvsegment", 1, NULL, TCMD_COMMIT_NV_BDATA_SEGMENT},
	//    {"nvcompressed", 1, NULL, TCMD_COMMIT_NV_BDATA_COMPRESSED},
	

	case TCMD_COMMIT_NV_BDATA:
            {
		printf("TCMD_COMMIT_NV_BDATA TLV ..\n");
		createCommand(_OP_GENERIC_NART_CMD);
		opCode = _OP_GENERIC_NART_CMD;
		value = M_WRITE_FW_BD_ID;

		addParameterToCommand((A_UINT8 *)"commandId",(A_UINT8 *)&value);
		respNeeded = TRUE;
		break;
	    }
	case TCMD_COMMIT_NV_BDATA_TYPE:
	    {
		    printf("TCMD_COMMIT_NV_BDATA_TYPE TLV ..\n");
		value = atoi(optarg) & 0xf; //0: EEPROM; 1:otp
		addParameterToCommand((A_UINT8 *)"param1",(A_UINT8 *)&value);
	        break;
	    }
	case TCMD_COMMIT_NV_BDATA_SEGMENT:
	    {
		    printf("TCMD_COMMIT_NV_BDATA_SEGMENT TLV ..\n");
		value = atoi(optarg) & 0xffff; //0: WHOLE BOARD SPACE
		addParameterToCommand((A_UINT8 *)"param2",(A_UINT8 *)&value);
		break;
	    }
	    case TCMD_COMMIT_NV_BDATA_COMPRESSED:
	    {
		        printf("TCMD_COMMIT_NV_BDATA_COMPRESSED TLV ..\n");
		value = atoi(optarg) & 0xf; // 0: UNCOMPRESS
		addParameterToCommand((A_UINT8 *)"param3",(A_UINT8 *)&value);
		break;
            }*/

        case 'X':
            {
                A_UINT8 mac[ATH_MAC_LEN];

#ifdef USE_TLV2
                if(tlv2Construct == FALSE)
                {
                	tlv2CreateCmdHeader(CMD_RX);
                	tlv2Construct = TRUE;
                    opCode = _OP_RX;
                }
#else
                /*if ( tlvConstruct == FALSE )
                {
                    createCommand(_OP_RX);
                    opCode = _OP_RX;
                    tlvConstruct = TRUE;
                }*/
#endif //USE_TLV2
                if ( rxSetMac == FALSE ) {
                    value = TCMD_CONT_RX_SETMAC;
                    addParm(PARM_RXMODE, "rxMode", value);
                    rxSetMac = TRUE;
                }

                if (ath_ether_aton(optarg, mac) != A_OK) {
                    A_ERR(-1, "Invalid mac address format! \n");
                }
                addArrayParm(PARM_BSSID, "bssid", mac, ATH_MAC_LEN);

                printf("JLU: tcmd: setbssid 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x\n",
                        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                break;
            }

        case 'u':
            {
                value = atoi(optarg) & 0xff;
                addParm(PARM_AIFSN, "aifsn", value);
                printf("AIFS:%d\n", value);
            }
            break;
        case 'U':
            {
                int dutyCycle, frameLength;
                float txFrameDurPercent;

                frameLength = (A_UINT32) (((TX_FRAME_DURATION + 20) *2 * ActualDataRate[dataRate]) / 8.0);
                txFrameDurPercent = 1.0;
                if (frameLength > MAX_FRAME_LENGTH) {
                    txFrameDurPercent = ((float) MAX_FRAME_LENGTH) / (float) frameLength;
                    frameLength = MAX_FRAME_LENGTH;
                }

                dutyCycle = atoi(optarg) & 0xff;
                if (dutyCycle == 0) {
                    dutyCycle = 1;
                }
                if(dutyCycle > 99) {
                    dutyCycle = 99;
                }

                value = computeNumSlots(dutyCycle,dataRate,txFrameDurPercent);
                addParm(PARM_AIFSN, "aifsn", value);
                addParm(PARM_PKTSZ, "pktSz", frameLength);
            }
            break;
        case 'a':
            value = TRUE;
            addParm(PARM_ENANI, "enANI", value);
            break;
        case 'W':
#ifdef USE_TLV2
            value = atoi(optarg);
            A_UINT8 gi, ltf;
            if (value)
            {
                gi = (value & 0xF);
                ltf = ((value & 0xF0) >> 4);
                printf("GI level is %u\nLTF Mode is %u\nInput value is %u\n", gi, ltf, value);
                if (((gi >=1) && (gi <= 4)) && ((ltf >=0) && (ltf <= 3)))
                {
                    addParm(PARM_GI, "gI", value);
                }
                else
                {
                    printf ("\nInvalid option for GI\n");
                    usage();
                }
            }
            else
            {
                printf ("No GI set\n");
            }
#endif
            break;
        case 'A':
            miscFlags |= PAPRD_ENA_MASK;
            break;
        case 'Y':
            miscFlags |= DESC_STBC_ENA_MASK;
            break;
        case 'Z':
            miscFlags |= DESC_LDPC_ENA_MASK;
            break;
        case 'o':
            value = TRUE;
            addParm(PARM_SCRAMBLEROFF, "scramblerOff", value);
            break;
        case 'S':
            if (argc < 4)
            {
                usage();
                return 0;
            }
#ifdef USE_TLV2
            if ( tlv2Construct == FALSE )
            {
            	tlv2CreateCmdHeader(CMD_RX);
            	tlv2Construct = TRUE;
                opCode = _OP_RX;
            }
#else
            /*if ( tlvConstruct == FALSE )
            {
                createCommand(_OP_RX);
                tlvConstruct = TRUE;
                opCode = _OP_RX;
            }*/
#endif //USE_TLV2
            value = TCMD_CONT_RX_SET_ANT_SWITCH_TABLE;
            addParm(PARM_RXMODE, "rxMode", value);

            value = (unsigned int) atoi(argv[2]);
            addParm(PARM_ANTSWITCH1, "antswitch1", value);
            value = (unsigned int) atoi(argv[3]);
            addParm(PARM_ANTSWITCH2, "antswitch2", value);
            break;
        case 'w':
            value = 1;
            addParm(PARM_OTPWRITEFLAG, "otpWriteFlag", value);
            break;
        case 'E':
            {
                regDmn[0] = 0xffff&(strtoul(optarg, (char **)NULL, 0));
                regDmn[1] = 0xffff&(strtoul(optarg, (char **)NULL, 0)>>16);

#ifdef USE_TLV2
                tlv2AddParms(4, PARM_REGDMN, 2, regDmn);
#else
                //addParameterToCommand((A_UINT8 *)"regDmn0",(A_UINT8 *)&regDmn[0]);
                //addParameterToCommand((A_UINT8 *)"regDmn1",(A_UINT8 *)&regDmn[0]);
#endif //USE_TVL2
            }
            break;
        case 'B':
            {
                A_UINT8 btaddr[ATH_MAC_LEN];
                if (ath_ether_aton(optarg, btaddr) != A_OK) {
                    A_ERR(-1, "Invalid mac address format! \n");
                }
                addArrayParm(PARM_BTADDR, "btaddr", btaddr, ATH_MAC_LEN);

                printf("JLU: tcmd: setbtaddr 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x\n",
                        btaddr[0], btaddr[1], btaddr[2], btaddr[3], btaddr[4], btaddr[5]);
            }
            break;
        case TCMD_READ_THERMAL:
            {
               printf("TODO... readThermal need to support in TLV ..\n");
            }
            break;
        case TCMD_EEPROM_ERASE:
            {
#define EEPROM_READ_BLOCK_SIZE    4096
                A_UINT32 offset = 0;
                A_UINT16 blksize = EEPROM_READ_BLOCK_SIZE;
                A_INT8 data[EEPROM_READ_BLOCK_SIZE];
                A_UINT32 bdsize = 0;
                error = UTF_CMD_INIT(ifname,cmdReplyFunc_v2);

                if (error)
                {
                     printf("error in UTF_CMD_INIT %d\n", error);
                     exit (-1);
                }
                g_eeprom_size = 0;
                if((!checkEepromSize()) || (g_eeprom_size == 0))
                {
                     printf("Eeprom Size is invalid \n");
                     exit(-1);
                }
                bdsize = g_eeprom_size;

                memset(&data, -1, sizeof(data));
                while (offset + blksize <= bdsize)
                {
                    responseStatus = 0;
                    if((eepromWrite(offset, blksize, data, 0) == 0) || (responseStatus != 0))
                    {
                         printf("EEPROM ERASE FAILED\n");
                         exit(-1);
                    }
                    offset = offset + EEPROM_READ_BLOCK_SIZE;
                    if (offset >= bdsize)
                        break;

                    if ((offset + blksize) > bdsize)
                        blksize = bdsize - offset;
                }
                printf("EEPROM ERASE SUCCESS SIZE %d\n",bdsize);
                goto EXIT;
            }
            break;
            case TCMD_EEPROM_WRITE:
            {
                 printf("EEPROM WRITE\n");
                 error = UTF_CMD_INIT(ifname,cmdReplyFunc_v2);
                 if (error)
                 {
                     printf("error in UTF_CMD_INIT %d\n", error);
                     exit(-1);
                 }
                 g_eeprom_size = 0;
                 if((!checkEepromSize()) || (g_eeprom_size == 0))
                 {
                     printf("Eeprom Size is invalid \n");
                     exit(-1);
                 }
                 opCode = _OP_GENERIC_NART_CMD;
            }
            break;
            case TCMD_EEPROM_BLOCK_DATA:
            {
                 A_BOOL rc;
                 char fileName[_MAX_FILENAME_LEN];
                 A_UINT32 len = 0;
                 A_UINT32 offset = 0;
                 A_UINT16 blksize = EEPROM_READ_BLOCK_SIZE;
                 A_INT8 data[EEPROM_READ_BLOCK_SIZE];
                 A_UINT32 bdsize = 0;
                 bdsize = g_eeprom_size;
                 memset(&bdata,0,bdsize);
                 strlcpy(fileName, optarg,sizeof(fileName));
                 rc = readBinFile(fileName, (void*)&bdata[0], &len, bdsize);
                 if (!rc || (len != bdsize)) {
                      printf("Bd file not read correctly\n");
                      exit(0);
                 }
                 else
                 {
                      while (offset + blksize <= bdsize)
                      {
                           responseStatus = 0;
                           memcpy(&data,&bdata[offset],blksize);
                           if((eepromWrite(offset, blksize, data, 1) == 0) || (responseStatus != 0))
                           {
                               printf("EEPROM WRITE FAILED \n");
                               exit(-1);
                           }
                           offset = offset + EEPROM_READ_BLOCK_SIZE;
                           if (offset >= bdsize)
                                break;
                           if ((offset + blksize) > bdsize)
                                blksize = bdsize - offset;
                      }
                 }
                 printf("SUCCESS EERPROM WRITE - SIZE %d\n",bdsize);
                 goto EXIT;
             }
             break;
             case TCMD_EEPROM_DUMP:
             {
                A_BOOL rc;
                char fileName[_MAX_FILENAME_LEN];
                A_UINT32 len = 0;
                A_UINT32 offset = 0;
                A_UINT16 blksize = EEPROM_READ_BLOCK_SIZE;
                A_UINT32 bdsize = 0;
                error = UTF_CMD_INIT(ifname,cmdReplyFunc_v2);

                if (error)
                {
                     printf("error in UTF_CMD_INIT %d\n", error);
                     exit (-1);
                }
                g_eeprom_size = 0;
                if((!checkEepromSize()) || (g_eeprom_size == 0))
                {
                     printf("Eeprom Size is invalid \n");
                     exit(-1);
                }

                bdsize = g_eeprom_size;
                memset(&bdata,0,bdsize);
                while (offset + blksize <= bdsize)
                {
                    responseStatus = 0;
                    if((eepromRead(bdsize,offset,blksize) == 0) || (responseStatus != 0))
                    {
                         printf("EEPROM READ FAILED responseStatus %d\n",responseStatus);
                         exit(-1);
                    }
                    offset = offset + EEPROM_READ_BLOCK_SIZE;
                    if (offset >= bdsize)
                        break;

                    if ((offset + blksize) > bdsize)
                        blksize = bdsize - offset;
                }
                memset(fileName,'\0',20); //At the max filename will be of 20 characters
                strlcpy(fileName, ifname,sizeof(fileName));
                strlcat(fileName,"_ERead.bin",sizeof(fileName));
                rc = writeBinFile(fileName, (void*)&bdata[0], &len, bdsize);
                if (!rc) {
                    printf("File write failed\n");
                }

                printf("EEPROM DUMP SUCCESS - SIZE %d\n",bdsize);
                goto EXIT;

             }
             break;
        /*case TCMD_OTP_DUMP:
            {
#define OTP_READ_CMD_ID 175
               printf("otp dump TLV ..\n");
               respNeeded = TRUE;
               createCommand(_OP_GENERIC_NART_CMD);
               printf("otp dump create cmd ..\n");
               opCode = _OP_GENERIC_NART_CMD;
               value = OTP_READ_CMD_ID;
               addParameterToCommand((A_UINT8 *)"commandId",(A_UINT8 *)&value);
               printf("otp dump add nart cmd ..\n");
               value = 0x0;
               addParameterToCommand((A_UINT8 *)"param1",(A_UINT8 *)&value);
               addParameterToCommand((A_UINT8 *)"param2",(A_UINT8 *)&value);
            }
            break;

	case TCMD_OTP_RESET:
            {
	       respNeeded = TRUE;
               printf("otp reset TLV ..\n");
               createCommand(_OP_GENERIC_NART_CMD);
	       printf("otp dump create cmd ..\n");
               opCode = _OP_GENERIC_NART_CMD;
               value = OTP_RESET_CMD_ID;
               addParameterToCommand((A_UINT8 *)"commandId",(A_UINT8 *)&value);
               printf("otp reset add nart cmd ..\n");
               value = atoi(optarg);
		if (value  == 0 || value > 2)  {
			printf("invalid parameter: value=%d\n", value);
			return 0;
		}
		addParameterToCommand((A_UINT8 *)"param1",(A_UINT8 *)&value);

            }
            break;

        case TCMD_EFUSE_DUMP:
            {
//#define EFUSE_READ_CMD_ID 177
               printf("otp efuse dump TLV ..\n");
               respNeeded = TRUE;
               createCommand(_OP_GENERIC_NART_CMD);
               printf("otp efuse dump create cmd ..\n");
               opCode = _OP_GENERIC_NART_CMD;
               value = EFUSE_READ_CMD_ID;
               addParameterToCommand((A_UINT8 *)"commandId",(A_UINT8 *)&value);
               printf("efuse dump add nart cmd ..\n");
            }
            break;
        case TCMD_EFUSE_START:
            {
               efuseStart = atoi(optarg);
               value = efuseStart;
               addParameterToCommand((A_UINT8 *)"param2",(A_UINT8 *)&value);
            }
            break;
        case TCMD_EFUSE_END:
            {
               efuseEnd = atoi(optarg);
               value= efuseEnd - efuseStart;
               addParameterToCommand((A_UINT8 *)"param1",(A_UINT8 *)&value);
            }
            break;
        case TCMD_EFUSE_WRITE:
            {
               A_UINT8 efusedata[_MAX_LENGTH_2_READ];
               A_UINT8 i;
               A_UINT16 pos;
               A_UINT16 len;
               A_UINT8  *buf;

               printf("efuse write, num of argc=%d\n",argc);

//#define EFUSE_WRITE_CMD_ID 188
               if (argc >= 5) {
                   pos = (A_UINT16)(strtoul(argv[2], (char **)NULL,0));
                   len = (A_UINT16)(strtoul(argv[3], (char **)NULL,0));
                   printf("xpos:%d xlen:%d\n",pos,len);
                   if ((len + 4) == argc) {
                       printf("pos 0x%x len 0x%x\n",pos,len);
                       buf = efusedata;
                       for (i=0;i<len;i++)
                       {
                           printf("0x%lx ",strtoul(argv[4+i], (char **)NULL,0));
                           *buf++ = (A_UINT8)(strtoul(argv[4+i], (char **)NULL,0));
                       }
                       printf("\n");
                       printf("otp efuse write TLV ..\n");
                       respNeeded = TRUE;
                       createCommand(_OP_GENERIC_NART_CMD);
                       printf("otp efuse create cmd ..\n");
                       opCode = _OP_GENERIC_NART_CMD;
                       value = EFUSE_WRITE_CMD_ID;
                       addParameterToCommand((A_UINT8 *)"commandId",(A_UINT8 *)&value);
                       printf("efuse dump add nart cmd ..\n");
                       value = len;
                       addParameterToCommand((A_UINT8 *)"param1",(A_UINT8 *)&value);
                       value = pos;
                       addParameterToCommand((A_UINT8 *)"param2",(A_UINT8 *)&value);
                       addParameterToCommand((A_UINT8 *)"data",(A_UINT8 *)efusedata);

                    } else {
                        printf("incorrect(should be %d) number of parameters\n",len+2);
                    }
               }
            }
            break;
        case TCMD_OTP_WRITE:
		{
//#define OTP_WRITE_CMD_ID 174

			if (argc >= 5)
			{
				A_UINT8 macdata[6];
				A_UINT8 i;
				A_UINT16 mac_sec;
				A_UINT16 len;
				A_UINT8  *buf;
				tOptStream opt_str = {1, 1, {4, 6, {0x00, 0x00, 0x00, 0x00, 0x00, 0x01}}, 0};
				A_UINT8 *pdata = (A_UINT8 *)&opt_str;

				mac_sec = (A_UINT16)(strtoul(argv[2], (char **)NULL,0));
				printf("mac section id is (%d)\r\n", mac_sec);
				if (mac_sec != 4)
				{
					printf("mac section id(%d) is wrong\r\n", mac_sec);
					return 0;
				}
				//pos = strtoul(argv[5], (char **)NULL,0);
				len = (A_UINT16)(strtoul(argv[3], (char **)NULL,0));
				printf("mac len  is (%d)\r\n", len);
				if ((len + 4) != argc || len != 6)
				{
					printf("incorrect number of parameters\n");
					return 0;
				}

				buf = macdata;
				for (i=0;i<len;i++)
				{
					printf("0x%lx ",strtoul(argv[4+i], (char **)NULL,0));
					*buf++ = (A_UINT8)(strtoul(argv[4+i], (char **)NULL,0));
				}
				printf("\n otp stream  write mac section TLV ..\n");

				createCommand(_OP_GENERIC_NART_CMD);
				printf("otp efuse create cmd ..\n");
				opCode = _OP_GENERIC_NART_CMD;
				value = OTP_WRITE_CMD_ID;
				addParameterToCommand((A_UINT8 *)"commandId",(A_UINT8 *)&value);
				printf("otp write mac section add nart cmd ..\n");
				value = 0;
				addParameterToCommand((A_UINT8 *)"param2",(A_UINT8 *)&value);
				//value = mac_sec;
				//addParameterToCommand((A_UINT8 *)"param3",(A_UINT8 *)&value);

				value = sizeof(tOptStream);
				addParameterToCommand((A_UINT8 *)"param1",(A_UINT8 *)&value);
				memcpy((A_UINT8 *)opt_str.sec_mac.mac_addr, macdata, 6);
				opt_str.crc = computeCrc8(pdata, sizeof(tOptStream)-1);

				addParameterToCommand((A_UINT8 *)"data",(A_UINT8 *)&opt_str);

			}
			else {
				A_BOOL rc;
				A_UINT8 efusedata[_MAX_LENGTH_2_READ];
				char fileName[_MAX_FILENAME_LEN];
				A_UINT32 len;
				strlcpy(fileName, optarg,_MAX_FILENAME_LEN);
				if (!(rc = readBinFile(fileName, (void*)efusedata, &len, TC_CMDS_SIZE_MAX))) {
					printf("Test otp stream file not read correctly\n");
				} else {
					printf("otp stream file:%s len:%d\n",fileName,len);

					printf("otp stream  write TLV ..\n");
					respNeeded = TRUE;
					createCommand(_OP_GENERIC_NART_CMD);
					printf("otp stream create cmd ..\n");
					opCode = _OP_GENERIC_NART_CMD;
					value = OTP_WRITE_CMD_ID;
					addParameterToCommand((A_UINT8 *)"commandId",(A_UINT8 *)&value);
					printf("otp write add nart cmd ..\n");
					value = len;
					addParameterToCommand((A_UINT8 *)"param1",(A_UINT8 *)&value);
					value = 0;
					addParameterToCommand((A_UINT8 *)"param2",(A_UINT8 *)&value);

					addParameterToCommand((A_UINT8 *)"data",(A_UINT8 *)efusedata);
				}
			}
		}
            break;
        case TCMD_TX_STATUS:
 //#define M_TX_DATA_STATUS_CMD_ID 187
               printf("tx status ..\n");
               respNeeded = TRUE;
               createCommand(_OP_GENERIC_NART_CMD);
               printf("tx status create cmd ..\n");
               opCode = _OP_GENERIC_NART_CMD;
               value = M_TX_DATA_STATUS_CMD_ID;
               addParameterToCommand((A_UINT8 *)"commandId",(A_UINT8 *)&value);
               value = 0x1;
               addParameterToCommand((A_UINT8 *)"param1",(A_UINT8 *)&value);
               addParameterToCommand((A_UINT8 *)"param2",(A_UINT8 *)&value);
            break;
        case TCMD_CAL:
            printf("CAL\n");
            tlvConstruct = TRUE;
            createCommand(_OP_CAL);
            opCode = _OP_CAL;
            if (!strcmp(optarg, "sweep")) {
                value = TCMD_CAL_SWEEP;
            } else if (!strcmp(optarg, "verify")) {
                value = TCMD_CAL_VERIFY;
            } else if (!strcmp(optarg, "forcedGainIdx")) {
                value = TCMD_CAL_BY_FORCED_GAIN_IDX;
            } else if (!strcmp(optarg, "forcedTgtPwr")) {
                value = TCMD_CAL_BY_TGT_PWR;
            }else {
                printf("Error \n");
		usage();
                return 0;
            }
            respNeeded = FALSE;
            addParameterToCommand((A_UINT8 *)"_calParm_code_addrMode0",(A_UINT8 *)&value); // "borrow" the first field

            break;
	    case TCMD_EEPROM_DUMP:
		{
//#define M_EEEPROM_BLOCK_READ_ID 200
			printf("TCMD_EEPROM_DUMP TLV ..\n");
			createCommand(_OP_GENERIC_NART_CMD);
			printf("TCMD_EEPROM_DUMP cmd ..\n");
			opCode = _OP_GENERIC_NART_CMD;
			value = M_EEEPROM_BLOCK_READ_ID;
			addParameterToCommand((A_UINT8 *)"commandId",(A_UINT8 *)&value);
			respNeeded = TRUE;
		}
		break;
	    case TCMD_EEPROM_WRITE:
		{
//#define M_EEEPROM_BLOCK_WRITE_ID 201
			printf("TCMD_EEPROM_WRITE TLV ..\n");
			createCommand(_OP_GENERIC_NART_CMD);
			printf("TCMD_EEPROM_WRITE cmd ..\n");
			opCode = _OP_GENERIC_NART_CMD;
			value = M_EEEPROM_BLOCK_WRITE_ID;
			addParameterToCommand((A_UINT8 *)"commandId",(A_UINT8 *)&value);
			respNeeded = TRUE;
			bdata_offset = 0;
		}
		    break;

	    case TCMD_EEPROM_TOTAL_SIZE:
    		value = atoi(optarg);
		//printf("total size:%d\n",value);
    		addParameterToCommand((A_UINT8 *)"param1",(A_UINT8 *)&value);
    		break;
    	case	TCMD_EEPROM_OFFSET:
    		value = atoi(optarg);
    		bdata_offset = value;
    		addParameterToCommand((A_UINT8 *)"param2",(A_UINT8 *)&value);
    		break;
    	case TCMD_EEPROM_BLOCK_SIZE:
    		value = atoi(optarg);
    		bdata_block_size = value;
    		addParameterToCommand((A_UINT8 *)"param3",(A_UINT8 *)&value);
    		break;
	case TCMD_EEPROM_ERASE:
                {
                        printf("TCMD_EEPROM_ERASE TLV ..\n");
                        createCommand(_OP_GENERIC_NART_CMD);
                        printf("TCMD_EEPROM_ERASE cmd ..\n");
                        opCode = _OP_GENERIC_NART_CMD;
                        value = M_EEEPROM_BLOCK_ERASE_ID;
                        addParameterToCommand((A_UINT8 *)"commandId",(A_UINT8 *)&value);
                        respNeeded = TRUE;
                        bdata_offset = 0;
                }
                break;
	case TCMD_EEPROM_CHECK:
                {
                        printf("TCMD_EEPROM_CHECK TLV ..\n");
                        createCommand(_OP_GENERIC_NART_CMD);
                        printf("TCMD_EEPROM_CHECK cmd ..\n");
                        opCode = _OP_GENERIC_NART_CMD;
                        value = M_EEEPROM_BLOCK_CHECK_ID;
                        addParameterToCommand((A_UINT8 *)"commandId",(A_UINT8 *)&value);
                        respNeeded = TRUE;
                        bdata_offset = 0;
                }
                break;
	case TCMD_GET_CAL_BDATA:
		{
			printf("TCMD_GET_CAL_BDATA TLV ..\n");
			createCommand(_OP_GENERIC_NART_CMD);
			opCode = _OP_GENERIC_NART_CMD;
			value = M_READ_FW_BD_ID;
			addParameterToCommand((A_UINT8 *)"commandId",(A_UINT8 *)&value);
			respNeeded = TRUE;
			bdata_offset = 0;
		}
		break;
	case TCMD_GET_CAL_BDATA_SIZE:
		{
			printf("TCMD_GET_CAL_BDATA_SIZE TLV ..\n");
			createCommand(_OP_GENERIC_NART_CMD);
			opCode = _OP_GENERIC_NART_CMD;
			value = M_READ_FW_BD_SIZE_ID;
			addParameterToCommand((A_UINT8 *)"commandId",(A_UINT8 *)&value);
			respNeeded = TRUE;
			bdata_offset = 0;
		}
		break;
    	case TCMD_EEPROM_BLOCK_DATA:
		{
			A_BOOL rc;
			//A_UINT8 *pbdata = &bdata[0];
			//temp[]={0x00,0x00,0x00,0x00,0x00,0x00,0x70,0x8e,0xac,0xff,0xff,0xff,0xff,0xff,0xff,0xff};
			//A_UINT8 temp[]={0x00,0x00,0x00,0x00,0x00,0x00,0x70,0x8e,0xff,0xff,0xff,0xff,0xff,0xaa,0xff,0xff};

			char fileName[_MAX_FILENAME_LEN];
			A_UINT32 len;
			//memset(pbdata, 0xa5, BOARD_DATA_SZ_MAX);

			strlcpy(fileName, optarg,sizeof(fileName));
			rc = readBinFile(fileName, (void*)&bdata[0], &len, BOARD_DATA_SZ_MAX);
			if (!rc) {
				printf("Test bdata file not read correctly\n");
				exit(0);
			}
			else
			{
				A_UINT32 loop_i=0;
				printf("bdata file:%s len = %d, offset = %d, blocksize = %d\n",fileName,len, bdata_offset, bdata_block_size);
				printf("TCMD_EEPROM_BLOCK_DATA:\n");
				for (loop_i = 0; loop_i < bdata_block_size; loop_i++) {
					printf("0x%02x ",bdata[bdata_offset + loop_i]);
					if (((loop_i + 1)%16) == 0) printf("\n");
				}
				printf("\n");

				addParameterToCommandWithLen((A_UINT8 *)"data", (A_UINT8 *)(&bdata[bdata_offset]), (A_UINT16)bdata_block_size);
				//addParameterToCommandWithLen((A_UINT8 *)"data", (A_UINT8 *)(&temp[0]), bdata_block_size);
				//exit(0);
			}
		}
		    break;*/


	case TCMD_HWCAL:
			tlv2CreateCmdHeader(CMD_HWCAL);
			tlv2Construct = TRUE;
                useTLV1p0 = FALSE;

			if (!strcmp(optarg, "fullresetcal")) {
                value = HWCALOP_FULL_WHAL_RESET_CAL;
            } else if (!strcmp(optarg, "reset1")) {
                value = HWCALOP_WHAL_RESET_PART_1;
            } else if (!strcmp(optarg, "hwreset")) {
                value = HWCALOP_HW_RESET;
            } else if (!strcmp(optarg, "initbl")) {
                value = HWCALOP_PROGRAM_INI_TABLES;
            } else if (!strcmp(optarg, "reset2")) {
                value = HWCALOP_WHAL_RESET_PART_2;
            } else if (!strcmp(optarg, "fullcal")) {
                value = HWCALOP_FULL_CAL;
            } else if (!strcmp(optarg, "rxdco")) {
                value = HWCALOP_RXDCO;
            } else if (!strcmp(optarg, "rxfilt")) {
                value = HWCALOP_RXFILT;
            } else if (!strcmp(optarg, "rxiq")) {
                value = HWCALOP_RXIQ;
            } else if (!strcmp(optarg, "txiq")) {
                value = HWCALOP_TXIQ;
            } else if (!strcmp(optarg, "txcl")) {
                value = HWCALOP_TXCL;
            } else if (!strcmp(optarg, "pkdet")) {
                value = HWCALOP_PKDET;
            } else if (!strcmp(optarg, "reset3")) {
                value = HWCALOP_WHAL_RESET_PART_3;
            } else if (!strcmp(optarg, "noisefloor")) {
                value = HWCALOP_NOISEFLOOR;
            } else if (!strcmp(optarg, "dpd")) {
                value = HWCALOP_DPD;
            } else if (!strcmp(optarg, "lna")) {
                value = HWCALOP_LNA;
            } else {
                printf("Error \n");
                usage();
                return 0;
            }

		    tlv2AddParms(2, PARM_CALOP, value);
            break;

        case TCMD_CAL_LM:
            respNeeded = TRUE;
            useTLV1p0=FALSE;
            // create a command stream, with example tx parms
            pCmdStream = (TESTFLOW_CMD_STREAM_V2 *)createCmdRsp(CMD_TPCCAL, 2, PARM_RADIOID, 88);
            cmdStreamLen = (sizeof(TESTFLOW_CMD_STREAM_HEADER_V2) + pCmdStream->cmdStreamHeader.length);
            rCmdStream = (A_UINT8 *)pCmdStream;
            break;

        case TCMD_LOOP:
            value = atoi(optarg);
            tlv2AddParms(2, PARM_LOOPCNT, value);
            break;

	        case TCMD_CAL_XTAL:       //
                    respNeeded = TRUE;
                    useTLV1p0=FALSE;
                    value = atoi(optarg) & 0xff; //
                    //if (value > 8) value = 0;
                    //addParameterToCommand((A_UINT8 *)"param2",(A_UINT8 *)&value);

                    printf("sending TCMD_CAL_XTAL %d \n", value);
                    // create a command stream, with example tx parms
                    //pCmdStream = (TESTFLOW_CMD_STREAM_V2 *)createCmdRsp(CMD_XTALCALPROC, 6, PARM_CAPIN, 45, PARM_CAPOUT, 15, PARM_CTRLFLAG, value);
                    pCmdStream = (TESTFLOW_CMD_STREAM_V2 *)createCmdRsp(CMD_XTALCALPROC, 6, PARM_CAPIN, value+30, PARM_CAPOUT, value, PARM_CTRLFLAG, 0);
                    cmdStreamLen = (sizeof(TESTFLOW_CMD_STREAM_HEADER_V2) + pCmdStream->cmdStreamHeader.length);
                    rCmdStream = (A_UINT8 *)pCmdStream;
                    break;


	        /*case TCMD_LM_TXINIT:       //
                   // tlv2Construct = TRUE;
                    respNeeded = TRUE;
                    useTLV1p0=FALSE;
                    value = atoi(optarg); //

                    printf("sending TCMD_LM_TXINIT--> tx packet number = %d \n", value);

                    // create a command stream, with example txpackets parms

                    pCmdStream = (TESTFLOW_CMD_STREAM_V2 *)createCmdRsp(CMD_LMTXINIT, 2, PARM_TXNUMPACKETS, value);
                    cmdStreamLen = (sizeof(TESTFLOW_CMD_STREAM_HEADER_V2) + pCmdStream->cmdStreamHeader.length);
                    rCmdStream = (A_UINT8 *)pCmdStream;
                    printf("cmdStreamLen=%d, pCmdStream->cmdStreamHeader.length=%d, sizeof(TESTFLOW_CMD_STREAM_HEADER_V2)=%d ", cmdStreamLen, pCmdStream->cmdStreamHeader.length, sizeof(TESTFLOW_CMD_STREAM_HEADER_V2));

                    break;

	        case TCMD_LM_RXINIT:       //
                   // tlv2Construct = TRUE;
                    respNeeded = TRUE;
                    useTLV1p0=FALSE;
                    if (!strcmp(optarg, "promis")) {
                        value = TCMD_CONT_RX_PROMIS;
                        printf("sending TCMD_LM_RXINIT-->rxMode = %s \n", optarg);

                        // create a command stream, with example txpackets parms

                        pCmdStream = (TESTFLOW_CMD_STREAM_V2 *)createCmdRsp(CMD_LMRXINIT, 2, PARM_RXMODE, value);
                        cmdStreamLen = (sizeof(TESTFLOW_CMD_STREAM_HEADER_V2) + pCmdStream->cmdStreamHeader.length);
                        rCmdStream = (A_UINT8 *)pCmdStream;
                        printf("cmdStreamLen=%d, pCmdStream->cmdStreamHeader.length=%d, sizeof(TESTFLOW_CMD_STREAM_HEADER_V2)=%d ", cmdStreamLen, pCmdStream->cmdStreamHeader.length, sizeof(TESTFLOW_CMD_STREAM_HEADER_V2));
                    }
                    else
                        printf("try to input correct command for --lmrxinit promis!!!\n");
                    break;

            case TCMD_LM_CHANNEL_LIST:       //

                    respNeeded = TRUE;
                    //tlv2Construct = TRUE;
                    useTLV1p0=FALSE;
                    value = atoi(optarg) & 0xff; //

                    int itemNum;

                    if (argc == 4)
                        itemNum = atoi(argv[3]) & 0xff; //
                    else
                        itemNum = 1;
                    printf("sending TCMD_LM_CHANNEL_LIST FLAG = %x itemNum =%d \n", value, itemNum);

                    pCmdStream = (TESTFLOW_CMD_STREAM_V2 *)createCmdRsp(CMD_LMCHANNELLIST , 4 + 7 * 4 ,
                                            PARM_LM_FLAG, value, PARM_LM_ITEMNUM, itemNum, PARM_LM_FREQ, itemNum, 0, lm_freq,
                                            PARM_LM_FREQ2, itemNum, 0, lm_freq2, PARM_LM_CHAINMASK, itemNum, 0, lm_chainMask,
                                            PARM_LM_CBSTATE, itemNum, 0, lm_cbState, PARM_LM_PHYID, itemNum, 0, lm_phyId,
                                            PARM_LM_TXPOWER, itemNum, 0, lm_txPower, PARM_LM_RATEINDEX, itemNum, 0, lm_rateBitIdx);
                    cmdStreamLen = (sizeof(TESTFLOW_CMD_STREAM_HEADER_V2) + pCmdStream->cmdStreamHeader.length);
                    rCmdStream = (A_UINT8 *)pCmdStream;
                    printf("cmdStreamLen=%d, pCmdStream->cmdStreamHeader.length=%d, sizeof(TESTFLOW_CMD_STREAM_HEADER_V2)=%d ", cmdStreamLen, pCmdStream->cmdStreamHeader.length, sizeof(TESTFLOW_CMD_STREAM_HEADER_V2));

                    break;

            case TCMD_LM_QUERY:
                   // tlv2Construct = TRUE;
                    respNeeded = TRUE;
                    useTLV1p0=FALSE;
                    value = atoi(optarg) & 0xff; //

                    printf("sending TCMD_LM_QUERY (CMD_LMQUERY =  %d ) for cmdID: %d \n", CMD_LMQUERY, value);
                    pCmdStream = (TESTFLOW_CMD_STREAM_V2 *)createCmdRsp(CMD_LMQUERY, 2, PARM_LM_CMDID, value);
                    cmdStreamLen = (sizeof(TESTFLOW_CMD_STREAM_HEADER_V2) + pCmdStream->cmdStreamHeader.length);
                    rCmdStream = (A_UINT8 *)pCmdStream;
                    printf("cmdStreamLen=%d, pCmdStream->cmdStreamHeader.length=%d, sizeof(TESTFLOW_CMD_STREAM_HEADER_V2)=%d ", cmdStreamLen, pCmdStream->cmdStreamHeader.length, sizeof(TESTFLOW_CMD_STREAM_HEADER_V2));

                    break;

            case TCMD_LM_GO:
                respNeeded = TRUE;
                useTLV1p0=FALSE;
                value = atoi(optarg) & 0xff; //
                int timeOn, timeOff;
                printf("sending TCMD_LM_GO %d \n", value);
                if (argc == 5)
                {
                    timeOn = atoi(argv[3]);
                    timeOff = atoi(argv[4]);
                }
                else
                {
                    timeOn = 1000;
                    timeOff = 500;
                }

                pCmdStream = (TESTFLOW_CMD_STREAM_V2 *)createCmdRsp(CMD_LMGO, 6, PARM_LM_CMDID, value, PARM_TIMEON, timeOn, PARM_TIMEOFF, timeOff);
                cmdStreamLen = (sizeof(TESTFLOW_CMD_STREAM_HEADER_V2) + pCmdStream->cmdStreamHeader.length);
                rCmdStream = (A_UINT8 *)pCmdStream;
                printf("cmdStreamLen=%d, pCmdStream->cmdStreamHeader.length=%d, sizeof(TESTFLOW_CMD_STREAM_HEADER_V2)=%d ", cmdStreamLen, pCmdStream->cmdStreamHeader.length, sizeof(TESTFLOW_CMD_STREAM_HEADER_V2));

                break;*/

        case TCMD_CAL_PWR:
            respNeeded = TRUE;
            useTLV1p0=FALSE;
            fetchTxCALPwr(&numMeasuredPwr, measuredPwr);
            pCmdStream = (TESTFLOW_CMD_STREAM_V2 *)createCmdRsp(CMD_TPCCALPWR, 6, PARM_NUMMEASUREDPWR, numMeasuredPwr, PARM_MEASUREDPWR, numMeasuredPwr, 0, &(measuredPwr[0]));
            cmdStreamLen = (sizeof(TESTFLOW_CMD_STREAM_HEADER_V2) + pCmdStream->cmdStreamHeader.length);
            rCmdStream = (A_UINT8 *)pCmdStream;
            printf("sending %d meas len %d with enve %zu, %d %d %d %d %d\n", numMeasuredPwr, cmdStreamLen, sizeof(*pCmdStream), measuredPwr[0], measuredPwr[1], measuredPwr[2], measuredPwr[3],measuredPwr[4]);
            printf("0x%x 0x%x 0x%x\n", rCmdStream[0], rCmdStream[1], rCmdStream[2]);
            //prtCmdStream((A_UINT8 *)rCmdStream, (A_UINT32) cmdStreamLen);
            break;

        case TCMD_CHAIN_MASK:
            value = atoi(optarg);
            tlv2AddParms(2, PARM_CHAINMASK, value);
        	break;

        case TCMD_LOOPBACK:
            value = atoi(optarg);
            tlv2AddParms(2, PARM_LOOPBACK, value);
        	break;

        case TCMD_SAVE_CAL:
            value = atoi(optarg);
            tlv2AddParms(2, PARM_SAVECAL, value);
        	break;

        case TCMD_NF_CAL:
            value = atoi(optarg);
            tlv2AddParms(2, PARM_NOISEFLOORCAL, value);
        	break;

        case TCMD_SETUP:
        	printf("TCMD_SETUP");
            miscFlags |= TCMD_SETTUP_MASK;
            break;

        case TCMD_GO:
        	printf("TCMD_GO");
            miscFlags |= TCMD_GO_MASK;
        	break;
        case TCMD_RSSI_AVG_DISABLE:
            printf("TCMD_RSSI_AVG_DISABLE \n");
            miscFlags |= TCMD_RSSI_AVG_DISABLE_MASK;
            break;
        case TCMD_REG_READ:
#ifdef USE_TLV2
            tlv2CreateCmdHeader(CMD_REGREAD);
            param_addr_mode = 'r';
            tlv2Construct = TRUE;
#else
            /*createCommand(_OP_GENERIC_NART_CMD);
            printf("reg read ..\n");
            opCode = _OP_GENERIC_NART_CMD;
            value = REG_READ_CMD_ID;
            addParameterToCommand((A_UINT8 *)"commandId",(A_UINT8 *)&value);*/
#endif //USE_TLV2
            respNeeded = TRUE;
            break;

        case TCMD_REG_WRITE:
#ifdef USE_TLV2
            tlv2CreateCmdHeader(CMD_REGWRITE);
            param_addr_mode = 'r';
            param_value_mode = 'r';
            tlv2Construct = TRUE;
#else
            /*createCommand(_OP_GENERIC_NART_CMD);
            printf("reg write ..\n");
            opCode = _OP_GENERIC_NART_CMD;
            value = REG_WRITE_CMD_ID;
            addParameterToCommand((A_UINT8 *)"commandId",(A_UINT8 *)&value);*/
#endif //USE_TLV2
            break;

		case TCMD_MEM_READ:
			#ifdef USE_TLV2
			tlv2CreateCmdHeader(CMD_MEMREAD);
			printf("memory read ..\n");
			param_addr_mode = 'm';
			param_value_mode = 'm';
			tlv2Construct = TRUE;
			respNeeded = TRUE;
			#endif //USE_TLV2
			break;

		case TCMD_MEM_WRITE:
			#ifdef USE_TLV2
			tlv2CreateCmdHeader(CMD_MEMWRITE);
			printf("memory write ..\n");
			param_addr_mode = 'm';
			param_value_mode = 'm';
			tlv2Construct = TRUE;
			#endif //USE_TLV2
			break;

		case TCMD_NUMBYTES:
			#ifdef USE_TLV2
			value = atoi(optarg);
			if (value < 0) value = 0;
			if (value > 255) value = 255;
			A_UINT8 rvalue = value;
			param_value_array_size = value;
			param_value_offset = 0;
			printf("number of bytes = %d\n", rvalue);
			tlv2AddParms(2, PARM_NUMBYTES, rvalue);
			#endif //USE_TLV2
			break;

		case TCMD_ADDR:
			if (optarg[1] == 'x' || optarg[1] == 'X')
			{
			value = strtol(optarg, NULL, 16);
			}
			else
			{
			value = atoi(optarg);
			}
			printf("addr = 0x%x\n", value);
			#ifdef USE_TLV2
			if (param_addr_mode == 'm')
			tlv2AddParms(2, PARM_MEMADDRESS, value);
			else if (param_addr_mode == 'r')
			#endif //USE_TLV2
			addParm(PARM_REGADDRESS, "param1", value);
			break;

		case TCMD_VALUETYPE:
			value = atoi(optarg);
			if (value != 1 && value != 2 && value != 4)
			{
				printf("invalid valuetype. Must be 1, 2, or 4 (bytes)\n");
			}
			else
			{
				printf("valuetype = %d\n", value);
				param_valuetype = value;
				tlv2AddParms(2, PARM_VALUETYPE, value);
			}
			break;

		case TCMD_VALUE:
			if (optarg[1] == 'x' || optarg[1] == 'X')
			{
				if (param_value_mode == 'm')
					value = strtoul(optarg, NULL, 16);
				else
					value = strtol(optarg, NULL, 16);
			}
			else
			{
				value = atoi(optarg);
			}
			printf("value = 0x%08x\n", value);
			#ifdef USE_TLV2
			if (param_value_mode == 'm')
			{
				int tt;
				for (tt = 0; tt < param_valuetype; tt++) param_value_array[param_value_offset+tt] = (value >> (8*(param_valuetype-tt-1))) & 0x000000FF;
				param_value_offset += param_valuetype;
				if (param_value_array_size <= param_value_offset)
				{
					printf("adding array param...\n");
					tlv2AddParms(4, PARM_MEMVALUE, param_value_array_size, 0, param_value_array);
				}
			}
			else //if (param_value_mode == 'r')
			#endif //USE_TLV2
				addParm(PARM_REGVALUE, "param2", value);
			break;

		/*case TCMD_DPD_TUNING_LOOPBK_TIMINGIDX:

#ifdef USE_TLV2
            tlv2CreateCmdHeader(CMD_DPDLOOPBACKTIMING);
            //tlv2AddParms(6, PARM_DPDTIMINGIDX, value1, PARM_DPDLOOPBACKATTN, value2, PARM_GLUTIDX, value3);
//            tlv2AddParms(2, PARM_DPDTIMINGIDX, 20);
            tlv2AddParms(6, PARM_DPDTIMINGIDX, 17, PARM_DPDLOOPBACKATTN, 3, PARM_GLUTIDX, 1);
            tlv2Construct = TRUE;
            printf("TCMD_DPD_TUNING_LOOPBK_TIMINGIDX:\n");
            //tlv2AddParms(4, PARM_DPDLOOPBACKATTN, value1, PARM_GLUTIDX, value2);
#endif
                break;

		case TCMD_DPD_TUNING_LOOPBK_ATTEN:
#ifdef USE_TLV2
            tlv2CreateCmdHeader(CMD_DPDLOOPBACKATTN);
            tlv2AddParms(4, PARM_DPDLOOPBACKATTN, 3, PARM_GLUTIDX, 2);
            tlv2Construct = TRUE;
#endif
                break;

	       case TCMD_DPD_TUNING_TRAINING_QUALITY:
#ifdef USE_TLV2
			tlv2CreateCmdHeader(CMD_DPDTRAININGQUALITY);
                        tlv2AddParms(2, PARM_PHYID, 3);
                        printf("TCMD_DPD_TUNING_TRAINING_QUALITY:\n");
			tlv2Construct = TRUE;
			respNeeded = TRUE;
#endif //USE_TLV2
               break;

	       case TCMD_DPD_TUNING_AGC2PWR:
#ifdef USE_TLV2
			tlv2CreateCmdHeader(CMD_DPDAGC2PWR);
                        printf("TCMD_DPD_TUNING_AGC2PWR:\n");
                        tlv2AddParms(2, PARM_PHYID, 3);
			tlv2Construct = TRUE;
			respNeeded = TRUE;
#endif //USE_TLV2
               break;*/

	    case '?':
            break;

        default:
            usage();
        }
    }

    if(opCode == _OP_GENERIC_NART_CMD)
    {
        printf("Invalid Command - Refer Usage\n");
        exit(-1);
    }

    printf("opCode %d, flags 0x%x\n", opCode, miscFlags);
    if (miscFlags && ((opCode == _OP_TX) || (opCode == _OP_RX)))
    {
        addParm(PARM_FLAGS, "flags", miscFlags);
	    miscFlags = 0;
    }
#ifdef USE_TLV2
    if (opCode == _OP_TX)
    {
        addParm(PARM_PKTSZ, "pktSz", packet_size);
    }
    if ((opCode == _OP_TX) || (opCode == _OP_RX))
    {
        addParm(PARM_RATE, "rateBw", rate_Bw);

        if((rate_Bw == 18) || (rate_Bw == 19)||(rate_Bw == 21))
        {
          tlv2AddParms(2, PARM_CTRLFLAG, phy165Mode);
        }

        tlv2AddParms(2, PARM_WIFISTANDARD, 2); // TODO: 2 - WiFiStandard_Legacy11AX
    }
    /* In firmware, buffer allocation for stats happens based on rateMask[0], so sending default rateMask */
    if (opCode == _OP_RX)
    {
        tlv2AddParms(4, PARM_RATEMASK, 2, 0, rtMask);
    }

    /* Send unique bssid, srcMac and dstMac for 2G and 5G, default addresses will be sent if user doesn't provide them */
    if (opCode == _OP_TX)
    {
        addArrayParm(PARM_BSSID, "bssid", bssid[is2GHz], ATH_MAC_LEN);
        addArrayParm(PARM_TXSTATION, "txStation", srcMac[is2GHz], ATH_MAC_LEN);
        addArrayParm(PARM_RXSTATION, "rxStation", dstMac[is2GHz], ATH_MAC_LEN);
        /* if destination MAC address is present then set broadcast to off */
        value = 0;
        addParm(PARM_BROADCAST, "broadcast", value);
    }
#endif //USE_TLV2
    //printf("out %d, 0x%x 0x%x 0x%x\n", cmdStreamLen, rCmdStream[0], rCmdStream[1], rCmdStream[2]);
    replyReceived = FALSE;
    if (TRUE == useTLV1p0) {
		printf("TLV1p0\n");
		//commandComplete(&rCmdStream, &cmdStreamLen);
		//error = UTF_CMD_INIT(ifname,cmdReplyFunc);
    } else { // for now TLV2p0
        printf("TLV2p0\n");
        if (tlv2Construct == TRUE)
        {
	        pCmdStream = (TESTFLOW_CMD_STREAM_V2 *) tlv2CompleteCmdRsp();
	        cmdStreamLen = (sizeof(TESTFLOW_CMD_STREAM_HEADER_V2) + pCmdStream->cmdStreamHeader.length);
	        rCmdStream = (A_UINT8 *) pCmdStream;
        }
        error = UTF_CMD_INIT(ifname,cmdReplyFunc_v2);
    }

    if (error)
    {
        printf("error in UTF_CMD_INIT %d\n", error);
        exit (-1);
    }

    printf("copying %d, 0x%x 0x%x 0x%x\n", cmdStreamLen, rCmdStream[0], rCmdStream[1], rCmdStream[2]);
    memcpy(&(buf[STREAM_SHIFT]),rCmdStream,cmdStreamLen);
    printf("sending %d, 0x%x 0x%x 0x%x\n", cmdStreamLen, rCmdStream[0], rCmdStream[1], rCmdStream[2]);
    UTF_CMD_SEND(buf,cmdStreamLen,((respNeeded) ? 1 : 0));

    A_UINT32 k;
    for (k = 0; k < cmdStreamLen; k++)
    printf("%02X ", buf[k]);
    printf("\nsent %d\n", cmdStreamLen);
    //prtCmdStream((A_UINT8 *)buf, (A_UINT32) (cmdStreamLen +8));

    if (respNeeded == TRUE)
    {
        while (replyReceived == FALSE);
    }
EXIT :
    exit (0);
}

#if 0
static void prtCmdStream(A_UINT8 *stream, A_UINT32 streamLen)
{
    int i,num=0;
    A_UINT32 *pt32=(A_UINT32 *)stream;
    printf("ver2..stream: ");
    for (i=0;i<streamLen;i+=4) {
        //printf("%d ", stream[i]);
        pt32=(A_UINT32 *)&(stream[i]);
        printf("%d ", (int)(*pt32));
        num++;
        if (!(num % 40)) printf("\n");
    }
    printf("\n");
    return;
}
#endif //0

void setPowerPerRateIndex(A_BOOL is11ACRate, A_UINT32 value)
{
#ifdef USE_TLV2
    PARM_CODE parmStart, parmEnd;
    A_UINT32 powerGain[36];
    A_UINT32 i, numPwrGain;

    if (is11ACRate == TRUE)
    {
    	parmStart = PARM_PWRGAINSTART11AC;
    	parmEnd = PARM_PWRGAINEND11AC;
    	numPwrGain = 16;
    }
    else
    {
    	parmStart = PARM_PWRGAINSTART;
       	parmEnd = PARM_PWRGAINEND;
       	numPwrGain = 36;
    }
    for (i = 0; i < numPwrGain; i++)
    {
    	getPowerGain(&(powerGain[i]),value);
    }
    tlv2AddParms(4, parmStart, numPwrGain, 0, powerGain);
    tlv2AddParms(4, parmEnd, numPwrGain, 0, powerGain);

#else
    /*A_UINT8 pwrGainBuf[25];
    A_UINT32 i=0,powerGain=0,endPwrGain = 16; //PWRGAIN_ROW_MAX

    if (is11ACRate == TRUE )
    {
        endPwrGain = 27; //PWRGAIN_11AC_ROW_MAX
    }

    for(i=0;i<endPwrGain;i++)
    {
        memset(pwrGainBuf,0,sizeof(pwrGainBuf));
        if((strlen("pwrGnACStart") + 2) < sizeof(pwrGainBuf))
        {
             if (is11ACRate == TRUE ) {
            	  snprintf((char*)pwrGainBuf,sizeof(pwrGainBuf),"%s%d","pwrGnACStart",i);
             }
             else
             {
            	  snprintf((char*)pwrGainBuf,sizeof(pwrGainBuf),"%s%d","pwrGainStart",i);
             }
        }
        getPowerGain(&powerGain,value);

        addParameterToCommand((A_UINT8 *)pwrGainBuf,(A_UINT8 *)&powerGain);

        if (is11ACRate == FALSE ) {
            memset(pwrGainBuf,0,sizeof(pwrGainBuf));
            snprintf((char*)pwrGainBuf,sizeof(pwrGainBuf),"%s%d","pwrGainEnd",i);
            getPowerGain(&powerGain,value);
            addParameterToCommand((A_UINT8 *)pwrGainBuf,(A_UINT8 *)&powerGain);
       }
   }
   if ((is11ACRate == TRUE) && (is4x4Rate == TRUE))
   {
       endPwrGain = 9;//PWRGAIN_11_AC_ROW_4x4_MAX
       for(i=0;i<endPwrGain;i++)
       {
            memset(pwrGainBuf,0,sizeof(pwrGainBuf));
            if((strlen("pwrGnAC4x4")+ 1) < 25)
            {
                snprintf((char*)pwrGainBuf,sizeof(pwrGainBuf),"%s%d","pwrGnAC4x4",i);
            }
            getPowerGain(&powerGain,value);
            addParameterToCommand((A_UINT8 *)pwrGainBuf,(A_UINT8 *)&powerGain);
       }
   }


   if ( is11ACRate == TRUE ){
      A_UINT16 tempValue = (A_UINT16)value;
      memset(pwrGainBuf,0,sizeof(pwrGainBuf));
      if(strlen("pwrGnACEnd") < 25)
      {
            snprintf((char*)pwrGainBuf,sizeof(pwrGainBuf),"%s","pwrGnACEnd");
      }
      addParameterToCommand((A_UINT8 *)pwrGainBuf,(A_UINT8 *)&tempValue);
   }*/
#endif //USE_TLV2
}

static A_UINT32 freqValid(A_UINT32 val)
{
    do {
        if (val <= A_CHAN_MAX) {
            A_UINT16 freq;

            if (val < BG_CHAN_MIN)
                break;

            freq = wmic_ieee2freq(val);
            if (INVALID_FREQ == freq)
                break;
            else
                return freq;
        }

        if ((val == BG_FREQ_MAX) ||
            ((val < BG_FREQ_MAX) && (val >= BG_FREQ_MIN) && !((val - BG_FREQ_MIN) % 5)))
            return val;
        else if ((val >= A_FREQ_MIN) && (val < A_20MHZ_BAND_FREQ_MAX) && !((val - A_FREQ_MIN) % 20))
            return val;
        else if ((val >= A_20MHZ_BAND_FREQ_MAX) && (val <= A_FREQ_MAX) && !((val - A_20MHZ_BAND_FREQ_MAX) % 5))
            return val;
    } while (FALSE);

    //A_ERR(-1, "Invalid channel or freq #: %d !\n", val);
return val;
    //return 0;
}

#if 0
static A_UINT32 rateValid(A_UINT32 val, A_UINT32 freq)
{
    if (((freq >= A_FREQ_MIN) && (freq <= A_FREQ_MAX) && (val >= A_RATE_NUM)) ||
        ((freq >= BG_FREQ_MIN) && (freq <= BG_FREQ_MAX) && (val >= G_RATE_NUM))) {
        printf("Invalid rate value %d for frequency %d! \n", val, freq);
        prtRateTbl(freq);
        A_ERR(-1, "Invalid rate value %d for frequency %d! \n", val, freq);
    }

    return val;
}
#endif
#if 0
static void prtRateTbl(A_UINT32 freq)
{
    int i;

        for (i = 0; i < G_RATE_NUM; i++) {
            printf("<rate> %d \t \t %s \n", i, bgRateStrTbl[i]);
        }
        printf("\n");
}
#endif
/*
 * converts ieee channel number to frequency
 */
static A_UINT16
wmic_ieee2freq(A_UINT32 chan)
{
    if (chan == BG_CHAN_MAX) {
        return BG_FREQ_MAX;
    }
    if (chan < BG_CHAN_MAX) {    /* 0-13 */
        return (A_UINT16)(BG_CHAN0_FREQ + (chan*5));
    }
    if (chan <= A_CHAN_MAX) {
        return (A_UINT16)(A_CHAN0_FREQ + (chan*5));
    }
    else {
        return INVALID_FREQ;
    }
}

static A_UINT32 antValid(A_UINT32 val)
{
    if (val > 2) {
        A_ERR(-1, "Invalid antenna setting! <0: auto;  1/2: ant 1/2>\n");
    }

//    return val;
    return 0;  //TODO: antenna diversity currently not supported
}

static A_UINT32 pktSzValid(A_UINT32 val)
{
    if (( val < 32 )||(val > 11000)){
        A_ERR(-1, "Invalid package size! < 32 - 11000 >\n");
    }
    return val;
}
#ifdef NOTYET

// Validate a hex character
static A_BOOL
_is_hex(char c)
{
    return (((c >= '0') && (c <= '9')) ||
            ((c >= 'A') && (c <= 'F')) ||
            ((c >= 'a') && (c <= 'f')));
}

// Convert a single hex nibble
static int
_from_hex(char c)
{
    int ret = 0;

    if ((c >= '0') && (c <= '9')) {
        ret = (c - '0');
    } else if ((c >= 'a') && (c <= 'f')) {
        ret = (c - 'a' + 0x0a);
    } else if ((c >= 'A') && (c <= 'F')) {
        ret = (c - 'A' + 0x0A);
    }
    return ret;
}

// Convert a character to lower case
static char
_tolower(char c)
{
    if ((c >= 'A') && (c <= 'Z')) {
        c = (c - 'A') + 'a';
    }
    return c;
}

// Validate alpha
static A_BOOL
isalpha(int c)
{
    return (((c >= 'a') && (c <= 'z')) ||
            ((c >= 'A') && (c <= 'Z')));
}

// Validate digit
static A_BOOL
isdigit(int c)
{
    return ((c >= '0') && (c <= '9'));
}

// Validate alphanum
static A_BOOL
isalnum(int c)
{
    return (isalpha(c) || isdigit(c));
}
#endif

/*------------------------------------------------------------------*/
/*
 * Input an Ethernet address and convert to binary.
 */
static A_STATUS
ath_ether_aton(const char *orig, A_UINT8 *eth)
{
    int mac[6];
    if (sscanf(orig, "%02x:%02x:%02X:%02X:%02X:%02X",
               &mac[0], &mac[1], &mac[2],
               &mac[3], &mac[4], &mac[5])==6) {
        int i;
#ifdef DEBUG
        if (*(orig+12+5) !=0) {
            fprintf(stderr, "%s: trailing junk '%s'!\n", __func__, orig);
            return A_EINVAL;
        }
#endif
        for (i=0; i<6; ++i)
            eth[i] = mac[i] & 0xff;
        return A_OK;
    }
    return A_EINVAL;
}

static A_INT32
computeNumSlots(A_UINT32 dutyCycle, A_UINT32 dataRateIndex, float txFrameDurationPercent)
{
    A_UINT32 frameLength = TX_FRAME_DURATION, slotTime;
    A_INT32  numSlots;
    frameLength = (A_UINT32) ((float)TX_FRAME_DURATION * txFrameDurationPercent);
    if ((dataRateIndex >= 0 && dataRateIndex <= 3) ||
       (dataRateIndex >= 44 && dataRateIndex <= 45)) {
        if (dataRateIndex >= 44 && dataRateIndex <= 45) {
            frameLength += PREAMBLE_TIME_CCK_SHORT;
        }
        else {
            frameLength += PREAMBLE_TIME_CCK_LONG;
        }
        slotTime = SLOT_TIME;
    }
    else {
        frameLength += PREAMBLE_TIME_OFDM;
        slotTime = SLOT_TIME;
    }
    numSlots = ((A_INT32)(((float)frameLength * (100 - dutyCycle)) / ((float)dutyCycle * slotTime)));
    if (numSlots > MAX_NUM_SLOTS) numSlots = MAX_NUM_SLOTS;
    return(numSlots);
}

static A_BOOL readBinFile(char *fileName, void *buf, A_UINT32 *length, A_UINT32 length2Read)
{
    FILE *fp;
    A_BOOL rc=TRUE;
    A_UINT32 numBytes;
    A_UINT32 len2Read=length2Read;

    if( (fp = fopen(fileName, "rb")) == NULL) {
        printf("Could not open %s to read\n", fileName);
        return FALSE;
    }

    if (!length2Read) len2Read = BOARD_DATA_SZ_MAX;

    if (len2Read == (numBytes = fread((A_UCHAR *)buf, 1, len2Read, fp))) {
        printf("Read %d from %s\n", numBytes, fileName);
        rc = TRUE;
    }
    else {
        if (feof(fp)) {
            printf("Read %d from %s, expected length %d, check if it ok\n", numBytes, fileName, length2Read);
            rc = TRUE;
        }
        else if (ferror(fp)) {
            printf("Error reading %s\n", fileName);
            rc = FALSE;
        }
        else {printf("Unknown fread rc\n"); rc = FALSE; }
    }
    if (fp) fclose(fp);

    if (rc) *length = numBytes;
    else *length=0;

    return(rc);
}

static A_BOOL writeBinFile(char *fileName, void *buf, A_UINT32 *length, A_UINT32 length2Read)
{
    FILE *fp;
    A_BOOL rc=TRUE;
    A_UINT32 numBytes;
    A_UINT32 len2Read=length2Read;
    if( (fp = fopen(fileName, "wb")) == NULL) {
         printf("Could not open %s to write\n", fileName);
         return FALSE;
    }

    if (len2Read != (numBytes = fwrite((A_UCHAR *)buf, 1, len2Read, fp))) {
         printf("Error in writing to %s - expected %d but written %d \n", fileName, len2Read, numBytes);
         rc = FALSE;
    }
    if (fp) fclose(fp);

    if (rc) *length = numBytes;
    else *length=0;

    return(rc);
}

static A_BOOL fetchTxCALPwr(A_UINT32 *numMeasuredPwr, A_INT16 *measuredPwr)
{
    FILE *fp;
    char fileName[32]="_measPwrGolden.txt";
    char lineBuf[512], *pLine;
    A_INT16 pwr;
    int lineNo=0;

    if( (fp = fopen(fileName, "r")) == NULL) {
        printf("Could not open %s to read\n", fileName);
        return FALSE;
    }

#define  MAX_LINE_BUF 512
    while(fgets(lineBuf, (MAX_LINE_BUF - 2), fp) != NULL) {
        pLine = lineBuf;
        lineNo++;

        while(isspace(*pLine)) {pLine++;}

        if(!sscanf(pLine, "%d", (int*)&pwr)) {
            printf("Error reading line %d\n", lineNo);
            pwr=80;  // TBD??? 10dBm for now
        }

        measuredPwr[(*numMeasuredPwr)++] = pwr;

        if (lineNo != *numMeasuredPwr) {
            printf("why\n");
        }
    }

    if (fp) fclose(fp);

    printf("Read %d lines %d pwr meas\n", lineNo, *numMeasuredPwr);

    return(TRUE);

}


A_UINT8 eepromWrite(A_UINT32 offset, A_UINT16 blksize, A_INT8 *data, A_UINT8 enablePrint)
{
    char buf[2048 + 8];
    A_UINT8 *rCmdStream = NULL;
    A_UINT32 cmdStreamLen=0;
    A_UINT8 numSeg = 0;
    TESTFLOW_CMD_STREAM_V2 *pCmdStream;
    replyReceived = FALSE;
    memset(buf, 0, sizeof(buf));
    numSeg = createCmdRspExt(MAX_PAYLOAD_LEN, sizeof(CMD_EEPROMWRITE_PARMS) + (12 * 10), CMD_EEPROMWRITE, 6,
                PARM_OFFSET, offset,
                PARM_SIZE, blksize,
                PARM_DATA4K, data);

    for(int itr = 0; itr < numSeg; itr++)
    {
         pCmdStream = tlvGetNextStream(&cmdStreamLen);

         if (pCmdStream) {
             rCmdStream = (A_UINT8 *)pCmdStream;
         }
         else
         {
             printf("pCmdStream is NULL\n");
             return 0;
         }


         if(enablePrint) 
             printf("copying %d, 0x%x 0x%x 0x%x\n", cmdStreamLen, rCmdStream[0], rCmdStream[1], rCmdStream[2]);

         memcpy(&(buf[STREAM_SHIFT]),rCmdStream,cmdStreamLen);

         if(enablePrint)
             printf("sending %d, 0x%x 0x%x 0x%x\n", cmdStreamLen, rCmdStream[0], rCmdStream[1], rCmdStream[2]);

         UTF_CMD_SEND(buf,cmdStreamLen,1);

         if(enablePrint)
         {
             A_UINT32 k;
             for (k = 8; k < cmdStreamLen + 8; k++)
             printf("%02X ", buf[k]);
             printf("\nsent %d\n", cmdStreamLen);
         }
         while (replyReceived == FALSE);

         if(enablePrint)
              printf("reply recievied %d\n",replyReceived);
    }
    return 1;
}

A_UINT8 eepromRead(A_UINT32 bdsize,A_UINT32 offset,A_UINT16 blksize)
{
	char buf[2048 + 8];
	A_UINT8 *rCmdStream = NULL;
	A_UINT32 cmdStreamLen=0;
	TESTFLOW_CMD_STREAM_V2 *pCmdStream = NULL;

	memset(buf, 0, sizeof(buf));
	pCmdStream = (TESTFLOW_CMD_STREAM_V2 *) createCmdRsp(CMD_EEPROMREAD, 6, PARM_EEPROMCALDATASIZE, bdsize, PARM_OFFSET, offset, PARM_SIZE, blksize);
	if(pCmdStream)
	{
		cmdStreamLen = (sizeof(TESTFLOW_CMD_STREAM_HEADER_V2) + pCmdStream->cmdStreamHeader.length);
		rCmdStream = (A_UINT8 *) pCmdStream;
		memcpy(&(buf[STREAM_SHIFT]),rCmdStream,cmdStreamLen);
		UTF_CMD_SEND(buf,cmdStreamLen, 1);
	}
	
	return g_eeprom_read_done;
}

A_UINT8 checkEepromSize()
{
	char buf[2048 + 8];
	A_UINT8 *rCmdStream = NULL;
	A_UINT32 cmdStreamLen=0;
	TESTFLOW_CMD_STREAM_V2 *pCmdStream = NULL;

	memset(buf, 0, sizeof(buf));
	replyReceived = FALSE;
	pCmdStream = (TESTFLOW_CMD_STREAM_V2 *) createCmdRsp(CMD_EEPROMGETSIZE, 0);
	if(pCmdStream)
	{
		cmdStreamLen = (sizeof(TESTFLOW_CMD_STREAM_HEADER_V2) + pCmdStream->cmdStreamHeader.length);
		rCmdStream = (A_UINT8 *) pCmdStream;
		memcpy(&(buf[STREAM_SHIFT]),rCmdStream,cmdStreamLen);
		UTF_CMD_SEND(buf,cmdStreamLen,1);
	}

	while (replyReceived == FALSE);

	return replyReceived;
}

