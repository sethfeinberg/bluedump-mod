#include <gccore.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <fat.h>
#include <sdcard/wiisd_io.h>
#include <ogc/usbstorage.h>

#include "tools.h"
#include "RuntimeIOSPatch.h"

//#define DEBUG

extern DISC_INTERFACE __io_usbstorage;

static void *xfb = NULL;
static GXRModeObj *rmode = NULL;

void Reboot()
{
	if (*(u32*)0x80001800) exit(0);
	SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
}

void waitforbuttonpress(u32 *out, u32 *outGC)
{
	u32 pressed = 0;
	u32 pressedGC = 0;

	while (true)
	{
		WPAD_ScanPads();
		pressed = WPAD_ButtonsDown(0) | WPAD_ButtonsDown(1) | WPAD_ButtonsDown(2) | WPAD_ButtonsDown(3);

		PAD_ScanPads();
		pressedGC = PAD_ButtonsDown(0) | PAD_ButtonsDown(1) | PAD_ButtonsDown(2) | PAD_ButtonsDown(3);

		if(pressed || pressedGC) 
		{
			if (pressedGC)
			{
				// Without waiting you can't select anything
				usleep (20000);
			}
			if (out) *out = pressed;
			if (outGC) *outGC = pressedGC;
			return;
		}
	}
}

