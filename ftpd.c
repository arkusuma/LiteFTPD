/* LiteFTP Server v.0.3 (stable)
 *
 * Author      : AR Kusuma
 * Reference   : RFC 959
 *
 * Start time  : 25 July 2003
 * Last update : 28 February 2004
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

#include <windows.h>

/* #define DEBUG */

/* #define USE_SITE */

/* #define USE_SPECIAL_FILE */
/* #define USE_SCREEN_BMP */
/* #define USE_SCREEN_JPG */

#ifndef USE_SPECIAL_FILE
    #ifdef USE_SCREEN_BMP
    #undef USE_SCREEN_BMP
    #endif
    #ifdef USE_SCREEN_JPG
    #undef USE_SCREEN_JPG
    #endif
#endif

#define MAX_FTP_PATH    MAX_PATH
#define MAX_FTP_COMMAND 512

#define MAX_BUFFER_FILLED       0x2000
#define MAX_BUFFER              (2*MAX_BUFFER_FILLED)
#define MAX_SESSION             20

typedef struct
{
    int sid;            /* session id */
    int running;
    SOCKET control;
    SOCKET data;
    int type;
    int mode;
    int stru;
    long host;
    short port;
    long data_host;     /* server address for passive transfer */
    short data_port;    /* server port for passive transfer */
    int passive;
    int curr_command;
    int prev_command;
    int closed;         /* set to TRUE to terminate session */
    int tick;           /* last command tick count */
    size_t restart;     /* parameter of REST */
    char rename[MAX_FTP_PATH];  /* parameter of RNFR */
    char dir[MAX_FTP_PATH];     /* current ftp directory */
} ftp_session;

char *ftp_command[] =
{
    /* Access Control Commands */
    "USER", "PASS", "ACCT", "CWD",
    "CDUP", "SMNT", "REIN", "QUIT",
    /* Transfer Parameter Commands */
    "PORT", "PASV", "TYPE", "STRU",
    "MODE",
    /* FTP Service Commands */
    "RETR", "STOR", "STOU", "APPE",
    "ALLO", "REST", "RNFR", "RNTO",
    "ABOR", "DELE", "RMD",  "MKD",
    "PWD",  "LIST", "NLST", "SITE",
    "SYST", "STAT", "HELP", "NOOP",
    /* From RFC 1123, we just translate it to standard command */
    "XCWD", "XCUP", "XRMD", "XMKD",
    "XPWD",
    0
};

enum
{
    /* Access Control Commands */
    cmd_USER, cmd_PASS, cmd_ACCT, cmd_CWD,
    cmd_CDUP, cmd_SMNT, cmd_REIN, cmd_QUIT,
    /* Transfer Parameter Commands */
    cmd_PORT, cmd_PASV, cmd_TYPE, cmd_STRU,
    cmd_MODE,
    /* FTP Service Commands */
    cmd_RETR, cmd_STOR, cmd_STOU, cmd_APPE,
    cmd_ALLO, cmd_REST, cmd_RNFR, cmd_RNTO,
    cmd_ABOR, cmd_DELE, cmd_RMD,  cmd_MKD,
    cmd_PWD,  cmd_LIST, cmd_NLST, cmd_SITE,
    cmd_SYST, cmd_STAT, cmd_HELP, cmd_NOOP,
    /* From RFC 1123 */
    cmd_XCWD, cmd_XCUP, cmd_XRMD, cmd_XMKD,
    cmd_XPWD,
    /* Not an FTP command */
    cmd_INVALID
};

int do_USER(ftp_session *s, char *param);
int do_CWD(ftp_session *s, char *param);
int do_CDUP(ftp_session *s, char *param);
int do_QUIT(ftp_session *s, char *param);
int do_PORT(ftp_session *s, char *param);
int do_PASV(ftp_session *s, char *param);
int do_TYPE(ftp_session *s, char *param);
int do_STRU(ftp_session *s, char *param);
int do_MODE(ftp_session *s, char *param);
int do_RETR(ftp_session *s, char *param);
int do_STOR(ftp_session *s, char *param);
int do_REST(ftp_session *s, char *param);
int do_RNFR(ftp_session *s, char *param);
int do_RNTO(ftp_session *s, char *param);
int do_DELE_RMD(ftp_session *s, char *param);
int do_MKD(ftp_session *s, char *param);
int do_PWD(ftp_session *s, char *param);
int do_LIST_NLST(ftp_session *s, char *param);
#ifdef USE_SITE
int do_SITE(ftp_session *s, char *param);
#endif
int do_SYST(ftp_session *s, char *param);
int do_HELP(ftp_session *s, char *param);
int do_NOOP(ftp_session *s, char *param);
int do_NIMP(ftp_session *s, char *param); /* NIMP = not implemented */

typedef int (*HANDLER)(ftp_session *s, char *param);

HANDLER ftp_handler[] =
{
    /* Access Control Commands */
    do_USER, /*do_PASS*/ do_NIMP, /*do_ACCT*/ do_NIMP, do_CWD,
    do_CDUP, /*do_SMNT*/ do_NIMP, /*do_REIN*/ do_NIMP, do_QUIT,
    /* Transfer Parameter Commands */
    do_PORT, do_PASV, do_TYPE, do_STRU,
    do_MODE,
    /* FTP Service Commands */
    do_RETR, do_STOR, /*do_STOU*/ do_NIMP, /*do_APPE*/ do_NIMP,
    /*do_ALLO*/ do_NIMP, do_REST, do_RNFR, do_RNTO,
    /*do_ABOR*/ do_NIMP, do_DELE_RMD, do_DELE_RMD, do_MKD,
    do_PWD,  do_LIST_NLST, do_LIST_NLST,
    #ifdef USE_SITE
        do_SITE,
    #else
        do_NIMP,
    #endif
    do_SYST, /*do_STAT*/ do_NIMP, do_HELP, do_NOOP,
    /* From RFC 1123 */
    do_CWD, do_CDUP, do_DELE_RMD, do_MKD,
    do_PWD
};

