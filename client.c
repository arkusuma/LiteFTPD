#include <stdio.h>
#include <string.h>

#include <windows.h>
#include <commctrl.h>

#include "resource.h"

#define MAX_BUFFER      0x8000

HWND hmain;

HBITMAP screen = 0;
POINT screen_pos;
SIZE screen_size;

SOCKET control[2] = {-1, -1};
int connected = TRUE;
int update_delay = 4000;        /* in miliseconds */
int last_download = 0;

int timeout = 30000;            /* in miliseconds */

HANDLE thread;

#define PROGRESS_MAX    1000

#define GET_SCROLL_X    GetScrollPos(hmain, SB_HORZ)
#define GET_SCROLL_Y    GetScrollPos(hmain, SB_VERT)

char screen_file[MAX_PATH];

void update_scrollbars();
void delete_screen();
int load_screen(const char *filename);

void message(const char *format, ...)
{
    char buf[1024];
    va_list args;

    va_start(args, format);
    vsprintf(buf, format, args);

    MessageBox(hmain, buf, "Message", MB_ICONINFORMATION | MB_OK);
}

void invalidate(HWND hwnd, BOOL erase)
{
    RECT rect;
    GetClientRect(hwnd, &rect);
    InvalidateRect(hwnd, &rect, erase);
}

void disconnect(int wait)
{
    int i;

    for (i = 0; i < 2; i++)
        if (control[i] != -1)
        {
            const char *quit = "QUIT\r\n";
            send(control[i], quit, strlen(quit), 0);
            closesocket(control[i]);
            control[i] = -1;
        }

    if (connected)
    {
        connected = FALSE;
        SetDlgItemText(hmain, IDC_CONNECT, "&Connect");
        SendDlgItemMessage(hmain, IDC_PROGRESS, PBM_SETPOS, 0, 0);

        if (wait)
        {
            int ret;
            DWORD status;

            do
            {
                ret = GetExitCodeThread(thread, &status);
                Sleep(100);
            } while (ret && status == STILL_ACTIVE);
        }
    }
}

void update_scrollbars()
{
    RECT rect, r2;

    do
    {
        GetClientRect(hmain, &rect);
        SendMessage(hmain, WM_SIZE, 0,
                (rect.right - rect.left) | (rect.bottom - rect.top) << 16);
        GetClientRect(hmain, &r2);
    } while (rect.right != r2.right || rect.bottom != r2.bottom);
}

void delete_screen()
{
    if (screen != 0)
    {
        DeleteObject(screen);

        screen = 0;
        screen_size.cx = 0;
        screen_size.cy = 0;
    }
}

int load_screen(const char *filename)
{
    delete_screen();
    screen = LoadImage(0, filename, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);

    if (screen != 0)
    {
        BITMAP bmp;

        GetObject(screen, sizeof(bmp), &bmp);
        screen_size.cx = bmp.bmWidth;
        screen_size.cy = bmp.bmHeight;
    }

    return (screen != 0);
}

