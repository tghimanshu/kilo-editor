/***** INCLUDES *****/

// These feature test macros allow us to use functions like `getline`
// without worrying about the C library version.
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

// #include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/***** DEFINES *****/

// bitwise AND with 0001 1111 -> maps to control key
#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
  BACKSPACE = 127,   // ASCII code for backspace key (just to keep known)
  ARROW_LEFT = 1000, // all others will auto map to 1001, 1002 and so on
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  PAGE_UP,
  PAGE_DOWN,
  HOME_KEY,
  END_KEY,
  DEL_KEY,
};

#define KILO_VERSION "0.0.1"
#define KILO_TAB_STOP 8
#define KILO_QUIT_TIMES 3

/***** EXIT SEQUENCES *****/

// Quit Sequence ":q\n"
char *quitSequence = ":q\n";
char quitBuffer[3];

/***** DATA *****/

typedef struct erow {
  int size;
  int rsize;
  char *chars;
  char *render;
} erow;

struct editorConfig {
  int guttersize;
  int cx, cy;
  int rx;
  int rowoff;
  int coloff;
  int screenrows;
  int screencols;
  int numrows;
  erow *row;
  int dirty;
  char *filename;
  char statusmsg[80];
  time_t statusmsg_time;
  struct termios orig_termios;
};

struct editorConfig E;

/***** PROTOTYPES *****/

void editorSetStatusMessage(const char *fmt, ...);

/***** TERMINAL *****/

void die(const char *s) {
  write(STDOUT_FILENO, "\x1b[2J", 4); // clear entire screen
  write(STDOUT_FILENO, "\x1b[H", 3);  // reposition cursor to top-left corner
  perror(s);                          // print error message to stderr
  exit(1);                            // exit with failure status
}

void disableRawMode() {
  // Return terminal to original state (passed by reference)
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    die("tcsetattr"); // handle error
}
void enableRawMode() {
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
    die("tcgetattr");
  atexit(disableRawMode);

  struct termios raw = E.orig_termios; // this copies the original state

  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8); // set character size to 8 bits per byte
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

  raw.c_cc[VMIN] =
      0; // minimum number of bytes of input needed before read() can return
  raw.c_cc[VTIME] = 1; // maximum amount of time to wait before read() returns

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr");
}

int editorReadKey() {
  int nread; // number of bytes read
  char c;    // character read

  // read 1 byte from standard input
  while ((nread = read(STDIN_FILENO, &c, 1) != 1)) {
    // EAGAIN: Resource temporarily unavailable
    if (nread == -1 && errno != EAGAIN)
      die("read");
  }

  // Arrow Keys
  if (c == '\x1b') {
    char seq[3];

    if (read(STDIN_FILENO, &seq[0], 1) != 1)
      return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1)
      return '\x1b';

    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1)
          return '\x1b';
        if (seq[2] == '~') {
          switch (seq[1]) {
          case '3':
            return DEL_KEY;
          case '5':
            return PAGE_UP;
          case '6':
            return PAGE_DOWN;
          case '1':
          case '7':
            return HOME_KEY;
          case '4':
          case '8':
            return END_KEY;
          }
        }
      } else {

        switch (seq[1]) {
        case 'A':
          return ARROW_UP;
        case 'B':
          return ARROW_DOWN;
        case 'C':
          return ARROW_RIGHT;
        case 'D':
          return ARROW_LEFT;
        case 'H':
          return HOME_KEY;
        case 'F':
          return END_KEY;
        }
      }
    } else if (seq[0] == 'O') {
      switch (seq[1]) {
      case 'H':
        return HOME_KEY;
      case 'F':
        return END_KEY;
      }
    }

    return '\x1b';
  } else {
    return c;
  }
}

int getCursorPosition(int *rows, int *cols) {
  char buff[32];
  unsigned int i = 0;

  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
    return -1;

  while (i < sizeof(buff) - 1) {
    if (read(STDIN_FILENO, &buff[i], 1) != 1)
      break;
    if (buff[i] == 'R')
      break;
    i++;
  }
  buff[i] = '\0';

  printf("\r\n buff[1]: '%s' \r\n", &buff[1]);

  if (buff[0] != '\x1b' || buff[1] != '[')
    return -1;

  if (sscanf(&buff[2], "%d:%d", rows, cols) != 2)
    return -1;

  return 0;
}

