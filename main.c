#include <efi.h>
#include <efilib.h>
#include <libsmbios.h>

#if defined(_M_X64) || defined(__x86_64__)
static CHAR16* Arch = L"x64";
static CHAR16* ArchName = L"64-bit x86";
#elif defined(_M_IX86) || defined(__i386__)
static CHAR16* Arch = L"ia32";
static CHAR16* ArchName = L"32-bit x86";
#else
#  error Unsupported architecture
#endif

static EFI_GUID GraphicsOutputProtocolGUID = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;

EFI_FILE_HANDLE GetVolume(EFI_HANDLE image)
{
	EFI_LOADED_IMAGE* loaded_image = NULL;                  /* image interface */
	EFI_GUID lipGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;      /* image interface GUID */
	EFI_FILE_IO_INTERFACE* IOVolume;                        /* file system interface */
	EFI_GUID fsGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID; /* file system interface GUID */
	EFI_FILE_HANDLE Volume;                                 /* the volume's interface */

	/* get the loaded image protocol interface for our "image" */
	uefi_call_wrapper(BS->HandleProtocol, 3, image, &lipGuid, (void**)&loaded_image);
	/* get the volume handle */
	uefi_call_wrapper(BS->HandleProtocol, 3, loaded_image->DeviceHandle, &fsGuid, (VOID*)&IOVolume);
	uefi_call_wrapper(IOVolume->OpenVolume, 2, IOVolume, &Volume);
	return Volume;
}

UINT64 FileSize(EFI_FILE_HANDLE FileHandle)
{
	UINT64 ret;
	EFI_FILE_INFO* FileInfo;         /* file information structure */
	/* get the file's size */
	FileInfo = LibFileInfo(FileHandle);
	ret = FileInfo->FileSize;
	FreePool(FileInfo);
	return ret;
}

