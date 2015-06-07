#include "ExtIO_HackRF.h"
#include "resource.h"
//---------------------------------------------------------------------------
// #define WIN32_LEAN_AND_MEAN             // Selten verwendete Teile der Windows-Header nicht einbinden.
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <stdio.h>
#include <hackrf.h>
#include <new>
#include <pthread.h>

//---------------------------------------------------------------------------
#define EXTIO_EXPORTS		1
#define HWNAME				"ExtIO HackRF"

static hackrf_device *device;
HWND h_dialog = NULL;
int result;
short *short_buf = NULL;
static int buffer_len;
#define DEFAULT_BUF_LENGTH		262144
#define MAXIMUM_OVERSAMPLE		1	
#define BUF_LEN  (MAXIMUM_OVERSAMPLE * DEFAULT_BUF_LENGTH) /* must be multiple of 512 */
#define BYTES_PER_SAMPLE  2 /* HackRF device produces 8 bit signed IQ data */

#define EXT_HWTYPE			exthwUSBdata16

#define FREQ_MIN_HZ	1000000 /* 1 MHz */
#define FREQ_MAX_HZ	7250000000 /* 7250MHz */

typedef struct sr {
	uint32_t value;
	TCHAR *name;
} sr_t;

static sr_t samplerates[] = {
	{ 8000000, TEXT("8 MSPS") },
	{ 10000000, TEXT("10 MSPS") },
	{ 12500000, TEXT("12.5 MSPS") },
	{ 16000000, TEXT("16 MSPS") },
	{ 20000000, TEXT("20 MSPS") }
};

static int samplerate_default = 1; // 10 MSPS

pfnExtIOCallback	pfnCallback = NULL;
volatile long gExtSampleRate = 10000000;//Default 10MSPS
volatile int64_t	glLOfreq = 101700000L;//Default 101.7Mhz
volatile bool	gbThreadRunning = false;
unsigned int lna_gain = 8, vga_gain = 20;

uint8_t board_id = BOARD_ID_INVALID;


#define BORLAND				0
int_fast16_t i;
#pragma warning(disable : 4996)

wchar_t str[10];
static char SDR_progname[32 + 1] = "\0";
static int  SDR_ver_major = -1;
static int  SDR_ver_minor = -1;


int hackrf_rx_callback(hackrf_transfer* transfer){


	short *short_ptr = (short*)&short_buf[0];
	unsigned char* char_ptr = transfer->buffer;

	if (gbThreadRunning){
		for (int i = 0; i < transfer->valid_length; i++)
		{
			(*short_ptr) = ((short)(*char_ptr)) << 8;
			char_ptr++;
			short_ptr++;
		}
		pfnCallback(buffer_len, 0, 0, (void*)short_buf);

	}
	return 0;
}


//---------------------------------------------------------------------------

#if BORLAND
//---------------------------------------------------------------------------
//   Important note about DLL memory management when your DLL uses the
//   static version of the RunTime Library:
//
//   If your DLL exports any functions that pass String objects (or structs/
//   classes containing nested Strings) as parameter or function results,
//   you will need to add the library MEMMGR.LIB to both the DLL project and
//   any other projects that use the DLL.  You will also need to use MEMMGR.LIB
//   if any other projects which use the DLL will be performing new or delete
//   operations on any non-TObject-derived classes which are exported from the
//   DLL. Adding MEMMGR.LIB to your project will change the DLL and its calling
//   EXE's to use the BORLNDMM.DLL as their memory manager.  In these cases,
//   the file BORLNDMM.DLL should be deployed along with your DLL.
//
//   To avoid using BORLNDMM.DLL, pass string information using "char *" or
//   ShortString parameters.
//
//   If your DLL uses the dynamic version of the RTL, you do not need to
//   explicitly add MEMMGR.LIB as this will be done implicitly for you
//---------------------------------------------------------------------------

#pragma argsused
BOOL WINAPI DllMain(HINSTANCE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)

#else

HMODULE hInst;

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
	)
#endif
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		hInst = hModule;
#if PTW32_STATIC_LIB
		pthread_win32_process_attach_np();
#endif
	case DLL_THREAD_ATTACH:
#if PTW32_STATIC_LIB
		pthread_win32_thread_attach_np();
#endif
		break;

	case DLL_THREAD_DETACH:
#if PTW32_STATIC_LIB
		pthread_win32_thread_detach_np();
#endif
		break;

	case DLL_PROCESS_DETACH:
#if PTW32_STATIC_LIB
		pthread_win32_thread_detach_np();
		pthread_win32_process_detach_np();
