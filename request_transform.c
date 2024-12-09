//////////////////////////////////////////////////////////////////////////////////
// request_transform.c for Cosmos+ OpenSSD
// Copyright (c) 2017 Hanyang University ENC Lab.
// Contributed by Yong Ho Song <yhsong@enc.hanyang.ac.kr>
//				  Jaewook Kwak <jwkwak@enc.hanyang.ac.kr>
//			      Sangjin Lee <sjlee@enc.hanyang.ac.kr>
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
// Module Name: Request Scheduler
// File Name: request_transform.c
//
// Version: v1.0.0
//
// Description:
//	 - transform request information
//   - check dependency between requests
//   - issue host DMA request to host DMA engine
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Revision History:
//
// * v1.0.0
//   - First draft
//////////////////////////////////////////////////////////////////////////////////

#include "xil_printf.h"
#include <assert.h>
#include "nvme/nvme.h"
#include "nvme/host_lld.h"
#include "memory_map.h"
#include "ftl_config.h"

P_ROW_ADDR_DEPENDENCY_TABLE rowAddrDependencyTablePtr;

void InitDependencyTable()
{
	unsigned int blockNo, wayNo, chNo;
	rowAddrDependencyTablePtr = (P_ROW_ADDR_DEPENDENCY_TABLE)ROW_ADDR_DEPENDENCY_TABLE_ADDR;

	for (blockNo = 0; blockNo < MAIN_BLOCKS_PER_DIE; blockNo++)
	{
		for (wayNo = 0; wayNo < USER_WAYS; wayNo++)
		{
			for (chNo = 0; chNo < USER_CHANNELS; chNo++)
			{
				rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].permittedProgPage = 0;
				rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].blockedReadReqCnt = 0;
				rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].blockedEraseReqFlag = 0;
			}
		}
	}
}

void ReqTransNvmeToSlice(unsigned int cmdSlotTag, unsigned int startLba, unsigned int nlb, unsigned int cmdCode)
{
	unsigned int reqSlotTag, requestedNvmeBlock, tempNumOfNvmeBlock, transCounter, tempLsa, loop, nvmeBlockOffset, nvmeDmaStartIndex, reqCode;

	requestedNvmeBlock = nlb + 1;
	transCounter = 0;
	nvmeDmaStartIndex = 0;
	tempLsa = startLba / NVME_BLOCKS_PER_SLICE;												  // slice address 表示在第几个slice上
	loop = ((startLba % NVME_BLOCKS_PER_SLICE) + requestedNvmeBlock) / NVME_BLOCKS_PER_SLICE; // 一共要操作几个slice

	if (cmdCode == IO_NVM_WRITE)
		reqCode = REQ_CODE_WRITE;
	else if (cmdCode == IO_NVM_READ)
		reqCode = REQ_CODE_READ;
	else if (cmdCode == IO_NVM_PWRITE)
	{
		reqCode = REQ_CODE_PWRITE;
	}
	else
		assert(!"[WARNING] Not supported command code [WARNING]");

	// first transform
	nvmeBlockOffset = (startLba % NVME_BLOCKS_PER_SLICE);
	if (loop)
		// 单词处理一个slice的多少个block
		tempNumOfNvmeBlock = NVME_BLOCKS_PER_SLICE - nvmeBlockOffset;
	else
		tempNumOfNvmeBlock = requestedNvmeBlock;

	// 获取rq里面的slot位置
	reqSlotTag = GetFromFreeReqQ();

	reqPoolPtr->reqPool[reqSlotTag].reqType = REQ_TYPE_SLICE;
	reqPoolPtr->reqPool[reqSlotTag].reqCode = reqCode;
	reqPoolPtr->reqPool[reqSlotTag].nvmeCmdSlotTag = cmdSlotTag;
	reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr = tempLsa;
	reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.startIndex = nvmeDmaStartIndex;
	reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.nvmeBlockOffset = nvmeBlockOffset;
	reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.numOfNvmeBlock = tempNumOfNvmeBlock;

	PutToSliceReqQ(reqSlotTag);

	tempLsa++;
	transCounter++;
	nvmeDmaStartIndex += tempNumOfNvmeBlock;

	// transform continue
	while (transCounter < loop)
	{
		nvmeBlockOffset = 0;
		tempNumOfNvmeBlock = NVME_BLOCKS_PER_SLICE;

		reqSlotTag = GetFromFreeReqQ();

		reqPoolPtr->reqPool[reqSlotTag].reqType = REQ_TYPE_SLICE;
		reqPoolPtr->reqPool[reqSlotTag].reqCode = reqCode;
		reqPoolPtr->reqPool[reqSlotTag].nvmeCmdSlotTag = cmdSlotTag;
		reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr = tempLsa;
		reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.startIndex = nvmeDmaStartIndex;
		reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.nvmeBlockOffset = nvmeBlockOffset;
		reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.numOfNvmeBlock = tempNumOfNvmeBlock;

		PutToSliceReqQ(reqSlotTag);

		tempLsa++;
		transCounter++;
		nvmeDmaStartIndex += tempNumOfNvmeBlock;
	}

	// last transform
	nvmeBlockOffset = 0;
	tempNumOfNvmeBlock = (startLba + requestedNvmeBlock) % NVME_BLOCKS_PER_SLICE;
	if ((tempNumOfNvmeBlock == 0) || (loop == 0))
		return;

	reqSlotTag = GetFromFreeReqQ();

	reqPoolPtr->reqPool[reqSlotTag].reqType = REQ_TYPE_SLICE;
	reqPoolPtr->reqPool[reqSlotTag].reqCode = reqCode;
	reqPoolPtr->reqPool[reqSlotTag].nvmeCmdSlotTag = cmdSlotTag;
	reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr = tempLsa;
	reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.startIndex = nvmeDmaStartIndex;
	reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.nvmeBlockOffset = nvmeBlockOffset;
	reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.numOfNvmeBlock = tempNumOfNvmeBlock;

	PutToSliceReqQ(reqSlotTag);
}

