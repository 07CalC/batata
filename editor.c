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

enum keys {
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  PG_UP,
  PG_DN,
  HOME,
  END,
  DEL
};

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

int readkey() {
  int n;
  char c;
  while ((n = read(STDIN_FILENO, &c, 1)) != 1) {
    if (n == -1 && errno == EAGAIN)
      kill("read");
  }

  if (c == '\x1b') {
    char sq[3];

    if (read(STDIN_FILENO, &sq[0], 1) != 1)
      return '\x1b';
    if (read(STDIN_FILENO, &sq[1], 1) != 1)
      return '\x1b';

    if (sq[0] == '[') {
      if (sq[1] >= '0' && sq[1] <= '9') {
        if (read(STDIN_FILENO, &sq[2], 1) != 1)
          return '\x1b';
        if (sq[2] == '~') {
          switch (sq[1]) {
          case '1':
            return HOME;
          case '3':
            return DEL;
          case '4':
            return END;
          case '5':
            return PG_UP;
          case '6':
            return PG_DN;
          case '7':
            return HOME;
          case '8':
            return END;
          }
        }
      } else {
        switch (sq[1]) {
        case 'A':
          return ARROW_UP;
        case 'B':
          return ARROW_DOWN;
        case 'C':
          return ARROW_RIGHT;
        case 'D':
          return ARROW_LEFT;
        case 'H':
          return HOME;
        case 'F':
          return END;
        }
      }
    } else if (sq[0] == '0') {
      switch (sq[1]) {
      case 'H':
        return HOME;
      case 'F':
        return END;
      }
    }
    return '\x1b';
  } else {
    return c;
  }
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

  abAdd(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

void movecursor(int key) {
  switch (key) {
  case ARROW_LEFT:
    if (E.cx != 0)
      E.cx--;
    break;
  case ARROW_DOWN:
    if (E.cy != E.rows - 1)
      E.cy++;
    break;
  case ARROW_UP:
    if (E.cy != 0)
      E.cy--;
    break;
  case ARROW_RIGHT:
    if (E.cx != E.cols - 1)
      E.cx++;
    break;
  }
}

void processkey() {
  int c = readkey();
  switch (c) {
  case CTRL_KEY('q'):
    write(STDOUT_FILENO, "\x1b[2j", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    exit(0);
    break;

  case PG_UP:
  case PG_DN: {
    int times = E.rows;
    while (times--)
      movecursor(c == PG_UP ? ARROW_UP : ARROW_DOWN);
  } break;

  case HOME:
  case END: {
    int times = E.cols;
    while (times--)
      movecursor(c == HOME ? ARROW_LEFT : ARROW_RIGHT);
  } break;

  case ARROW_LEFT:
  case ARROW_DOWN:
  case ARROW_UP:
  case ARROW_RIGHT:
    movecursor(c);
    break;
  }
  clearscreen();
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
