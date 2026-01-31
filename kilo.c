#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

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

struct termios orig_termios;

void disableRawMode() {
  tcsetattr(
      STDIN_FILENO, TCSAFLUSH,
      &orig_termios); // Return terminal to original state (passed by reference)
}
void enableRawMode() {
  struct termios raw = orig_termios; // this copies the original state

  tcgetattr(STDIN_FILENO, &raw);
  atexit(disableRawMode);

  // disable canonical mode and echoing
  raw.c_lflag &= ~(ECHO | ICANON);

  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main() {
  enableRawMode();

  char c;
  while (read(STDIN_FILENO, &c, 1) == 1) {
    // if (iscntrl(c)) {
    //   printf("%d\n", c);
    // } else {
    //   printf("%d %c\n", c, c);
    // }
    printf("%c", c);
    ;
    checkQuitSequence(c);
  }

  return 0;
}
