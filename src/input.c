#include "ashe_string.h"
#include "ashe_utils.h"
#include "async.h"
#include "errors.h"
#include "input.h"
#include "jobctl.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>

/// State Bits
#define S_CLR 0x1 // all bits clear
#define S_ESC 0x2 // '\'
#define S_DQ  0x4 // '"'
#define S_END 0x8 // found newline and S_ESC & S_DQ bits are not set
/// Bit manipulation
#define TESTBIT(state, bit) ((state) & (bit))
#define SETBIT(state, bit)  (state) |= (bit);
#define CLRBIT(state, bit)  (state) &= ~((bit));
#define XORBIT(state, bit)  (state) ^= (bit);

#define CTRL_KEY(k) ((k) &0x1f)
#define ESCAPE      0x1B
#define CR          0x0D

typedef enum {
    BACKSPACE = 127,
    L_ARW = 0x101,
    U_ARW,
    D_ARW,
    R_ARW,
    HOME_KEY,
    END_KEY,
    DEL_KEY
} terminal_key;

#define IMPLEMENTED(c) (c != U_ARW && c != D_ARW && c != ESCAPE)

/// Flag that gets set if the rcmdline routine
/// gets interrupted while it is still reading
volatile atomic_bool reading = false;
/// Shell terminal modes
struct termios dflterm, rawterm;

typedef uint16_t termkey_t;

static void inbuff_cursor_right(inbuff_t *buffer);
static void inbuff_cursor_left(inbuff_t *buffer);
static void inbuff_remove(inbuff_t *inbuff);
static void inbuff_insert(inbuff_t *inbuff, char c);
static int get_window_size(uint16_t *width, uint16_t *height);
static int get_cursor_pos(uint16_t *row, uint16_t *col);
static int get_window_size_fallback(uint16_t *height, uint16_t *width);
static termkey_t read_key();

void pprompt(void)
{
    byte username[MAXNAME];
    struct utsname system;

    if(__glibc_unlikely(uname(&system) < 0)) {
        PW_USERNAME;
        die();
    }

    if(__glibc_unlikely(getlogin_r(username, MAXNAME) < 0)) {
        PW_SYSNAME;
        die();
    }

    byte jobs[100];
    size_t jobn = joblist_len();

    if(jobn > 0)
        sprintf(jobs, obrack bold(byellow("%ld")) cbrack, jobn);
    else
        jobs[0] = NULL_TERM;

    fprintf(
        stderr,
        "\r\n"
        "%s" obrack bold(bred("ashe")) cbrack obrack bold(magenta("%s")) bold(cyan("@"))
            bold(bwhite("%s")) cbrack bwhite(": "),
        jobs,
        username,
        system.sysname);
    fflush(stderr);
}

void init_rawterm(void)
{
    tcgetattr(TERMINAL_FD, &rawterm);
    rawterm = dflterm;
    rawterm.c_iflag &= ~(BRKINT | INPCK | ISTRIP | IXON | ICRNL);
    rawterm.c_oflag &= ~(OPOST);
    rawterm.c_lflag &= ~(ECHO | ICANON | IEXTEN);
    rawterm.c_cflag |= (CS8);
    rawterm.c_cc[VMIN] = 1;
    rawterm.c_cc[VTIME] = 0;
}

void init_dflterm(void)
{
    tcgetattr(TERMINAL_FD, &dflterm);
}

void set_rawtmode(void)
{
    tcsetattr(TERMINAL_FD, TCSAFLUSH, &rawterm);
}

void set_dflmode(void)
{
    tcsetattr(TERMINAL_FD, TCSAFLUSH, &dflterm);
}

static int get_window_size(uint16_t *height, uint16_t *width)
{
    struct winsize ws;
    if(ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) < 0 || ws.ws_col == 0) {
        return get_window_size_fallback(height, width);
    } else {
        if(is_some(height))
            *height = ws.ws_row;
        if(is_some(width))
            *width = ws.ws_col;

        return SUCCESS;
    }
}

static int get_window_size_fallback(uint16_t *height, uint16_t *width)
{
    size_t n = sizeof(sv_cur_pos mv_cur_right(999) mv_cur_down(999) req_cur_pos ld_cur_pos);
    write_or_die(sv_cur_pos mv_cur_right(999) mv_cur_down(999) req_cur_pos ld_cur_pos, n);
    return get_cursor_pos(height, width);
}