typedef struct
{
    int code;
    char *text;
} REPLY_CODE;

const REPLY_CODE reply_code[] =
{
    /* { 110, "MARK yyyy = mmmm" }, */
    /* { 120, "Service ready in %03d minutes." }, */
    /* { 125, "Data connection already open; transfer starting." }, */
    /* { 150, "About to open data connection." }, */
    { 200, "Command successfull." },
    { 202, "Command not implemented." },
    /* { 211, "System status, or system help reply." }, */
    /* { 212, "Directory status." }, */
    /* { 213, "File status." }, */
    /* { 214, "Help message." }, by function */
    { 215, "Windows" },
    /* { 220, "Service ready for new user." }, by function */
    { 221, "Closing control connection." },
    /* { 225, "Data connection open; no transfer in progress." }, */
    { 226, "Transfer complete." },
    /* { 227, "Entering Passive Mode (h1,h2,h3,h4,p1,p2)." }, */
    { 230, "User logged in, proceed." },
    { 250, "Command successfull." },
    /* { 257, "\"%s\"" }, by function */
    /* { 331, "User name okay, need password." }, */
    /* { 332, "Need account for login." }, */
    /* { 350, "Requested file action pending further information." }, by function */
    { 421, "Service not available, closing control connection." },
    { 425, "Can't open data connection." },
    { 426, "Connection closed; transfer aborted." },
    { 450, "File unavailable." },
    { 451, "Requested action aborted: local error in processing." },
    { 452, "Insufficient storage space." },
    { 500, "Syntax error, command unrecognized." },
    { 501, "Syntax error in parameters or arguments." },
    { 502, "Command not implemented." },
    { 503, "Bad sequence of commands." },
    { 504, "Command not implemented for that parameter." },
    /* { 530, "Not logged in." }, */
    /* { 532, "Need account for storing files." }, */
    { 550, "No such file or directory." },
    /* { 551, "Requested action aborted: page type unknown" }, */
    /* { 552, "Requested file action aborted." }, */
    { 553, "File name not allowed." },
    { 0,   "" }
};

const char *reply_150 = "150 Opening data connection.\r\n";

const char *month[] =
{
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

#ifdef USE_SPECIAL_FILE
const char *special_file[] =
{
    #ifdef USE_SCREEN_BMP
    "screen.bmp",
    #endif
    #ifdef USE_SCREEN_JPG
    "screen.jpg",
    #endif
    0
};

enum {
    #ifdef USE_SCREEN_BMP
    file_SCREEN_BMP,
    #endif
    #ifdef USE_SCREEN_JPG
    file_SCREEN_ZIP,
    #endif
    file_INVALID
};
#endif

#ifdef DEBUG
const char *log_file = "ftp.log";
int logging_enabled = TRUE;
#endif

char ftp_owner[9] = "owner";
char ftp_group[9] = "group";

#define FTP_CONTROL_PORT 21
#define FTP_DATA_PORT    20

short ftp_control_port = FTP_CONTROL_PORT;

int session_timeout = 600; /* in second */
int passive_timeout = 30;  /* in second */

int auto_start = FALSE;
int list_cdrom = FALSE;       /* show cdrom drive in root path */
int list_floppy = FALSE;      /* show floppy drive in root path */
int readonly = FALSE;         /* cannot store or delete anything */
int terminated = FALSE;       /* set to TRUE to terminate server */

ftp_session session[MAX_SESSION];

#define IS_CHAR(C)      (C && !(C & 0x80) && C != '\n' && C != '\r')
#define IS_PR_CHAR(C)   (C >= 33 && C <= 126)

#define IS_TYPE_CODE(C) (C == 'A' || C == 'E' || C == 'I' || C == 'L')
#define IS_MODE_CODE(C) (C == 'S' || C == 'B' || C == 'C')
#define IS_STRU_CODE(C) (C == 'F' || C == 'R' || C == 'P')
#define IS_FORM_CODE(C) (C == 'N' || C == 'T' || C == 'C')

#define MATCH(s,c)	{ if (*s++ != c) return 501; }
#define MATCH_CRLF(s)	{ MATCH(s, '\r'); MATCH(s, '\n'); }
#define MATCH_SP(s)	{ MATCH(s, ' '); while (*s == ' ') s++; }

int is_file_exists(const char *dir)
{
    int code = GetFileAttributes(dir);
    return (code != -1) && ((code & FILE_ATTRIBUTE_DIRECTORY) == 0);
}

int is_dir_exists(const char *dir)
{
    int code = GetFileAttributes(dir);
    return (code != -1) && ((code & FILE_ATTRIBUTE_DIRECTORY) != 0);
}

int is_ftp_dir_exists(const char *dir)
{
    char drive[MAX_PATH];
    int i, type;

    if (strcmp(dir, "/") == 0)
        return TRUE;

    if (dir[2] != 0 && dir[2] != '/')
        return FALSE;

    drive[0] = dir[1];
    strcpy(&drive[1], ":\\");
    type = GetDriveType(drive);
    if (type == DRIVE_UNKNOWN || type == DRIVE_NO_ROOT_DIR)
        return FALSE;

    strcpy(&drive[3], &dir[3]);
    for (i = 3; drive[i] != 0; i++)
        if (drive[i] == '/')
            drive[i] = '\\';

    return is_dir_exists(drive);
}

#ifdef USE_SPECIAL_FILE
int get_special_file(const char *file)
{
    int i;
    for (i = 0; special_file[i] != 0; i++)
        if (strcmp(&file[1], special_file[i]) == 0)
            break;
    return i;
}
#endif

#ifdef DEBUG
void log(const char *msg)
{
    FILE *fp;

    if (is_file_exists(log_file))
        fp = fopen(log_file, "ab");
    else
        fp = fopen(log_file, "wb");
    fwrite(msg, 1, strlen(msg), fp);
    fclose(fp);
}
#endif

/* ensure all data to be send */
int my_send(SOCKET s, const char *buf, int len, int flags)
{
    int  write, size;

    size = 0;
    while (len > 0)
    {
        write = send(s, &buf[size], len, flags);
        if (write == SOCKET_ERROR)
            return SOCKET_ERROR;
        size += write;
        len -= write;
    }
    return size;
}

int ftp_connect(ftp_session *s)
{
    SOCKET sockfd;
    SOCKADDR_IN your_addr;
    int sin_size;

    if (s->passive)
    {
        int ret, tick;

        tick = GetTickCount();
        do
        {
            fd_set readfds;
            TIMEVAL t;

            FD_ZERO(&readfds);
            FD_SET(s->data, &readfds);
            t.tv_sec = 1;
            t.tv_usec = 0;

            ret = select(0, &readfds, NULL, NULL, &t);

        } while (!s->closed && ret == 0 &&
                 GetTickCount() < tick+(passive_timeout*1000));

        if (ret == 0)
            return -1;

        sin_size = sizeof(your_addr);
        sockfd = accept(s->data, (SOCKADDR *) &your_addr, &sin_size);
    }
    else
    {
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd == -1)
            return -1;

        your_addr.sin_family = AF_INET;
        your_addr.sin_addr.s_addr = htonl(s->host);
        your_addr.sin_port = htons(s->port);

        if (connect(sockfd, (SOCKADDR *) &your_addr, sizeof(your_addr)) == -1)
        {
            closesocket(sockfd);
            return -1;
        }
    }

    return sockfd;
}