#endif
		break;


	}
	return TRUE;
}
//---------------------------------------------------------------------
static INT_PTR CALLBACK MainDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam){
	static HBRUSH BRUSH_RED = CreateSolidBrush(RGB(255, 0, 0));
	static HBRUSH BRUSH_GREEN = CreateSolidBrush(RGB(0, 255, 0));
	switch (uMsg){
	case WM_INITDIALOG:{

						   for (int i = 0; i < (sizeof(samplerates) / sizeof(samplerates[0])); i++)
						   {

							   ComboBox_AddString(GetDlgItem(hwndDlg, IDC_SAMPLERATE), samplerates[i].name);

						   }

						   ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_SAMPLERATE), samplerate_default);

						   SendDlgItemMessage(hwndDlg, IDC_LNA, TBM_SETRANGEMIN, FALSE, 0);
						   SendDlgItemMessage(hwndDlg, IDC_LNA, TBM_SETRANGEMAX, FALSE, 40);
						   for (int i = 0; i < 40; i +=8){
							   SendDlgItemMessage(hwndDlg, IDC_LNA, TBM_SETTIC, FALSE, i);
						   }
						   SendDlgItemMessage(hwndDlg, IDC_LNA, TBM_SETPOS, TRUE, (int)lna_gain);


						   SendDlgItemMessage(hwndDlg, IDC_VGA, TBM_SETRANGEMIN, FALSE, 0);
						   SendDlgItemMessage(hwndDlg, IDC_VGA, TBM_SETRANGEMAX, FALSE, 62);
						   for (int i = 0; i < 62; i += 2){
							   SendDlgItemMessage(hwndDlg, IDC_VGA, TBM_SETTIC, FALSE, i);
							  
						   }
						   SendDlgItemMessage(hwndDlg, IDC_VGA, TBM_SETPOS, TRUE, (int)vga_gain);

						   return TRUE;
	}
	case WM_HSCROLL:
		if (GetDlgItem(hwndDlg, IDC_LNA) == (HWND)lParam){
			if (LOWORD(wParam) != TB_THUMBTRACK && LOWORD(wParam) != TB_ENDTRACK){
				if (lna_gain != (SendDlgItemMessage(hwndDlg, IDC_LNA, TBM_GETPOS, 0, NULL)& ~0x07)){
					lna_gain = SendDlgItemMessage(hwndDlg, IDC_LNA, TBM_GETPOS, 0, NULL)& ~0x07;
					_itow_s(lna_gain, str, 10, 10);
					wcscat(str, TEXT(" dB"));
					if (device){ 
						if (hackrf_set_lna_gain(device, lna_gain) == HACKRF_SUCCESS) Static_SetText(GetDlgItem(hwndDlg, IDC_LNAVALUE), str);
					
					};
				}
			}
		}
		if (GetDlgItem(hwndDlg, IDC_VGA) == (HWND)lParam){
			if (LOWORD(wParam) != TB_THUMBTRACK && LOWORD(wParam) != TB_ENDTRACK)
			{
				if (vga_gain != (SendDlgItemMessage(hwndDlg, IDC_LNA, TBM_GETPOS, 0, NULL)& ~0x01)){
					vga_gain = SendDlgItemMessage(hwndDlg, IDC_VGA, TBM_GETPOS, 0, NULL)& ~0x01;
					_itow_s(vga_gain, str, 10, 10);
					wcscat(str, TEXT(" dB"));
					Static_SetText(GetDlgItem(hwndDlg, IDC_VGAVALUE), str);
					if (device){ 
						if (hackrf_set_vga_gain(device, vga_gain) == HACKRF_SUCCESS)Static_SetText(GetDlgItem(hwndDlg, IDC_VGAVALUE), str);;
					}
				}
			}
		}

		break;
	case WM_COMMAND:
		switch (GET_WM_COMMAND_ID(wParam, lParam)){
		case IDC_SAMPLERATE:{
								if (GET_WM_COMMAND_CMD(wParam, lParam) == CBN_SELCHANGE)
								{
									gExtSampleRate = samplerates[ComboBox_GetCurSel(GET_WM_COMMAND_HWND(wParam, lParam))].value;
									hackrf_set_sample_rate(device, gExtSampleRate);
									pfnCallback(-1, extHw_Changed_SampleRate, 0, NULL);

								}
								return TRUE;
		}


		}
		break;


	case WM_CLOSE:
		ShowWindow(h_dialog, SW_HIDE);
		return TRUE;
		break;
	case WM_DESTROY:
		h_dialog = NULL;
		return TRUE;
		break;
	}


	return FALSE;
}