static int get_cursor_pos(uint16_t *row, uint16_t *col)
{
    byte c;
    size_t i = 0;
    int nread;
    uint16_t srow, scol;
    byte buf[32];

    write_or_die(req_cur_pos, sizeof(req_cur_pos));

    while(i < sizeof(buf) && (nread = read(STDIN_FILENO, &c, 1)) == 1) {
        if(c == 'R')
            break;
        buf[i++] = c;
    }

    if(nread == -1) {
        perr();
        return FAILURE;
    }

    sscanf(buf, "\033[%hu;%hu", &srow, &scol);

    if(is_some(row))
        *row = srow;
    if(is_some(col))
        *col = scol;

    return SUCCESS;
}

static void inbuff_insert(inbuff_t *inbuff, char c)
{
    if(inbuff->len < MAXLINE - 1) {
        uint16_t maxcol;
        get_window_size(NULL, &maxcol);
        byte *src = inbuff->buffer + inbuff->curp;
        size_t n = inbuff->len - inbuff->curp;
        size_t cap = sizeof(byte) + sizeof(sv_cur_pos ld_cur_pos) + sizeof("\r\n") + n;
        byte buff[cap + 10]; /// Add extra I am bad at math

        buff[0] = '\0';

        memmove(src + 1, src, n);
        memcpy(src, &c, sizeof(byte));

        inbuff->len++;
        inbuff->curp++;

        strncat(buff, &c, sizeof(byte));

        if(inbuff->cur_col >= maxcol)
            strcat(buff, "\r\n");

        strcat(buff, sv_cur_pos);
        strncat(buff, inbuff->buffer + inbuff->curp, n);
        strcat(buff, ld_cur_pos);

        write_or_die(buff, strlen(buff));
    }
}

static void inbuff_remove(inbuff_t *inbuff)
{
    if(inbuff->curp > 0) {

        byte *src = inbuff->buffer + inbuff->curp;
        size_t n = inbuff->len - inbuff->curp;
        size_t cap
            = sizeof(mv_cur_up(1) mv_cur_right(999) del_char sv_cur_pos clrscr_down ld_cur_pos) + n;
        byte buff[cap + 10];

        buff[0] = '\0';

        memmove(src - 1, src, n);

        inbuff->len--;
        inbuff->curp--;

        if(inbuff->cur_col <= 1)
            strcat(buff, mv_cur_up(1) mv_cur_col(999));
        else
            strcat(buff, del_char);

        strcat(buff, sv_cur_pos clrscr_down);
        strncat(buff, inbuff->buffer + inbuff->curp, n);
        strcat(buff, ld_cur_pos);

        write_or_die(buff, strlen(buff));
    }
}

void inbuff_print(inbuff_t *buffer, bool interrupted)
{
    size_t len = buffer->len;
    size_t n = buffer->len - buffer->curp;
    size_t cap = (sizeof(mv_cur_left(1)) * n) + len;
    byte buff[cap + 10];

    buff[0] = '\0';

    strncat(buff, buffer->buffer, len);
    if(interrupted)
        while(n--)
            strcat(buff, mv_cur_left(1));

    write_or_die(buff, strlen(buff));
}

static void inbuff_cursor_left(inbuff_t *buffer)
{
    if(buffer->curp > 0) {
        buffer->curp--;
        if(buffer->cur_col > 1)
            write_or_die(mv_cur_left(1), sizeof(mv_cur_left(1)));
        else
            write_or_die(mv_cur_up(1) mv_cur_col(999), sizeof(mv_cur_up(1) mv_cur_col(999)));
    }
}

void goto_eol(inbuff_t *buffer)
{
    uint16_t maxcol;
    get_window_size(NULL, &maxcol);

    byte buff[maxcol * sizeof(mv_cur_right(1)) + sizeof(byte)];
    buff[0] = '\0';

    while(buffer->curp < buffer->len && buffer->cur_col < maxcol) {
        buffer->curp++;
        buffer->cur_col++;
        strcat(buff, mv_cur_right(1));
    }

    write_or_die(buff, strlen(buff));
}