int ftp_printf(SOCKET s, const char *format, ...)
{
    char buf[1024];
    va_list args;

    va_start(args, format);
    vsprintf(buf, format, args);

    #ifdef DEBUG
    if (logging_enabled)
        log(buf);
    #endif

    return my_send(s, buf, strlen(buf), 0);
}

#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))

int get_string(const char *line, char *result, int size)
{
    int i, min;
    for (i = 0; IS_CHAR(line[i]); i++)
        /* loop */;
    if (size > 0)
    {
        min = MIN(i, size-1);
        strncpy(result, line, min);
        result[min] = 0;
    }
    return i;
}

int get_pr_string(const char *line, char *result, int size)
{
    int i, min;
    for (i = 0; IS_PR_CHAR(line[i]); i++)
        /* loop */;
    if (size > 0)
    {
        min = MIN(i, size-1);
        strncpy(result, line, min);
        result[min] = 0;
    }
    return i;
}

int get_number(const char *line, char *result, int size)
{
    int i, min;
    for (i = 0; isdigit(line[i]); i++)
        /* loop */;
    if (size > 0)
    {
        min = MIN(i, size-1);
        strncpy(result, line, min);
        result[min] = 0;
    }
    return i;
}

int get_command(char **command_list, char *command)
{
    char cmd[8];
    int i;

    for (i = 0; isalpha(command[i]) && i < sizeof(cmd)-1; i++)
        cmd[i] = toupper(command[i]);
    cmd[i] = 0;

    for (i = 0; command_list[i] != 0; i++)
        if (strcmp(cmd, command_list[i]) == 0)
            break;

    return i;
}

int parse_dir(const char *current, const char *change, char *result)
{
    int i, j;
    char *pos;

    if (change[0] == '/')
        strcpy(result, "/");
    else
        strcpy(result, current);

    pos = result + strlen(result);
    if (pos[-1] != '/')
        *pos++ = '/';

    for (i = 0; change[i] != 0; i = j)
    {
        while (change[i] == '/')
            i++;

        if (change[i] == 0)
            break;

        j = i+1;
        while (change[j] != 0 && change[j] != '/')
            j++;

        strncpy(pos, &change[i], j-i);
        pos[j-i] = 0;

        if (strcmp(pos, ".") == 0)
            *pos = 0;
        else if (strcmp(pos, "..") == 0)
        {
            if (pos-result == 1) /* already in root */
                return FALSE;
            else
            {
                pos -= 2;
                while (*pos != '/')
                    pos--;
                *(++pos) = 0;
            }
        }
        else
        {
            pos += j-i;
            *pos++ = '/';
            *pos   = 0;
        }
    }

    /* trim '/' on the right side, if needed */
    if (pos-result != 1)
        pos[-1] = 0;

    return TRUE;
}

int ftp_to_fs(const char *ftp, char *fs)
{
    int i;

    if (strcmp(ftp, "/") == 0)
        return FALSE;

    #ifdef USE_SPECIAL_FILE
    if (get_special_file(ftp) != file_INVALID)
        return FALSE;
    #endif

    if (ftp[2] != 0 && ftp[2] != '/')
        return FALSE;

    sprintf(fs, "%c:\\", ftp[1]);
    if (ftp[2] != 0)
    {
        strcpy(&fs[3], &ftp[3]);
        for (i = 3; fs[i] != 0; i++)
            if (fs[i] == '/')
                fs[i] = '\\';
    }

    return TRUE;
}

