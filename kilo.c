/***** INCLUDES *****/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/***** DEFINES *****/

// bitwise AND with 0001 1111 -> maps to control key
#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
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

/***** EXIT SEQUENCES *****/

// Quit Sequence ":q\n"
char *quitSequence = ":q\n";
char quitBuffer[3];

/***** DATA *****/

struct editorConfig {
  int cx, cy;
  int screenrows;
  int screencols;
  struct termios orig_termios;
};

struct editorConfig E;

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

void editorDrawRows(struct abuf *ab) {
  // TODO: Make this dynamic when you know how to concat strings with integers
  // in C haha
  for (int y = 0; y < E.screenrows; ++y) {
    if (y == E.screenrows / 3) {
      char welcome[80];

      int welcomelen = snprintf(welcome, sizeof(welcome),
                                "Kilo editor -- version %s", KILO_VERSION);
      if (welcomelen > E.screencols)
        welcomelen = E.screencols;

      int padding = (E.screencols - welcomelen) / 2;

      if (padding) {
        abAppend(ab, "~", 1);
        padding--;
      }

      while (padding--) {
        abAppend(ab, " ", 1);
      }

      abAppend(ab, welcome, welcomelen);
    } else {
      abAppend(ab, "~", 1);
    }

    abAppend(ab, "\x1b[K", 3);
    if (y < E.screenrows - 2) {
      abAppend(ab, "\r\n", 2);
    }
  }
}

void editorRefreshScreen() {
  struct abuf ab = ABUF_INIT;

  abAppend(&ab, "\x1b[?25l", 6);
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);

  char buff[32];
  snprintf(buff, sizeof(buff), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
  abAppend(&ab, buff, strlen(buff));

  abAppend(&ab, "\x1b[?25h", 6); // show cursor
                                 //
  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

/***** INPUT *****/

void editorMoveCursor(int key) {
  switch (key) {
  case ARROW_LEFT:
  case 'h':
    if (E.cx <= 0) {
      E.cx = 0;
    } else {
      E.cx--;
    }
    break;
  case ARROW_RIGHT:
  case 'l':
    if (E.cx >= E.screencols - 1) {
      E.cx = E.screencols - 1;
    } else {
      E.cx++;
    }
    break;
  case ARROW_DOWN:
  case 'j':
    if (E.cy >= E.screenrows - 1) {
      E.cy = E.screenrows - 1;
    } else {
      E.cy++;
    }
    break;
  case ARROW_UP:
  case 'k':
    if (E.cy <= 0) {
      E.cy = 0;
    } else {
      E.cy--;
    }
    break;
  }
}

void editorProcessKeypress() {
  int c = editorReadKey();

  switch (c) {
  case CTRL_KEY('q'):
    write(STDOUT_FILENO, "\x1b[2J", 4); // clear entire screen
    write(STDOUT_FILENO, "\x1b[H", 3);  // cursor to top-left corner
    exit(0);
    break;

  case PAGE_UP:
  case PAGE_DOWN: {
    int times = E.screenrows;
    while (times--)
      editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
  }
  case HOME_KEY:
  case END_KEY: {
    int times = E.screencols;
    while (times--)
      editorMoveCursor(c == HOME_KEY ? ARROW_LEFT : ARROW_RIGHT);
  }

  case ARROW_UP:
  case ARROW_DOWN:
  case ARROW_LEFT:
  case ARROW_RIGHT:
  case 'h':
  case 'j':
  case 'k':
  case 'l':
    editorMoveCursor(c);
    break;
  }
}

/***** MAIN *****/

int initEditor() {
  E.cx = 0;
  E.cy = 0;

  if (getWindowSize(&E.screenrows, &E.screencols) == -1)
    die("getWindowSize");
  return 0;
}

int main() {
  enableRawMode();
  initEditor();

  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
    // checkQuitSequence(c);
  }

  return 0;
}