void EvictDataBufEntry(unsigned int originReqSlotTag)
{
	unsigned int reqSlotTag, virtualSliceAddr, dataBufEntry;

	dataBufEntry = reqPoolPtr->reqPool[originReqSlotTag].dataBufInfo.entry;
	if (dataBufMapPtr->dataBuf[dataBufEntry].dirty == DATA_BUF_DIRTY)
	{
		reqSlotTag = GetFromFreeReqQ();
		virtualSliceAddr = AddrTransWrite(dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr);

		reqPoolPtr->reqPool[reqSlotTag].reqType = REQ_TYPE_NAND;
		reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_WRITE;
		reqPoolPtr->reqPool[reqSlotTag].nvmeCmdSlotTag = reqPoolPtr->reqPool[originReqSlotTag].nvmeCmdSlotTag;
		reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr = dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat = REQ_OPT_DATA_BUF_ENTRY;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr = REQ_OPT_NAND_ADDR_VSA;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEcc = REQ_OPT_NAND_ECC_ON;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEccWarning = REQ_OPT_NAND_ECC_WARNING_ON;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck = REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.blockSpace = REQ_OPT_BLOCK_SPACE_MAIN;
		reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry = dataBufEntry;
		UpdateDataBufEntryInfoBlockingReq(dataBufEntry, reqSlotTag);
		reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr = virtualSliceAddr;

		SelectLowLevelReqQ(reqSlotTag);

		dataBufMapPtr->dataBuf[dataBufEntry].dirty = DATA_BUF_CLEAN;
	}
}

void DataReadFromNand(unsigned int originReqSlotTag)
{
	unsigned int reqSlotTag, virtualSliceAddr;

	virtualSliceAddr = AddrTransRead(reqPoolPtr->reqPool[originReqSlotTag].logicalSliceAddr);

	if (virtualSliceAddr != VSA_FAIL)
	{
		reqSlotTag = GetFromFreeReqQ();

		reqPoolPtr->reqPool[reqSlotTag].reqType = REQ_TYPE_NAND;
		reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_READ;
		reqPoolPtr->reqPool[reqSlotTag].nvmeCmdSlotTag = reqPoolPtr->reqPool[originReqSlotTag].nvmeCmdSlotTag;
		reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr = reqPoolPtr->reqPool[originReqSlotTag].logicalSliceAddr;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat = REQ_OPT_DATA_BUF_ENTRY;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr = REQ_OPT_NAND_ADDR_VSA;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEcc = REQ_OPT_NAND_ECC_ON;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEccWarning = REQ_OPT_NAND_ECC_WARNING_ON;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck = REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.blockSpace = REQ_OPT_BLOCK_SPACE_MAIN;

		reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry = reqPoolPtr->reqPool[originReqSlotTag].dataBufInfo.entry;
		UpdateDataBufEntryInfoBlockingReq(reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry, reqSlotTag);
		reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr = virtualSliceAddr;

		SelectLowLevelReqQ(reqSlotTag);
	}
}

