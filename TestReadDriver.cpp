//
// Created by Giacomo Bergami on 16/09/2026.
//

#ifndef MAX_NAME_LENGTH
#define MAX_NAME_LENGTH     (25)
#endif

#include <magic_enum/magic_enum.hpp>
#include "malamore/utility/StdStream.h"

enum error_type { NO_ERROR_WHATSOEVER = 0,FSCC_TIMEOUT=1, FSCC_INCORRECT_MODE,
                  FSCC_BUFFER_TOO_SMALL, FSCC_PORT_NOT_FOUND,
                  FSCC_INVALID_ACCESS, FSCC_INVALID_PARAMETER };
enum transmit_type { XF=0, XREP=1, TXT=2, TXEXT=4 };

#include <Windows.h>

int translate_error(int e)
{
#ifdef _WIN32
    switch (e) {
        case ERROR_ACCESS_DENIED:
            return FSCC_INVALID_ACCESS;
        case ERROR_FILE_NOT_FOUND:
            return FSCC_PORT_NOT_FOUND;
        case ERROR_SEM_TIMEOUT:
            return FSCC_TIMEOUT;
        case ERROR_INSUFFICIENT_BUFFER:
            return FSCC_BUFFER_TOO_SMALL;
        default:
            return e;
    }
#else
    switch (e) {
        case ENOENT:
            return FSCC_PORT_NOT_FOUND;
        case EACCES:
            return FSCC_INVALID_ACCESS;
        case EOPNOTSUPP:
            return FSCC_INCORRECT_MODE;
        case ETIMEDOUT:
            return FSCC_TIMEOUT;
        case ENOBUFS:
            return FSCC_BUFFER_TOO_SMALL;
        default:
            return e;
    }
#endif
}

int fscc_connect(unsigned port_num, HANDLE *h)
{
    char name[MAX_NAME_LENGTH];

#ifdef _WIN32
    sprintf_s(name, MAX_NAME_LENGTH, "\\\\.\\FSCC%u", port_num);

    *h = CreateFile(name,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
            NULL
    );

    return (*h != INVALID_HANDLE_VALUE) ? 0 : translate_error(GetLastError());
#else
    sprintf(name, "/dev/fscc%u", port_num);

    *h = open(name, O_RDWR);

    return (*h != -1) ? 0 : translate_error(errno);
#endif
}

int fscc_write(HANDLE h, char *buf, unsigned size,
               unsigned long *bytes_written, OVERLAPPED *o)
{
    int result;
    OVERLAPPED o2;
    memset(&o2, 0, sizeof(o));
    // o2.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!o) {
        o = &o2;
    }

#ifdef _WIN32
    result = WriteFile(h, buf, size, (DWORD*)bytes_written, o);

    return (result == TRUE) ? 0 : translate_error(GetLastError());
#else
    UNUSED(o);

    result = write(h, buf, size);

    if (result == -1)
        return translate_error(errno);

    *bytes_written = result;

    return 0;
#endif
}

int fscc_read(HANDLE h, char *buf, unsigned size, unsigned *bytes_read,
              OVERLAPPED *o)
{
    int result;
#ifdef _WIN32

    result = ReadFile(h, buf, size, (DWORD*)bytes_read, o);

    return (result == TRUE) ? 0 : translate_error(GetLastError());
#else
    UNUSED(o);

    result = read(h, buf, size);

    if (result == -1)
        return translate_error(errno);

    *bytes_read = result;

    return 0;
#endif
}


int fscc_read_with_blocking(HANDLE h, char *buf, unsigned size,
                            unsigned *bytes_read)
{
#ifdef _WIN32
    int result;
    OVERLAPPED ol;

    memset(&ol, 0, sizeof(ol));

    result = fscc_read(h, buf, size, bytes_read, &ol);

    if (result == 997)
        result = GetOverlappedResult(h, &ol, (DWORD *)bytes_read, 1);

    return (result == TRUE) ? 0 : translate_error(GetLastError());
#else
    return fscc_read(h, buf, size, bytes_read, 0);
#endif
}