int ftp_to_fs_read(const char *ftp, char *fs)
{
    if (!ftp_to_fs(ftp, fs))
        return FALSE;

    return is_file_exists(fs);
}

int ftp_to_fs_write(const char *ftp, char *fs)
{
    if (readonly || !ftp_to_fs(ftp, fs))
        return FALSE;

    return TRUE;
}

int extract_file_name(const char *path, char* file_name)
{
    int i, len;
    
    len = strlen(path);
    for (i = len-1; i >= 0 && path[i] != '/' && path[i] != '\\'; i--)
        /* loop */;
    memmove(file_name, &path[i+1],  len-i);
    return len-i-1;
}

#include  "site.c"

int do_USER(ftp_session *s, char *param)
{
    int len;

    MATCH_SP(param);

    len = get_string(param, NULL, 0);
    if (len == 0)
        return 501;
    param += len;

    MATCH_CRLF(param);

    return 230;
}

int do_CWD(ftp_session *s, char *param)
{
    int len;
    char dir[MAX_FTP_PATH], new_dir[MAX_FTP_PATH];

    MATCH_SP(param);

    len = get_string(param, dir, sizeof(dir));
    if (len == 0)
        return 501;
    param += len;

    MATCH_CRLF(param);

    if (!parse_dir(s->dir, dir, new_dir))
        return 550;
    if (!is_ftp_dir_exists(new_dir))
        return 550;

    strcpy(s->dir, new_dir);
    return 250;
}

int do_CDUP(ftp_session *s, char *param)
{
    char new_dir[MAX_FTP_PATH];

    MATCH_CRLF(param);

    if (!parse_dir(s->dir, "..", new_dir))
        return 550;
    if (!is_ftp_dir_exists(new_dir))
        return 550;

    strcpy(s->dir, new_dir);
    return 200;
}

int do_QUIT(ftp_session *s, char *param)
{
    MATCH_CRLF(param);
    s->closed = TRUE;
    return 221;
}

int do_PORT(ftp_session *s, char *param)
{
    unsigned int h[6], i;

    for (i = 0; i < 6; i++)
    {
        char buf[MAX_FTP_COMMAND];
        int len;

        MATCH(param, (i == 0 ? ' ' : ','));
        len = get_number(param, buf, sizeof(buf));
        if (len == 0)
            return 501;
        h[i] = atoi(buf);
        if (h[i] > 255)
            return 501;
        param += len;
    }

    MATCH_CRLF(param);

    s->host = h[0] << 24 | h[1] << 16 | h[2] << 8 | h[3];
    s->port = h[4] << 8 | h[5];

    if (s->passive)
    {
        s->passive = FALSE;
        closesocket(s->data);
    }

    return 200;
}

int do_PASV(ftp_session *s, char *param)
{
    SOCKET sockfd;
    SOCKADDR_IN my_addr;
    int sin_size;
    long h;
    short p;

    MATCH_CRLF(param);

    if (!s->passive)
    {
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd == -1)
        {
            s->closed = TRUE;
            return 421;
        }

        s->passive = TRUE;
        s->data = sockfd;

        /* get server address */
        sin_size = sizeof(my_addr);
        getsockname(s->control, (SOCKADDR *) &my_addr, &sin_size);
        s->data_host = ntohl(my_addr.sin_addr.s_addr);

        my_addr.sin_family = AF_INET;
        my_addr.sin_port = 0;
        memset(my_addr.sin_zero, 0, sizeof(my_addr.sin_zero));

        if (bind(sockfd, (SOCKADDR *) &my_addr, sizeof(my_addr)) == -1 ||
                listen(sockfd, 0) == -1)
        {
            s->closed = TRUE;
            return 421;
        }

        /* get listening port */
        sin_size = sizeof(my_addr);
        getsockname(sockfd, (SOCKADDR *) &my_addr, &sin_size);
        s->data_port = ntohs(my_addr.sin_port);
    }

    h = s->data_host;
    p = s->data_port;

    ftp_printf(s->control, "227 Entering Passive Mode (%d,%d,%d,%d,%d,%d).\r\n",
            (h >> 24) &  255, (h >> 16) & 255, (h >> 8) & 255, h & 255,
            (p >> 8) & 255, p & 255);

    return 0;
}

int do_TYPE(ftp_session *s, char *param)
{
    int type;

    MATCH_SP(param);
    type = toupper(*param++);
    MATCH_CRLF(param);
    if (!IS_TYPE_CODE(type))
        return 501;
    if (type == 'E' || type == 'L' || (type == 'A' && *param == ' '))
        return 504;
    s->type = type;
    return 200;
}

int do_STRU(ftp_session *s, char *param)
{
    int stru;

    MATCH_SP(param);
    stru = toupper(*param++);
    MATCH_CRLF(param);
    if (!IS_STRU_CODE(stru))
        return 501;
    if (stru != 'F')
        return 504;
    s->stru = stru;
    return 200;
}

int do_MODE(ftp_session *s, char *param)
{
    int mode;

    MATCH_SP(param);
    mode = toupper(*param++);
    MATCH_CRLF(param);
    if (!IS_MODE_CODE(mode))
        return 501;
    if (mode == 'C')
        return 504;
    s->mode = mode;
    return 200;
}

#include "screen.c"
#include "jpeg.c"

