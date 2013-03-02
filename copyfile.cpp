// Just copy file ignoring any error (including CRC32)
// (dennis@conus.info)

#pragma comment( lib, "comctl32" )

// 10 MiB
#define BUFSIZE 10*1024*1024
// 1 MiB
#define INITIAL_BUF_TO_SKIP 1024*1024

#include <windows.h>

#include <CommCtrl.h>

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "resource.h"

#include <string>

LARGE_INTEGER fsize;
__int64 uncopied=0;
__int64 copied=0;

__int64 last_uncopied=0;
__int64 last_copied=0;

wchar_t filename_in[10240];
wchar_t filename_out[10240];

std::wstring wstrfmt (const wchar_t * szFormat, ...)
{
	va_list va;
	std::wstring rt;
	wchar_t * buf;
	int sz, bufsize;

	va_start (va, szFormat);

	sz=_vscwprintf (szFormat, va);
	bufsize=sz+1;

	buf=(wchar_t*)malloc (bufsize*sizeof(wchar_t));

	assert (buf);

	if (_vsnwprintf_s (buf, bufsize, sz, szFormat, va)==-1)
	{
		wprintf (L"wstrfmt (%s...)\n", szFormat);
		wprintf (L"_vsnwprintf returned -1\n");
		assert (0);
	}
	else
		rt=buf;

	free (buf);

	return rt;
};

std::wstring number_with_commas (__int64 in)
{
	__int64 val=in;
	std::wstring rt;
	bool first_3=true;

	// 7FFFFFFFFFFFFFFF =  9,223,372,036,854,775,807
	// FFFFFFFFFFFFFFFF = 18,...

	__int64 divisor=1000000000000000000;

	for (;;)
	{
		if ((val/divisor)!=0)
		{
			if (first_3)
				rt+=wstrfmt (L"%u", val/divisor);
			else
				rt+=wstrfmt (L"%03u", val/divisor);

			if (divisor!=1) 
				rt+=L",";
			val-=(val/divisor)*divisor;
			first_3=false;
		};

		if (divisor==1)
			break;
		divisor/=1000;
	};

	if (rt.size()==0)
		rt=L"0";
	return rt;
};

std::wstring get_last_error (std::wstring title)
{
	std::wstring rt;

	LPCTSTR lpMsgBuf;
	DWORD dw = GetLastError(); 

	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | 
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		dw,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR) &lpMsgBuf,
		0, NULL );

	rt=lpMsgBuf;

	if (title.size()>0)
		rt=rt+L" "+title;

	LocalFree((LPVOID)lpMsgBuf);

	return rt;
}

std::wstring get_last_error()
{
	return get_last_error (L"");
};

DWORD progress_thread_id;
HWND progress_thread_HWND;
bool progress_thread_HWND_present=false;
bool cancel=false;

#define ID_TIMER 1

