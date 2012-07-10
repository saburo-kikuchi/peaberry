// Copyright 2012 David Turnbull AE9RB
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <peaberry.h>

#define PCM3060_ADDR 0x46

// Delay a whole sample to swap endians on 24-bit words using DMA.
void LoadSwapOrder(uint8* a) {
    a[0] = 5;
    a[1] = 4;
    a[2] = 3;
    a[3] = 8;
    a[4] = 7;
    a[5] = 6;
    a[6] = 2;
    a[7] = 1;
    a[8] = 0;
}


uint8 RxI2S_Buff_Chan, RxI2S_Buff_TD[DMA_AUDIO_BUFS];
volatile uint8 RxI2S_Buff[USB_AUDIO_BUFS][I2S_BUF_SIZE], RxI2S_Swap[9], RxI2S_Move, RxI2S_DMA_TD;

CY_ISR(RxI2S_DMA_done) {
    RxI2S_DMA_TD = DMAC_CH[RxI2S_Buff_Chan].basic_status[1] & 0x7Fu;
}

void DmaRxConfiguration(void)
{
    uint8 RxI2S_Swap_Chan, RxI2S_Stage_Chan, RxI2S_Stage_TD[9], RxI2S_Swap_TD[9];
	uint8 i, n, order[9];
    LoadSwapOrder(order);

    RxI2S_Stage_Chan = RxI2S_Stage_DmaInitialize(1, 1, HI16(CYDEV_PERIPH_BASE), HI16(CYDEV_SRAM_BASE));
	for (i=0; i < 9; i++) RxI2S_Stage_TD[i]=CyDmaTdAllocate();
    for (i=0; i < 9; i++) {
	    n = i + 1;
	    if (n >= 9) n=0;
        CyDmaTdSetConfiguration(RxI2S_Stage_TD[i], 1, RxI2S_Stage_TD[n], RxI2S_Stage__TD_TERMOUT_EN );
	    CyDmaTdSetAddress(RxI2S_Stage_TD[i], LO16(I2S_RX_FIFO_0_PTR), LO16(&RxI2S_Swap[i]));
    }
	CyDmaChSetInitialTd(RxI2S_Stage_Chan, RxI2S_Stage_TD[0]);

    RxI2S_Swap_Chan = RxI2S_Swap_DmaInitialize(1, 1, HI16(CYDEV_SRAM_BASE), HI16(CYDEV_SRAM_BASE));
	for (i=0; i < 9; i++) RxI2S_Swap_TD[i]=CyDmaTdAllocate();
    for (i=0; i < 9; i++) {
        n = i + 1;
        if (n >= 9) n=0;
        CyDmaTdSetConfiguration(RxI2S_Swap_TD[i], 1, RxI2S_Swap_TD[n], RxI2S_Swap__TD_TERMOUT_EN);
        CyDmaTdSetAddress(RxI2S_Swap_TD[i], LO16(&RxI2S_Swap[order[i]]), LO16(&RxI2S_Move));
    }
	CyDmaChSetInitialTd(RxI2S_Swap_Chan, RxI2S_Swap_TD[0]);

    RxI2S_Buff_Chan = RxI2S_Buff_DmaInitialize(1, 1, HI16(CYDEV_SRAM_BASE), HI16(CYDEV_SRAM_BASE));
	for (i=0; i < DMA_AUDIO_BUFS; i++) RxI2S_Buff_TD[i] = CyDmaTdAllocate();
	for (i=0; i < DMA_AUDIO_BUFS; i++) {
	    CyDmaTdSetConfiguration(RxI2S_Buff_TD[i], I2S_BUF_SIZE/2, RxI2S_Buff_TD[i+1], TD_INC_DST_ADR | RxI2S_Buff__TD_TERMOUT_EN );	
	    CyDmaTdSetAddress(RxI2S_Buff_TD[i], LO16(&RxI2S_Move), LO16(RxI2S_Buff[i/2]));
        i++;
	 	n = i + 1;
		if (n >= DMA_AUDIO_BUFS) n=0;
	    CyDmaTdSetConfiguration(RxI2S_Buff_TD[i], I2S_BUF_SIZE/2, RxI2S_Buff_TD[n], TD_INC_DST_ADR | RxI2S_Buff__TD_TERMOUT_EN );	
	    CyDmaTdSetAddress(RxI2S_Buff_TD[i], LO16(&RxI2S_Move), LO16(RxI2S_Buff[i/2] + I2S_BUF_SIZE/2));
	}
	CyDmaChSetInitialTd(RxI2S_Buff_Chan, RxI2S_Buff_TD[0]);
	
    RxI2S_DMA_TD = RxI2S_Buff_TD[0];
	RxI2S_done_isr_Start();
    RxI2S_done_isr_SetVector(RxI2S_DMA_done);

    CyDmaChEnable(RxI2S_Buff_Chan, 1u);
	CyDmaChEnable(RxI2S_Stage_Chan, 1u);
	CyDmaChEnable(RxI2S_Swap_Chan, 1u);
}


