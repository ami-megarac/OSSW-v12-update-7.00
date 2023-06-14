/*
// Copyright (C) 2018 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions
// and limitations under the License.
//
//
// SPDX-License-Identifier: Apache-2.0
*/

#include "common.h"
#ifndef SPX_BMC_ACD
#include <cJSON.h>
#endif
#include "TorDump.h"


/******************************************************************************
*
*   torDumpRaw
*
*   This function writes the TOR dump into the provided raw file
*
******************************************************************************/
#ifdef BUILD_RAW
static void torDumpRaw(UINT8 u8CPU, UINT32 u32NumReads, UINT32 * pu32TorDump, FILE * fpRaw)
{
#ifdef SPX_BMC_ACD
    UN_USED(u8CPU);
#endif
	fwrite(&u32NumReads, sizeof(UINT32), 1, fpRaw);
	if((pu32TorDump != NULL) && (fpRaw != NULL))
    {
        fwrite(pu32TorDump, (sizeof(UINT32) * u32NumReads), 1, fpRaw);
    }
    else
    {
        printf("the parameters of fwrite are null\n");
    }
}
#endif //BUILD_RAW


/******************************************************************************
*
*   torDumpJson
*
*   This function formats the TOR dump into a JSON object
*
******************************************************************************/
#ifdef BUILD_JSON
static void torDumpJson(UINT8 u8CPU, UINT32 u32NumReads, UINT32 * pu32TorDump, UINT8 * pu8CC, cJSON * pJsonChild)
{
    cJSON * socket;
    cJSON * channel;
    cJSON * tor;
    char jsonItemName[TD_JSON_STRING_LEN] = {0};
    char jsonItemString[TD_JSON_STRING_LEN] = {0};
    UINT8 u8ChannelNum = 0;
    UINT8 u8TorNum, u8DwordNum;
    UINT32 u32TorIndex = 0;

    // Add the socket number item to the TOR dump JSON structure
    snprintf(jsonItemName, TD_JSON_STRING_LEN, TD_JSON_SOCKET_NAME, u8CPU);
    cJSON_AddItemToObject(pJsonChild, jsonItemName, socket = cJSON_CreateObject());

    // Add the TOR dump info to the TOR dump JSON structure
    while (u32TorIndex < u32NumReads) {
        // Add the channel number item to the TOR dump JSON structure
        snprintf(jsonItemName, TD_JSON_STRING_LEN, TD_JSON_CHA_NAME, u8ChannelNum++);
        cJSON_AddItemToObject(socket, jsonItemName, channel = cJSON_CreateObject());

        // Add the TOR data for this channel
        for (u8TorNum = 0; u8TorNum < TD_TORS_PER_CHA; u8TorNum++) {
            // Add the TOR number item to the TOR dump JSON structure
            snprintf(jsonItemName, TD_JSON_STRING_LEN, TD_JSON_TOR_NAME, u8TorNum);
            cJSON_AddItemToObject(channel, jsonItemName, tor = cJSON_CreateObject());

            // Add the data for this TOR
            for (u8DwordNum = 0; u8DwordNum < TD_DWORDS_PER_TOR; u8DwordNum++) {
                // Add the DWORD number item to the TOR dump JSON structure
                snprintf(jsonItemName, TD_JSON_STRING_LEN, TD_JSON_DWORD_NAME, u8DwordNum);

		if (pu32TorDump != NULL && pu8CC != NULL) {
			if (pu8CC[u32TorIndex] == PECI_CC_SUCCESS)
				snprintf(jsonItemString, TD_JSON_STRING_LEN, "0x%x", pu32TorDump[u32TorIndex]);
			else
				snprintf(jsonItemString, TD_JSON_STRING_LEN, "0x%x,%s0x%02x", pu32TorDump[u32TorIndex],
					UA_STRING, pu8CC[u32TorIndex]);
		}
		u32TorIndex++;

                cJSON_AddStringToObject(tor, jsonItemName, jsonItemString);
            }
        }
    }
}
#endif //BUILD_JSON