void Init_Console()
{
	// Initialise the video system
	VIDEO_Init();
	
	// Obtain the preferred video mode from the system
	// This will correspond to the settings in the Wii menu
	rmode = VIDEO_GetPreferredMode(NULL);

	// Allocate memory for the display in the uncached region
	xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	
	// Set up the video registers with the chosen mode
	VIDEO_Configure(rmode);
	
	// Tell the video hardware where our display memory is
	VIDEO_SetNextFramebuffer(xfb);
	
	// Make the display visible
	VIDEO_SetBlack(FALSE);

	// Flush the video register changes to the hardware
	VIDEO_Flush();

	// Wait for Video setup to complete
	VIDEO_WaitVSync();
	if(rmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();

	// Set console parameters
    int x = 24, y = 32, w, h;
    w = rmode->fbWidth - (32);
    h = rmode->xfbHeight - (48);

    // Initialize the console - CON_InitEx works after VIDEO_ calls
	CON_InitEx(rmode, x, y, w, h);

	// Clear the garbage around the edges of the console
    VIDEO_ClearFrameBuffer(rmode, xfb, COLOR_BLACK);
}

void resetscreen()
{
	printf("\x1b[2J");
}

void printheadline()
{
	int rows, cols;
	CON_GetMetrics(&cols, &rows);
	
	printf("BlueDump MOD v0.1.");
	
	char buf[64];
	sprintf(buf, "IOS%u (v%u)", IOS_GetVersion(), IOS_GetRevision());
	printf("\x1B[%d;%dH", 0, cols-strlen(buf)-1);
	printf(buf);
	
	printf("\nOriginal code by nicksasa and WiiPower.");
	printf("\nFixes and corrections by DarkMatterCore.\n\n");
}

void set_highlight(bool highlight)
{
	if (highlight)
	{
		printf("\x1b[%u;%um", 47, false);
		printf("\x1b[%u;%um", 30, false);
	} else
	{
		printf("\x1b[%u;%um", 37, false);
		printf("\x1b[%u;%um", 40, false);
	}
}

void Con_ClearLine()
{
	s32 cols, rows;
	u32 cnt;

	printf("\r");
	fflush(stdout);

	/* Get console metrics */
	CON_GetMetrics(&cols, &rows);

	/* Erase line */
	for (cnt = 1; cnt < cols; cnt++) {
		printf(" ");
		fflush(stdout);
	}

	printf("\r");
	fflush(stdout);
}

s32 Init_SD()
{
	fatUnmount("sd");
	
	__io_wiisd.shutdown();
	
	if (!fatMountSimple("sd", &__io_wiisd))
	{
		return -1;
	}
	
	return 0;
}

void Close_SD()
{
	fatUnmount("sd");
	__io_wiisd.shutdown();
}

s32 Init_USB()
{
	fatUnmount("usb");
	
	bool isMounted = fatMountSimple("usb", &__io_usbstorage);
	
	if(!isMounted)
	{
		fatUnmount("usb");
		
		if (!fatMountSimple("usb", &__io_usbstorage))
		{
			return -1;
		}
		
		bool isInserted = __io_usbstorage.isInserted();
		
		if(isInserted)
		{
			int retry = 10;
			while(retry)
			{ 
				isMounted = fatMountSimple("usb", &__io_usbstorage);
				
				if (isMounted) break;
				
				sleep(1);
				
				retry--;
			}
		}
	}
	
	return 0;
}

void Close_USB()
{
	fatUnmount("usb");
	__io_usbstorage.shutdown();
}

int ahbprot_menu()
{
	u32 pressed;
	u32 pressedGC;

	/* HW_AHBPROT check */
	if (AHBPROT_DISABLED)
	{
		printf("HW_AHBPROT protection is disabled!\n");
		printf("Current IOS: %u.\n\n", IOS_GetVersion());
		
		printf("Press A button to use full hardware access.\n");
		printf("Press B button to reload to another IOS.\n");
		printf("Press HOME or Start to exit.\n\n");
		
		for (;;)
		{
			waitforbuttonpress(&pressed, &pressedGC);
			
			/* A button */
			if (pressed == WPAD_BUTTON_A || pressedGC == PAD_BUTTON_A)
			{
				break;
			}
			
			/* B button */
			if (pressed == WPAD_BUTTON_B || pressedGC == PAD_BUTTON_B)
			{
				resetscreen();
				printheadline();
				return -1;
			}
			
			/* HOME/Start button */
			if (pressed == WPAD_BUTTON_HOME || pressedGC == PAD_BUTTON_START)
			{
				Reboot();
			}
		}
		
		printf("Initializing IOS patches...");
		if (!IOSPATCH_Apply())
		{
			/* This is a very, very weird error */
			
			printf(" ERROR!\n\n");
			printf("\tUnable to load the initial patches. Maybe the loaded IOS isn't\n");
			printf("\tvulnerable for an unknown reason.\n");
			sleep(4);
			printf("\tThis error is very uncommon. I already checked if the HW_AHBPROT\n");
			printf("\tprotection was disabled. You should report this to me as soon as\n");
			printf("\tyou can.\n");
			sleep(4);
			printf("\tI'll let you reload to another IOS instead of kicking you out\n");
			printf("\tto the loader...");
			sleep(4);
			
			resetscreen();
			printheadline();
			
			return -1;
		}
		
		printf(" OK!\n\n");
	} else {
		return -1;
	}
	
	return 0;
}

s32 __u8Cmp(const void *a, const void *b)
{
	return *(u8 *)a-*(u8 *)b;
}

u8 *get_ioslist(u32 *cnt)
{
	u64 *buf = 0;
	s32 i, res;
	u32 tcnt = 0, icnt;
	u8 *ioses = NULL;
	
	//Get stored IOS versions.
	res = ES_GetNumTitles(&tcnt);
	if(res < 0)
	{
		printf("\t- ES_GetNumTitles: Error! (result = %d).\n", res);
		return 0;
	}
	buf = memalign(32, sizeof(u64) * tcnt);
	res = ES_GetTitles(buf, tcnt);
	if(res < 0)
	{
		printf("\t- ES_GetTitles: Error! (result = %d).\n", res);
		if (buf) free(buf);
		return 0;
	}

	icnt = 0;
	for(i = 0; i < tcnt; i++)
	{
		if(*((u32 *)(&(buf[i]))) == 1 && (u32)buf[i] > 2 && (u32)buf[i] < 0x100)
		{
			icnt++;
			ioses = (u8 *)realloc(ioses, sizeof(u8) * icnt);
			ioses[icnt - 1] = (u8)buf[i];
		}
	}

	ioses = (u8 *)malloc(sizeof(u8) * icnt);
	icnt = 0;
	
	for(i = 0; i < tcnt; i++)
	{
		if(*((u32 *)(&(buf[i]))) == 1 && (u32)buf[i] > 2 && (u32)buf[i] < 0x100)
		{
			icnt++;
			ioses[icnt - 1] = (u8)buf[i];
		}
	}
	free(buf);
	qsort(ioses, icnt, 1, __u8Cmp);

	*cnt = icnt;
	return ioses;
}

int ios_selectionmenu(int default_ios)
{
	u32 pressed;
	u32 pressedGC;
	int selection = 0;
	u32 ioscount;
	u8 *list = get_ioslist(&ioscount);
	
	int i;
	for (i=0;i<ioscount;i++)
	{
		// Default to default_ios if found, else the loaded IOS
		if (list[i] == default_ios)
		{
			selection = i;
			break;
		}
		if (list[i] == IOS_GetVersion())
		{
			selection = i;
		}
	}	
	
	while (true)
	{
		printf("\x1B[%d;%dH",4,0);	// move console cursor to y/x
		printf("Select the IOS version to use:       \b\b\b\b\b\b");
		
		set_highlight(true);
		printf("< IOS%u >\n", list[selection]);
		set_highlight(false);
		
		printf("\nPress LEFT/RIGHT to change IOS version.");
		printf("\nPress A button to load the selected IOS.");
		printf("\nPress B to continue without IOS Reload.");
		printf("\nPress HOME or Start to exit.\n");
		
		waitforbuttonpress(&pressed, &pressedGC);
		
		if (pressed == WPAD_BUTTON_LEFT || pressedGC == PAD_BUTTON_LEFT)
		{	
			if (selection > 0)
			{
				selection--;
			} else
			{
				selection = ioscount - 1;
			}
		}
		if (pressed == WPAD_BUTTON_RIGHT || pressedGC == PAD_BUTTON_RIGHT)
		{
			if (selection < ioscount -1	)
			{
				selection++;
			} else
			{
				selection = 0;
			}
		}
		if (pressed == WPAD_BUTTON_A || pressedGC == PAD_BUTTON_A) break;
		if (pressed == WPAD_BUTTON_B || pressedGC == PAD_BUTTON_B) return 0;
		if (pressed == WPAD_BUTTON_HOME || pressedGC == PAD_BUTTON_START) Reboot();
	}
	return list[selection];
}

void Mount_Devices()
{
	int ret;
	u32 pressed;
	u32 pressedGC;
	
	printf("\n\t- SD Card: ");
	ret = Init_SD();
	if (ret < 0)
	{
		printf("FAILED.\n");
		SDmnt = false;
	} else {
		printf("OK.\n");
		SDmnt = true;
	}
	
	printf("\n\t- USB drive: ");
	ret = Init_USB();
	if (ret < 0)
	{
		printf("FAILED.\n");
		USBmnt = false;
	} else {
		printf("OK.\n");
		USBmnt = true;
	}
	
	if (SDmnt && !USBmnt)
	{
		isSD = true;
		printf("\nThe SD Card will be used as the storage device.");
		sleep(2);
	} else
	if (!SDmnt && USBmnt)
	{
		isSD = false;
		printf("\nThe USB drive will be used as the storage device.");
		sleep(2);
	} else
	if (!SDmnt && !USBmnt)
	{
		printf("\nNo device detected. Good bye...");
		sleep(2);
		Reboot();
	} else {
		printf("\nPress A to use the SD Card.\n");
		printf("Press B to use the USB device.");
		
		while(true)
		{
			waitforbuttonpress(&pressed, &pressedGC);
			if (pressed == WPAD_BUTTON_A || pressedGC == PAD_BUTTON_A) isSD = true;
			if (pressed == WPAD_BUTTON_B || pressedGC == PAD_BUTTON_B) isSD = false;
			break;
		}
	}
}

void Unmount_Devices()
{
	ISFS_Deinitialize();
	
	if(SDmnt)
	{
		Close_SD();
	}
	
	if(USBmnt)
	{
		Close_USB();
	}
}

void reset_log()
{
	if (isSD)
	{
		remove("sd:/BlueDump.log");
	} else {
		remove("usb:/BlueDump.log");
	}
}

void logfile(const char *format, ...)
{
#ifdef DEBUG
	char buffer[256];
	va_list args;
	va_start(args, format);
	vsprintf(buffer, format, args);
	FILE *f;
	
	if (isSD)
	{
		f = fopen("sd:/BlueDump.log", "a");
	} else {
		f = fopen("usb:/BlueDump.log", "a");
	}
	
	if (!f) return;
	
	fputs(buffer, f);
	fclose(f);
	va_end (args);
#endif
}

void hexdump_log(void *d, int len)
{
    u8 *data;
    int i, f, off;
    data = (u8*)d;
    for (off=0; off<len; off += 16) 
	{
        logfile("%08x:  ",16*(off/16));
		for(f=0; f < 16; f += 4)
		{
			for(i=0; i<4; i++)
			{
				if((i+off)>=len)
				{
					logfile(" ");
				} else
				{
					logfile("%02x",data[off+f+i]);
				}  
                
				//logfile(" ");
			}
			logfile(" ");
		}	
		logfile("\n");
		//logfile(" ");
    }		
    logfile("\n");
}

void hex_key_dump(void *d, int len)
{
	u8 *data;
	int i;
	data = (u8*)d;
	
	for(i = 0; i < len; i++)
	{
		logfile("%02x ", data[i]);
	}
}