#ifdef USE_SITE

#ifndef _SITE_C_
#define _SITE_C_

#define MOUSE_DELAY 50

char *site_command[] =
{
    "WRITE",
    "MOUSE",
    "CMD",
    "CHMOD",
    0
};

enum {
    site_WRITE,
    site_MOUSE,
    site_CMD,
    site_CHMOD,
    site_INVALID
};

int do_SITE_WRITE(ftp_session *s, char *param);
int do_SITE_MOUSE(ftp_session *s, char *param);
int do_SITE_CMD(ftp_session *s, char *param);
int do_SITE_CHMOD(ftp_session *s, char *param);

HANDLER site_handler[] =
{
    do_SITE_WRITE,
    do_SITE_MOUSE,
    do_SITE_CMD,
    do_SITE_CHMOD
};

int do_SITE(ftp_session *s, char *param)
{
    int cmd;

    MATCH_SP(param);

    cmd = get_command(site_command, param);
    if (cmd == site_INVALID)
        return 500;
    else
        return site_handler[cmd](s, param + strlen(site_command[cmd]));
}

int do_SITE_WRITE(ftp_session *s, char *param)
{
    static int translated = FALSE;
    static char unshifted[256];
    static char shifted[256];

    int caps_on, use_shift, use_escape;
    char key, c, *p;
    int len, i, j;

    MATCH_SP(param);

    len = get_string(param, NULL, 0);
    if (len == 0)
        return 501;
    p = param;
    param += len;

    MATCH_CRLF(param);

    if (!translated)
    {
        static const char *set0 = ")!@#$%^&*("; /* shifted '0'..'9' */
        static const char *set1 = ":+<_>?~";    /* shifted 0xBA..0xC0 */
        static const char *set2 = "{|}""";      /* shifted 0xDB..0xDE */

        for (i = 0; i < 256; i++)
            unshifted[i] = MapVirtualKey(i, 2);
        /* MapVirtualKey() returned upper case alphabet instead of lower */
        for (i = 'A'; i <= 'Z'; i++)
            unshifted[i] = i - 'A' + 'a';
        /* We want VK_RETURN to be 10 ('\n') instead of 13 ('\r') */
        unshifted[VK_RETURN] = '\n';

        memset(shifted, 0, sizeof(shifted));
        for (i = 'A'; i <= 'Z'; i++)
            shifted[i] = i;
        for (i = 0; set0[i] != 0; i++)
            shifted['0'+i] = set0[i];
        for (i = 0; set1[i] != 0; i++)
            shifted[0xBA+i] = set1[i];
        for (i = 0; set2[i] != 0; i++)
            shifted[0xDB+i] = set2[i];

        translated = TRUE;
    }

    caps_on = (GetKeyState(VK_CAPITAL) & 1) != 0;
    for (i = 0; i < len; i++)
    {
        use_escape = FALSE;
        if (p[i] != '\\')
            key = p[i];
        else
        {
            i++;
            switch (p[i])
            {
                case 'a': key = '\a'; break;
                case 'b': key = '\b'; break;
                case 't': key = '\t'; break;
                case 'v': key = '\v'; break;
                case 'f': key = '\f'; break;
                case 'n': key = '\n'; break;
                case 'x': {
                              c = toupper(p[++i]);
                              if (isdigit(c))
                                  key = c - '0';
                              else if (c >= 'A' && c <= 'F')
                                  key = c - 'A' + 10;
                              else
                                  continue;
                              key <<= 4;

                              c = toupper(p[++i]);
                              if (isdigit(c))
                                  key |= c - '0';
                              else if (c >= 'A' && c <= 'F')
                                  key |= c - 'A' + 10;
                              else
                                  continue;

                              use_escape = TRUE;

                              break;
                          }
                default: key = p[i];
            }
            if (i >= len)
                break;
        }

        use_shift = FALSE;
        if (!use_escape)
        {
            use_shift = TRUE;
            for (j = 0; j < 256; j++)
                if (key == unshifted[j])
                {
                    key = j;
                    use_shift = FALSE;
                }

            if (use_shift)
            {
                use_shift = FALSE;
                for (j = 0; j < 256; j++)
                    if (key == shifted[j])
                    {
                        key = j;
                        use_shift = TRUE;
                    }
            }

            if (isalpha(key) && caps_on)
                use_shift = !use_shift;
        }

        if (use_shift)
            keybd_event(VK_SHIFT, 0, 0, 0);

        keybd_event(key, 0, 0, 0);
        keybd_event(key, 0, KEYEVENTF_KEYUP, 0);

        if (use_shift)
            keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0);
    }

    return 200;
}