/******************************************************************************
*
*    logTorDump
*
*    BMC performs the TOR dump retrieve from the processor directly via
*    PECI interface after a platform three (3) strike failure.  The PECI flow is
*    listed below to generate a TOR Dump, and decode it.
*
*    WrPkgConfig() -
*         0x80 0x0003 0x00030002
*         Open TOR Dump Sequence
*
*    WrPkgConfig() -
*         0x80 0x0001 0x80000000
*         Set Parameter 0
*
*    RdPkgConfig() -
*         0x80 0x3001
*         Start TOR dump sequence
*
*    RdPkgConfig() -
*         0x80 0x0002
*         Returns the number of additional RdPkgConfig() commands required to collect all the data
*         for the dump.
*
*    RdPkgConfig() * N -
*         0x80 0x0002
*         TOR Dump data [1-N]
*
*    WrPkgConfig() -
*         0x80 0x0004 0x00030002 Close TOR Dump Sequence.
*
******************************************************************************/
ESTATUS logTorDump(FILE * fpRaw, FILE * fp, cJSON * pJsonChild)
{
    EPECIStatus ePECIStatus = PECI_CC_SUCCESS;
    UINT8 u8CPU;
    UINT8 u8StartData[4];
    UINT32 * pu32TorDump = NULL;
    UINT8 * pu8CC = NULL;
    UINT32 u32NumReads = 0;
    UINT32 i;
    ESTATUS eStatus = ST_OK;
    UINT32 u32work;
    SRdPkgConfigReq sRdPkgConfigReq;
    SRdPkgConfigRes sRdPkgConfigRes;
    SWrPkgConfigReq sWrPkgConfigReq;
    SWrPkgConfigRes sWrPkgConfigRes;

#ifdef SPX_BMC_ACD
    UN_USED(fp);
#endif

    // Go through all CPUs
    for (u8CPU = CPU0_ID; u8CPU < MAX_CPU; u8CPU++) {
        if (!IsCpuPresent(u8CPU)) {
            continue;
        }
        // Start the TOR dump log
        PRINT(PRINT_DBG, PRINT_INFO, "Platform Debug - TOR dump log %d\n", u8CPU);

        // Open the TOR dump sequence
        PRINT(PRINT_DBG, PRINT_INFO, "[DBG] - CPU #%d TOR Dump Sequence Opened\n", u8CPU);
        memset(&sWrPkgConfigReq, 0 , sizeof(SWrPkgConfigReq));
        memset(&sWrPkgConfigRes, 0 , sizeof(SWrPkgConfigRes));
        sWrPkgConfigReq.sHeader.u8ClientAddr = PECI_BASE_ADDR + u8CPU;
        sWrPkgConfigReq.sHeader.u8WriteLength = sizeof(UINT32) + 6;
        sWrPkgConfigReq.sHeader.u8ReadLength = 0x01;
        sWrPkgConfigReq.u8CmdCode = 0xA5;
        sWrPkgConfigReq.u8HostID_Retry = 0x02;
        sWrPkgConfigReq.u8Index = MBX_INDEX_VCU;
        sWrPkgConfigReq.u16Parameter = VCU_OPEN_SEQ;
        u32work = TD_SEQ_DATA;
        memcpy (sWrPkgConfigReq.u8Data, &u32work, sizeof(UINT32));
	PECI_WrPkgConfig(&sWrPkgConfigReq, &sWrPkgConfigRes);

	ePECIStatus = sWrPkgConfigRes.u8CompletionCode;
        if (ePECIStatus != PECI_CC_SUCCESS) {
            // TOR dump sequence failed, abort the sequence and go to the next CPU
            memset(&sWrPkgConfigReq, 0 , sizeof(SWrPkgConfigReq));
            memset(&sWrPkgConfigRes, 0 , sizeof(SWrPkgConfigRes));
            sWrPkgConfigReq.sHeader.u8ClientAddr = PECI_BASE_ADDR + u8CPU;
            sWrPkgConfigReq.sHeader.u8WriteLength = sizeof(UINT32) + 6;
            sWrPkgConfigReq.sHeader.u8ReadLength = 0x01;
            sWrPkgConfigReq.u8CmdCode = 0xA5;
            sWrPkgConfigReq.u8HostID_Retry = 0x02;
            sWrPkgConfigReq.u8Index = MBX_INDEX_VCU;
            sWrPkgConfigReq.u16Parameter = VCU_ABORT_SEQ;
            u32work = TD_SEQ_DATA;
            memcpy (sWrPkgConfigReq.u8Data, &u32work, sizeof(UINT32));
	    PECI_WrPkgConfig(&sWrPkgConfigReq, &sWrPkgConfigRes);
            PRINT(PRINT_DBG, PRINT_ERROR, "[DBG] - CPU #%d TOR dump Sequence Failed\n", u8CPU);
            eStatus = ST_HW_FAILURE;
            continue;
        }

        // Set TOR dump parameter 0
        PRINT(PRINT_DBG, PRINT_INFO, "[DBG] - CPU #%d Set TOR Dump Parameter 0\n", u8CPU);
        memset(&sWrPkgConfigReq, 0 , sizeof(SWrPkgConfigReq));
        memset(&sWrPkgConfigRes, 0 , sizeof(SWrPkgConfigRes));
        sWrPkgConfigReq.sHeader.u8ClientAddr = PECI_BASE_ADDR + u8CPU;
        sWrPkgConfigReq.sHeader.u8WriteLength = sizeof(UINT32) + 6;
        sWrPkgConfigReq.sHeader.u8ReadLength = 0x01;
        sWrPkgConfigReq.u8CmdCode = 0xA5;
        sWrPkgConfigReq.u8HostID_Retry = 0x02;
        sWrPkgConfigReq.u8Index = MBX_INDEX_VCU;
        sWrPkgConfigReq.u16Parameter = VCU_SET_PARAM;
        u32work = TD_PARAM_ZERO;
        memcpy (sWrPkgConfigReq.u8Data, &u32work, sizeof(UINT32));
	PECI_WrPkgConfig(&sWrPkgConfigReq, &sWrPkgConfigRes);

	ePECIStatus = sWrPkgConfigRes.u8CompletionCode;
        if (ePECIStatus != PECI_CC_SUCCESS) {
            // TOR dump sequence failed, abort the sequence and go to the next CPU
            memset(&sWrPkgConfigReq, 0 , sizeof(SWrPkgConfigReq));
            memset(&sWrPkgConfigRes, 0 , sizeof(SWrPkgConfigRes));
            sWrPkgConfigReq.sHeader.u8ClientAddr = PECI_BASE_ADDR + u8CPU;
            sWrPkgConfigReq.sHeader.u8WriteLength = sizeof(UINT32) + 6;
            sWrPkgConfigReq.sHeader.u8ReadLength = 0x01;
            sWrPkgConfigReq.u8CmdCode = 0xA5;
            sWrPkgConfigReq.u8HostID_Retry = 0x02;
            sWrPkgConfigReq.u8Index = MBX_INDEX_VCU;
            sWrPkgConfigReq.u16Parameter = VCU_ABORT_SEQ;
            u32work = TD_SEQ_DATA;
            memcpy (sWrPkgConfigReq.u8Data, &u32work, sizeof(UINT32));
	    PECI_WrPkgConfig(&sWrPkgConfigReq, &sWrPkgConfigRes);
            PRINT(PRINT_DBG, PRINT_ERROR, "[DBG] - CPU #%d TOR dump Sequence Failed\n", u8CPU);
            eStatus = ST_HW_FAILURE;
            continue;
        }

        // Start the TOR dump
        memset(&sRdPkgConfigReq, 0 , sizeof(SRdPkgConfigReq));
        memset(&sRdPkgConfigRes, 0 , sizeof(SRdPkgConfigRes));
        sRdPkgConfigReq.sHeader.u8ClientAddr = PECI_BASE_ADDR + u8CPU;
        sRdPkgConfigReq.sHeader.u8WriteLength = 0x05;
        sRdPkgConfigReq.sHeader.u8ReadLength = sizeof(UINT32) + 1;
        sRdPkgConfigReq.u8CmdCode = 0xA1;
        sRdPkgConfigReq.u8HostID_Retry = 0x02;
        sRdPkgConfigReq.u8Index = MBX_INDEX_VCU;
        sRdPkgConfigReq.u16Parameter = TD_START_PARAM;
        if (0 == PECI_RdPkgConfig (&sRdPkgConfigReq, &sRdPkgConfigRes))
        {
            memcpy(u8StartData, sRdPkgConfigRes.u8Data, sizeof(UINT32));
        }

        ePECIStatus = sRdPkgConfigRes.u8CompletionCode;
        if (ePECIStatus != PECI_CC_SUCCESS && ePECIStatus != PECI_CC_MAYBE_SUCCESS && ePECIStatus != PECI_CC_CMD_PASSED) {
            // TOR dump sequence failed, abort the sequence and go to the next CPU
            memset(&sWrPkgConfigReq, 0 , sizeof(SWrPkgConfigReq));
            memset(&sWrPkgConfigRes, 0 , sizeof(SWrPkgConfigRes));
            sWrPkgConfigReq.sHeader.u8ClientAddr = PECI_BASE_ADDR + u8CPU;
            sWrPkgConfigReq.sHeader.u8WriteLength = sizeof(UINT32) + 6;
            sWrPkgConfigReq.sHeader.u8ReadLength = 0x01;
            sWrPkgConfigReq.u8CmdCode = 0xA5;
            sWrPkgConfigReq.u8HostID_Retry = 0x02;
            sWrPkgConfigReq.u8Index = MBX_INDEX_VCU;
            sWrPkgConfigReq.u16Parameter = VCU_ABORT_SEQ;
            u32work = TD_SEQ_DATA;
            memcpy (sWrPkgConfigReq.u8Data, &u32work, sizeof(UINT32));
	    PECI_WrPkgConfig(&sWrPkgConfigReq, &sWrPkgConfigRes);
            PRINT(PRINT_DBG, PRINT_ERROR, "[DBG] - CPU #%d TOR dump Sequence Failed\n", u8CPU);
            eStatus = ST_HW_FAILURE;
            continue;
        }

		// initialize to 0
		u32NumReads = 0;

        // Get the number of dwords to read
        memset(&sRdPkgConfigReq, 0 , sizeof(SRdPkgConfigReq));
        memset(&sRdPkgConfigRes, 0 , sizeof(SRdPkgConfigRes));
        sRdPkgConfigReq.sHeader.u8ClientAddr = PECI_BASE_ADDR + u8CPU;
        sRdPkgConfigReq.sHeader.u8WriteLength = 0x05;
        sRdPkgConfigReq.sHeader.u8ReadLength = sizeof(UINT32) + 1;
        sRdPkgConfigReq.u8CmdCode = 0xA1;
        sRdPkgConfigReq.u8HostID_Retry = 0x02;
        sRdPkgConfigReq.u8Index = MBX_INDEX_VCU;
        sRdPkgConfigReq.u16Parameter = VCU_READ;
        if (0 == PECI_RdPkgConfig (&sRdPkgConfigReq, &sRdPkgConfigRes))
        {
            memcpy(&u32NumReads, &sRdPkgConfigRes.u8Data, sizeof(UINT32));
        }
        ePECIStatus = sRdPkgConfigRes.u8CompletionCode;

	if (ePECIStatus != PECI_CC_SUCCESS) {
		// chop off the CHA count on 0x91/0x94 read if exceed the max
		if (ePECIStatus == PECI_CC_MAYBE_SUCCESS || ePECIStatus == PECI_CC_CMD_PASSED) {
			if (u32NumReads > MAX_TOR_SIZE)
				u32NumReads = MAX_TOR_SIZE;
		}
		else {
			// don't trust any other CC cases, 
			// TOR dump sequence failed, abort the sequence and go to the next CPU
			memset(&sWrPkgConfigReq, 0, sizeof(SWrPkgConfigReq));
			memset(&sWrPkgConfigRes, 0, sizeof(SWrPkgConfigRes));
			sWrPkgConfigReq.sHeader.u8ClientAddr = PECI_BASE_ADDR + u8CPU;
			sWrPkgConfigReq.sHeader.u8WriteLength = sizeof(UINT32) + 6;
			sWrPkgConfigReq.sHeader.u8ReadLength = 0x01;
			sWrPkgConfigReq.u8CmdCode = 0xA5;
			sWrPkgConfigReq.u8HostID_Retry = 0x02;
			sWrPkgConfigReq.u8Index = MBX_INDEX_VCU;
			sWrPkgConfigReq.u16Parameter = VCU_ABORT_SEQ;
			u32work = TD_SEQ_DATA;
			memcpy(sWrPkgConfigReq.u8Data, &u32work, sizeof(UINT32));
			PECI_WrPkgConfig(&sWrPkgConfigReq, &sWrPkgConfigRes);
			PRINT(PRINT_DBG, PRINT_ERROR, "[DBG] - CPU #%d TOR count (%d) Failed (cc=%02x)\n", u8CPU, u32NumReads, ePECIStatus);
			eStatus = ST_HW_FAILURE;
			continue;
		}
	}

        // Get the raw data
	pu32TorDump = (UINT32 *)calloc(u32NumReads, sizeof(UINT32));
	pu8CC = (UINT8 *)calloc(u32NumReads, sizeof(UINT8));
	if (pu32TorDump == NULL || pu8CC == NULL) {
            // calloc failed, abort the sequence and go to the next CPU
            memset(&sWrPkgConfigReq, 0 , sizeof(SWrPkgConfigReq));
            memset(&sWrPkgConfigRes, 0 , sizeof(SWrPkgConfigRes));
            sWrPkgConfigReq.sHeader.u8ClientAddr = PECI_BASE_ADDR + u8CPU;
            sWrPkgConfigReq.sHeader.u8WriteLength = sizeof(UINT32) + 6;
            sWrPkgConfigReq.sHeader.u8ReadLength = 0x01;
            sWrPkgConfigReq.u8CmdCode = 0xA5;
            sWrPkgConfigReq.u8HostID_Retry = 0x02;
            sWrPkgConfigReq.u8Index = MBX_INDEX_VCU;
            sWrPkgConfigReq.u16Parameter = VCU_ABORT_SEQ;
            u32work = TD_SEQ_DATA;
            memcpy (sWrPkgConfigReq.u8Data, &u32work, sizeof(UINT32));
	    PECI_WrPkgConfig(&sWrPkgConfigReq, &sWrPkgConfigRes);
            PRINT(PRINT_DBG, PRINT_ERROR, "[DBG] - CPU #%d TOR dump Sequence Failed\n", u8CPU);
            eStatus = ST_HW_FAILURE;

	    if (pu32TorDump != NULL) {
		free(pu32TorDump);
		pu32TorDump = NULL;
	    }

	    if (pu8CC != NULL) {
		free(pu8CC);
		pu8CC = NULL;
	    }

            continue;
        }
        for (i = 0; i < u32NumReads; i++) {
            memset(&sRdPkgConfigReq, 0 , sizeof(SRdPkgConfigReq));
            memset(&sRdPkgConfigRes, 0 , sizeof(SRdPkgConfigRes));
            sRdPkgConfigReq.sHeader.u8ClientAddr = PECI_BASE_ADDR + u8CPU;
            sRdPkgConfigReq.sHeader.u8WriteLength = 0x05;
            sRdPkgConfigReq.sHeader.u8ReadLength = sizeof(UINT32) + 1;
            sRdPkgConfigReq.u8CmdCode = 0xA1;
            sRdPkgConfigReq.u8HostID_Retry = 0x02;
            sRdPkgConfigReq.u8Index = MBX_INDEX_VCU;
            sRdPkgConfigReq.u16Parameter = VCU_READ;
            if (0 == PECI_RdPkgConfig (&sRdPkgConfigReq, &sRdPkgConfigRes))
            {
                memcpy(&pu32TorDump[i], &sRdPkgConfigRes.u8Data, sizeof(UINT32));
            }
 
	    // save on PECI completion code
	    pu8CC[i] = sRdPkgConfigRes.u8CompletionCode;

            if (pu8CC[i] != PECI_CC_SUCCESS &&
		pu8CC[i] != PECI_CC_MAYBE_SUCCESS && 
		pu8CC[i] != PECI_CC_CMD_PASSED) {
                // TOR dump sequence failed, note the number of dwords read and abort the sequence
                u32NumReads = i;
                memset(&sWrPkgConfigReq, 0 , sizeof(SWrPkgConfigReq));
                memset(&sWrPkgConfigRes, 0 , sizeof(SWrPkgConfigRes));
                sWrPkgConfigReq.sHeader.u8ClientAddr = PECI_BASE_ADDR + u8CPU;
                sWrPkgConfigReq.sHeader.u8WriteLength = sizeof(UINT32) + 6;
                sWrPkgConfigReq.sHeader.u8ReadLength = 0x01;
                sWrPkgConfigReq.u8CmdCode = 0xA5;
                sWrPkgConfigReq.u8HostID_Retry = 0x02;
                sWrPkgConfigReq.u8Index = MBX_INDEX_VCU;
                sWrPkgConfigReq.u16Parameter = VCU_ABORT_SEQ;
                u32work = TD_SEQ_DATA;
                memcpy (sWrPkgConfigReq.u8Data, &u32work, sizeof(UINT32));
		PECI_WrPkgConfig(&sWrPkgConfigReq, &sWrPkgConfigRes);
                break;
            }
        }

        // Close the TOR dump sequence
        PRINT(PRINT_DBG, PRINT_INFO, "[DBG] - CPU #%d TOR dump Sequence Closed\n", u8CPU);
        memset(&sWrPkgConfigReq, 0 , sizeof(SWrPkgConfigReq));
        memset(&sWrPkgConfigRes, 0 , sizeof(SWrPkgConfigRes));
        sWrPkgConfigReq.sHeader.u8ClientAddr = PECI_BASE_ADDR + u8CPU;
        sWrPkgConfigReq.sHeader.u8WriteLength = sizeof(UINT32) + 6;
        sWrPkgConfigReq.sHeader.u8ReadLength = 0x01;
        sWrPkgConfigReq.u8CmdCode = 0xA5;
        sWrPkgConfigReq.u8HostID_Retry = 0x02;
        sWrPkgConfigReq.u8Index = MBX_INDEX_VCU;
        sWrPkgConfigReq.u16Parameter = VCU_CLOSE_SEQ;
        u32work = TD_SEQ_DATA;
        memcpy (sWrPkgConfigReq.u8Data, &u32work, sizeof(UINT32));
	PECI_WrPkgConfig(&sWrPkgConfigReq, &sWrPkgConfigRes);

        // Log the TOR dump for this CPU
#ifdef BUILD_RAW
        if (fpRaw != NULL) {
            torDumpRaw(u8CPU, u32NumReads, pu32TorDump, fpRaw);
        }
#endif //BUILD_RAW
#ifdef BUILD_JSON
        if (pJsonChild != NULL) {
            torDumpJson(u8CPU, u32NumReads, pu32TorDump, pu8CC, pJsonChild);
        }
#endif //BUILD_JSON

        if (pu32TorDump != NULL)
		free(pu32TorDump);

	if (pu8CC != NULL)
		free(pu8CC);
	}

#ifdef BUILD_RAW
    if (eStatus != ST_OK) {
        torDumpRaw(0, 0, NULL, fpRaw);
        torDumpRaw(1, 0, NULL, fpRaw);
    }
#endif //BUILD_RAW

    return eStatus;
}