int getWindowSize(int *rows, int *cols) {
  // Cursor Positions: https://vt100.net/docs/vt100-ug/chapter3.html#CUF

  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
      return -1;

    return getCursorPosition(rows, cols);
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

/***** ROW OPERATIONS *****/

int editorRowCxToRx(erow *row, int cx) {
  int rx = 0;
  for (int j = 0; j < cx; ++j) {
    if (row->chars[j] == '\t')
      rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP);
    rx++;
  }

  return rx;
}

void editorUpdateRow(erow *row, int at) {
  int tabs = 0;
  int j = 0;
  for (j = 0; j < row->size; ++j) {
    if (row->chars[j] == '\t')
      tabs++;
  }

  free(row->render);
  row->render = malloc(row->size + tabs * (KILO_TAB_STOP - 1) + 1);

  int idx = 0;
  for (j = 0; j < row->size; ++j) {
    if (row->chars[j] == '\t') {
      row->render[idx++] = ' ';
      while (idx % KILO_TAB_STOP != 0)
        row->render[idx++] = ' ';
    } else {
      row->render[idx++] = row->chars[j];
    }
  }
  row->render[idx] = '\0';
  row->rsize = idx;
}

void editorAppendRow(char *s, size_t len) {
  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
  int at = E.numrows;

  E.row[at].size = len + 1;
  E.row[E.numrows].chars = malloc(len + 1);

  memcpy(E.row[at].chars, s, len + 1);
  E.row[at].chars[len] = '\0';

  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  editorUpdateRow(&E.row[at], at);

  E.numrows++;
  E.dirty++;
}

void editorRowInsertChar(erow *row, int at, int c) {
  if (at < 0 || at > row->size)
    at = row->size;

  at -= E.guttersize + 1; // adjust for line number and space

  row->chars = realloc(row->chars, row->size + 2);
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
  row->size++;
  row->chars[at] = c;
  editorUpdateRow(row, at);
  E.dirty++;
}

void editorRowDelChar(erow *row, int at) {
  if (at < 0 || at >= row->size)
    return;
  memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
  row->size--;
  editorUpdateRow(row, at);
  E.dirty++;
}

void editorDelChar() {
  if (E.cy == E.numrows)
    return;

  erow *row = &E.row[E.cy];
  if (E.cx > E.guttersize + 1) {
    editorRowDelChar(row, E.cx - 1 - E.guttersize - 1);
    E.cx--;
  }
}

/***** Editor Operations *****/

void editorInsertChar(int c) {
  if (E.cy == E.numrows) {
    editorAppendRow("", 0);
  }
  editorRowInsertChar(&E.row[E.cy], E.cx, c);
  E.cx++;
}

/***** File I/O *****/

char *editorRowsToString(int *buflen) {
  int totlen = 0;
  int j = 0;
  for (j = 0; j < E.numrows; ++j)
    totlen += E.row[j].size + 1;
  *buflen = totlen;

  char *buf = malloc(totlen);
  char *p = buf;
  for (j = 0; j < E.numrows; ++j) {
    memcpy(p, E.row[j].chars, E.row[j].size);
    p += E.row[j].size;
    *p += '\n';
    p++;
  }

  return buf;
}