static EFI_STATUS printColorGraphicsFromBinary(EFI_HANDLE image, EFI_SYSTEM_TABLE* systemTable) {
	EFI_BOOT_SERVICES* bs = systemTable->BootServices;
	EFI_STATUS status;
	EFI_GRAPHICS_OUTPUT_PROTOCOL* graphicsProtocol;
	SIMPLE_TEXT_OUTPUT_INTERFACE* conOut = systemTable->ConOut;
	EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* info;
	UINTN SizeOfInfo, sWidth, sHeight;

	//READING THE FRAMES INTO A BUFFER
	EFI_FILE_HANDLE Volume = GetVolume(image);
	CHAR16* FileName = L"APPLE.BIN";
	EFI_FILE_HANDLE     FileHandle;

	// open the file
	uefi_call_wrapper(Volume->Open, 5, Volume, &FileHandle, FileName, EFI_FILE_MODE_READ, EFI_FILE_READ_ONLY | EFI_FILE_HIDDEN | EFI_FILE_SYSTEM);

	// read from the file
	UINT64 ReadSize = FileSize(FileHandle);

	UINT8* Buffer = AllocatePool(ReadSize);
	uefi_call_wrapper(FileHandle->Read, 3, FileHandle, &ReadSize, Buffer);

	Print(L"\n%H*** FINISHED LOADING! STARTING IN 5 SECONDS ***%N\n\n");
	bs->Stall(5000000);
	// close the file 
	uefi_call_wrapper(FileHandle->Close, 1, FileHandle);

	//FRAMES ARE LOADED, NOW WE INIT THE GRAPHICS PROTOCOL 
	status = bs->LocateProtocol(&GraphicsOutputProtocolGUID, NULL,
		(void**)&graphicsProtocol);

	if (EFI_ERROR(status) || graphicsProtocol == NULL) {
		conOut->OutputString(conOut, L"Failed to init gfx!\r\n");
		return status;
	}

	conOut->ClearScreen(conOut);

	//Switch to current mode so gfx is started.
	status = graphicsProtocol->SetMode(graphicsProtocol, graphicsProtocol->Mode->Mode);
	if (EFI_ERROR(status)) {
		conOut->OutputString(conOut, L"Failed to set default mode!\r\n");
		return status;
	}

	EFI_PHYSICAL_ADDRESS framebufferaddr;
	UINTN framebuffersize;
	UINT32 pixelscanline;

	//Setting (video) GOP mode 2 (800x600) 
	status = uefi_call_wrapper(graphicsProtocol->SetMode, 2, graphicsProtocol, 2);
	if (EFI_ERROR(status)) {
		bs->Stall(500000);
		Print(L"Unable to set mode %03d\n", 1);
	}
	else {
		// get framebuffer
		Print(L"Framebuffer address %x size %d, width %d height %d pixelsperline %d\n",
			graphicsProtocol->Mode->FrameBufferBase,
			graphicsProtocol->Mode->FrameBufferSize,
			graphicsProtocol->Mode->Info->HorizontalResolution,
			graphicsProtocol->Mode->Info->VerticalResolution,
			graphicsProtocol->Mode->Info->PixelsPerScanLine
		);
	}

	//I SHOULD REALLY BE USING DOUBLE BUFFERING HERE (BUT I'M NOT)
	int x = 0;
	int y = 0;

	int i = 0;
	int frame = 0;

	for (frame = 0; frame < 6568; frame++) //LOGIC FOR HANDLING FRAMES, USING A FIXED NUMBER OF FRAMES
	{
		for (y = 0; y < 600; y++)
		{
			for (x = 0; x < 800; )
			{
				// WE NEED TO READ EVERY UINT8 BIT BY BIT, SINCE EVERY BIT IS A PIXEL
				int bit = 0;
				for (bit = 7; 0 <= bit; bit--)
				{
					if (((Buffer[i] >> bit) & 0x01))
					{
						*((uint32_t*)(graphicsProtocol->Mode->FrameBufferBase + 4 * graphicsProtocol->Mode->Info->PixelsPerScanLine * y + 4 * x)) = 16777215; //16777215 is white
 					}
					else
					{
						*((uint32_t*)(graphicsProtocol->Mode->FrameBufferBase + 4 * graphicsProtocol->Mode->Info->PixelsPerScanLine * y + 4 * x)) = 0;
					}
					x++;
				}
				i++;
			}
		}

		//For some reason running on real hardware (MSI Prestige 14 - i7-1185G7) is slow. 
		//On QEMU this Stall time gives me the correct framerate
		#if defined(_DEBUG)
			bs->Stall(16400); // (1 sec / 60 fps) converted to usec - about 200us for the cpu to do stuff
		#endif
		//FINISHED PAINTING A FRAME
		y = 0;
		x = 0;
	}

	return EFI_SUCCESS;
}

// Application entrypoint (must be set to 'efi_main' for gnu-efi crt0 compatibility)
EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
	UINTN Event;

#if defined(_GNU_EFI)
	InitializeLib(ImageHandle, SystemTable);
#endif

	// The platform logo may still be displayed → remove it
	SystemTable->ConOut->ClearScreen(SystemTable->ConOut);

	/*
	 * In addition to the standard %-based flags, Print() supports the following:
	 *   %N       Set output attribute to normal
	 *   %H       Set output attribute to highlight
	 *   %E       Set output attribute to error
	 *   %r       Human readable version of a status code
	 */
	Print(L"\n%H*** LOADING BAD APPLE TO RAM ***%N\n\n");

	printColorGraphicsFromBinary(ImageHandle, SystemTable);

	Print(L"\n%HPress any key to exit.%N\n");
	SystemTable->ConIn->Reset(SystemTable->ConIn, FALSE);
	SystemTable->BootServices->WaitForEvent(1, &SystemTable->ConIn->WaitForKey, &Event);
#if defined(_DEBUG)
	// If running in debug mode, use the EFI shut down call to close QEMU
	SystemTable->RuntimeServices->ResetSystem(EfiResetShutdown, EFI_SUCCESS, 0, NULL);
#endif

	return EFI_SUCCESS;
}