BOOL FAR PASCAL progress_thread_proc ( HWND hwnd, unsigned msg, UINT wparam, LONG lparam )
{
	WORD cmd;

	switch( msg ) 
	{
	case WM_INITDIALOG:
		{
			progress_thread_HWND=hwnd;
			progress_thread_HWND_present=true;
			SetTimer (hwnd, ID_TIMER, 250, NULL); // 0.25 sec

			wchar_t drive[_MAX_DRIVE];
			wchar_t dir[_MAX_DIR];
			wchar_t fname[_MAX_FNAME];
			wchar_t ext[_MAX_EXT];

			if (_wsplitpath_s (filename_in, drive, _MAX_DRIVE, dir, _MAX_DIR, fname, _MAX_FNAME, ext, _MAX_EXT)==0)
				SetDlgItemText(hwnd, IDC_TEXT, wstrfmt(L"%s%s", fname, ext).c_str());
			else
				SetDlgItemText(hwnd, IDC_TEXT, L"Copying file...");
		}
		return (TRUE);

	case WM_CLOSE:
		EndDialog( hwnd, 0 );
		cancel=true;
		KillTimer (hwnd, ID_TIMER);
		return (TRUE);

	case WM_TIMER:
		{
			if (last_copied!=copied || last_uncopied!=uncopied)
			{
				// in megabytes:
				DWORD total_progress=(DWORD)(fsize.QuadPart/1000000);
				DWORD current_progress=(DWORD)((copied+uncopied)/1000000);

				SendMessage(GetDlgItem (hwnd, IDC_PROGRESS_BAR), PBM_SETRANGE, 0, MAKELPARAM(0, total_progress)); 
				SendMessage(GetDlgItem (hwnd, IDC_PROGRESS_BAR), PBM_SETPOS, current_progress, 0);

				SetDlgItemText(hwnd, IDC_EDIT_BYTES_COPYED, number_with_commas (copied).c_str());

				SetDlgItemText(hwnd, IDC_EDIT_BYTES_FAILED_TO_COPY, number_with_commas (uncopied).c_str());

				last_copied=copied;
				last_uncopied=uncopied;
			};
		};
		return (TRUE);

	case WM_COMMAND:

		cmd = LOWORD( wparam );

		switch( cmd ) 
		{

		case IDC_CANCEL:
			EndDialog( hwnd, 0 );
			cancel=true;
			KillTimer (hwnd, ID_TIMER);
			return (TRUE);
		};

		return (FALSE);
		break;
	};

	return (FALSE);
};

static void WINAPI progress_thread (DWORD param) 
{
	DialogBox( NULL, L"COPYFILE", NULL, &progress_thread_proc );
};

void start_progress_thread()
{
	CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)progress_thread, (PVOID)0, 0, &progress_thread_id);
};

void stop_progress_thread()
{
	while (progress_thread_HWND_present==false)
		Sleep (100);
	SendMessage (progress_thread_HWND, WM_CLOSE, 0, 0);
	progress_thread_HWND_present=false;
};

