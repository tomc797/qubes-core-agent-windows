#include <Windows.h>
#include <tchar.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <Shellapi.h>
#include <Strsafe.h>
#include <ioall.h>
#include "dvm2.h"
#include "utf8-conv.h"
#include "ioall.h"
#include "log.h"

#define TMP_SUBDIR TEXT("Qubes-open\\")

// older mingw workaround
#define SEE_MASK_NOASYNC 0x100 

HANDLE hStdIn = INVALID_HANDLE_VALUE;
HANDLE hStdOut = INVALID_HANDLE_VALUE;

int get_tempdir(PTCHAR *pBuf, size_t *pcchBuf)
{
	int		size, size_all;
	PTCHAR	pTmpBuf;
	
	size = GetTempPath(0, NULL);
	if (!size) {
		perror("get_tempdir: GetTempPath");
		return 0;
	}

	size_all = size + _tcslen(TMP_SUBDIR);
	pTmpBuf = malloc(size_all  * sizeof(TCHAR));
	if (!pTmpBuf)
		return 0;

	size = GetTempPath(size, pTmpBuf);
	if (!size) {
		perror("get_tempdir: GetTempPath");
		free(pTmpBuf);
		return 0;
	}

	if (FAILED(StringCchCat(pTmpBuf, size_all, TMP_SUBDIR))) {
		perror("get_tempdir: StringCchCat");
		free(pTmpBuf);
		return 0;
	}
	if (!CreateDirectory(pTmpBuf, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
		perror("get_tempdir: CreateDirectory");
		free(pTmpBuf);
		return 0;
	}

	*pBuf = pTmpBuf;
	*pcchBuf = size_all;
	return size_all * sizeof(TCHAR);
}

TCHAR *get_filename()
{
	char buf[DVM_FILENAME_SIZE+1];
	PTCHAR basename;
	PTCHAR retname;
	PTCHAR tmpname;
	size_t basename_len, retname_len, tmpname_len;
	int i;

	if (!read_all(hStdIn, buf, sizeof(buf))) {
		errorf("get_filename: read_all failed\n");
		exit(1);
	}
	buf[DVM_FILENAME_SIZE] = 0;
	if (strchr(buf, '/')) {
		errorf("get_filename: filename contains '/'\n");
		exit(1);
	}
	if (strchr(buf, '\\')) {
		errorf("get_filename: filename contains '\\'\n");
		exit(1);
	}
	for (i=0; i < DVM_FILENAME_SIZE && buf[i]!=0; i++) {
		// replace some characters with _ (eg mimeopen have problems with some of them)
		if (strchr(" !?\"#$%^&*()[]<>;`~", buf[i]))
			buf[i]='_';
	}
	if (FAILED(ConvertUTF8ToUTF16(buf, &basename, &basename_len))) {
		errorf("get_filename: ConvertUTF8ToUTF16 failed\n");
		exit(1);
	}
	if (!get_tempdir(&tmpname, &tmpname_len)) {
		free(basename);
		errorf("get_filename: get_tempdir failed\n");
		exit(1);
	}
	retname_len = tmpname_len + basename_len + 1;
	retname = malloc(sizeof(TCHAR) * retname_len);
	// TODO: better tmp filename set up - at least unique...
	StringCchPrintf(retname, retname_len, TEXT("%s%s"), tmpname, basename);
	free(tmpname);
	free(basename);
	return retname;
}

void copy_file(PTCHAR filename)
{
	HANDLE fd = CreateFile(filename, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);

	if (fd == INVALID_HANDLE_VALUE) {
		if (GetLastError() == ERROR_FILE_EXISTS)
			errorf("File already exists, cleanup temp directory\n");
		else {
			perror("copy_file: CreateFile");
			errorf("Failed to create file %s\n", filename);
		}
		exit(1);
	}
	if (!copy_fd_all(fd, hStdIn)) {
		errorf("copy_file: copy_fd_all failed\n");
        exit(1);
	}
	CloseHandle(fd);
}

void send_file_back(PTCHAR filename)
{
	HANDLE fd = CreateFile(filename, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (fd == INVALID_HANDLE_VALUE) {
		perror("send_file_back: CreateFile");
		exit(1);
	}
	if (hStdOut == INVALID_HANDLE_VALUE) {
		exit(1);
	}

	if (!copy_fd_all(hStdOut, fd)) {
		errorf("Failed to read/write file: %lu\n", GetLastError());
		exit(1);
	}
	CloseHandle(fd);
	CloseHandle(hStdOut);
}

int __cdecl _tmain(ULONG argc, PTCHAR argv[])
{
	WIN32_FILE_ATTRIBUTE_DATA stat_pre, stat_post;
	PTCHAR	filename;
	DWORD	dwExitCode;
	SHELLEXECUTEINFO sei;

	log_init(NULL, TEXT("vm-file-editor"));
	
	hStdIn = GetStdHandle(STD_INPUT_HANDLE);
	hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
	if (hStdIn == INVALID_HANDLE_VALUE || hStdOut == INVALID_HANDLE_VALUE) {
		errorf("GetStdHandle");
		exit(1);
	}

	filename = get_filename();
	copy_file(filename);
	if (!GetFileAttributesEx(filename, GetFileExInfoStandard, &stat_pre)) {
		perror("GetFileAttributesEx pre");
		exit(1);
	}

	sei.cbSize = sizeof(sei);
	sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
#ifdef UNICODE
	sei.fMask |= SEE_MASK_UNICODE;
#endif
	sei.hwnd = NULL;
	sei.lpVerb = TEXT("open");
	sei.lpFile = filename;
	sei.lpParameters = NULL;
	sei.lpDirectory = NULL;
	sei.nShow = SW_SHOW;
	sei.hProcess = NULL;

	if (FAILED(ShellExecuteEx(&sei))) {
		perror("ShellExecuteEx");
		errorf("Editor startup failed\n");
		exit(1);
	}

	if (sei.hProcess == NULL) {
		errorf("Don't know how to wait for editor finish, exiting\n");
		exit(0);
	}

    // Wait until child process exits.
    WaitForSingleObject(sei.hProcess, INFINITE);

	if (!GetExitCodeProcess(sei.hProcess, &dwExitCode)) {
		perror("GetExitCodeProcess");
		exit(1);
	}

    // Close process handle. 
    CloseHandle(sei.hProcess);

	if (dwExitCode != 0) {
		errorf("Editor failed: %d\n", dwExitCode);
		exit(1);
	}

	if (!GetFileAttributesEx(filename, GetFileExInfoStandard, &stat_post)) {
		perror("GetFileAttributesEx post");
		exit(1);
	}

	if (stat_pre.ftLastWriteTime.dwLowDateTime != stat_post.ftLastWriteTime.dwLowDateTime ||
	    stat_pre.ftLastWriteTime.dwHighDateTime != stat_post.ftLastWriteTime.dwHighDateTime)
		send_file_back(filename);
	DeleteFile(filename);
	return 0;
}