void ReqTransSliceToLowLevel()
{
	unsigned int reqSlotTag, dataBufEntry;

	// 当 slice 请求队列头部不为空时，持续处理请求
	while (sliceReqQ.headReq != REQ_SLOT_TAG_NONE)
	{
		// 从 slice 请求队列中获取一个请求槽标识符
		reqSlotTag = GetFromSliceReqQ();

		// 如果队列为空，返回
		if (reqSlotTag == REQ_SLOT_TAG_FAIL)
			return;

		// 为请求分配数据缓冲区条目
		dataBufEntry = CheckDataBufHit(reqSlotTag);

		// 如果数据缓冲区命中（即请求的数据已经在缓冲区中），则直接使用现有的缓冲区
		if (dataBufEntry != DATA_BUF_FAIL)
		{
			reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry = dataBufEntry;
		}
		else
		{
			// 如果数据缓冲区未命中，分配一个新的数据缓冲区条目
			dataBufEntry = AllocateDataBuf();
			reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry = dataBufEntry;

			// 清除已分配的数据缓冲区条目，这个条目之前可能已经被其他请求使用
			EvictDataBufEntry(reqSlotTag);

			// 更新分配的数据缓冲区条目的元数据
			dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr = reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr;

			// 将分配的数据缓冲区条目加入到数据缓冲区哈希列表
			PutToDataBufHashList(dataBufEntry);

			// 如果请求是读取操作，则从 NAND 中读取数据
			if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_READ)
				DataReadFromNand(reqSlotTag);
			else if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_WRITE)
				// 如果请求是写操作并且是读-修改写操作（即写操作跨越多个块），也需要先读取数据
				if (reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.numOfNvmeBlock != NVME_BLOCKS_PER_SLICE)
					DataReadFromNand(reqSlotTag);
		}

		// 将这个 slice 请求转换为 NVMe 请求
		if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_WRITE)
		{
			// 如果是写操作，标记数据缓冲区为脏（已修改）
			dataBufMapPtr->dataBuf[dataBufEntry].dirty = DATA_BUF_DIRTY;
			reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_RxDMA;
		}
		else if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_READ)
			reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_TxDMA;
		else
			assert(!"[WARNING] Not supported reqCode. [WARNING]");

		// 更新请求的类型为 NVMe DMA 请求
		reqPoolPtr->reqPool[reqSlotTag].reqType = REQ_TYPE_NVME_DMA;

		// 设置请求的选项，指示数据缓冲区格式为数据缓冲区条目
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat = REQ_OPT_DATA_BUF_ENTRY;

		// 更新与请求相关的数据缓冲区条目信息
		UpdateDataBufEntryInfoBlockingReq(dataBufEntry, reqSlotTag);

		// 将请求加入低级请求队列
		SelectLowLevelReqQ(reqSlotTag);
	}
}

unsigned int CheckBufDep(unsigned int reqSlotTag)
{
	if (reqPoolPtr->reqPool[reqSlotTag].prevBlockingReq == REQ_SLOT_TAG_NONE)
		return BUF_DEPENDENCY_REPORT_PASS;
	else
		return BUF_DEPENDENCY_REPORT_BLOCKED;
}