//---------------------------------------------------------------------------
extern "C"
bool __declspec(dllexport) __stdcall InitHW(char *name, char *model, int& type)
{

	result = hackrf_init();
	if (result != HACKRF_SUCCESS) {
		return FALSE;
	}
	result = hackrf_open(&device);
	if (result != HACKRF_SUCCESS) {
		MessageBox(NULL, TEXT("No HackRF devices found"),
			TEXT("ExtIO HackRF"),
			MB_ICONERROR | MB_OK);
		return FALSE;
	}
	result = hackrf_board_id_read(device, &board_id);
	if (result != HACKRF_SUCCESS) {
		MessageBox(NULL, TEXT("hackrf_board_id_read() failed"),
			TEXT("ExtIO HackRF"),
			MB_ICONERROR | MB_OK);
		return FALSE;
	}


	type = EXT_HWTYPE;
	strcpy(name, HWNAME);
	strcpy(model, hackrf_board_id_name((hackrf_board_id)board_id));
	hackrf_close(device);
	return TRUE;

}

//---------------------------------------------------------------------------
extern "C"
bool EXTIO_API OpenHW(void)
{

	result = hackrf_open(&device);
	if (result != HACKRF_SUCCESS) {
		MessageBox(NULL, TEXT("hackrf_open Faile"), NULL, MB_OK);
		return FALSE;
	}
	gExtSampleRate = samplerates[samplerate_default].value;
	result = hackrf_set_sample_rate(device, gExtSampleRate);
	if (result != HACKRF_SUCCESS) {
		MessageBox(NULL, TEXT("hackrf_set_sample_rate_manual Failed"), NULL, MB_OK);
		return FALSE;
	}

	h_dialog = CreateDialog(hInst, MAKEINTRESOURCE(IDD_HACKRF_SETTINGS), NULL, (DLGPROC)MainDlgProc);
	ShowWindow(h_dialog, SW_HIDE);

	buffer_len = BUF_LEN;
	short_buf = new (std::nothrow) short[buffer_len];

	result = hackrf_set_lna_gain(device, lna_gain);
	result |= hackrf_set_vga_gain(device, vga_gain);
	result |= hackrf_start_rx(device, hackrf_rx_callback, NULL);
	if (result != HACKRF_SUCCESS) {
		MessageBox(NULL, TEXT("hackrf_start_rx Failed"), NULL, MB_OK);
		delete short_buf;
		return FALSE;
	}
	while (!hackrf_is_streaming(device));

	return TRUE;
}
//

extern "C"
void EXTIO_API ShowGUI()
{
	ShowWindow(h_dialog, SW_SHOW);
	SetForegroundWindow(h_dialog);
	return;
}

extern "C"
void EXTIO_API HideGUI()
{
	ShowWindow(h_dialog, SW_HIDE);
	return;
}

extern "C"
void EXTIO_API SwitchGUI()
{
	if (IsWindowVisible(h_dialog))
		ShowWindow(h_dialog, SW_HIDE);
	else
		ShowWindow(h_dialog, SW_SHOW);
	return;
}
//---------------------------------------------------------------------------


extern "C"
int  EXTIO_API StartHW(long LOfreq)
{
	int64_t ret = StartHW64((int64_t)LOfreq);
	return (int)ret;
}

//---------------------------------------------------------------------------
extern "C"
int64_t  EXTIO_API StartHW64(int64_t LOfreq)
{
	if (!device) {

		MessageBox(NULL, TEXT("StartHW Failed"), NULL, MB_OK);
		return -1;

	}

	if (short_buf == 0) {
		MessageBox(NULL, TEXT("Couldn't Allocate Buffer!"), TEXT("Error!"), MB_OK | MB_ICONERROR);
		return -1;
	}
	glLOfreq = LOfreq;
	SetHWLO64(glLOfreq);

	gbThreadRunning = TRUE;


	// number of complex elements returned each
	// invocation of the callback routine
	return buffer_len / BYTES_PER_SAMPLE;
}


//---------------------------------------------------------------------------
extern "C"
void EXTIO_API StopHW(void)
{
	gbThreadRunning = FALSE;

}

//---------------------------------------------------------------------------
extern "C"
void EXTIO_API CloseHW(void)
{

	hackrf_stop_rx(device);
	hackrf_close(device);
	hackrf_exit();
	delete short_buf;
}
//--------------
extern "C"
int  EXTIO_API SetHWLO(long LOfreq)
{
	int64_t ret = SetHWLO64((int64_t)LOfreq);
	return (ret & 0xFFFFFFFF);
}
//---------------------------------------------------------------------------
extern "C"
int64_t  EXTIO_API SetHWLO64(int64_t LOfreq)
{
	int64_t ret = 0;
	if (LOfreq < FREQ_MIN_HZ){
		glLOfreq = FREQ_MIN_HZ;
		ret = -1 * (FREQ_MIN_HZ);
	}

	if (LOfreq > FREQ_MAX_HZ){
		glLOfreq = FREQ_MAX_HZ;
		ret = FREQ_MAX_HZ;
	}

	if (glLOfreq != LOfreq){
		glLOfreq = LOfreq;
		result = hackrf_set_freq(device, glLOfreq);
		if (result = HACKRF_SUCCESS){
			pfnCallback(-1, extHw_Changed_LO, 0, NULL);
		}

		if (result != HACKRF_SUCCESS)
		{
			MessageBox(NULL, TEXT("hackrf_set_freq Failed"), NULL, MB_OK);
		}
	}
	return ret;
}



