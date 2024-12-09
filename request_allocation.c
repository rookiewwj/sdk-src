//////////////////////////////////////////////////////////////////////////////////
// request_allocation.c for Cosmos+ OpenSSD
// Copyright (c) 2017 Hanyang University ENC Lab.
// Contributed by Yong Ho Song <yhsong@enc.hanyang.ac.kr>
//				  Jaewook Kwak <jwkwak@enc.hanyang.ac.kr>
//
// This file is part of Cosmos+ OpenSSD.
//
// Cosmos+ OpenSSD is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3, or (at your option)
// any later version.
//
// Cosmos+ OpenSSD is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Cosmos+ OpenSSD; see the file COPYING.
// If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Company: ENC Lab. <http://enc.hanyang.ac.kr>
// Engineer: Jaewook Kwak <jwkwak@enc.hanyang.ac.kr>
//
// Project Name: Cosmos+ OpenSSD
// Design Name: Cosmos+ Firmware
// Module Name: Request Allocator
// File Name: request_allocation.c
//
// Version: v1.0.0
//
// Description:
//   - allocate requests to each request queue
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Revision History:
//
// * v1.0.0
//   - First draft
//////////////////////////////////////////////////////////////////////////////////

#include "xil_printf.h"
#include <assert.h>
#include "memory_map.h"

P_REQ_POOL reqPoolPtr;
FREE_REQUEST_QUEUE freeReqQ;
SLICE_REQUEST_QUEUE sliceReqQ;
BLOCKED_BY_BUFFER_DEPENDENCY_REQUEST_QUEUE blockedByBufDepReqQ;
BLOCKED_BY_ROW_ADDR_DEPENDENCY_REQUEST_QUEUE blockedByRowAddrDepReqQ[USER_CHANNELS][USER_WAYS];
NVME_DMA_REQUEST_QUEUE nvmeDmaReqQ;
NAND_REQUEST_QUEUE nandReqQ[USER_CHANNELS][USER_WAYS];

unsigned int notCompletedNandReqCnt;
unsigned int blockedReqCnt;

void InitReqPool()
{
	int chNo, wayNo, reqSlotTag;

	reqPoolPtr = (P_REQ_POOL)REQ_POOL_ADDR; // revise address

	freeReqQ.headReq = 0;
	freeReqQ.tailReq = AVAILABLE_OUNTSTANDING_REQ_COUNT - 1;

	sliceReqQ.headReq = REQ_SLOT_TAG_NONE;
	sliceReqQ.tailReq = REQ_SLOT_TAG_NONE;
	sliceReqQ.reqCnt = 0;

	blockedByBufDepReqQ.headReq = REQ_SLOT_TAG_NONE;
	blockedByBufDepReqQ.tailReq = REQ_SLOT_TAG_NONE;
	blockedByBufDepReqQ.reqCnt = 0;

	nvmeDmaReqQ.headReq = REQ_SLOT_TAG_NONE;
	nvmeDmaReqQ.tailReq = REQ_SLOT_TAG_NONE;
	nvmeDmaReqQ.reqCnt = 0;

	for (chNo = 0; chNo < USER_CHANNELS; chNo++)
		for (wayNo = 0; wayNo < USER_WAYS; wayNo++)
		{
			blockedByRowAddrDepReqQ[chNo][wayNo].headReq = REQ_SLOT_TAG_NONE;
			blockedByRowAddrDepReqQ[chNo][wayNo].tailReq = REQ_SLOT_TAG_NONE;
			blockedByRowAddrDepReqQ[chNo][wayNo].reqCnt = 0;

			nandReqQ[chNo][wayNo].headReq = REQ_SLOT_TAG_NONE;
			nandReqQ[chNo][wayNo].tailReq = REQ_SLOT_TAG_NONE;
			nandReqQ[chNo][wayNo].reqCnt = 0;
		}

	for (reqSlotTag = 0; reqSlotTag < AVAILABLE_OUNTSTANDING_REQ_COUNT; reqSlotTag++)
	{
		reqPoolPtr->reqPool[reqSlotTag].reqQueueType = REQ_QUEUE_TYPE_FREE;
		reqPoolPtr->reqPool[reqSlotTag].prevBlockingReq = REQ_SLOT_TAG_NONE;
		reqPoolPtr->reqPool[reqSlotTag].nextBlockingReq = REQ_SLOT_TAG_NONE;
		reqPoolPtr->reqPool[reqSlotTag].prevReq = reqSlotTag - 1;
		reqPoolPtr->reqPool[reqSlotTag].nextReq = reqSlotTag + 1;
	}

	reqPoolPtr->reqPool[0].prevReq = REQ_SLOT_TAG_NONE;
	reqPoolPtr->reqPool[AVAILABLE_OUNTSTANDING_REQ_COUNT - 1].nextReq = REQ_SLOT_TAG_NONE;
	freeReqQ.reqCnt = AVAILABLE_OUNTSTANDING_REQ_COUNT;

	notCompletedNandReqCnt = 0;
	blockedReqCnt = 0;
}