unsigned int CheckRowAddrDep(unsigned int reqSlotTag, unsigned int checkRowAddrDepOpt)
{
	unsigned int dieNo, chNo, wayNo, blockNo, pageNo;

	if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr == REQ_OPT_NAND_ADDR_VSA)
	{
		dieNo = Vsa2VdieTranslation(reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr);
		chNo = Vdie2PchTranslation(dieNo);
		wayNo = Vdie2PwayTranslation(dieNo);
		blockNo = Vsa2VblockTranslation(reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr);
		pageNo = Vsa2VpageTranslation(reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr);
	}
	else
		assert(!"[WARNING] Not supported reqOpt-nandAddress [WARNING]");

	if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_READ)
	{
		if (checkRowAddrDepOpt == ROW_ADDR_DEPENDENCY_CHECK_OPT_SELECT)
		{
			if (rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].blockedEraseReqFlag)
				SyncReleaseEraseReq(chNo, wayNo, blockNo);

			if (pageNo < rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].permittedProgPage)
				return ROW_ADDR_DEPENDENCY_REPORT_PASS;

			rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].blockedReadReqCnt++;
		}
		else if (checkRowAddrDepOpt == ROW_ADDR_DEPENDENCY_CHECK_OPT_RELEASE)
		{
			if (pageNo < rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].permittedProgPage)
			{
				rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].blockedReadReqCnt--;
				return ROW_ADDR_DEPENDENCY_REPORT_PASS;
			}
		}
		else
			assert(!"[WARNING] Not supported checkRowAddrDepOpt [WARNING]");
	}
	else if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_WRITE)
	{
		if (pageNo == rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].permittedProgPage)
		{
			rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].permittedProgPage++;

			return ROW_ADDR_DEPENDENCY_REPORT_PASS;
		}
	}
	else if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_ERASE)
	{
		if (rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].permittedProgPage == reqPoolPtr->reqPool[reqSlotTag].nandInfo.programmedPageCnt)
			if (rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].blockedReadReqCnt == 0)
			{
				rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].permittedProgPage = 0;
				rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].blockedEraseReqFlag = 0;

				return ROW_ADDR_DEPENDENCY_REPORT_PASS;
			}

		if (checkRowAddrDepOpt == ROW_ADDR_DEPENDENCY_CHECK_OPT_SELECT)
			rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].blockedEraseReqFlag = 1;
		else if (checkRowAddrDepOpt == ROW_ADDR_DEPENDENCY_CHECK_OPT_RELEASE)
		{
			// pass, go to return
		}
		else
			assert(!"[WARNING] Not supported checkRowAddrDepOpt [WARNING]");
	}
	else
		assert(!"[WARNING] Not supported reqCode [WARNING]");

	return ROW_ADDR_DEPENDENCY_REPORT_BLOCKED;
}

unsigned int UpdateRowAddrDepTableForBufBlockedReq(unsigned int reqSlotTag)
{
	unsigned int dieNo, chNo, wayNo, blockNo, pageNo, bufDepCheckReport;

	if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr == REQ_OPT_NAND_ADDR_VSA)
	{
		dieNo = Vsa2VdieTranslation(reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr);
		chNo = Vdie2PchTranslation(dieNo);
		wayNo = Vdie2PwayTranslation(dieNo);
		blockNo = Vsa2VblockTranslation(reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr);
		pageNo = Vsa2VpageTranslation(reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr);
	}
	else
		assert(!"[WARNING] Not supported reqOpt-nandAddress [WARNING]");

	if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_READ)
	{
		if (rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].blockedEraseReqFlag)
		{
			SyncReleaseEraseReq(chNo, wayNo, blockNo);

			bufDepCheckReport = CheckBufDep(reqSlotTag);
			if (bufDepCheckReport == BUF_DEPENDENCY_REPORT_PASS)
			{
				if (pageNo < rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].permittedProgPage)
					PutToNandReqQ(reqSlotTag, chNo, wayNo);
				else
				{
					rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].blockedReadReqCnt++;
					PutToBlockedByRowAddrDepReqQ(reqSlotTag, chNo, wayNo);
				}

				return ROW_ADDR_DEPENDENCY_TABLE_UPDATE_REPORT_SYNC;
			}
		}
		rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].blockedReadReqCnt++;
	}
	else if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_ERASE)
		rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].blockedEraseReqFlag = 1;

	return ROW_ADDR_DEPENDENCY_TABLE_UPDATE_REPORT_DONE;
}

