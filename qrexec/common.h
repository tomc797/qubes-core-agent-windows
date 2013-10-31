#pragma once
#include <windows.h>
#include "libvchan.h"
#include "qrexec.h"

//#define BUILD_AS_SERVICE

#define SERVICE_NAME	TEXT("qrexec-agent")
#define DEFAULT_USER_PASSWORD_UNICODE	L"userpass"

//#define DISPLAY_CONSOLE_OUTPUT

//#define START_SERVICE_AFTER_INSTALLATION

#define READ_BUFFER_SIZE (65536)

#define VCHAN_BUFFER_SIZE 65536
#define PIPE_BUFFER_SIZE 65536
#define PIPE_DEFAULT_TIMEOUT 50

typedef enum {
    PTYPE_INVALID = 0,
    PTYPE_STDOUT,
    PTYPE_STDERR
} PIPE_TYPE;

// child i/o state for a single pipe
typedef struct _PIPE_DATA {
    HANDLE	hReadPipe;
    PIPE_TYPE	bPipeType;
    BOOL	bReadInProgress;
    BOOL	bDataIsReady;
    BOOL bPipeClosed;
    BOOL bVchanWritePending;
    DWORD	dwSentBytes;
    OVERLAPPED	olRead;
    CHAR	ReadBuffer[READ_BUFFER_SIZE + 1];
} PIPE_DATA;

// state of a child process
typedef struct _CHILD_INFO {
    BOOLEAN	bChildIsReady;

    HANDLE	hProcess;
    HANDLE	hWriteStdinPipe;
    BOOL	bStdinPipeClosed;
    BOOL	bChildExited;
    DWORD	dwExitCode;

    BOOL	bReadingIsDisabled;

    PIPE_DATA	Stdout;
    PIPE_DATA	Stderr;

    libvchan_t *vchan; // associated client's vchan for i/o data exchange

    // Usually qrexec-client is the vchan server, but in vm/vm connections
    // two agents are connected. This field is TRUE if we're the server.
    BOOL bIsVchanServer;
} CHILD_INFO;

ULONG AddExistingChild(
    CHILD_INFO *pChildInfo
);

ULONG CreateChildPipes(
    CHILD_INFO *pChildInfo,
    HANDLE *phPipeStdin,
    HANDLE *phPipeStdout,
    HANDLE *phPipeStderr
);

ULONG CloseReadPipeHandles(
    CHILD_INFO *pChildInfo,
    PIPE_DATA *pPipeData
);

ULONG send_msg_to_vchan(
    libvchan_t *vchan,
    int type,
    void *pData,
    ULONG uDataSize,
    ULONG *puDataWritten
);

ULONG send_exit_code_vchan(
    libvchan_t *vchan,
    int status
    );

ULONG send_exit_code(
    CHILD_INFO *,
    int status
    );

// data handlers for vchan (qrexec-client or another agent in case of vm/vm connection)
ULONG handle_stdin(struct msg_header *hdr, CHILD_INFO *);
ULONG handle_stdout(struct msg_header *hdr, CHILD_INFO *);
ULONG handle_stderr(struct msg_header *hdr, CHILD_INFO *);