void PutToFreeReqQ(unsigned int reqSlotTag)
{
	if (freeReqQ.tailReq != REQ_SLOT_TAG_NONE)
	{
		reqPoolPtr->reqPool[reqSlotTag].prevReq = freeReqQ.tailReq;
		reqPoolPtr->reqPool[reqSlotTag].nextReq = REQ_SLOT_TAG_NONE;
		reqPoolPtr->reqPool[freeReqQ.tailReq].nextReq = reqSlotTag;
		freeReqQ.tailReq = reqSlotTag;
	}
	else
	{
		reqPoolPtr->reqPool[reqSlotTag].prevReq = REQ_SLOT_TAG_NONE;
		reqPoolPtr->reqPool[reqSlotTag].nextReq = REQ_SLOT_TAG_NONE;
		freeReqQ.headReq = reqSlotTag;
		freeReqQ.tailReq = reqSlotTag;
	}

	reqPoolPtr->reqPool[reqSlotTag].reqQueueType = REQ_QUEUE_TYPE_FREE;
	freeReqQ.reqCnt++;
}

unsigned int GetFromFreeReqQ()
{
	unsigned int reqSlotTag;

	reqSlotTag = freeReqQ.headReq;

	if (reqSlotTag == REQ_SLOT_TAG_NONE)
	{
		SyncAvailFreeReq();
		reqSlotTag = freeReqQ.headReq;
	}

	if (reqPoolPtr->reqPool[reqSlotTag].nextReq != REQ_SLOT_TAG_NONE)
	{
		freeReqQ.headReq = reqPoolPtr->reqPool[reqSlotTag].nextReq;
		reqPoolPtr->reqPool[reqPoolPtr->reqPool[reqSlotTag].nextReq].prevReq = REQ_SLOT_TAG_NONE;
	}
	else
	{
		freeReqQ.headReq = REQ_SLOT_TAG_NONE;
		freeReqQ.tailReq = REQ_SLOT_TAG_NONE;
	}

	reqPoolPtr->reqPool[reqSlotTag].reqQueueType = REQ_QUEUE_TYPE_NONE;
	freeReqQ.reqCnt--;

	return reqSlotTag;
}

void PutToSliceReqQ(unsigned int reqSlotTag)
{
	if (sliceReqQ.tailReq != REQ_SLOT_TAG_NONE)
	{
		reqPoolPtr->reqPool[reqSlotTag].prevReq = sliceReqQ.tailReq;
		reqPoolPtr->reqPool[reqSlotTag].nextReq = REQ_SLOT_TAG_NONE;
		reqPoolPtr->reqPool[sliceReqQ.tailReq].nextReq = reqSlotTag;
		sliceReqQ.tailReq = reqSlotTag;
	}
	else
	{
		reqPoolPtr->reqPool[reqSlotTag].prevReq = REQ_SLOT_TAG_NONE;
		reqPoolPtr->reqPool[reqSlotTag].nextReq = REQ_SLOT_TAG_NONE;
		sliceReqQ.headReq = reqSlotTag;
		sliceReqQ.tailReq = reqSlotTag;
	}

	reqPoolPtr->reqPool[reqSlotTag].reqQueueType = REQ_QUEUE_TYPE_SLICE;
	sliceReqQ.reqCnt++;
}

unsigned int GetFromSliceReqQ()
{
	unsigned int reqSlotTag;

	reqSlotTag = sliceReqQ.headReq;

	if (reqSlotTag == REQ_SLOT_TAG_NONE)
		return REQ_SLOT_TAG_FAIL;

	if (reqPoolPtr->reqPool[reqSlotTag].nextReq != REQ_SLOT_TAG_NONE)
	{
		sliceReqQ.headReq = reqPoolPtr->reqPool[reqSlotTag].nextReq;
		reqPoolPtr->reqPool[reqPoolPtr->reqPool[reqSlotTag].nextReq].prevReq = REQ_SLOT_TAG_NONE;
	}
	else
	{
		sliceReqQ.headReq = REQ_SLOT_TAG_NONE;
		sliceReqQ.tailReq = REQ_SLOT_TAG_NONE;
	}

	reqPoolPtr->reqPool[reqSlotTag].reqQueueType = REQ_QUEUE_TYPE_NONE;
	sliceReqQ.reqCnt--;

	return reqSlotTag;
}