void SelectLowLevelReqQ(unsigned int reqSlotTag)
{
	unsigned int dieNo, chNo, wayNo, bufDepCheckReport, rowAddrDepCheckReport, rowAddrDepTableUpdateReport;

	// 检查缓冲区依赖性，返回缓冲区依赖性报告
	bufDepCheckReport = CheckBufDep(reqSlotTag);

	// 如果缓冲区依赖性检查通过
	if (bufDepCheckReport == BUF_DEPENDENCY_REPORT_PASS)
	{
		// 如果请求是 NVMe DMA 请求
		if (reqPoolPtr->reqPool[reqSlotTag].reqType == REQ_TYPE_NVME_DMA)
		{
			// 发出 NVMe DMA 请求
			IssueNvmeDmaReq(reqSlotTag);
			// 将请求加入到 NVMe DMA 请求队列
			PutToNvmeDmaReqQ(reqSlotTag);
		}
		// 如果请求是 NAND 请求
		else if (reqPoolPtr->reqPool[reqSlotTag].reqType == REQ_TYPE_NAND)
		{
			// 如果 NAND 地址类型是虚拟切片地址 (VSA)
			if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr == REQ_OPT_NAND_ADDR_VSA)
			{
				// 将虚拟切片地址转换为物理块（die），然后映射到物理通道和物理位线
				dieNo = Vsa2VdieTranslation(reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr);
				chNo = Vdie2PchTranslation(dieNo);
				wayNo = Vdie2PwayTranslation(dieNo);
			}
			// 如果 NAND 地址类型是物理地址
			else if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr == REQ_OPT_NAND_ADDR_PHY_ORG)
			{
				chNo = reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalCh;
				wayNo = reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalWay;
			}
			else
				assert(!"[WARNING] Not supported reqOpt-nandAddress [WARNING]");

			// 如果要求检查行地址依赖性
			if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck == REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK)
			{
				// 检查行地址依赖性
				rowAddrDepCheckReport = CheckRowAddrDep(reqSlotTag, ROW_ADDR_DEPENDENCY_CHECK_OPT_SELECT);

				// 如果行地址依赖性检查通过
				if (rowAddrDepCheckReport == ROW_ADDR_DEPENDENCY_REPORT_PASS)
					// 将请求加入到 NAND 请求队列
					PutToNandReqQ(reqSlotTag, chNo, wayNo);
				// 如果行地址依赖性检查被阻塞
				else if (rowAddrDepCheckReport == ROW_ADDR_DEPENDENCY_REPORT_BLOCKED)
					// 将请求加入到被行地址依赖性阻塞的请求队列
					PutToBlockedByRowAddrDepReqQ(reqSlotTag, chNo, wayNo);
				else
					assert(!"[WARNING] Not supported report [WARNING]");
			}
			// 如果不检查行地址依赖性
			else if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck == REQ_OPT_ROW_ADDR_DEPENDENCY_NONE)
				// 将请求加入到 NAND 请求队列
				PutToNandReqQ(reqSlotTag, chNo, wayNo);
			else
				assert(!"[WARNING] Not supported reqOpt [WARNING]");
		}
		else
			assert(!"[WARNING] Not supported reqType [WARNING]");
	}
	// 如果缓冲区依赖性检查被阻塞
	else if (bufDepCheckReport == BUF_DEPENDENCY_REPORT_BLOCKED)
	{
		// 如果请求是 NAND 请求且需要检查行地址依赖性
		if (reqPoolPtr->reqPool[reqSlotTag].reqType == REQ_TYPE_NAND)
			if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck == REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK)
			{
				// 更新行地址依赖表，处理缓冲区阻塞请求
				rowAddrDepTableUpdateReport = UpdateRowAddrDepTableForBufBlockedReq(reqSlotTag);

				// 如果行地址依赖性表更新完成
				if (rowAddrDepTableUpdateReport == ROW_ADDR_DEPENDENCY_TABLE_UPDATE_REPORT_DONE)
				{
					// 继续执行，将请求加入到缓冲区阻塞请求队列
				}
				// 如果行地址依赖性表需要同步，返回，等待同步完成
				else if (rowAddrDepTableUpdateReport == ROW_ADDR_DEPENDENCY_TABLE_UPDATE_REPORT_SYNC)
					return;
				else
					assert(!"[WARNING] Not supported report [WARNING]");
			}

		// 将请求加入到被缓冲区依赖性阻塞的请求队列
		PutToBlockedByBufDepReqQ(reqSlotTag);
	}
	else
		assert(!"[WARNING] Not supported report [WARNING]");
}