DWORD WINAPI manage_download(void *arg)
{
    char buf[MAX_BUFFER];
    SOCKET sockfd, readfd;
    SOCKADDR_IN my_addr;
    int len, h, p, tick;
    HANDLE fh;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    /* get my host address */
    len = sizeof(my_addr);
    getsockname(control[1], (SOCKADDR *) &my_addr, &len);
    h = ntohl(my_addr.sin_addr.s_addr);

    my_addr.sin_family = AF_INET;
    my_addr.sin_port = 0;

    if (bind(sockfd, (SOCKADDR *) &my_addr, sizeof(my_addr)) == -1 ||
        listen(sockfd, 0) == -1)
    {
        closesocket(sockfd);
        disconnect(FALSE);
        return 0;
    }

    /* get my port */
    len = sizeof(my_addr);
    getsockname(sockfd, (SOCKADDR *) &my_addr, &len);
    p = ntohs(my_addr.sin_port);

    while (connected)
    {
        SendDlgItemMessage(hmain, IDC_PROGRESS, PBM_SETPOS, PROGRESS_MAX, 0);

        sprintf(buf, "PORT %d,%d,%d,%d,%d,%d\r\n",
                (h >> 24) & 255, (h >> 16) & 255, (h >> 8) & 255, h & 255,
                (p >> 8) & 255, p & 255);

        send(control[1], buf, strlen(buf), 0);
        len = recv(control[1], buf, sizeof(buf), 0);
        if (len == 0 || len == SOCKET_ERROR || buf[0] != '2')
            break;

        sprintf(buf, "RETR /screen.bmp\r\n");
        send(control[1], buf, strlen(buf), 0);
        len = recv(control[1], buf, sizeof(buf), 0);
        if (len == 0 || len == SOCKET_ERROR || buf[0] != '1')
            break;

        tick = GetTickCount();
        do
        {
            fd_set readfds;
            TIMEVAL timeval;

            FD_ZERO(&readfds);
            FD_SET(sockfd, &readfds);
            timeval.tv_sec = 1;
            timeval.tv_usec = 0;

            len = select(0, &readfds, NULL, NULL, &timeval);

        } while (len == 0 && connected && GetTickCount() < tick+timeout);

        if (len == 0 || !connected)
            break;

        len = sizeof(my_addr);
        readfd = accept(sockfd, (SOCKADDR *) &my_addr, &len);
        if (readfd == -1)
            break;

        fh = CreateFile(screen_file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                FILE_ATTRIBUTE_TEMPORARY, NULL);
        if (fh == INVALID_HANDLE_VALUE)
        {
            closesocket(readfd);
            break;
        }

        for (; connected;)
        {
            DWORD written;

            len = recv(readfd, buf, sizeof(buf), 0);
            if (len == 0 || len == SOCKET_ERROR)
                break;
            WriteFile(fh, buf, len, &written, NULL);
        }

        closesocket(readfd);
        CloseHandle(fh);

        if (len == SOCKET_ERROR)
        {
            DeleteFile(screen_file);
            break;
        }

        len = recv(control[1], buf, sizeof(buf), 0);
        if (len == 0 || len == SOCKET_ERROR || buf[0] != '2')
        {
            DeleteFile(screen_file);
            break;
        }

        if (load_screen(screen_file))
            last_download = GetTickCount();

        DeleteFile(screen_file);

        update_scrollbars();

        while (connected)
        {
            int delta = last_download+update_delay-GetTickCount();
            if (delta <= 0)
                break;
            delta = PROGRESS_MAX - MulDiv(delta, PROGRESS_MAX, update_delay);
            SendDlgItemMessage(hmain, IDC_PROGRESS, PBM_SETPOS, delta, 0);
            Sleep(100);
        }
    }

    closesocket(sockfd);
    disconnect(FALSE);

    return 0;
}

int my_connect(const char *arg)
{
    SOCKADDR_IN your_addr;
    struct hostent *ent;
    char buf[MAX_BUFFER];
    int i, len;

    memset(&your_addr.sin_zero, 0, sizeof(your_addr.sin_zero));
    your_addr.sin_family = AF_INET;
    your_addr.sin_port = htons(21);

    your_addr.sin_addr.s_addr = inet_addr(arg);
    if (your_addr.sin_addr.s_addr == -1)
    {
        ent = gethostbyname(arg);
        if (ent == NULL)
            return FALSE;
        your_addr.sin_addr = *((struct in_addr *) ent->h_addr);
    }

    for (i = 0; i < 2; i++)
    {
        control[i] = socket(AF_INET, SOCK_STREAM, 0);

        if (connect(control[i], (SOCKADDR *) &your_addr, sizeof(your_addr)) == -1)
            break;

        len = recv(control[i], buf, sizeof(buf), 0);
        if (len == 0 || len == SOCKET_ERROR)
            break;

        buf[len] = 0;
        if (strstr(buf, "LiteFTP") == NULL)
            break;
    }

    if (i != 2)
    {
        disconnect(FALSE);
        return FALSE;
    }

    return TRUE;
}