int do_RETR(ftp_session *s, char *param)
{
    int len;
    char arg[MAX_FTP_PATH], ftp_dir[MAX_FTP_PATH], buf[MAX_BUFFER];
    FILE *fp;
    size_t read, write;
    SOCKET sockfd;
    #ifdef USE_SPECIAL_FILE
    int type, start, size;
    char *mem;
    #endif

    MATCH_SP(param);

    len = get_string(param, arg, sizeof(arg));
    if (len == 0)
        return 501;
    param += len;

    MATCH_CRLF(param);

    if (!parse_dir(s->dir, arg, ftp_dir))
        return 550;
    #ifndef USE_SPECIAL_FILE
    if (!ftp_to_fs_read(ftp_dir, arg))
        return 550;
    #else
    type = get_special_file(ftp_dir);
    if (type == file_INVALID && !ftp_to_fs_read(ftp_dir, arg))
        return 550;

    if (type != file_INVALID)
    {
        start = 0;
        switch (type)
        {
            #ifdef USE_SCREEN_BMP
            case file_SCREEN_BMP:
                mem = create_snapshot(&size);
                if (mem == NULL)
                    return 450;
                break;
            #endif
            #ifdef USE_SCREEN_JPG
            case file_SCREEN_ZIP:
                {
                    mem = create_jpeg(&size);
                    if (mem == NULL)
                        return 450;
                    break;
                }
            #endif
            default:
                return 450;
        }
    }
    else
    #endif
    {
        fp = fopen(arg, "rb");
        if (fp == NULL)
            return 450;
    }

    if (s->prev_command == cmd_REST)
    {
        #ifdef USE_SPECIAL_FILE
        if (type != file_INVALID)
            start = s->restart;
        else
        #endif
            fseek(fp, s->restart, SEEK_SET);
    }

    ftp_printf(s->control, reply_150);

    sockfd = ftp_connect(s);
    if (sockfd == -1)
    {
        #ifdef USE_SPECIAL_FILE
        if (type != file_INVALID)
            free(mem);
        else
        #endif
            fclose(fp);
        return 425;
    }

    #ifdef USE_SPECIAL_FILE
    if (type != file_INVALID)
    {
        write = my_send(sockfd, &mem[start], size-start, 0);
        free(mem);
        if (write != size-start)
        {
            closesocket(sockfd);
            return 426;
        }
    }
    else
    #endif
    {
        while (!feof(fp))
        {
            s->tick = GetTickCount();
            read = fread(buf, 1, sizeof(buf), fp);
            if (ferror(fp))
            {
                closesocket(sockfd);
                fclose(fp);
                return 451;
            }
            write = my_send(sockfd, buf, read, 0);
            if (read != write)
            {
                closesocket(sockfd);
                fclose(fp);
                return 426;
            }
        }
        fclose(fp);
    }

    closesocket(sockfd);

    return 226;
}

int do_STOR(ftp_session *s, char *param)
{
    int len;
    char arg[MAX_FTP_PATH], ftp_dir[MAX_FTP_PATH], buf[MAX_BUFFER];
    FILE *fp;
    SOCKET sockfd;

    MATCH_SP(param);

    len = get_string(param, arg, sizeof(arg));
    if (len == 0)
        return 501;
    param += len;

    MATCH_CRLF(param);

    if (!parse_dir(s->dir, arg, ftp_dir))
        return 553;
    if (!ftp_to_fs_write(ftp_dir, arg))
        return 553;

    if (s->prev_command == cmd_REST)
    {
        fp = fopen(arg, is_file_exists(arg) ? "r+b" : "wb");
        fseek(fp, s->restart, SEEK_SET);
    }
    else
        fp = fopen(arg, "wb");

    if (fp == NULL)
        return 450;

    ftp_printf(s->control, reply_150);

    sockfd = ftp_connect(s);
    if (sockfd == -1)
    {
        fclose(fp);
        return 425;
    }

    for (;;)
    {
        s->tick = GetTickCount();
        len = recv(sockfd, buf, sizeof(buf), 0);
        if (len == 0)
            break;
        if (len == SOCKET_ERROR)
        {
            closesocket(sockfd);
            fclose(fp);
            return 426;
        }
        if (len != fwrite(buf, 1, len, fp))
        {
            closesocket(sockfd);
            fclose(fp);
            return 452;
        }
    }

    closesocket(sockfd);
    fclose(fp);

    return 226;
}

int do_REST(ftp_session *s, char *param)
{
    int len;
    char arg[MAX_FTP_COMMAND];

    s->curr_command = cmd_NOOP;

    MATCH_SP(param);

    len = get_number(param, arg, sizeof(arg));
    if (len == 0)
        return 501;
    param += len;

    MATCH_CRLF(param);

    s->curr_command = cmd_REST;

    s->restart = atoi(arg);
    ftp_printf(s->control, "350 Restarting at %d.\r\n", s->restart);

    return 0;
}

int do_RNFR(ftp_session *s, char *param)
{
    int len;
    char arg[MAX_FTP_PATH], ftp_path[MAX_FTP_PATH];

    s->curr_command = cmd_NOOP;

    MATCH_SP(param);

    len = get_string(param, arg, sizeof(arg));
    if (len == 0)
        return 501;
    param += len;

    MATCH_CRLF(param);

    if (readonly)
        return 550;
    if (!parse_dir(s->dir, arg, ftp_path))
        return 550;
    if (!ftp_to_fs(ftp_path, arg))
        return 550;
    if (!(is_file_exists(arg) || is_dir_exists(arg)))
        return 550;

    s->curr_command = cmd_RNFR;

    strcpy(s->rename, arg);
    ftp_printf(s->control, "350 File exists, ready for destination name.\r\n");

    return 0;
}