void PutToBlockedByBufDepReqQ(unsigned int reqSlotTag)
{
	if (blockedByBufDepReqQ.tailReq != REQ_SLOT_TAG_NONE)
	{
		reqPoolPtr->reqPool[reqSlotTag].prevReq = blockedByBufDepReqQ.tailReq;
		reqPoolPtr->reqPool[reqSlotTag].nextReq = REQ_SLOT_TAG_NONE;
		reqPoolPtr->reqPool[blockedByBufDepReqQ.tailReq].nextReq = reqSlotTag;
		blockedByBufDepReqQ.tailReq = reqSlotTag;
	}
	else
	{
		reqPoolPtr->reqPool[reqSlotTag].prevReq = REQ_SLOT_TAG_NONE;
		reqPoolPtr->reqPool[reqSlotTag].nextReq = REQ_SLOT_TAG_NONE;
		blockedByBufDepReqQ.headReq = reqSlotTag;
		blockedByBufDepReqQ.tailReq = reqSlotTag;
	}

	reqPoolPtr->reqPool[reqSlotTag].reqQueueType = REQ_QUEUE_TYPE_BLOCKED_BY_BUF_DEP;
	blockedByBufDepReqQ.reqCnt++;
	blockedReqCnt++;
}
void SelectiveGetFromBlockedByBufDepReqQ(unsigned int reqSlotTag)
{
	unsigned int prevReq, nextReq;

	if (reqSlotTag == REQ_SLOT_TAG_NONE)
		assert(!"[WARNING] Wrong reqSlotTag [WARNING]");

	prevReq = reqPoolPtr->reqPool[reqSlotTag].prevReq;
	nextReq = reqPoolPtr->reqPool[reqSlotTag].nextReq;

	if ((nextReq != REQ_SLOT_TAG_NONE) && (prevReq != REQ_SLOT_TAG_NONE))
	{
		reqPoolPtr->reqPool[prevReq].nextReq = nextReq;
		reqPoolPtr->reqPool[nextReq].prevReq = prevReq;
	}
	else if ((nextReq == REQ_SLOT_TAG_NONE) && (prevReq != REQ_SLOT_TAG_NONE))
	{
		reqPoolPtr->reqPool[prevReq].nextReq = REQ_SLOT_TAG_NONE;
		blockedByBufDepReqQ.tailReq = prevReq;
	}
	else if ((nextReq != REQ_SLOT_TAG_NONE) && (prevReq == REQ_SLOT_TAG_NONE))
	{
		reqPoolPtr->reqPool[nextReq].prevReq = REQ_SLOT_TAG_NONE;
		blockedByBufDepReqQ.headReq = nextReq;
	}
	else
	{
		blockedByBufDepReqQ.headReq = REQ_SLOT_TAG_NONE;
		blockedByBufDepReqQ.tailReq = REQ_SLOT_TAG_NONE;
	}

	reqPoolPtr->reqPool[reqSlotTag].reqQueueType = REQ_QUEUE_TYPE_NONE;
	blockedByBufDepReqQ.reqCnt--;
	blockedReqCnt--;
}