int do_SITE_MOUSE(ftp_session *s, char *param)
{
    static const char *msg = "200 Mouse is at (%d,%d).\r\n";

    POINT cursor;
    long *pos;
    char *p, arg[MAX_FTP_COMMAND];
    int i, event, len, width, height;
    HDC hdc;

    pos = (long *) &cursor;
    GetCursorPos(&cursor);
    if (*param != ' ')
    {
        MATCH_CRLF(param);

        ftp_printf(s->control, msg, cursor.x, cursor.y);

        return 0;
    }

    param++;
    for (i = 0; i < 2; i++)
    {
        if (i != 0)
            MATCH(param, ',');

        len = get_number(param, arg, sizeof(arg));
        if (len == 0)
            return 501;
        param += len;

        pos[i] = atoi(arg);
    }

    hdc = GetDC(0);
    width = GetDeviceCaps(hdc, HORZRES);
    height = GetDeviceCaps(hdc, VERTRES);
    ReleaseDC(0, hdc);

    pos[0] = pos[0] * 0xFFFF / (width-1);
    pos[1] = pos[1] * 0xFFFF / (height-1);

    if (*param != ' ')
    {
        MATCH_CRLF(param);

        mouse_event(MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE, pos[0], pos[1], 0, 0);

        GetCursorPos(&cursor);
        ftp_printf(s->control, msg, cursor.x, cursor.y);

        return 0;
    }

    MATCH_SP(param);

    len = get_string(param, NULL, 0);
    if (len == 0)
        return 501;

    p = param;
    for (i = 0; i < len; i++)
        switch (toupper(p[i]))
        {
            case 'L':
            case 'R':
            case 'M':
                break;
            default:
                return 501;
        }
    param += len;

    MATCH_CRLF(param);

    mouse_event(MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE, pos[0], pos[1], 0, 0);
    for (i = 0; i < len; i++)
    {
        event = 0;
        switch (p[i])
        {
            case 'l': event = MOUSEEVENTF_LEFTDOWN; break;
            case 'r': event = MOUSEEVENTF_RIGHTDOWN; break;
            case 'm': event = MOUSEEVENTF_MIDDLEDOWN; break;
            case 'L': event = MOUSEEVENTF_LEFTUP; break;
            case 'R': event = MOUSEEVENTF_RIGHTUP; break;
            case 'M': event = MOUSEEVENTF_MIDDLEUP; break;
        }

        mouse_event(event, 0, 0, 0, 0);
        Sleep(MOUSE_DELAY);
    }

    GetCursorPos(&cursor);
    ftp_printf(s->control, msg, cursor.x, cursor.y);

    return 0;
}

int do_SITE_CMD(ftp_session *s, char *param)
{
    int len;
    STARTUPINFO si;
    PROCESS_INFORMATION pi;

    MATCH_SP(param);
    len = strlen(param);
    if (len < 2 || param[len-2] != '\r' || param[len-1] != '\n')
        return 501;
    param[len-2] = 0;

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_FORCEOFFFEEDBACK;
    if (CreateProcess(NULL, param, NULL, NULL, FALSE, NORMAL_PRIORITY_CLASS,
                NULL, NULL, &si, &pi))
        return 200;
    else
        return 550;
}

int do_SITE_CHMOD(ftp_session *s, char *param)
{
    return 202;
}

#endif
#endif