void editorOpen(char *filename) {
  free(E.filename);
  E.filename = strdup(filename);

  FILE *fp = fopen(filename, "r");
  if (!fp)
    die("fopen");

  size_t linecap = 0;

  char *line = NULL;
  ssize_t linelen;
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    if (linelen != -1) {
      while (linelen > 0 &&
             (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
        linelen--;

      editorAppendRow(line, linelen);
    }
  }

  free(line);
  fclose(fp);
  E.dirty = 0;
}

void editorSave() {
  if (E.filename == NULL)
    return;

  int len;
  char *buf = editorRowsToString(&len);

  int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
  if (fd != -1) {
    if (ftruncate(fd, len) != -1) {
      if (write(fd, buf, len) == len) {
        close(fd);
        free(buf);
        editorSetStatusMessage("%d bytes written to disk", len);
        E.dirty = 0;
        return;
      }
    }
    close(fd);
  }
  editorSetStatusMessage("Can't save! I/O Error: %s", strerror(errno));
  free(buf);
}

/***** APPEND BUFFER *****/

struct abuf {
  char *b;
  int len;
};

void abAppend(struct abuf *ab, const char *s, int len) {
  char *new = realloc(ab->b, ab->len + len);

  if (new == NULL)
    return;
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

void abFree(struct abuf *ab) { free(ab->b); }

#define ABUF_INIT {NULL, 0}

/***** OUTPUT *****/

void editorScroll() {
  E.rx = 0;
  if (E.cy < E.numrows) {
    E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
  }

  if (E.cy < E.rowoff) {
    E.rowoff = E.cy;
  }

  if (E.cy >= E.rowoff + E.screenrows) {
    E.rowoff = E.cy - E.screenrows + 1;
  }

  if (E.rx < E.coloff) {
    E.coloff = E.rx;
  }

  if (E.rx >= E.coloff + E.screencols) {
    E.coloff = E.rx - E.screencols + 1;
  }
}

void editorDrawRowNumberPadding(struct abuf *ab, int y) {
  if (y < 9) {
    abAppend(ab, "   ", 3);
  } else if (y < 99) {
    abAppend(ab, "  ", 2);
  }
  // BUG: For line numbers 100 and above, no padding is added but still it has
  // padding and breaks the alignment.
}

void editorDrawRows(struct abuf *ab) {
  for (int y = 0; y < E.screenrows; ++y) {
    char rowNumber[32];
    int fileRow = y + E.rowoff;

    snprintf(rowNumber, sizeof(rowNumber), "%d", fileRow + 1);

    if (y >= E.numrows) {

      if (E.numrows == 0 && y == E.screenrows / 3) {
        char welcome[80];

        int welcomelen = snprintf(welcome, sizeof(welcome),
                                  "Kilo editor -- version %s", KILO_VERSION);
        if (welcomelen > E.screencols)
          welcomelen = E.screencols;

        int padding = (E.screencols - welcomelen) / 2;

        if (padding) {
          editorDrawRowNumberPadding(ab, y);
          abAppend(ab, rowNumber, strlen(rowNumber) + 1);
          abAppend(ab, " ~", 2);
          padding -= strlen(rowNumber) + 3;
        }

        while (padding--) {
          abAppend(ab, " ", 1);
        }

        abAppend(ab, welcome, welcomelen);
      } else {
        if (y < 9) {
          abAppend(ab, " ", 1);
        }
        editorDrawRowNumberPadding(ab, y);
        abAppend(ab, rowNumber, strlen(rowNumber) + 1);
        abAppend(ab, " ~", 2);
      }
    } else {
      int len = (E.row[fileRow].rsize - E.coloff) + strlen(rowNumber) + 2;
      if (len < 0)
        len = 0;
      if (len > E.screencols)
        len = E.screencols;

      editorDrawRowNumberPadding(ab, y);
      abAppend(ab, rowNumber, strlen(rowNumber) + 1);
      abAppend(ab, " ", 1);
      abAppend(ab, &E.row[fileRow].render[E.coloff],
               len - strlen(rowNumber) - 2);
    }

    abAppend(ab, "\x1b[K", 3); // clear line from cursor to right edge
    abAppend(ab, "\r\n", 2);
  }
}

void editorDrawStatusBar(struct abuf *ab) {
  abAppend(ab, "\x1b[7m", 4); // Switch to inverted Colors
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
                     E.filename ? E.filename : "[No Name]", E.numrows,
                     E.dirty ? "(modified)" : "");
  int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d - %d/%d", E.cx,
                      E.row[E.cy].size, E.cy + 1, E.numrows);
  if (len > E.screencols)
    len = E.screencols;
  abAppend(ab, status, len);

  while (len < E.screencols) {
    if (E.screencols - len == rlen) {
      abAppend(ab, rstatus, rlen);
      break;
    } else {
      abAppend(ab, " ", 1);
      len++;
    }
  }
  abAppend(ab, "\x1b[m", 3); // Switch back to normal colors
  abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab) {
  abAppend(ab, "\x1b[K", 3);
  int msglen = strlen(E.statusmsg);
  if (msglen > E.screencols)
    msglen = E.screencols;
  if (msglen && time(NULL) - E.statusmsg_time < 5)
    abAppend(ab, E.statusmsg, msglen);
}

void editorRefreshScreen() {
  editorScroll();

  struct abuf ab = ABUF_INIT;

  abAppend(&ab, "\x1b[?25l", 6);
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);
  editorDrawStatusBar(&ab);
  editorDrawMessageBar(&ab);

  char buff[32];
  snprintf(buff, sizeof(buff), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1,
           (E.rx - E.coloff) + 1);
  abAppend(&ab, buff, strlen(buff));

  abAppend(&ab, "\x1b[?25h", 6); // show cursor

  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...) {
  va_list ap; // variable argument list
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt,
            ap); // format the status message with the provided arguments
  va_end(ap);
  E.statusmsg_time = time(NULL); // set the time when the status message was set
}

