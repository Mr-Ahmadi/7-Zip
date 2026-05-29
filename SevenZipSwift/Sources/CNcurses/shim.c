#include "include/shim.h"
#include <locale.h>

// ── NCurses wrapper functions for Swift ────────────────────────────────

void nc_init(void) {
    setlocale(LC_ALL, "");
    initscr();
    raw();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    start_color();
    use_default_colors();
}

void nc_end(void) {
    curs_set(1);
    endwin();
}

int nc_rows(void) {
    return LINES;
}

int nc_cols(void) {
    return COLS;
}

void nc_refresh(void) {
    refresh();
}

void nc_erase(void) {
    erase();
}

int nc_getch(void) {
    return getch();
}

void nc_move(int y, int x) {
    move(y, x);
}

void nc_addstr(int y, int x, const char *str) {
    if (y >= 0) move(y, x);
    addstr(str);
}

void nc_addch(chtype ch) {
    addch(ch);
}

void nc_attron(int attrs) {
    attron(attrs);
}

void nc_attroff(int attrs) {
    attroff(attrs);
}

void nc_init_pair(short pair, short fg, short bg) {
    init_pair(pair, fg, bg);
}

void nc_curs_set(int vis) {
    curs_set(vis);
}

// ── Chtype (attribute) constants ───────────────────────────────────────

int nc_A_BOLD(void) { return A_BOLD; }
int nc_A_REVERSE(void) { return A_REVERSE; }
int nc_A_UNDERLINE(void) { return A_UNDERLINE; }
int nc_A_DIM(void) { return A_DIM; }
int nc_A_STANDOUT(void) { return A_STANDOUT; }
int nc_A_BLINK(void) { return A_BLINK; }

int nc_COLOR_PAIR(int n) { return COLOR_PAIR(n); }

// ── Color constants ────────────────────────────────────────────────────

int nc_COLOR_BLACK(void) { return COLOR_BLACK; }
int nc_COLOR_RED(void) { return COLOR_RED; }
int nc_COLOR_GREEN(void) { return COLOR_GREEN; }
int nc_COLOR_YELLOW(void) { return COLOR_YELLOW; }
int nc_COLOR_BLUE(void) { return COLOR_BLUE; }
int nc_COLOR_MAGENTA(void) { return COLOR_MAGENTA; }
int nc_COLOR_CYAN(void) { return COLOR_CYAN; }
int nc_COLOR_WHITE(void) { return COLOR_WHITE; }

// ── Key constants ──────────────────────────────────────────────────────

int nc_KEY_DOWN(void) { return KEY_DOWN; }
int nc_KEY_UP(void) { return KEY_UP; }
int nc_KEY_LEFT(void) { return KEY_LEFT; }
int nc_KEY_RIGHT(void) { return KEY_RIGHT; }
int nc_KEY_ENTER(void) { return KEY_ENTER; }
int nc_KEY_BACKSPACE(void) { return KEY_BACKSPACE; }
int nc_KEY_HOME(void) { return KEY_HOME; }
int nc_KEY_END(void) { return KEY_END; }
int nc_KEY_PPAGE(void) { return KEY_PPAGE; }
int nc_KEY_NPAGE(void) { return KEY_NPAGE; }