void ReleaseBlockedByBufDepReq(unsigned int reqSlotTag)
{
	unsigned int targetReqSlotTag, dieNo, chNo, wayNo, rowAddrDepCheckReport;

	targetReqSlotTag = REQ_SLOT_TAG_NONE;
	if (reqPoolPtr->reqPool[reqSlotTag].nextBlockingReq != REQ_SLOT_TAG_NONE)
	{
		targetReqSlotTag = reqPoolPtr->reqPool[reqSlotTag].nextBlockingReq;
		reqPoolPtr->reqPool[targetReqSlotTag].prevBlockingReq = REQ_SLOT_TAG_NONE;
		reqPoolPtr->reqPool[reqSlotTag].nextBlockingReq = REQ_SLOT_TAG_NONE;
	}

	if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat == REQ_OPT_DATA_BUF_ENTRY)
	{
		if (dataBufMapPtr->dataBuf[reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry].blockingReqTail == reqSlotTag)
			dataBufMapPtr->dataBuf[reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry].blockingReqTail = REQ_SLOT_TAG_NONE;
	}
	else if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat == REQ_OPT_DATA_BUF_TEMP_ENTRY)
	{
		if (tempDataBufMapPtr->tempDataBuf[reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry].blockingReqTail == reqSlotTag)
			tempDataBufMapPtr->tempDataBuf[reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry].blockingReqTail = REQ_SLOT_TAG_NONE;
	}

	if ((targetReqSlotTag != REQ_SLOT_TAG_NONE) && (reqPoolPtr->reqPool[targetReqSlotTag].reqQueueType == REQ_QUEUE_TYPE_BLOCKED_BY_BUF_DEP))
	{
		SelectiveGetFromBlockedByBufDepReqQ(targetReqSlotTag);

		if (reqPoolPtr->reqPool[targetReqSlotTag].reqType == REQ_TYPE_NVME_DMA)
		{
			IssueNvmeDmaReq(targetReqSlotTag);
			PutToNvmeDmaReqQ(targetReqSlotTag);
		}
		else if (reqPoolPtr->reqPool[targetReqSlotTag].reqType == REQ_TYPE_NAND)
		{
			if (reqPoolPtr->reqPool[targetReqSlotTag].reqOpt.nandAddr == REQ_OPT_NAND_ADDR_VSA)
			{
				dieNo = Vsa2VdieTranslation(reqPoolPtr->reqPool[targetReqSlotTag].nandInfo.virtualSliceAddr);
				chNo = Vdie2PchTranslation(dieNo);
				wayNo = Vdie2PwayTranslation(dieNo);
			}
			else
				assert(!"[WARNING] Not supported reqOpt-nandAddress [WARNING]");

			if (reqPoolPtr->reqPool[targetReqSlotTag].reqOpt.rowAddrDependencyCheck == REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK)
			{
				rowAddrDepCheckReport = CheckRowAddrDep(targetReqSlotTag, ROW_ADDR_DEPENDENCY_CHECK_OPT_RELEASE);

				if (rowAddrDepCheckReport == ROW_ADDR_DEPENDENCY_REPORT_PASS)
					PutToNandReqQ(targetReqSlotTag, chNo, wayNo);
				else if (rowAddrDepCheckReport == ROW_ADDR_DEPENDENCY_REPORT_BLOCKED)
					PutToBlockedByRowAddrDepReqQ(targetReqSlotTag, chNo, wayNo);
				else
					assert(!"[WARNING] Not supported report [WARNING]");
			}
			else if (reqPoolPtr->reqPool[targetReqSlotTag].reqOpt.rowAddrDependencyCheck == REQ_OPT_ROW_ADDR_DEPENDENCY_NONE)
				PutToNandReqQ(targetReqSlotTag, chNo, wayNo);
			else
				assert(!"[WARNING] Not supported reqOpt [WARNING]");
		}
	}
}

void ReleaseBlockedByRowAddrDepReq(unsigned int chNo, unsigned int wayNo)
{
	unsigned int reqSlotTag, nextReq, rowAddrDepCheckReport;

	reqSlotTag = blockedByRowAddrDepReqQ[chNo][wayNo].headReq;

	while (reqSlotTag != REQ_SLOT_TAG_NONE)
	{
		nextReq = reqPoolPtr->reqPool[reqSlotTag].nextReq;

		if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck == REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK)
		{
			rowAddrDepCheckReport = CheckRowAddrDep(reqSlotTag, ROW_ADDR_DEPENDENCY_CHECK_OPT_RELEASE);

			if (rowAddrDepCheckReport == ROW_ADDR_DEPENDENCY_REPORT_PASS)
			{
				SelectiveGetFromBlockedByRowAddrDepReqQ(reqSlotTag, chNo, wayNo);
				PutToNandReqQ(reqSlotTag, chNo, wayNo);
			}
			else if (rowAddrDepCheckReport == ROW_ADDR_DEPENDENCY_REPORT_BLOCKED)
			{
				// pass, go to while loop
			}
			else
				assert(!"[WARNING] Not supported report [WARNING]");
		}
		else
			assert(!"[WARNING] Not supported reqOpt [WARNING]");

		reqSlotTag = nextReq;
	}
}