int do_RNTO(ftp_session *s, char *param)
{
    int len;
    char arg[MAX_FTP_PATH], ftp_path[MAX_FTP_PATH];

    MATCH_SP(param);

    len = get_string(param, arg, sizeof(arg));
    if (len == 0)
        return 501;
    param += len;

    MATCH_CRLF(param);

    if (s->prev_command != cmd_RNFR)
        return 503;
    if (!parse_dir(s->dir, arg, ftp_path))
        return 550;
    if (!ftp_to_fs(ftp_path, arg))
        return 550;
    if (!(is_file_exists(s->rename) || is_dir_exists(s->rename)))
        return 550;
    if (!MoveFile(s->rename, arg))
        return 450;

    return 250;
}

int do_DELE_RMD(ftp_session *s, char *param)
{
    int len, attr, del_mode;
    char arg[MAX_FTP_PATH], ftp_path[MAX_FTP_PATH];

    MATCH_SP(param);

    len = get_string(param, arg, sizeof(arg));
    if (len == 0)
        return 501;
    param += len;

    MATCH_CRLF(param);

    if (readonly)
        return 550;
    if (!parse_dir(s->dir, arg, ftp_path))
        return 550;
    if (!ftp_to_fs(ftp_path, arg))
        return 550;

    del_mode = s->curr_command == cmd_DELE;
    if (del_mode && !is_file_exists(arg))
        return 550;
    if (!del_mode && !is_dir_exists(arg))
        return 550;

    attr = GetFileAttributes(arg);
    SetFileAttributes(arg, 0);
    if (del_mode ? !DeleteFile(arg) : !RemoveDirectory(arg))
    {
        SetFileAttributes(arg, attr);
        return 450;
    }

    return 250;
}

int do_MKD(ftp_session *s, char *param)
{
    int len;
    char arg[MAX_FTP_PATH], ftp_dir[MAX_FTP_PATH], fs_dir[MAX_FTP_PATH];

    MATCH_SP(param);

    len = get_string(param, arg, sizeof(arg));
    if (len == 0)
        return 501;
    param += len;

    MATCH_CRLF(param);

    if (readonly)
        return 550;
    if (!parse_dir(s->dir, arg, ftp_dir))
        return 550;
    if (!ftp_to_fs(ftp_dir, fs_dir))
        return 550;
    if (!CreateDirectory(fs_dir, NULL))
        return 550;

    ftp_printf(s->control, "257 \"%s\" created.\r\n", arg);

    return 0;
}

int do_PWD(ftp_session *s, char *param)
{
    MATCH_CRLF(param);
    ftp_printf(s->control, "257 \"%s\" is current directory.\r\n", s->dir);
    return 0;
}

