#include <ogc/exi.h>
#include <ogc/machine/processor.h>

#include "CommonFuncs.h"
#include "hwtests.h"

#define MEMCARD_UNLOCKED 0x40
#define MEMCARD_READY 0x01

// Card latency table
static u32 g_cardLatency[] =
{
	0x00000004,
	0x00000008,
	0x00000010,
	0x00000020,
	0x00000030,
	0x00000080,
	0x00000100,
	0x00000200
};

// Card sector size latency
static u32 g_sectorSize[] =
{
	0x0002000,
	0x0004000,
	0x0008000,
	0x0010000,
	0x0020000,
	0x0040000,
	0x0000000,
	0x0000000
};

// Static buffers
static u8 g_writeData[128];
static u8 g_readData[0x200];

// TC flags
static bool g_writeTC = true;
static bool g_readTC = true;

static s32 exi_deviceRemoveCallback(s32 channel, s32 device)
{
	// Stub
}

static s32 exi_TransferComplete(s32 channel, s32 device)
{
	g_writeTC = true;
	EXI_Deselect(channel);
	EXI_Unlock(channel);
}

static s32 exi_readPageTransferComplete(s32 channel, s32 device)
{
	g_readTC = true;
	EXI_Deselect(channel);
	EXI_Unlock(channel);
}

void clearStatusMemcard(s32 channel, s32 frequency);

static s32 exi_exiInterrupt(s32 channel, s32 device)
{
	// Is this what we do here?
	clearStatusMemcard(channel, EXI_SPEED16MHZ);
}

u16 __idMemcard(s32 channel, s32 frequency)
{
	const u8 idOp[] = { 0x85, 0x00 };

	u16 ret = 0x0000;
	EXI_Select(channel, EXI_DEVICE_0, frequency);
	EXI_Imm(channel, (void*)idOp, 2, EXI_WRITE, nullptr);
	EXI_Sync(channel);
	EXI_Imm(channel, &ret, 2, EXI_READ, nullptr);
	EXI_Sync(channel);
	EXI_Deselect(channel);
	return ret;
}

u8 __statusMemcard(s32 channel, s32 frequency)
{
	const u8 statusOp[] = { 0x83, 0x00 };

	u8 ret = 0x00;
	EXI_Select(channel, EXI_DEVICE_0, frequency);
	EXI_Imm(channel, (void*)statusOp, 2, EXI_WRITE, nullptr);
	EXI_Sync(channel);
	EXI_Imm(channel, &ret, 1, EXI_READ, nullptr);
	EXI_Sync(channel);
	EXI_Deselect(channel);
	return ret;
}

void __clearStatusMemcard(s32 channel, s32 frequency)
{
	const u8 clearStatusOp[] = { 0x89 };

	EXI_Select(channel, EXI_DEVICE_0, frequency);
	EXI_Imm(channel, (void*)clearStatusOp, 1, EXI_WRITE, nullptr);
	EXI_Sync(channel);
	EXI_Deselect(channel);
}

u16 idMemcard(s32 channel, s32 frequency)
{
	EXI_Lock(channel, EXI_DEVICE_0, nullptr);
	u16 ret = __idMemcard(channel, frequency);
	EXI_Unlock(channel);
	return ret;
}

void enableInterruptMemcard(s32 channel, s32 frequency, u8 enable)
{
	EXI_Lock(channel, EXI_DEVICE_0, nullptr);
	EXI_Select(channel, EXI_DEVICE_0, frequency);

	u8 enableInterruptOp[] =
	{
		0x81,
		enable
	};
	EXI_Imm(channel, (void*)enableInterruptOp, 2, EXI_WRITE, nullptr);
	EXI_Sync(channel);

	EXI_Deselect(channel);
	EXI_Unlock(channel);
}

u8 statusMemcard(s32 channel, s32 frequency)
{
	EXI_Lock(channel, EXI_DEVICE_0, nullptr);
	u8 ret = __statusMemcard(channel, frequency);
	EXI_Unlock(channel);
	return ret;
}

void clearStatusMemcard(s32 channel, s32 frequency)
{
	EXI_Lock(channel, EXI_DEVICE_0, nullptr);
	__clearStatusMemcard(channel, frequency);
	EXI_Unlock(channel);
}

void memcardPageProgram(s32 channel, s32 frequency, u32 offset)
{
	network_printf("EXICHANNEL[%u] page program start;\n", channel);

	EXI_Lock(channel, EXI_DEVICE_0, nullptr);

	u8 memcardStatus = 0x00;
	while(!((memcardStatus = __statusMemcard(channel, frequency)) & MEMCARD_READY))
	{
		network_printf("EXICHANNEL[%u] waiting for ready status; memcardStatus:0x%02x\n", channel, memcardStatus);
	}
	network_printf("EXICHANNEL[%u] card is ready; memcardSatus:0x%02x\n", channel, memcardStatus);

	// Begin block write
	u8 writeOp[5] =
	{
		0xF2,
		(offset >> 17) & 0x3F,
		(offset >> 9) & 0xFF,
		(offset >> 7) & 3,
		offset & 0x7F
	};

	network_printf("EXICHANNEL[%u] writing block start; size:%u\n", channel, 128);
	network_printf("EXICHANNEL[%u] op bytes; 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n", channel, writeOp[0], writeOp[1], writeOp[2], writeOp[3], writeOp[4]);
	EXI_Select(channel, EXI_DEVICE_0, frequency);
	EXI_ImmEx(channel, (void*)writeOp, 5, EXI_WRITE);
	EXI_Dma(channel, g_writeData, 128, EXI_WRITE, exi_TransferComplete);
	g_writeTC = false;

	network_printf("EXICHANNEL[%u] writing block end;\n", channel);
	network_printf("EXICHANNEL[%u] page program end;\n", channel);
}