void IssueNvmeDmaReq(unsigned int reqSlotTag)
{
	unsigned int devAddr, dmaIndex, numOfNvmeBlock;

	// 获取 DMA 索引，起始地址和 NVMe 块的数量
	dmaIndex = reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.startIndex;
	devAddr = GenerateDataBufAddr(reqSlotTag); // 生成数据缓冲区的物理地址
	numOfNvmeBlock = 0;						   // 初始化已处理的 NVMe 块数

	// 如果请求是接收 DMA 请求 (RxDMA)
	if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_RxDMA)
	{
		// 循环处理所有需要接收的 NVMe 块
		while (numOfNvmeBlock < reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.numOfNvmeBlock)
		{
			// 设置自动接收 DMA 操作
			set_auto_rx_dma(reqPoolPtr->reqPool[reqSlotTag].nvmeCmdSlotTag, dmaIndex, devAddr, NVME_COMMAND_AUTO_COMPLETION_ON);

			// 更新已处理的 NVMe 块数，调整 DMA 索引和数据地址
			numOfNvmeBlock++;
			dmaIndex++;
			devAddr += BYTES_PER_NVME_BLOCK; // 偏移到下一个 NVMe 块
		}

		// 设置 DMA 请求的尾部指针和溢出计数
		reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.reqTail = g_hostDmaStatus.fifoTail.autoDmaRx;
		reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.overFlowCnt = g_hostDmaAssistStatus.autoDmaRxOverFlowCnt;
	}
	// 如果请求是发送 DMA 请求 (TxDMA)
	else if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_TxDMA)
	{
		// 循环处理所有需要发送的 NVMe 块
		while (numOfNvmeBlock < reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.numOfNvmeBlock)
		{
			// 设置自动发送 DMA 操作
			set_auto_tx_dma(reqPoolPtr->reqPool[reqSlotTag].nvmeCmdSlotTag, dmaIndex, devAddr, NVME_COMMAND_AUTO_COMPLETION_ON);

			// 更新已处理的 NVMe 块数，调整 DMA 索引和数据地址
			numOfNvmeBlock++;
			dmaIndex++;
			devAddr += BYTES_PER_NVME_BLOCK; // 偏移到下一个 NVMe 块
		}

		// 设置 DMA 请求的尾部指针和溢出计数
		reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.reqTail = g_hostDmaStatus.fifoTail.autoDmaTx;
		reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.overFlowCnt = g_hostDmaAssistStatus.autoDmaTxOverFlowCnt;
	}
	else
		// 如果请求的代码不是接收或发送 DMA，报错
		assert(!"[WARNING] Not supported reqCode [WARNING]");
}

void CheckDoneNvmeDmaReq()
{
	unsigned int reqSlotTag, prevReq;
	unsigned int rxDone, txDone;

	reqSlotTag = nvmeDmaReqQ.tailReq;
	rxDone = 0;
	txDone = 0;

	while (reqSlotTag != REQ_SLOT_TAG_NONE)
	{
		prevReq = reqPoolPtr->reqPool[reqSlotTag].prevReq;

		if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_RxDMA)
		{
			if (!rxDone)
				rxDone = check_auto_rx_dma_partial_done(reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.reqTail, reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.overFlowCnt);

			if (rxDone)
				SelectiveGetFromNvmeDmaReqQ(reqSlotTag);
		}
		else
		{
			if (!txDone)
				txDone = check_auto_tx_dma_partial_done(reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.reqTail, reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.overFlowCnt);

			if (txDone)
				SelectiveGetFromNvmeDmaReqQ(reqSlotTag);
		}

		reqSlotTag = prevReq;
	}
}