int fscc_read_with_timeout(HANDLE h, char *buf, unsigned size,
                           unsigned long *bytes_read, unsigned timeout)
{
#ifdef _WIN32
    OVERLAPPED o;
    BOOL result;

    memset(&o, 0, sizeof(o));

    o.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    if (o.hEvent == NULL)
        return GetLastError();

    result = ReadFile(h, buf, size, (DWORD*)bytes_read, &o);

    if (result == FALSE) {
        DWORD status;
        int e;

        /* There was an actual error instead of a pending read */
        if ((e = GetLastError()) != ERROR_IO_PENDING) {
            CloseHandle(o.hEvent);
            return e;
        }

        do {
            status = WaitForSingleObject(o.hEvent, timeout);

            switch (status) {
                case WAIT_TIMEOUT:
                    *bytes_read = 0;
                    /* Switch to CancelIoEx if using Vista or higher and prefer the
                       way CancelIoEx operates. */
                    /* CancelIoEx(h, &o); */
                    CancelIo(h);
                    CloseHandle(o.hEvent);
                    return ERROR_SUCCESS;

                case WAIT_FAILED:
                    e = GetLastError();
                    CloseHandle(o.hEvent);
                    return e;
            }
        }
        while (status != WAIT_OBJECT_0);

        result = GetOverlappedResult(h, &o, (DWORD *)bytes_read, TRUE);
    }

    CloseHandle(o.hEvent);

    return (result == TRUE) ? 0 : translate_error(GetLastError());
#else
    struct pollfd fds[1];
    int e = 0;

    fds[0].fd = h;
    fds[0].events = POLLIN;

    switch (poll(fds, 1, timeout)) {
        case -1:
            e = errno;
            break;

        case 0:
            break;

        default:
            e = fscc_read_with_blocking(h, buf, size, bytes_read);
            break;
    }

    return e;
#endif
}


/**
 * This main function is mainly done to check whether I can read something from the stream.
 * It only makes sense reading
 */
int main(void) {
    char odata[] = "Hello worldo!";
    char odata2[] = "Prima Io!";
    MalamorePath driver_path{L"\\\\.\\FSCC2"};
    printf("IS HARDWARE: %s\n", (driver_path.is_hardware_device() ? "TRUE" : "FALSE"));

    {
        MalamoreIOStream device;


        if (NT_SUCCESS(device.Open(L"\\\\.\\FSCC2", StreamFlags::Write | StreamFlags::Read | StreamFlags::ASync))) {
            if (device.Sputn(odata2, sizeof(odata2))) {
                if (!device.Flush()) {
                    fprintf(stderr, "Flush failed with %d\n", GetLastError());
                    return EXIT_FAILURE;
                }
            } else {
                fprintf(stderr, "Internal buffer write failed with %d\n", GetLastError());
                return EXIT_FAILURE;
            }
        }
    }


    HANDLE h = 0;
    DWORD tmp;

    char idata[20];

    /* Open port 0 in a blocking IO mode */
    h = CreateFile("\\\\.\\FSCC2",             GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
            NULL);
    fprintf(stderr, "Test LastError %d\n", GetLastError());
    if (h == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "CreateFile failed with %d\n", GetLastError());
        return EXIT_FAILURE;
    }
    int result;
    //
    // /* Send "Hello world!" text */
    // auto result = WriteFile(h, odata, sizeof(odata), &tmp, NULL);
    //
    //
    // result =fscc_write(h, odata, sizeof(odata), &tmp, NULL);
    // if (result) {
    //     fprintf(stderr, "WriteFile failed with %d\n", GetLastError());
    //     if (result != ERROR_IO_PENDING) {
    //         return EXIT_FAILURE;
    //     }
    // }

    /* Read the data back in (with our loopback connector) */
    result =  fscc_read_with_timeout(h, idata, sizeof(idata), &tmp, 3000);
    if (result) {
        fprintf(stderr, "ReadFile failed with %d\n", GetLastError());
        return EXIT_FAILURE;
    }

    fprintf(stdout, "RCV: %s\n", idata);
    CloseHandle(h);

    return EXIT_SUCCESS;
}