void goto_home(inbuff_t *buffer)
{
    uint16_t maxcol;
    if(get_window_size(NULL, &maxcol) == FAILURE)
        die();

    byte buff[maxcol * sizeof(mv_cur_left(1)) + sizeof(byte)];
    buff[0] = '\0';

    while(buffer->curp > 0 && buffer->cur_col > 1) {
        buffer->curp--;
        buffer->cur_col--;
        strcat(buff, mv_cur_left(1));
    }

    write_or_die(buff, strlen(buff));
}

static void inbuff_cursor_right(inbuff_t *buffer)
{
    uint16_t maxcol;
    if(get_window_size(NULL, &maxcol) == FAILURE)
        die();

    if(buffer->curp < buffer->len) {
        buffer->curp++;
        if(buffer->cur_col < maxcol)
            write_or_die(mv_cur_right(1), sizeof(mv_cur_right(1)));
        else
            write_or_die(mv_cur_down(1) mv_cur_col(1), sizeof(mv_cur_down(1) mv_cur_col(1)));
    }
}

static void clear_screen(inbuff_t *buffer)
{
    write_or_die(clrscr mv_cur_home, sizeof(clrscr mv_cur_home));
    pprompt();
    inbuff_print(buffer, true);
}

static termkey_t read_key(void)
{
    int nread;
    byte c;

    while((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if(nread == -1 && (errno != EINTR || (!sigchld_recv && !sigint_recv))) {
            ATOMIC_PRINT({
                PW_TERMINR;
                die();
            });
        } else if(sigchld_recv || sigint_recv) {
            sigchld_recv = sigint_recv = false;
        }
    }

    block_sigchld();
    block_sigint();

    if(c == ESCAPE) {
        byte seq[3];

        if(read(STDIN_FILENO, &seq[0], 1) != 1)
            return ESCAPE;
        if(read(STDIN_FILENO, &seq[1], 1) != 1)
            return ESCAPE;

        if(seq[0] == '[') {
            if(isdigit(seq[1])) {
                if(read(STDIN_FILENO, &seq[2], 1) != 1)
                    return ESCAPE;
                if(seq[2] == '~') {
                    switch(seq[1]) {
                        case '3': return DEL_KEY;
                        case '1':
                        case '7': return HOME_KEY;
                        case '4':
                        case '8': return END_KEY;
                        default: break;
                    }
                }
            } else {
                switch(seq[1]) {
                    case 'C': return R_ARW;
                    case 'D': return L_ARW;
                    case 'B': return D_ARW;
                    case 'A': return U_ARW;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                    default: break;
                }
            }
        } else if(seq[0] == 'O') {
            switch(seq[1]) {
                case 'H': return HOME_KEY;
                case 'F': return HOME_KEY;
                default: break;
            }
        }

        return ESCAPE;
    } else {
        return c;
    }
}

void inbuff_process_key(inbuff_t *buffer, byte *state)
{
    termkey_t c;
    uint16_t max_col;

    c = read_key();

    get_cursor_pos(NULL, &buffer->cur_col);
    get_window_size(NULL, &max_col);

    if((IMPLEMENTED(c))) {
        switch(c) {
            case '"':
                if(!TESTBIT(*state, S_ESC))
                    XORBIT(*state, S_DQ);
                goto insert;
            case '\\': XORBIT(*state, S_ESC); goto insert;
            case CR:
                if(TESTBIT(*state, S_DQ | S_ESC))
                    write_or_die("\r\n", sizeof("\r\n"));
                else
                    SETBIT(*state, S_END);
                break;
            case CTRL_KEY('h'):
            case DEL_KEY:
            case BACKSPACE: inbuff_remove(buffer); break;
            case END_KEY: goto_eol(buffer); break;
            case HOME_KEY: goto_home(buffer); break;
            case CTRL_KEY('l'): clear_screen(buffer); break;
            case L_ARW: inbuff_cursor_left(buffer); break;
            case R_ARW: inbuff_cursor_right(buffer); break;
            default:
            insert:
                inbuff_insert(buffer, c);
                break;
        }
    }

    if(c != '\\')
        CLRBIT(*state, S_ESC);
}

void read_input(inbuff_t *buffer)
{
    reading = true;
    byte state = S_CLR;

    fflush(stderr);
    set_rawtmode();

    while(!TESTBIT(state, S_END)) {
        inbuff_process_key(buffer, &state);
        unblock_sigint();
        unblock_sigchld();
    }

    buffer->buffer[buffer->len++] = '\0';
    reading = false;
    fflush(stderr);
    set_dflmode();
}
