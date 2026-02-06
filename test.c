#include <stdio.h>

int main() {
  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;

  FILE *fp = fopen("test.txt", "r");
  if (!fp) {
    perror("fopen");
    return 1;
  }

  linelen = getline(&line, &linecap, fp);
  printf("line length: %zd", linelen);
}