void PutToBlockedByRowAddrDepReqQ(unsigned int reqSlotTag, unsigned int chNo, unsigned int wayNo)
{
	if (blockedByRowAddrDepReqQ[chNo][wayNo].tailReq != REQ_SLOT_TAG_NONE)
	{
		reqPoolPtr->reqPool[reqSlotTag].prevReq = blockedByRowAddrDepReqQ[chNo][wayNo].tailReq;
		reqPoolPtr->reqPool[reqSlotTag].nextReq = REQ_SLOT_TAG_NONE;
		reqPoolPtr->reqPool[blockedByRowAddrDepReqQ[chNo][wayNo].tailReq].nextReq = reqSlotTag;
		blockedByRowAddrDepReqQ[chNo][wayNo].tailReq = reqSlotTag;
	}
	else
	{
		reqPoolPtr->reqPool[reqSlotTag].prevReq = REQ_SLOT_TAG_NONE;
		reqPoolPtr->reqPool[reqSlotTag].nextReq = REQ_SLOT_TAG_NONE;
		blockedByRowAddrDepReqQ[chNo][wayNo].headReq = reqSlotTag;
		blockedByRowAddrDepReqQ[chNo][wayNo].tailReq = reqSlotTag;
	}

	reqPoolPtr->reqPool[reqSlotTag].reqQueueType = REQ_QUEUE_TYPE_BLOCKED_BY_ROW_ADDR_DEP;
	blockedByRowAddrDepReqQ[chNo][wayNo].reqCnt++;
	blockedReqCnt++;
}
void SelectiveGetFromBlockedByRowAddrDepReqQ(unsigned int reqSlotTag, unsigned int chNo, unsigned int wayNo)
{
	unsigned int prevReq, nextReq;

	if (reqSlotTag == REQ_SLOT_TAG_NONE)
		assert(!"[WARNING] Wrong reqSlotTag [WARNING]");

	prevReq = reqPoolPtr->reqPool[reqSlotTag].prevReq;
	nextReq = reqPoolPtr->reqPool[reqSlotTag].nextReq;

	if ((nextReq != REQ_SLOT_TAG_NONE) && (prevReq != REQ_SLOT_TAG_NONE))
	{
		reqPoolPtr->reqPool[prevReq].nextReq = nextReq;
		reqPoolPtr->reqPool[nextReq].prevReq = prevReq;
	}
	else if ((nextReq == REQ_SLOT_TAG_NONE) && (prevReq != REQ_SLOT_TAG_NONE))
	{
		reqPoolPtr->reqPool[prevReq].nextReq = REQ_SLOT_TAG_NONE;
		blockedByRowAddrDepReqQ[chNo][wayNo].tailReq = prevReq;
	}
	else if ((nextReq != REQ_SLOT_TAG_NONE) && (prevReq == REQ_SLOT_TAG_NONE))
	{
		reqPoolPtr->reqPool[nextReq].prevReq = REQ_SLOT_TAG_NONE;
		blockedByRowAddrDepReqQ[chNo][wayNo].headReq = nextReq;
	}
	else
	{
		blockedByRowAddrDepReqQ[chNo][wayNo].headReq = REQ_SLOT_TAG_NONE;
		blockedByRowAddrDepReqQ[chNo][wayNo].tailReq = REQ_SLOT_TAG_NONE;
	}

	reqPoolPtr->reqPool[reqSlotTag].reqQueueType = REQ_QUEUE_TYPE_NONE;
	blockedByRowAddrDepReqQ[chNo][wayNo].reqCnt--;
	blockedReqCnt--;
}

void PutToNvmeDmaReqQ(unsigned int reqSlotTag)
{
	// 如果当前 DMA 请求队列的尾部请求不是空
	if (nvmeDmaReqQ.tailReq != REQ_SLOT_TAG_NONE)
	{
		// 将当前请求加入到队列的尾部
		// 设置当前请求的前一个请求为当前队列的尾部
		reqPoolPtr->reqPool[reqSlotTag].prevReq = nvmeDmaReqQ.tailReq;
		// 设置当前请求的下一个请求为空
		reqPoolPtr->reqPool[reqSlotTag].nextReq = REQ_SLOT_TAG_NONE;
		// 更新队列尾部请求的 nextReq 指向当前请求
		reqPoolPtr->reqPool[nvmeDmaReqQ.tailReq].nextReq = reqSlotTag;
		// 更新队列尾部指针为当前请求
		nvmeDmaReqQ.tailReq = reqSlotTag;
	}
	else
	{
		// 如果队列为空，当前请求将成为队列的唯一元素
		// 设置当前请求的前一个请求和下一个请求都为空
		reqPoolPtr->reqPool[reqSlotTag].prevReq = REQ_SLOT_TAG_NONE;
		reqPoolPtr->reqPool[reqSlotTag].nextReq = REQ_SLOT_TAG_NONE;
		// 设置队列的头部和尾部都指向当前请求
		nvmeDmaReqQ.headReq = reqSlotTag;
		nvmeDmaReqQ.tailReq = reqSlotTag;
	}

	// 设置当前请求的队列类型为 NVME_DMA 队列
	reqPoolPtr->reqPool[reqSlotTag].reqQueueType = REQ_QUEUE_TYPE_NVME_DMA;
	// 增加 DMA 请求队列中的请求数量
	nvmeDmaReqQ.reqCnt++;
}