HANDLE choose_and_open_src_file ()
{
	HANDLE rt;

	OPENFILENAME file_in;
	memset (filename_in, 0, sizeof(filename_in));

	memset(&file_in, 0, sizeof(OPENFILENAME));

	file_in.lStructSize = sizeof(OPENFILENAME);
	file_in.hwndOwner = NULL;
	file_in.lpstrFile = filename_in;
	file_in.nMaxFile = sizeof(filename_in);
	file_in.lpstrFilter = L"All\0*.*\0\0\0";
	file_in.nFilterIndex = 1;
	file_in.lpstrFileTitle = NULL;
	file_in.nMaxFileTitle = 0;
	file_in.lpstrInitialDir = NULL;
	file_in.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

	if (GetOpenFileName (&file_in)==FALSE)
		throw 0; // just exit with return code

	rt=CreateFile (filename_in, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (rt==INVALID_HANDLE_VALUE)
		throw get_last_error(L"while opening source file");

	if (GetFileSizeEx (rt, &fsize)==FALSE)
		throw get_last_error(L"while getting file size of source file");

	return rt;
}

HANDLE choose_and_open_dst_file ()
{
	HANDLE rt;
	OPENFILENAME file_out;

	while (true)
	{
		memset(&file_out,0,sizeof(OPENFILENAME));
		memset (filename_out, 0, sizeof(filename_out));

		file_out.lStructSize = sizeof (OPENFILENAME);
		file_out.hwndOwner = NULL; 
		file_out.lpstrFilter = L"All Files (*.*)\0*.*\0"; 
		file_out.lpstrFile = filename_out;
		file_out.nMaxFile = sizeof (filename_out);
		file_out.lpstrInitialDir = (LPTSTR)NULL; 
		file_out.lpstrTitle = L"Choose location folder to save file";
		file_out.Flags = OFN_SHOWHELP;

		if (GetSaveFileName (&file_out)==FALSE)
			throw 0; // just exit with return code

		if (wcscmp (filename_in, filename_out)==0)
			MessageBox (NULL, L"You can't copy to the source file", L"Error", MB_ICONERROR | MB_OK);
		else
			break;
	};

	rt=CreateFile (filename_out, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (rt==INVALID_HANDLE_VALUE)
		throw get_last_error(L"while opening source file");

	return rt;
}

void copy_file(HANDLE in, HANDLE out)
{
	BYTE* buf=NULL;

	buf=(BYTE*)malloc(BUFSIZE);
	if (buf==NULL)
		throw L"Can't allocate buffer memory";

	DWORD rd, wr;
	BOOL b;
	LARGE_INTEGER pos;
	pos.QuadPart=0;
	__int64 to_skip=INITIAL_BUF_TO_SKIP;
	for (;;)
	{
		b=ReadFile (in, buf, BUFSIZE, &rd, NULL);
		if (b==FALSE)
		{
#ifdef _DEBUG
			OutputDebugString (wstrfmt (L"error in ReadFile (in...): %s\n", get_last_error().c_str()).c_str());
#endif
			pos.QuadPart=pos.QuadPart + to_skip;
			uncopied+=to_skip;
			if (pos.QuadPart >= fsize.QuadPart)
			{
				// file end was not copied too
				free (buf);
				// set the same size to dst file
				b=SetFilePointerEx (out, fsize, NULL, FILE_BEGIN);
				if (b==FALSE) throw get_last_error(L"while setting pointer on destination file");
				return;
			}

			b=SetFilePointerEx (in, pos, NULL, FILE_BEGIN);
			if (b==FALSE) throw get_last_error(L"while setting pointer on source file");
			b=SetFilePointerEx (out, pos, NULL, FILE_BEGIN);
			if (b==FALSE) throw get_last_error(L"while setting pointer on destination file");
			to_skip=to_skip*2;
			OutputDebugString (wstrfmt (L"setting to_skip to %I64d\n", to_skip).c_str());
		}
		else
		{
			if (rd==0)
				break;
			b=WriteFile (out, buf, rd, &wr, NULL);
			if (b==FALSE)
				throw get_last_error(L"while writing file");
			pos.QuadPart+=rd;
			copied+=rd;
			to_skip=INITIAL_BUF_TO_SKIP;
		}

		if (cancel)
		{
			free (buf);
			throw std::wstring (L"canceled");
		};
	};
	free (buf);
}

int CALLBACK WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	int rt=0;
	HANDLE handle_in=NULL;
	HANDLE handle_out=NULL;

	try
	{
		INITCOMMONCONTROLSEX l;

		l.dwSize=sizeof (INITCOMMONCONTROLSEX);
		l.dwICC=ICC_DATE_CLASSES;
		InitCommonControlsEx(&l);

		MessageBox (NULL, L"Just copy file ignoring any error (including CRC32)\n(dennis@conus.info)", L"Copyfile utility", MB_ICONINFORMATION | MB_OK);

		handle_in=choose_and_open_src_file();

		handle_out=choose_and_open_dst_file();

		start_progress_thread();

		copy_file (handle_in, handle_out);

		MessageBox (NULL, 
			wstrfmt (L"Bytes copied: %s (%d%%), bytes failed to copy: %s (%d%%)", 
			number_with_commas (copied).c_str(), 
			(int)((double)copied/(double)fsize.QuadPart*100),
			number_with_commas (uncopied).c_str(),
			(int)((double)uncopied/(double)fsize.QuadPart*100)).c_str(), 
			L"Done", MB_ICONINFORMATION | MB_OK);
	}
	catch (std::wstring s)
	{
		MessageBox (NULL, s.c_str(), L"Error", MB_ICONERROR | MB_OK);
		if (handle_out!=NULL)
		{
			CloseHandle (handle_out);
			handle_out=NULL;
		}
		DeleteFile (filename_out);
	}
	catch (int i)
	{
		// just exit with exit code
		rt=i;
	}

	if (progress_thread_HWND_present)
		stop_progress_thread();
	if (handle_in!=NULL)
		CloseHandle (handle_in);
	if (handle_out!=NULL)
		CloseHandle (handle_out);

	return rt;
}