#ifdef __APPLE__
#define _XOPEN_SOURCE_EXTENDED 1
#endif
#include <curses.h>

// ── NCurses wrapper functions for Swift ────────────────────────────────

void nc_init(void);
void nc_end(void);
int nc_rows(void);
int nc_cols(void);
void nc_refresh(void);
void nc_erase(void);
int nc_getch(void);
void nc_move(int y, int x);
void nc_addstr(int y, int x, const char *str);
void nc_addch(chtype ch);
void nc_attron(int attrs);
void nc_attroff(int attrs);
void nc_init_pair(short pair, short fg, short bg);
void nc_curs_set(int vis);

// Attribute constants
int nc_A_BOLD(void);
int nc_A_REVERSE(void);
int nc_A_UNDERLINE(void);
int nc_A_DIM(void);
int nc_A_STANDOUT(void);
int nc_A_BLINK(void);
int nc_COLOR_PAIR(int n);

// Color constants
int nc_COLOR_BLACK(void);
int nc_COLOR_RED(void);
int nc_COLOR_GREEN(void);
int nc_COLOR_YELLOW(void);
int nc_COLOR_BLUE(void);
int nc_COLOR_MAGENTA(void);
int nc_COLOR_CYAN(void);
int nc_COLOR_WHITE(void);

// Key constants
int nc_KEY_DOWN(void);
int nc_KEY_UP(void);
int nc_KEY_LEFT(void);
int nc_KEY_RIGHT(void);
int nc_KEY_ENTER(void);
int nc_KEY_BACKSPACE(void);
int nc_KEY_HOME(void);
int nc_KEY_END(void);
int nc_KEY_PPAGE(void);
int nc_KEY_NPAGE(void);