//---------------------------------------------------------------------------
extern "C"
int  EXTIO_API GetStatus(void)
{
	return 0;
}

//---------------------------------------------------------------------------
extern "C"
void EXTIO_API SetCallback(pfnExtIOCallback funcptr)
{
	pfnCallback = funcptr;
	return;
}

//--------------------------
extern "C"
int64_t EXTIO_API GetHWLO64(void)
{
	return glLOfreq;
}
//---------------------------------------------------------------------------
extern "C"
long EXTIO_API GetHWLO(void)
{
	return (long)(glLOfreq & 0xFFFFFFFF);
}


//---------------------------------------------------------------------------
extern "C"
long EXTIO_API GetHWSR(void)
{
	return gExtSampleRate;
}


//---------------------------------------------------------------------------

// extern "C" long EXTIO_API GetTune(void);
// extern "C" void EXTIO_API GetFilters(int& loCut, int& hiCut, int& pitch);
// extern "C" char EXTIO_API GetMode(void);
// extern "C" void EXTIO_API ModeChanged(char mode);
// extern "C" void EXTIO_API IFLimitsChanged(long low, long high);
// extern "C" void EXTIO_API TuneChanged(long freq);

// extern "C" void    EXTIO_API TuneChanged64(int64_t freq);
// extern "C" int64_t EXTIO_API GetTune64(void);
// extern "C" void    EXTIO_API IFLimitsChanged64(int64_t low, int64_t high);

//---------------------------------------------------------------------------

// extern "C" void EXTIO_API RawDataReady(long samprate, int *Ldata, int *Rdata, int numsamples)

//---------------------------------------------------------------------------
extern "C"
void EXTIO_API VersionInfo(const char * progname, int ver_major, int ver_minor)
{
	SDR_progname[0] = 0;
	SDR_ver_major = -1;
	SDR_ver_minor = -1;

	if (progname)
	{
		strncpy(SDR_progname, progname, sizeof(SDR_progname)-1);
		SDR_ver_major = ver_major;
		SDR_ver_minor = ver_minor;

		// possibility to check program's capabilities
		// depending on SDR program name and version,
		// f.e. if specific extHWstatusT enums are supported
	}
}


//---------------------------------------------------------------------------

extern "C"
int EXTIO_API ExtIoGetSrates(int srate_idx, double * samplerate)
{
	if (srate_idx < (sizeof(samplerates) / sizeof(samplerates[0])))
	{
		*samplerate = samplerates[srate_idx].value;
		return 0;
	}
	else {// MessageBox(NULL, TEXT("ExtIoGetSrates"), NULL, MB_OK); 
		return -1;
	}

}

extern "C"
int  EXTIO_API ExtIoGetActualSrateIdx(void)
{
	return  ComboBox_GetCurSel(GetDlgItem(h_dialog, IDC_SAMPLERATE));
}


extern "C"
int  EXTIO_API ExtIoSetSrate(int srate_idx)
{
	if (srate_idx >= 0 && srate_idx < (sizeof(samplerates) / sizeof(samplerates[0])))
	{
		gExtSampleRate = samplerates[srate_idx].value;
		hackrf_set_sample_rate(device, gExtSampleRate);
		ComboBox_SetCurSel(GetDlgItem(h_dialog, IDC_SAMPLERATE), srate_idx);
		pfnCallback(-1, extHw_Changed_SampleRate, 0, NULL);// Signal application
		return 0;
	}
	else
	{
		MessageBox(NULL, TEXT("ExtIoSetSrate error"), NULL, MB_OK);
	}
	return 1;	// ERROR
}



extern "C"
int  EXTIO_API ExtIoGetSetting(int idx, char * description, char * value)
{
	switch (idx)
	{
	case 0:	_snprintf(description, 1024, "%s", "SampleRateIdx");
		_snprintf(value, 1024, "%d", ComboBox_GetCurSel(GetDlgItem(h_dialog, IDC_SAMPLERATE)));
		return 0;
	default:	return -1;	// ERROR
	}
	return -1;	// ERROR
}
//
extern "C"
void EXTIO_API ExtIoSetSetting(int idx, const char * value)
{
	int tempInt;

	switch (idx)
	{
	case 0:
		tempInt = atoi(value);
		if (tempInt >= 0 && tempInt < (sizeof(samplerates) / sizeof(samplerates[0])))
		{
			samplerate_default = tempInt;
		}
		break;

	}
}