void free_replies()
{
    fd_set readfds;
    TIMEVAL tv;
    char buf[256];

    tv.tv_sec = 0;
    tv.tv_usec = 0;

    for (;;)
    {
        FD_ZERO(&readfds);
        FD_SET(control[0], &readfds);

        if (select(0, &readfds, NULL, NULL, &tv) == 0)
            break;

        recv(control[0], buf, sizeof(buf), 0);
    }
}

typedef struct
{
    int x, y;
    int event;
} MOUSE_CLICK;

#define MAX_MOUSE_CLICK 32
#define CLICK_DELTA     5

MOUSE_CLICK mouse_click[MAX_MOUSE_CLICK];
int mouse_click_count = 0;

int i_abs(int i)
{
    return (i < 0) ? -i : i;
}

#define TOOL_HEIGHT     22
#define EDIT_HEIGHT     20

int WINAPI DlgProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_INITDIALOG:
            {
                HDC dc;
                int width, height, w_width, w_height;

                hmain = hwnd;
                disconnect(FALSE);
                update_scrollbars();

                dc = GetDC(0);
                width = GetDeviceCaps(dc, HORZRES);
                height = GetDeviceCaps(dc, VERTRES);
                ReleaseDC(0, dc);

                w_width = width * 3 / 4;
                w_height = height * 3 / 4;
                SetWindowPos(hwnd, 0,
                        (width - w_width) / 2, (height - w_height) / 2,
                        w_width, w_height, 0);

                SendDlgItemMessage(hwnd, IDC_PROGRESS, PBM_SETRANGE, 0, MAKELPARAM(0, PROGRESS_MAX));

                SendDlgItemMessage(hwnd, IDC_TIMER, TBM_SETRANGE, 0, MAKELPARAM(0, 10000));
                SendDlgItemMessage(hwnd, IDC_TIMER, TBM_SETPOS, TRUE, 4000);

                return TRUE;
            }
        case WM_CLOSE:
            EndDialog(hwnd, 0);
            return TRUE;
        case WM_SIZE:
            {
                int width, height;
                RECT rect;
                POINT normal;

                width = lParam & 0xFFFF;
                height = (lParam >> 16) - TOOL_HEIGHT - EDIT_HEIGHT;

                GetWindowRect(GetDlgItem(hwnd, IDC_PROGRESS), &rect);
                normal.x = 0;
                normal.y = 0;
                ClientToScreen(hwnd, &normal);
                OffsetRect(&rect, -normal.x, -normal.y);
                SetWindowPos(GetDlgItem(hwnd, IDC_PROGRESS), 0, rect.left, 0,
                        width-rect.left, TOOL_HEIGHT, 0);
                SetWindowPos(GetDlgItem(hwnd, IDC_EDIT), 0, 0, TOOL_HEIGHT,
                        width, EDIT_HEIGHT, 0);

                screen_pos.x = (width - screen_size.cx) / 2;
                screen_pos.y = (height - screen_size.cy) / 2;
                if (screen_pos.x < 0)
                    screen_pos.x = 0;
                if (screen_pos.y < 0)
                    screen_pos.y = 0;
                screen_pos.y += TOOL_HEIGHT + EDIT_HEIGHT;

                width = screen_size.cx - width;
                height = screen_size.cy - height;
                if (width < 0)
                    width = 0;
                if (height < 0)
                    height = 0;

                SetScrollRange(hwnd, SB_HORZ, 0, width, TRUE);
                SetScrollRange(hwnd, SB_VERT, 0, height, TRUE);

                invalidate(hwnd, FALSE);

                return TRUE;
            }
        case WM_HSCROLL:
            {
                int size;

                size = GET_SCROLL_X;
                switch (LOWORD(wParam))
                {
                    case SB_THUMBTRACK: 
                        if (lParam == (long) GetDlgItem(hwnd, IDC_TIMER))
                        {
                            char buf[9];
                            update_delay = SendDlgItemMessage(hwnd, IDC_TIMER, TBM_GETPOS, 0, 0);
                            sprintf(buf, "%d", update_delay);
                            SetDlgItemText(hwnd, IDC_EDIT, buf);
                            return TRUE;
                        }
                        size = wParam >> 16; 
                        break;
                    case SB_LINELEFT: size -= 10; break;
                    case SB_LINERIGHT: size += 10; break;
                    case SB_PAGELEFT: size -= 100; break;
                    case SB_PAGERIGHT: size += 100; break;
                }

                SetScrollPos(hwnd, SB_HORZ, size, TRUE);
                invalidate(hwnd, FALSE);

                SetWindowLong(hwnd, DWL_MSGRESULT, 0);
                return TRUE;
            }
        case WM_VSCROLL:
             {
                int size;

                size = GET_SCROLL_Y;
                switch (wParam & 0xFFFF)
                {
                    case SB_THUMBTRACK: size = wParam >> 16; break;
                    case SB_LINEUP: size -= 10; break;
                    case SB_LINEDOWN: size += 10; break;
                    case SB_PAGEUP: size -= 100; break;
                    case SB_PAGEDOWN: size += 100; break;
                }

                SetScrollPos(hwnd, SB_VERT, size, TRUE);
                invalidate(hwnd, FALSE);

                SetWindowLong(hwnd, DWL_MSGRESULT, 0);
                return TRUE;
            }
        case WM_ERASEBKGND:
             SetWindowLong(hwnd, DWL_MSGRESULT, 0);
             return TRUE;
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDBLCLK:
             {
                 int x, y;

                 if (mouse_click_count >= MAX_MOUSE_CLICK)
                     return TRUE;

                 x = (lParam & 0xFFFF) - screen_pos.x + GET_SCROLL_X;
                 y = (lParam >> 16) - screen_pos.y + GET_SCROLL_Y;
                 if (x < 0 || x >= screen_size.cx || y < 0 || y >= screen_size.cy)
                     return TRUE;

                 mouse_click[mouse_click_count].x = x;
                 mouse_click[mouse_click_count].y = y;
                 mouse_click[mouse_click_count].event = uMsg;
                 mouse_click_count++;

                 /* GetDoubleClickTime() is to much delay (500) */
                 SetTimer(hwnd, 100, 200, NULL);

                 return TRUE;
             }
        case WM_TIMER:
             {
                 char *c, buf[256];
                 int i, start, len;

                 start = 0;
                 while (start < mouse_click_count)
                 {
                     sprintf(buf, "SITE MOUSE %d,%d ",
                             mouse_click[start].x,
                             mouse_click[start].y);

                     c = buf + strlen(buf);
                     for (i = start; i < mouse_click_count; i++, c++)
                     {
                         if (i_abs(mouse_click[start].x - mouse_click[i].x) +
                             i_abs(mouse_click[start].y - mouse_click[i].y) > CLICK_DELTA)

                             break;

                         switch (mouse_click[i].event)
                         {
                             case WM_LBUTTONDOWN:
                             case WM_LBUTTONDBLCLK: *c = 'l'; break;
                             case WM_LBUTTONUP:     *c = 'L'; break;
                             case WM_RBUTTONDOWN:
                             case WM_RBUTTONDBLCLK: *c = 'r'; break;
                             case WM_RBUTTONUP:     *c = 'R'; break;
                             case WM_MBUTTONDOWN:
                             case WM_MBUTTONDBLCLK: *c = 'm'; break;
                             case WM_MBUTTONUP:     *c = 'M'; break;
                         }
                     }
                     start = i;
                     strcpy(c, "\r\n");

                     len = send(control[0], buf, strlen(buf), 0);
                     if (len == SOCKET_ERROR)
                         disconnect(TRUE);
                 }
                 free_replies();

                 mouse_click_count = 0;
                 KillTimer(hwnd, 100);

                 return TRUE;
             }
        case WM_PAINT:
            {
                RECT rect;
                POINT norm;
                HDC dc, screen_dc;
                PAINTSTRUCT paint;
                HBITMAP old_hbm;

                GetUpdateRect(hwnd, &rect, screen == 0);
                if (IsRectEmpty(&rect))
                    return FALSE;

                dc = BeginPaint(hwnd, &paint);

                norm.x = 0;
                norm.y = 0;
                ClientToScreen(hwnd, &norm);
                GetWindowRect(GetDlgItem(hwnd, IDC_EDIT), &rect);
                OffsetRect(&rect, -norm.x, -norm.y);
                ExcludeClipRect(dc, 0, 0, rect.right, rect.bottom);

                GetClientRect(hwnd, &rect);
                rect.left = screen_pos.x - GET_SCROLL_X;
                rect.top = screen_pos.y - GET_SCROLL_Y;
                rect.right = rect.left + screen_size.cx;
                rect.bottom = rect.top + screen_size.cy;

                screen_dc = CreateCompatibleDC(dc);
                old_hbm = SelectObject(screen_dc, screen);

                BitBlt(dc, rect.left, rect.top, screen_size.cx, screen_size.cy,
                        screen_dc, 0, 0, SRCCOPY);

                ExcludeClipRect(dc, rect.left, rect.top, rect.right, rect.bottom);

                GetClientRect(hwnd, &rect);
                FillRect(dc, &rect, (HBRUSH) (COLOR_BTNFACE+1));

                SelectObject(screen_dc, old_hbm);
                DeleteDC(screen_dc);

                EndPaint(hwnd, &paint);
                return TRUE;
            }
        case WM_COMMAND:
            {
                switch (wParam)
                {
                    case IDC_WRITE:
                        {
                            char buf[256];
                            int len;

                            if (!connected)
                            {
                                message("You must connect first.");
                                return TRUE;
                            }

                            strcpy(buf, "SITE WRITE ");
                            len = strlen(buf);
                            GetDlgItemText(hwnd, IDC_EDIT, &buf[len], sizeof(buf)-len-2);
                            strcat(buf, "\r\n");

                            len = send(control[0], buf, strlen(buf), 0);
                            if (len == SOCKET_ERROR)
                                disconnect(TRUE);
                            free_replies();

                            return TRUE;
                        }
                    case IDC_EXECUTE:
                        {
                            char buf[256];
                            int len;

                            if (!connected)
                            {
                                message("You must connect first.");
                                return TRUE;
                            }

                            strcpy(buf, "SITE CMD ");
                            len = strlen(buf);
                            GetDlgItemText(hwnd, IDC_EDIT, &buf[len], sizeof(buf)-len-2);
                            strcat(buf, "\r\n");

                            len = send(control[0], buf, strlen(buf), 0);
                            if (len == SOCKET_ERROR)
                                disconnect(TRUE);
                            free_replies();

                            return TRUE;

                        }
                    case IDC_CONNECT:
                        {
                            char buf[256];
                            DWORD tid;

                            if (connected)
                                disconnect(TRUE);
                            else
                            {
                                GetDlgItemText(hwnd, IDC_EDIT, buf, sizeof(buf));
                                connected = my_connect(buf);
                                if (connected)
                                {
                                    SetWindowText(GetDlgItem(hwnd, IDC_CONNECT), "&Disconnect");
                                    CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) manage_download, 0, 0, &tid);
                                }
                                else
                                    message("Could not connect to: %s", buf);
                                EnableWindow(GetDlgItem(hwnd, IDC_REFRESH), connected);
                            }

                            return TRUE;
                        }
                    case IDC_REFRESH:
                        last_download = 0;
                        return TRUE;
                }
            }
    }
    return FALSE;
}


int WINAPI
WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
        LPSTR lpCmdLine, int nCmdShow)
{
    WSADATA wsa_data;

    if (WSAStartup(MAKEWORD(1, 1), &wsa_data) != 0)
        return 1;

    GetTempPath(sizeof(screen_file), screen_file);
    strcat(screen_file, "screen.bmp");
    
    InitCommonControls();
    DialogBoxParam(hInstance, MAKEINTRESOURCE(IDD_MAIN), 0, DlgProc,
                  (LPARAM) lpCmdLine);

    delete_screen();
    WSACleanup();
    return 0;
}