void SelectiveGetFromNvmeDmaReqQ(unsigned int reqSlotTag)
{
	unsigned int prevReq, nextReq;

	prevReq = reqPoolPtr->reqPool[reqSlotTag].prevReq;
	nextReq = reqPoolPtr->reqPool[reqSlotTag].nextReq;

	if ((nextReq != REQ_SLOT_TAG_NONE) && (prevReq != REQ_SLOT_TAG_NONE))
	{
		reqPoolPtr->reqPool[prevReq].nextReq = nextReq;
		reqPoolPtr->reqPool[nextReq].prevReq = prevReq;
	}
	else if ((nextReq == REQ_SLOT_TAG_NONE) && (prevReq != REQ_SLOT_TAG_NONE))
	{
		reqPoolPtr->reqPool[prevReq].nextReq = REQ_SLOT_TAG_NONE;
		nvmeDmaReqQ.tailReq = prevReq;
	}
	else if ((nextReq != REQ_SLOT_TAG_NONE) && (prevReq == REQ_SLOT_TAG_NONE))
	{
		reqPoolPtr->reqPool[nextReq].prevReq = REQ_SLOT_TAG_NONE;
		nvmeDmaReqQ.headReq = nextReq;
	}
	else
	{
		nvmeDmaReqQ.headReq = REQ_SLOT_TAG_NONE;
		nvmeDmaReqQ.tailReq = REQ_SLOT_TAG_NONE;
	}

	reqPoolPtr->reqPool[reqSlotTag].reqQueueType = REQ_QUEUE_TYPE_NONE;
	nvmeDmaReqQ.reqCnt--;

	PutToFreeReqQ(reqSlotTag);
	ReleaseBlockedByBufDepReq(reqSlotTag);
}

void PutToNandReqQ(unsigned int reqSlotTag, unsigned chNo, unsigned wayNo)
{
	if (nandReqQ[chNo][wayNo].tailReq != REQ_SLOT_TAG_NONE)
	{
		reqPoolPtr->reqPool[reqSlotTag].prevReq = nandReqQ[chNo][wayNo].tailReq;
		reqPoolPtr->reqPool[reqSlotTag].nextReq = REQ_SLOT_TAG_NONE;
		reqPoolPtr->reqPool[nandReqQ[chNo][wayNo].tailReq].nextReq = reqSlotTag;
		nandReqQ[chNo][wayNo].tailReq = reqSlotTag;
	}
	else
	{
		reqPoolPtr->reqPool[reqSlotTag].prevReq = REQ_SLOT_TAG_NONE;
		reqPoolPtr->reqPool[reqSlotTag].nextReq = REQ_SLOT_TAG_NONE;
		nandReqQ[chNo][wayNo].headReq = reqSlotTag;
		nandReqQ[chNo][wayNo].tailReq = reqSlotTag;
	}

	reqPoolPtr->reqPool[reqSlotTag].reqQueueType = REQ_QUEUE_TYPE_NAND;
	nandReqQ[chNo][wayNo].reqCnt++;
	notCompletedNandReqCnt++;
}

void GetFromNandReqQ(unsigned int chNo, unsigned int wayNo, unsigned int reqStatus, unsigned int reqCode)
{
	unsigned int reqSlotTag;

	reqSlotTag = nandReqQ[chNo][wayNo].headReq;
	if (reqSlotTag == REQ_SLOT_TAG_NONE)
		assert(!"[WARNING] there is no request in Nand-req-queue[WARNING]");

	if (reqPoolPtr->reqPool[reqSlotTag].nextReq != REQ_SLOT_TAG_NONE)
	{
		nandReqQ[chNo][wayNo].headReq = reqPoolPtr->reqPool[reqSlotTag].nextReq;
		reqPoolPtr->reqPool[reqPoolPtr->reqPool[reqSlotTag].nextReq].prevReq = REQ_SLOT_TAG_NONE;
	}
	else
	{
		nandReqQ[chNo][wayNo].headReq = REQ_SLOT_TAG_NONE;
		nandReqQ[chNo][wayNo].tailReq = REQ_SLOT_TAG_NONE;
	}

	reqPoolPtr->reqPool[reqSlotTag].reqQueueType = REQ_QUEUE_TYPE_NONE;
	nandReqQ[chNo][wayNo].reqCnt--;
	notCompletedNandReqCnt--;

	PutToFreeReqQ(reqSlotTag);
	ReleaseBlockedByBufDepReq(reqSlotTag);
}