void memcardReadPage(s32 channel, s32 frequency, u32 latency, u32 offset)
{
	network_printf("EXICHANNEL[%u] read page start;\n", channel);

	EXI_Lock(channel, EXI_DEVICE_0, nullptr);

	// Begin block write
	u8 readOp[5] =
	{
		0x52,
		(offset >> 17) & 0x3F,
		(offset >> 9) & 0xFF,
		(offset >> 7) & 3,
		offset & 0x7F
	};

	network_printf("EXICHANNEL[%u] op bytes; 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n", channel, readOp[0], readOp[1], readOp[2], readOp[3], readOp[4]);
	EXI_Select(channel, EXI_DEVICE_0, frequency);
	EXI_ImmEx(channel, (void*)readOp, 5, EXI_WRITE);

	// Dummy writes
	EXI_ImmEx(channel, g_readData + 128, latency, EXI_WRITE);

	EXI_Dma(channel, g_readData, 512, EXI_READ, exi_readPageTransferComplete);
	g_readTC = false;

	network_printf("EXICHANNEL[%u] read page end;\n", channel);
}

void WaitTC()
{
	while(!g_writeTC);
	while(!g_readTC);
}

int main()
{
	network_init();

	// Initialize write data
	for(s32 i = 0; i < 128; i++)
		g_writeData[i] = i;

	// Clear read data to 0x00
	for(s32 i = 0; i < 0x200; i++)
		g_readData[i] = 0x00;

	// Disable CPU ISR
	//u32 ISR_level;
	//_CPU_ISR_Disable(ISR_level);
	//network_printf("CPU ISR level:%u\n", ISR_level);

	// Memory cards can only be on channel 0 and 1
	for(u32 currentChannel = EXI_CHANNEL_0; currentChannel <= EXI_CHANNEL_1; currentChannel++)
	{
		// Attach exi device
		EXI_Attach(currentChannel, exi_deviceRemoveCallback);

		// Get current state of exi channel
		s32 channelState = EXI_GetState(currentChannel);
		network_printf("EXICHANNEL[%u] state:0x%02x\n", currentChannel, channelState);

		if(channelState & EXI_FLAG_ATTACH)
		{
			u32 cardID = idMemcard(currentChannel, EXI_SPEED16MHZ);
			u32 cardSize = (cardID & 0xFC) / 8;

			u32 cardIDX = _rotl(cardID, 26) & 0x1C;
			u32 cardLatency = g_cardLatency[cardIDX >> 2];

			cardIDX = _rotl(cardID, 23) & 0x1C;
			u32 cardSectorSize = g_sectorSize[cardIDX >> 2];

			u32 cardBlocks = ((cardSize << 20) >> 3) / cardSectorSize;
			u8 cardStatus = statusMemcard(currentChannel, EXI_SPEED16MHZ);

			network_printf("EXICHANNEL[%u] start; id:0x%04x, latency:0x%08x, cardSize:%uMB, sectorSize:0x%08x, cardBlocks:0x%08x, status:0x%02x\n",
					currentChannel, cardID, cardLatency, cardSize, cardSectorSize, cardBlocks, cardStatus);

			clearStatusMemcard(currentChannel, EXI_SPEED16MHZ);
			cardStatus = statusMemcard(currentChannel, EXI_SPEED16MHZ);
			network_printf("EXICHANNEL[%u] status cleared; status:0x%02x\n", currentChannel, cardStatus);

			// Unlock memory card if it isn't already unlocked
			if(!(cardStatus & MEMCARD_UNLOCKED))
			{
				network_printf("EXICHANNEL[%u] card needs unlocked;\n", currentChannel);
			}

			// Enable exi interrupt
			// Is this needed?
			enableInterruptMemcard(currentChannel, EXI_SPEED16MHZ, 1);
			EXI_RegisterEXICallback(currentChannel, exi_exiInterrupt);

			// Test operations on each clock
			for(s32 currentSpeed = EXI_SPEED1MHZ; currentSpeed <= EXI_SPEED1MHZ; currentSpeed++)
			{
				network_printf("EXICHANNEL[%u] speed:0x%02x, testing...\n", currentChannel, currentSpeed);
				memcardPageProgram(currentChannel, currentSpeed, 0xa000);
				// THIS FAILS ON REAL CONSOLE
				//cardStatus = statusMemcard(currentChannel, currentSpeed);
				//network_printf("EXICHANNEL[%u] page write status check; status:0x%02x\n", currentChannel, cardStatus);
				//network_printf("EXICHANNEL[%u] waiting for write transfer to complete;\n", currentChannel);
				WaitTC();

				memcardReadPage(currentChannel, currentSpeed, cardLatency, 0xa000);
				// THIS FAILS ON REAL CONSOLE
				//cardStatus = statusMemcard(currentChannel, currentSpeed);
				//network_printf("EXICHANNEL[%u] page read status check; status:0x%02x\n", currentChannel, cardStatus);
				//network_printf("EXICHANNEL[%u] waiting for read transfer to complete;\n", currentChannel);
				WaitTC();
				network_printf("--Read Data Start--\n");
				for(s32 i = 0; i < 128; i++)
				{
					network_printf("0x%02x ", g_readData[i]);
					if((i % 16) == 15)
						network_printf("\n");
				}
				network_printf("--Read Data End--\n");
			}
		}
		else
		{
			network_printf("EXICHANNEL[%u] nothing attached, skipping\n", currentChannel);
		}

		// Detach EXI device
		EXI_Detach(currentChannel);
	}

	network_shutdown();

	//while(1);

	return 0;
}
