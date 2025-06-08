#include <asm-generic/errno-base.h>
#include <asm-generic/ioctls.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define editor_version "0.0.1"

struct editor {
  int cx, cy;
  int rows;
  int cols;
  struct termios og;
};

struct editor E;

void kill(const char *s) {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);

  perror(s);
  exit(1);
}

void disable_raw() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.og) == -1) {
    kill("tcsetattr");
  }
}

void rawmode() {
  if (tcgetattr(STDIN_FILENO, &E.og) == -1) {
    kill("tcgetattr");
  }
  atexit(disable_raw);
  struct termios raw = E.og;
  raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
  raw.c_iflag &= ~(BRKINT | IXON | ICRNL | INPCK | ISTRIP);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag &= (CS8);

  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
    kill("tcsetattr");
  }
}

char readkey() {
  int n;
  char c;
  while ((n = read(STDIN_FILENO, &c, 1)) != 1) {
    if (n == -1 && errno == EAGAIN)
      kill("read");
  }
  return c;
}

int cursorposition(int *rows, int *cols) {
  char buf[32];
  long unsigned int i = 0;
  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
    return -1;
  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1)
      break;
    if (buf[i] == 'R')
      break;
    ++i;
  }
  buf[i] = '\0';
  if (buf[0] != '\x1b' || buf[1] != '[')
    return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
    return -1;
  return 0;
}

int windowsize(int *rows, int *cols) {
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
      return -1;
    return cursorposition(rows, cols);
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

struct abuf {
  char *b;
  int len;
};

#define ABUF_INIT {NULL, 0}

void abAdd(struct abuf *ab, const char *s, int len) {
  char *new = realloc(ab->b, ab->len + len);

  if (new == NULL)
    return;
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

void abFree(struct abuf *ab) { free(ab->b); }

void drawrows(struct abuf *ab) {
  for (int y = 0; y < E.rows; y++) {
    if (y == E.rows / 3) {
      char message[80];
      int messagelen = snprintf(message, sizeof(message),
                                "Batata editor -- version %s", editor_version);
      if (messagelen > E.cols)
        messagelen = E.cols;

      int padding = (E.cols - messagelen) / 2;
      if (padding < 0)
        padding = 0;
      if (padding) {
        abAdd(ab, "~", 1);
        padding--;
      }
      while (padding--)
        abAdd(ab, " ", 1);
      abAdd(ab, message, messagelen);
    } else {
      abAdd(ab, "~", 1);
    }

    abAdd(ab, "\x1b[k", 3);
    if (y < E.rows - 1) {
      abAdd(ab, "\r\n", 2);
    }
  }
}

void clearscreen() {
  struct abuf ab = ABUF_INIT;

  abAdd(&ab, "\x1b[?25l", 6);
  abAdd(&ab, "\x1b[H", 3);
  abAdd(&ab, "\x1b[2J", 4);

  drawrows(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
  abAdd(&ab, buf, strlen(buf));

  abAdd(&ab, "\x1b[H", 3);
  abAdd(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

void processkey() {
  char c = readkey();
  switch (c) {
  case CTRL_KEY('q'):
    exit(0);
    break;
  }
}

void geteditor() {
  E.cx = 0;
  E.cy = 0;
  if (windowsize(&E.rows, &E.cols) == -1)
    kill("GetWindowSize");
}

int main() {
  rawmode();
  geteditor();
  clearscreen();
  char c;
  while (1) {
    processkey();
  }

  return 0;
}