uint8 TxI2S_Buff_Chan, TxI2S_Buff_TD[DMA_AUDIO_BUFS], TxBufCountdown = 0;
volatile uint8 TxI2S_Buff[USB_AUDIO_BUFS][I2S_BUF_SIZE], TxI2S_Swap[9], TxI2S_Stage, TxI2S_DMA_TD;

CY_ISR(TxI2S_DMA_done) {
    if (TxBufCountdown) TxBufCountdown--;
    TxI2S_DMA_TD = DMAC_CH[TxI2S_Buff_Chan].basic_status[1] & 0x7Fu;
}

void DmaTxConfiguration(void) {
    uint8 TxI2S_Swap_Chan, TxI2S_Swap_TD[9], TxI2S_Stage_Chan, TxI2S_Stage_TD[9];
	uint8 i, n, order[9];
    LoadSwapOrder(order);
	
    TxI2S_Swap_Chan = TxI2S_Swap_DmaInitialize(1, 1, HI16(CYDEV_SRAM_BASE), HI16(CYDEV_PERIPH_BASE));
    for (i=0; i < 9; i++) TxI2S_Swap_TD[i]=CyDmaTdAllocate();
    for (i=0; i < 9; i++) {
	 	n = i + 1;
		if (n >= 9) n=0;
	    CyDmaTdSetConfiguration(TxI2S_Swap_TD[i], 1, TxI2S_Swap_TD[n], TxI2S_Swap__TD_TERMOUT_EN);
	    CyDmaTdSetAddress(TxI2S_Swap_TD[i], LO16(&TxI2S_Swap[order[i]]), LO16(I2S_TX_FIFO_0_PTR));
    }
	CyDmaChSetInitialTd(TxI2S_Swap_Chan, TxI2S_Swap_TD[0]);
    
    TxI2S_Stage_Chan = TxI2S_Stage_DmaInitialize(1, 1, HI16(CYDEV_SRAM_BASE), HI16(CYDEV_SRAM_BASE));
    for (i=0; i < 9; i++) TxI2S_Stage_TD[i]=CyDmaTdAllocate();
    for (i=0; i < 9; i++) {
	 	n = i + 1;
		if (n >= 9) n=0;
	    CyDmaTdSetConfiguration(TxI2S_Stage_TD[i], 1, TxI2S_Stage_TD[n], TxI2S_Stage__TD_TERMOUT_EN);
	    CyDmaTdSetAddress(TxI2S_Stage_TD[i], LO16(&TxI2S_Stage), LO16(TxI2S_Swap + i));
    }
	CyDmaChSetInitialTd(TxI2S_Stage_Chan, TxI2S_Stage_TD[8]);

    TxI2S_Buff_Chan = TxI2S_Buff_DmaInitialize(1, 1, HI16(CYDEV_SRAM_BASE), HI16(CYDEV_SRAM_BASE));
	for (i=0; i < DMA_AUDIO_BUFS; i++) TxI2S_Buff_TD[i] = CyDmaTdAllocate();
	for (i=0; i < DMA_AUDIO_BUFS; i++) {
	    CyDmaTdSetConfiguration(TxI2S_Buff_TD[i], I2S_BUF_SIZE/2, TxI2S_Buff_TD[i+1], (TD_INC_SRC_ADR | TxI2S_Buff__TD_TERMOUT_EN) );	
	    CyDmaTdSetAddress(TxI2S_Buff_TD[i], LO16(TxI2S_Buff[i/2]), LO16(&TxI2S_Stage));
        i++;
	 	n = i + 1;
		if (n >= DMA_AUDIO_BUFS) n=0;
	    CyDmaTdSetConfiguration(TxI2S_Buff_TD[i], I2S_BUF_SIZE/2, TxI2S_Buff_TD[n], (TD_INC_SRC_ADR | TxI2S_Buff__TD_TERMOUT_EN) );	
	    CyDmaTdSetAddress(TxI2S_Buff_TD[i], LO16(TxI2S_Buff[i/2] + I2S_BUF_SIZE/2), LO16(&TxI2S_Stage));
	}
	CyDmaChSetInitialTd(TxI2S_Buff_Chan, TxI2S_Buff_TD[0]);

    TxI2S_DMA_TD = TxI2S_Buff_TD[0];
    TxI2S_done_isr_Start();
    TxI2S_done_isr_SetVector(TxI2S_DMA_done);

    CyDmaChEnable(TxI2S_Buff_Chan, 1u);
	CyDmaChEnable(TxI2S_Stage_Chan, 1u);
	CyDmaChEnable(TxI2S_Swap_Chan, 1u);
}