int do_LIST_NLST(ftp_session *s, char *param)
{
    char *p, *dir, buf[MAX_BUFFER], ftp_dir[MAX_FTP_PATH];
    int len, size;
    SOCKET sockfd;
    SYSTEMTIME stime;

    dir = s->dir;
    if (*param == ' ')
    {
        param++;
        /* Fix: for something like LIST -la /etc (e.g: on Midnight Commander)
         *      Not compatible with standard
         */
        if (*param == '-')
        {
            param++;
            while (*param != ' ' && *param != 0)
                param++;
            if (*param == ' ')
                param++;
        }
        len = get_string(param, buf, sizeof(buf));
        if (param == 0)
            return 501;

        if (!parse_dir(dir, buf, ftp_dir))
            return 450;

        param += len;
        dir = ftp_dir;
    }

    MATCH_CRLF(param);

    ftp_printf(s->control, reply_150);

    sockfd = ftp_connect(s);
    if (sockfd == -1)
        return 425;

    size = 0;
    GetLocalTime(&stime);
    if (strcmp(dir, "/") == 0)
    {
        char drive[4];
        char list_buf[128];
        char *list_item[32];
        int list_dir[32];
        int list_count, i;

        list_count = 0;
        p = list_buf;

        for (strcpy(drive, "a:\\"); drive[0] <= 'z'; drive[0]++)
        {
            int type = GetDriveType(drive);
            if (type == DRIVE_FIXED ||
                    (list_cdrom && type == DRIVE_CDROM) ||
                    (list_floppy && type == DRIVE_REMOVABLE))
            {
                list_item[list_count] = p;
                list_dir[list_count++] = TRUE;
                *p++ = drive[0];
                *p++ = 0;
            }
        }

        #ifdef USE_SPECIAL_FILE
        for (i = 0; special_file[i] != 0; i++)
        {
            list_item[list_count] = p;
            list_dir[list_count++] = FALSE;
            strcpy(p, special_file[i]);
            p += strlen(p)+1;
        }
        #endif

        for (i = 0; i < list_count; i++)
        {
            if (s->curr_command == cmd_LIST)
            {
                char ro = (readonly || !list_dir[i]) ? '-' : 'w';
                char dir = list_dir[i] ? 'd' : '-';
                char exec = list_dir[i] ? 'x' : '-';
                size += sprintf(&buf[size],
                        "%cr%c%cr%c%cr%c%c    1 %-8s %-8s %10d %s %2d %02d:%02d %s\r\n",
                        dir, ro, exec, ro, exec, ro, exec, ftp_owner, ftp_group, 0,
                        month[stime.wMonth-1], stime.wDay, stime.wHour, stime.wMinute,
                        list_item[i]);
            }
            else
                size += sprintf(&buf[size], "%s\r\n", list_item[i]);

            if (size >= MAX_BUFFER_FILLED)
            {
                my_send(sockfd, buf, MAX_BUFFER_FILLED, 0);
                size -= MAX_BUFFER_FILLED;
                memmove(buf, &buf[MAX_BUFFER_FILLED], size);
            }
        }
    }
    else
    {
        WIN32_FIND_DATA find_data;
        HANDLE handle;

        ftp_to_fs(dir, buf);
        strcpy(ftp_dir, buf);

        if (is_dir_exists(ftp_dir))
        {
            len = strlen(ftp_dir);
            if (ftp_dir[len-1] != '\\')
                ftp_dir[len++] = '\\';
            strcpy(&ftp_dir[len], "*.*");
        }

        handle = FindFirstFile(ftp_dir, &find_data);
        if (handle != INVALID_HANDLE_VALUE)
        {
            do
            {
                char year[6], dir, ro, exec;
                SYSTEMTIME time;

                if (strcmp(find_data.cFileName, ".") == 0 ||
                        strcmp(find_data.cFileName, "..") == 0)
                    continue;

                dir = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    ? 'd' : '-';
                ro = (find_data.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
                    || readonly ? '-' : 'w';
                exec = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    ? 'x' : '-';

                FileTimeToSystemTime(&find_data.ftLastWriteTime, &time);
                if (time.wYear == stime.wYear)
                    sprintf(year, "%02d:%02d", time.wHour, time.wMinute);
                else
                    sprintf(year, "%d", time.wYear);

                if (s->curr_command == cmd_LIST)
                {
                    size += sprintf(&buf[size],
                            "%cr%c%cr%c%cr%c%c    1 %-8s %-8s %10lu %s %2d %5s %s\r\n",
                            dir, ro, exec, ro, exec, ro, exec, ftp_owner, ftp_group,
                            find_data.nFileSizeLow, month[time.wMonth-1],
                            time.wDay, year, find_data.cFileName);
                }
                else
                    size += sprintf(&buf[size], "%s\r\n", find_data.cFileName);

                if (size >= MAX_BUFFER_FILLED)
                {
                    my_send(sockfd, buf, MAX_BUFFER_FILLED, 0);
                    size -= MAX_BUFFER_FILLED;
                    memmove(buf, &buf[MAX_BUFFER_FILLED], size);
                }
            } while (FindNextFile(handle, &find_data));
            FindClose(handle);
        }
    }

    if (size >= 0)
        my_send(sockfd, buf, size, 0);

    closesocket(sockfd);

    return 226;
}

int do_SYST(ftp_session *s, char *param)
{
    MATCH_CRLF(param);
    return 215;
}

int do_HELP(ftp_session *s, char *param)
{
    char buf[512], *p;
    int i;

    MATCH_CRLF(param);

    p = buf;
    strcpy(p, "214-Recognized commands. ( * = not implemented )\r\n");
    p += strlen(p);

    for (i = 0; ftp_command[i] != 0; i++)
        p += sprintf(p, (i+1)%6 == 0 ? "  %c%s\r\n" : "  %c%-5s",
                (ftp_handler[i] == do_NIMP) ? '*' : ' ', ftp_command[i]);

    if (i%6 != 0)
        p += sprintf(p, "\r\n");

    strcpy(p, "214 Command okay.\r\n");
    ftp_printf(s->control, buf);
    return 0;
}

int do_NOOP(ftp_session *s, char *param)
{
    MATCH_CRLF(param);
    return 200;
}

int do_NIMP(ftp_session *s, char *param)
{
    return 502;
}

int parse_ftp_command(ftp_session *s, char *command)
{
    int i, cmd, ret;

    #ifdef DEBUG
    if (logging_enabled)
    {
        char buf[MAX_FTP_COMMAND];
        sprintf(buf, "<--- %s", command);
        log(buf);
    }
    #endif

    s->prev_command = s->curr_command;
    s->curr_command = cmd = get_command(ftp_command, command);

    if (cmd == cmd_INVALID)
        ret = 500;
    else
        ret = ftp_handler[cmd](s, command + strlen(ftp_command[cmd]));

    if (ret != 0)
    {
        for (i = 0; reply_code[i].code != 0; i++)
            if (reply_code[i].code == ret)
                break;
        ftp_printf(s->control, "%03d %s\r\n", ret, reply_code[i].text);
    }

    return 0;
}

void init_ftp_session(ftp_session *s)
{
    s->running = TRUE;
    s->type = 'A';
    s->mode = 'S';
    s->stru = 'F';
    s->port = FTP_DATA_PORT;
    s->passive = FALSE;
    s->closed = FALSE;
    s->tick = GetTickCount();
    s->curr_command = cmd_NOOP;
    strcpy(s->dir, "/");
}

void WINAPI manage_ftp_session(ftp_session *s)
{
    char buf[MAX_FTP_COMMAND];
    int read, size, i, can_execute;

    init_ftp_session(s);
    ftp_printf(s->control, "220 LiteFTP Server ready.\r\n");
    size = 0;
    do
    {        
        do
        {
            fd_set readfds;
            TIMEVAL t;

            FD_ZERO(&readfds);
            FD_SET(s->control, &readfds);
            t.tv_sec = 1;
            t.tv_usec = 0;

            read = select(0, &readfds, NULL, NULL, &t);

        } while (!s->closed && read == 0);

        if (read == 0)
            break;

        s->tick = GetTickCount();
        read = recv(s->control, &buf[size], sizeof(buf)-size-1, 0);
        if (read == 0 || read == SOCKET_ERROR)
            break;

        do
        {
            size += read;
            can_execute = (size >= sizeof(buf)-1);
            for (i = 0; i < size-1; i++)
                if (buf[i] == '\r' && buf[i+1] == '\n')
                {
                    can_execute = TRUE;
                    break;
                }

            if (can_execute)
            {
                buf[size] = 0;
                parse_ftp_command(s, buf);
                i += 2;
                size -= i;
                read = 0;
                memmove(buf, &buf[i], size);
            }
        } while (can_execute);

    } while (!s->closed);

    closesocket(s->control);
    if (s->passive)
        closesocket(s->data);

    #ifdef DEBUG
    if (logging_enabled)
        log("Control connection closed.\r\n");
    #endif

    session[s->sid].running = FALSE;
}

int get_session_slot()
{
    int i;

    for (;;)
    {
        for (i = 0; i < MAX_SESSION; i++)
            if (session[i].running  && GetTickCount() > session[i].tick+(session_timeout*1000))
                session[i].closed = TRUE;

        for (i = 0; i < MAX_SESSION; i++)
            if (!session[i].running)
                return i;

        Sleep(1000);
    }
}

/*==============================================================
 * Parameters:
 *   -p nnn   listen ftp connection from port nnn (default 21)
 *   -a       auto start program on Windows startup
 *   -h       hide process from End Program window (Win 9x)
 *   -c       show cdrom drive on root dir
 *   -f       show floppy drive on root dir
 *   -r       readonly mode (no write or delete)
 */

int parse_command_line(char *cmd)
{
    const char *run_key = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const char *kernel = "kernel32.dll";
    const char *reg_service = "RegisterServiceProcess";

    char *my_cmd = cmd;
    char *exe;
    char src[MAX_PATH], dst[MAX_PATH];
    int len, hide;
    HKEY hkey;

    hide = FALSE;

    while (*cmd != 0)
    {
        while (isspace(*cmd))
            cmd++;

        if (*cmd == 0)
            break;

        if (*cmd++ != '-')
            return 1;

        switch (*cmd++)
        {
            case 'p': /* ftp port */
                while (isspace(*cmd))
                    cmd++;

                len = get_number(cmd, src, sizeof(src));
                if (len == 0)
                    return 1;
                cmd += len;

                ftp_control_port = atoi(src);
                break;
            case 'a': auto_start = TRUE; break;
            case 'h': hide = TRUE; break;
            case 'c': list_cdrom = TRUE; break;
            case 'f': list_floppy = TRUE; break;
            case 'r': readonly = TRUE; break;
            default: return 1;
        }
    }

    if (auto_start)
    {
        int copied;

        GetModuleFileName(NULL, src, sizeof(src));
        GetTempPath(sizeof(dst), dst);

        exe = strrchr(src, '\\')+1;
        strcat(dst, exe);

        copied = FALSE;
        if (stricmp(src, dst) != 0)
        {
            CopyFile(src, dst, FALSE);
            copied = TRUE;
        }

        if (strlen(my_cmd) > 0)
        {
            strcat(dst, " ");
            strcat(dst, my_cmd);
        }

        *strrchr(exe, '.') = 0;
        RegCreateKey(HKEY_CURRENT_USER, run_key, &hkey);
        RegSetValueEx(hkey, exe, 0, REG_SZ, dst, strlen(dst));
        RegCloseKey(hkey);

        if (copied)
        {
            STARTUPINFO si;
            PROCESS_INFORMATION pi;

            memset(&si, 0, sizeof(si));
            si.cb = sizeof(si);
            si.dwFlags = STARTF_FORCEOFFFEEDBACK;
            CreateProcess(NULL, dst, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
            exit(0);
        }
    }

    if (hide)
    {
        DWORD (WINAPI *RegisterServiceProcess)(DWORD, DWORD);

        RegisterServiceProcess =
           (void *) GetProcAddress(GetModuleHandle(kernel), reg_service);
        if (RegisterServiceProcess)
            RegisterServiceProcess(GetCurrentProcessId(), 1);
    }

    return 0;
}

int init_server()
{
    WSADATA wsa_data;

    #ifdef DEBUG
    logging_enabled = 1;

    if (logging_enabled && is_file_exists(log_file))
        DeleteFile(log_file);
    #endif

    if (WSAStartup(MAKEWORD(1, 1), &wsa_data) != 0)
        return 1;

    return 0;
}

int WINAPI
WinMain (HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    SOCKET sockfd;
    SOCKADDR_IN my_addr;
    SOCKADDR_IN your_addr;
    HANDLE thread;
    int i, sin_size, ret;

    parse_command_line(lpCmdLine);

    if (init_server() != 0)
        return 1;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
    {
        WSACleanup();
        return 1;
    }

    my_addr.sin_family = AF_INET;
    my_addr.sin_addr.s_addr = INADDR_ANY;
    my_addr.sin_port = htons(ftp_control_port);
    memset(my_addr.sin_zero, 0, sizeof(my_addr.sin_zero));

    if (bind(sockfd, (SOCKADDR *) &my_addr, sizeof(my_addr)) == -1 ||
        listen(sockfd, MAX_SESSION) == -1)
    {
        closesocket(sockfd);
        WSACleanup();
        return 1;
    }

    for (;;)
    {
        do
        {
            fd_set readfds;
            TIMEVAL t;

            i = get_session_slot();

            FD_ZERO(&readfds);
            FD_SET(sockfd, &readfds);
            t.tv_sec = 1;
            t.tv_usec = 0;

            ret = select(0, &readfds, NULL, NULL, &t);

        } while (!terminated && ret == 0);

        if (terminated)
            break;

        sin_size = sizeof(your_addr);
        session[i].control = accept(sockfd, (SOCKADDR *) &your_addr, &sin_size);
        session[i].host = ntohl(your_addr.sin_addr.s_addr);
        session[i].sid = i;

        thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) manage_ftp_session,
                (void *) &session[i], 0, (DWORD *) &session[i].running);
        SetThreadPriority(thread, THREAD_PRIORITY_LOWEST);
    }

    closesocket(sockfd);
    WSACleanup();
    return 0;
}
