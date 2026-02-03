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

/***** EXIT SEQUENCES *****/

// Quit Sequence ":q\n"
char *quitSequence = ":q\n";
char quitBuffer[3];

void checkQuitSequence(char c) {
  if (c == quitSequence[0]) {
    quitBuffer[0] = c;
  } else if (c == quitSequence[1] && quitBuffer[0] == quitSequence[0]) {
    quitBuffer[1] = c;
  } else if (c == quitSequence[2] && quitBuffer[1] == quitSequence[1]) {
    exit(0);
  } else {
    quitBuffer[0] = quitBuffer[1] = '\0';
  }
}

/***** DATA *****/

struct editorConfig {
  int screenrows;
  int screencols;
  struct termios orig_termios;
};

struct editorConfig E;

// struct termios orig_termios;

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

  // disable software flow control
  // IXON: disable Ctrl-S and ctrl-Q from being interpreted
  // -- Ctrl-S otherwise stops all output to the terminal
  // -- Ctrl-Q otherwise resumes output to the terminal
  // ICRNL: disable carriage return (CR) to newline (NL) translations
  // -- This prevents Enter key from being translated to '\n'
  // -- Without this, pressing Enter would send '\r' which is carriage return
  // -- fixes Ctrl-M behavior in short (Ctrl-M is carriage return character)
  // BRKINT: disable break condition from sending SIGINT signal
  // -- This prevents Ctrl-\ from terminating the program
  // INPCK: enables parity checking
  // -- Not commonly used, but we disable it for completeness
  // ISTRIP: disable stripping of the 8th bit
  // -- This allows us to read 8-bit characters
  //
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

  // OPOST: disables all output processing
  // -- This prevents newline characters from being translated to carriage
  // Return
  // -- newline on some systems
  //
  raw.c_oflag &= ~(OPOST);

  // CS8: set character size to 8 bits per byte
  // -- it's a bit mask that sets the character size to 8 bits per byte
  raw.c_cflag |= (CS8); // set character size to 8 bits per byte

  // disable canonical mode and echoing
  // ECHO: input characters are echoed back to the terminal and is dsiabled here
  // ICANON: canonical mode is disabled here, which means we are now in raw mode
  // -- (non-canonical mode)
  // -- This will allow us to read input byte-by-byte instead of line-by-line
  // ISIG: disable signal characters (like Ctrl-C and Ctrl-Z) from being
  // interpreted
  // -- Ctrl-C otherwise sends SIGINT to the program, terminating it
  // -- Ctrl-Z otherwise sends SIGTSTP to the program, suspending it to the
  // -- background
  // IEXTEN: Disable implementation-defined input processing
  // -- on some systems, this disables Ctrl-V from being interpreted
  // -- Ctrl-V otherwise allows the next character to be inserted
  // -- literally, even if it's a special character like Ctrl-C
  //
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

  raw.c_cc[VMIN] =
      0; // minimum number of bytes of input needed before read() can return
  raw.c_cc[VTIME] = 1; // maximum amount of time to wait before read() returns

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr");
}

char editorReadKey() {
  int nread; // number of bytes read
  char c;    // character read

  // read 1 byte from standard input
  while ((nread = read(STDIN_FILENO, &c, 1) != 1)) {
    // EAGAIN: Resource temporarily unavailable
    if (nread == -1 && errno != EAGAIN)
      die("read");
  }

  // return the character read
  return c;
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

  // ioctl: input/output control
  // STDOUT_FILENO: file descriptor for standard output
  // TIOCGWINSZ: terminal input/output control code to get window size
  // ws: pointer to struct winsize to store the window size
  // returns -1 on failure, 0 on success
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    // if (1 || ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    // move cursor to bottom-right corner
    // \x1b is the escape character in hexadecimal
    // [999C moves the cursor 999 columns to the right
    // [999B moves the cursor 999 rows down
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
  for (int y = 0; y <= E.screenrows; ++y) {
    // char line[8];

    // remove warning for truncated literals
    // int len = y > 99 ? 99 : y;

    // if (y == 0) {
    //   snprintf(line, 8, " %d  \r\n", len + 1);
    // } else if (y < 9) {
    //   snprintf(line, 8, " %d ~\r\n", len + 1);
    // } else {
    //   snprintf(line, 8, "%d ~\r\n", len + 1);
    // }
    abAppend(ab, "~", 1);

    if (y < E.screenrows - 1) {
      abAppend(ab, "\r\n", 2);
    }

    // write(STDOUT_FILENO, line, 6);
  }
  // int y;
  // for (y = 0; y < E.screenrows; ++y) {
  //   write(STDOUT_FILENO, "~", 1);
  //
  //   if (y < E.screenrows - 1) {
  //     write(STDOUT_FILENO, "\r\n", 2);
  //   }
  // }
}

void editorRefreshScreen() {
  struct abuf ab = ABUF_INIT;

  // clear screen
  // \x1b is the escape character in hexadecimal
  // [2J is the ANSI escape code to clear the entire screen
  // 4 is the length of the escape sequence
  // write(STDOUT_FILENO, "\x1b[2J", 4); // clear entire screen
  abAppend(&ab, "\x1b[2J", 4);
  //
  // reposition cursor to top-left corner
  // \x1b is the escape character in hexadecimal
  // [H is the ANSI escape code to move the cursor to the home position
  // top-left
  // write(STDOUT_FILENO, "\x1b[H", 3); // reposition cursor to top-left corner
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);

  abAppend(&ab, "\x1b[H", 3);

  // write(STDOUT_FILENO, "\x1b[H", 3);
  // write(STDOUT_FILENO, "\x1b[3C", 4);
  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

/***** INPUT *****/

void editorProcessKeypress() {
  char c = editorReadKey();

  switch (c) {
  case CTRL_KEY('q'):
    write(STDOUT_FILENO, "\x1b[2J", 4); // clear entire screen
    write(STDOUT_FILENO, "\x1b[H",
          3); // reposition cursor to top-left corner
    exit(0);
    break;
  }
}

/***** MAIN *****/

int initEditor() {
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