uint8* PCM3060_TxBuf(void) {
    static uint8 debounce = 0, use = 0;
    uint8 dma, td;
    td = TxI2S_DMA_TD; // volatile
    for (dma = 0; dma < DMA_AUDIO_BUFS; dma++) {
        if (td == TxI2S_Buff_TD[dma]) break;
    }
    USBAudio_SyncBufs(dma, &use, &debounce, !USBAudio_RX_Enabled);
    return TxI2S_Buff[use];
}

uint8* PCM3060_RxBuf(void) {
    static uint8 debounce = 0, use = 0;
    uint8 dma, td;
    td = RxI2S_DMA_TD; // volatile
    for (dma = 0; dma < DMA_AUDIO_BUFS; dma++) {
        if (td == RxI2S_Buff_TD[dma]) break;
    }
    USBAudio_SyncBufs(dma, &use, &debounce, USBAudio_RX_Enabled);
    return RxI2S_Buff[use];
}


void PCM3060_Start(void) {
	uint8 pcm3060_cmd[2], i, state = 0;

    DmaTxConfiguration();
    DmaRxConfiguration();
    
    I2S_Start();
    I2S_EnableTx();
    I2S_EnableRx();
    
    
    while (state < 2) {
        switch (state) {
        case 0: // Take PCM3060 out of sleep mode
        	pcm3060_cmd[0] = 0x40;
            pcm3060_cmd[1] = 0xC0;
            I2C_MasterWriteBuf(PCM3060_ADDR, pcm3060_cmd, 2, I2C_MODE_COMPLETE_XFER);
            state++;
            break;
        case 1:
            i = I2C_MasterStatus();
            if (i & I2C_MSTAT_ERR_XFER) {
                state--;
            } else if (i & I2C_MSTAT_WR_CMPLT) {
                state++;
            }
            break;
        }
    }
}


void PCM3060_Main(void) {
    static uint8 state = 0, volume = 0xFF, pcm3060_cmd[3];
    uint8 i;
    
    switch (state) {
    case 0:
        if (!Locked_I2C) {
            if (TX_Request && !TX_Enabled) {
                state = 1;
                volume = 0;
                Locked_I2C = 1;
            }
            else if (!TX_Request && TX_Enabled) {
                state = 10;
                volume = 0;
                Locked_I2C = 1;
            }
            else if (!TX_Enabled) {
                //TODO USB volume
            }
        }
        break;
    case 1:
    case 4:
    case 10:
    case 14:
    	pcm3060_cmd[0] = 0x41;
        pcm3060_cmd[1] = volume;
        pcm3060_cmd[2] = volume;
        I2C_MasterWriteBuf(PCM3060_ADDR, pcm3060_cmd, 3, I2C_MODE_COMPLETE_XFER);
        state++;
        break;
    case 2:
    case 5:
    case 11:
    case 15:
        i = I2C_MasterStatus();
        if (i & I2C_MSTAT_ERR_XFER) {
            state--;
        } else if (i & I2C_MSTAT_WR_CMPLT) {
            Locked_I2C = 0;
            TxBufCountdown = 85; // 42.5ms
            state++;
        }
        break;    
    case 3:
        if (!TxBufCountdown && !Locked_I2C) {
            TX_Enabled = 1;
            Locked_I2C = 1;
            volume = 0xFF;
            state++;
        }
        break;
    case 6:
        Control_Write(Control_Read() | CONTROL_TX_ENABLE);
        state = 0;
        break;
    case 12:
        if (!TxBufCountdown && !Locked_I2C) {
            TX_Enabled = 0;
            Locked_I2C = 1;
            state++;
        }
        break;
    case 13:
        volume = 0xFF; //TODO read USB
        state++;
        break;
    case 16:
        Control_Write(Control_Read() & ~CONTROL_TX_ENABLE);
        state = 0;
        break;
    }
}