/***** INPUT *****/

void editorMoveCursor(int key) {
  erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  switch (key) {

  case ARROW_LEFT:
    // case 'h':
    if (E.cx >= E.guttersize + 2) {
      E.cx--;
    } else if (E.cy > 0) {
      E.cy--;
      E.cx = E.row[E.cy].size + E.guttersize + 1;
    }
    // if (E.cx <= 0) {
    //   E.cx = 0;
    //   if (E.coloff > 0) {
    //     E.coloff--;
    //   }
    // } else {
    //   E.cx--;
    // }
    break;

  case ARROW_RIGHT:
    // case 'l':
    if (row && E.cx < row->size + E.guttersize) {
      E.cx++;
    } else if (row && E.cx == row->size + E.guttersize) {
      E.cy++;
      E.cx = E.guttersize + 1;
    }
    break;

  case ARROW_DOWN:
    // case 'j':
    if (E.cy < E.numrows) {
      E.cy++;
    }
    break;

  case ARROW_UP:
    // case 'k':
    if (E.cy <= 0) {
      E.cy = 0;
      if (E.rowoff > 0) {
        E.rowoff--;
      }
    } else {
      E.cy--;
    }
    break;
  }

  row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  int rowlen = row ? row->size + E.guttersize : 0;
  if (E.cx >= rowlen) {
    E.cx = rowlen;
  }
}

void editorProcessKeypress() {
  static int quit_times = KILO_QUIT_TIMES;

  int c = editorReadKey();

  switch (c) {
  case '\r':
    /* TODO: Implement Backspace */
    break;
  case CTRL_KEY('q'):
    if (E.dirty && quit_times > 0) {
      editorSetStatusMessage("WARNING!!! File has unsaved changes. Press "
                             "Ctrl-Q %d more times to quit.",
                             quit_times);
      quit_times--;
      return;
    }
    write(STDOUT_FILENO, "\x1b[2J", 4); // clear entire screen
    write(STDOUT_FILENO, "\x1b[H", 3);  // cursor to top-left corner
    exit(0);
    break;

  case CTRL_KEY('s'):
    editorSave();
    break;

  case BACKSPACE:
  case CTRL_KEY('h'): // Ctrl-H is often used as an alternative to Backspace (it
                      // was on old terminals)
  case DEL_KEY:
    /* TODO: Implement Backspace */
    if (c == DEL_KEY)
      editorMoveCursor(ARROW_RIGHT);
    editorDelChar();
    break;

  case PAGE_UP:
  case PAGE_DOWN: {
    if (c == PAGE_UP) {
      E.cy = E.rowoff;
    } else if (c == PAGE_DOWN) {
      E.cy = E.rowoff + E.screenrows - 1;
      if (E.cy > E.numrows)
        E.cy = E.numrows;
    }
    int times = E.screenrows;
    while (times--)
      editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
  } break;
  case HOME_KEY:
    E.cx = 0;
    break;
  case END_KEY:
    if (E.cy < E.numrows)
      E.cx = E.row[E.cy].size;
    break;

  case ARROW_UP:
  case ARROW_DOWN:
  case ARROW_LEFT:
  case ARROW_RIGHT:
    // case 'h':
    // case 'j':
    // case 'k':
    // case 'l':
    editorMoveCursor(c);
    break;

  case CTRL_KEY('l'): // Ctrl-L is often used to refresh the screen
  case '\x1b':        // Escape key
    break;

  default:
    editorInsertChar(c);
  }

  quit_times = KILO_QUIT_TIMES;
}

/***** MAIN *****/

int initEditor() {
  E.guttersize = 4;
  E.cx = E.guttersize + 1;
  E.cy = 0;
  E.rx = 3;
  E.numrows = 0;
  E.row = NULL;
  E.dirty = 0;
  E.rowoff = 0;
  E.coloff = 0;
  E.rowoff = 0;
  E.filename = NULL;
  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;

  if (getWindowSize(&E.screenrows, &E.screencols) == -1)
    die("getWindowSize");

  E.screenrows -= 2; // Make room for status bar and status msg
  return 0;
}

int main(int argc, char *argv[]) {
  enableRawMode();
  initEditor();
  if (argc >= 2)
    editorOpen(argv[1]);

  editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = Quit");

  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
    // checkQuitSequence(c);
  }

  return 0;
}
