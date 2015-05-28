#include <windows.h>
#include <shlwapi.h>
#include <Wtsapi32.h>

#include <xencontrol.h>
#include "log.h"

#define XS_TOOLS_PREFIX "qubes-tools/"

// userName needs to be freed with WtsFreeMemory
BOOL GetCurrentUser(OUT char **userName)
{
    WTS_SESSION_INFOA *sessionInfo;
    DWORD sessionCount;
    DWORD i;
    DWORD cbUserName;
    BOOL found;

    LogVerbose("start");

    if (!WTSEnumerateSessionsA(WTS_CURRENT_SERVER_HANDLE, 0, 1, &sessionInfo, &sessionCount))
    {
        perror("WTSEnumerateSessionsA");
        return FALSE;
    }

    found = FALSE;

    for (i = 0; i < sessionCount; i++)
    {
        if (sessionInfo[i].State == WTSActive)
        {
            if (!WTSQuerySessionInformationA(
                WTS_CURRENT_SERVER_HANDLE,
                sessionInfo[i].SessionId, WTSUserName,
                userName,
                &cbUserName))
            {
                perror("WTSQuerySessionInformationA");
                goto cleanup;
            }
            LogDebug("Found session: %S\n", *userName);
            found = TRUE;
        }
    }

cleanup:
    WTSFreeMemory(sessionInfo);

    LogVerbose("found=%d", found);

    return found;
}

/* just a helper function, the buffer needs to be at least MAX_PATH+1 length */
BOOL PrepareExePath(OUT WCHAR *fullPath, IN const WCHAR *exeName)
{
    WCHAR *lastBackslash = NULL;

    LogVerbose("exe: '%s'", exeName);

    if (!GetModuleFileName(NULL, fullPath, MAX_PATH))
    {
        perror("GetModuleFileName");
        return FALSE;
    }

    // cut off file name (qrexec_agent.exe)
    lastBackslash = wcsrchr(fullPath, L'\\');
    if (!lastBackslash)
    {
        LogError("qrexec-agent.exe path has no backslash\n");
        return FALSE;
    }

    // Leave trailing backslash
    lastBackslash++;
    *lastBackslash = L'\0';
    // add an executable filename
    PathAppend(fullPath, exeName);

    LogVerbose("success, path: '%s'", fullPath);

    return TRUE;
}

/* TODO - make this configurable? */
BOOL CheckGuiAgentPresence(void)
{
    WCHAR serviceFilePath[MAX_PATH + 1];

    LogVerbose("start");

    if (!PrepareExePath(serviceFilePath, L"wga.exe"))
        return FALSE;

    return PathFileExists(serviceFilePath);
}

BOOL NotifyDom0(void)
{
    WCHAR qrexecClientVmPath[MAX_PATH + 1];
    STARTUPINFO si = { 0 };
    PROCESS_INFORMATION pi;

    LogVerbose("start");

    if (!PrepareExePath(qrexecClientVmPath, L"qrexec-client-vm.exe"))
        return FALSE;

    si.cb = sizeof(si);
    si.wShowWindow = SW_HIDE;
    si.dwFlags = STARTF_USESHOWWINDOW;

    if (!CreateProcess(
        qrexecClientVmPath,
        TEXT("qrexec-client-vm.exe dom0 qubes.NotifyTools dummy"),
        NULL,
        NULL,
        FALSE,
        CREATE_NO_WINDOW,
        NULL,
        NULL,
        &si,
        &pi))
    {
        perror("CreateProcess(qrexec-client-vm.exe)");
        return FALSE;
    }

    /* fire and forget */
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    LogVerbose("success");

    return TRUE;
}

ULONG AdvertiseTools(void)
{
    HANDLE xif = NULL;
    ULONG status = ERROR_UNIDENTIFIED_ERROR;
    BOOL guiAgentPresent;
    CHAR *userName = NULL;

    LogVerbose("start");

    status = XenifaceOpen(&xif);
    if (status != ERROR_SUCCESS)
    {
        perror("XenifaceOpen");
        goto cleanup;
    }

    /* for now mostly hardcoded values, but this can change in the future */
    status = StoreWrite(xif, XS_TOOLS_PREFIX "version", "1");
    if (status != ERROR_SUCCESS)
    {
        perror("write 'version' entry");
        goto cleanup;
    }
    
    status = StoreWrite(xif, XS_TOOLS_PREFIX "os", "Windows");
    if (status != ERROR_SUCCESS)
    {
        perror("write 'os' entry");
        goto cleanup;
    }

    status = StoreWrite(xif, XS_TOOLS_PREFIX "qrexec", "1");
    if (status != ERROR_SUCCESS)
    {
        perror("write 'qrexec' entry");
        goto cleanup;
    }

    guiAgentPresent = CheckGuiAgentPresence();

    status = StoreWrite(xif, XS_TOOLS_PREFIX "gui", guiAgentPresent ? "1" : "0");
    if (status != ERROR_SUCCESS)
    {
        perror("write 'gui' entry");
        goto cleanup;
    }

    if (GetCurrentUser(&userName))
    {
        status = StoreWrite(xif, XS_TOOLS_PREFIX "default-user", userName);
        if (status != ERROR_SUCCESS)
        {
            perror("write 'default-user' entry");
            goto cleanup;
        }
    }

    if (!NotifyDom0())
    {
        /* error already reported */
        goto cleanup;
    }

    status = ERROR_SUCCESS;
    LogVerbose("success");

cleanup:
    if (xif)
        XenifaceClose(xif);
    if (userName)
        WTSFreeMemory(userName);
    return status;
}