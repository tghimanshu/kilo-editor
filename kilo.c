/***** INCLUDES *****/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

struct termios orig_termios;

/***** TERMINAL *****/

void die(const char *s) {
  write(STDOUT_FILENO, "\x1b[2J", 4); // clear entire screen
  write(STDOUT_FILENO, "\x1b[H", 3);  // reposition cursor to top-left corner
  perror(s);                          // print error message to stderr
  exit(1);                            // exit with failure status
}

void disableRawMode() {
  // Return terminal to original state (passed by reference)
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
    die("tcsetattr"); // handle error
}
void enableRawMode() {
  if (tcgetattr(STDIN_FILENO, &orig_termios) == -1)
    die("tcgetattr");
  atexit(disableRawMode);

  struct termios raw = orig_termios; // this copies the original state

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

/***** OUTPUT *****/

void editorDrawRows() {
  // TODO: Make this dynamic when you know how to concat strings with integers
  // in C haha
  for (int y = 0; y <= 24; ++y) {
    char line[6] = "  |~\r\n";
    switch (y) {
    case 0:
      line[1] = '1';
      break;
    case 1:
      line[1] = '2';
      break;
    case 2:
      line[1] = '3';
      break;
    case 3:
      line[1] = '4';
      break;
    case 4:
      line[1] = '5';
      break;
    case 5:
      line[1] = '6';
      break;
    case 6:
      line[1] = '7';
      break;
    case 7:
      line[1] = '8';
      break;
    case 8:
      line[1] = '9';
      break;
    case 9:
      line[0] = '1';
      line[1] = '0';
      break;
    case 10:
      line[0] = '1';
      line[1] = '1';
      break;
    case 11:
      line[0] = '1';
      line[1] = '2';
      break;
    case 12:
      line[0] = '1';
      line[1] = '3';
      break;
    case 13:
      line[0] = '1';
      line[1] = '4';
      break;
    case 14:
      line[0] = '1';
      line[1] = '5';
      break;
    case 15:
      line[0] = '1';
      line[1] = '6';
      break;
    case 16:
      line[0] = '1';
      line[1] = '7';
      break;
    case 17:
      line[0] = '1';
      line[1] = '8';
      break;
    case 18:
      line[0] = '1';
      line[1] = '9';
      break;
    case 19:
      line[0] = '2';
      line[1] = '0';
      break;
    case 20:
      line[0] = '2';
      line[1] = '1';
      break;
    case 21:
      line[0] = '2';
      line[1] = '2';
      break;
    case 22:
      line[0] = '2';
      line[1] = '3';
      break;
    case 23:
      line[0] = '2';
      line[1] = '4';
      break;
    case 24:
      line[0] = '2';
      line[1] = '5';
    default:
      break;
    }
    write(STDOUT_FILENO, line, 6);
  }
}

void editorRefreshScreen() {

  // clear screen
  // \x1b is the escape character in hexadecimal
  // [2J is the ANSI escape code to clear the entire screen
  // 4 is the length of the escape sequence
  write(STDOUT_FILENO, "\x1b[2J", 4); // clear entire screen
                                      //
  // reposition cursor to top-left corner
  // \x1b is the escape character in hexadecimal
  // [H is the ANSI escape code to move the cursor to the home position
  // top-left
  write(STDOUT_FILENO, "\x1b[H", 3); // reposition cursor to top-left corner

  editorDrawRows();

  write(STDOUT_FILENO, "\x1b[H", 3);
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

int main() {
  enableRawMode();

  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
    // checkQuitSequence(c);
  }

  return 0;
}
