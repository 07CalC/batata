#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <asm-generic/errno-base.h>
#include <asm-generic/ioctls.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define editor_version "0.0.1"
#define TAB_LENGTH 4
#define RELATIVE_LINE_NUMBERS 0 // 0 to disable 1 to enable

enum keys {
  BACKSPACE = 127,
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

enum Highlight {
  NORMAL = 0,
  NUMBER,
  STRING,
  SEPARATOR,
  COMMENT,
  MULTICOMMENT,
  KEY1,
  KEY2,
  MATCH
};

#define HL_NUMBERS (1 << 0)
#define HL_STRINGS (1 << 1)
#define HL_SEPARATORS (1 << 2)
struct syntax {
  char *singleCommentStart;
  char *multicommentstart;
  char *multicommentend;
  char *ftype;
  char **fmatch;
  char **keywords;
  int flags;
};

struct erow {
  int size;
  int rsize;
  char *line;
  char *render;
  unsigned char *highlight;
  int idx;
  bool openComment;
};

struct editor {
  int cx, cy;
  int rx;
  int rowoff;
  int coloff;
  int rows;
  int cols;
  int numrows;
  struct erow *row;
  char *filename;
  char status[80];
  time_t statusmsg_time;
  struct termios og;
  bool dirty;
  struct syntax *syntax;
};

struct editor E;

char *C_EXTENSIONS[] = {".c", ".h", ".cpp", NULL};
char *C_KEYWORDS[] = {"switch",    "if",      "while",   "for",    "break",
                      "continue",  "return",  "else",    "struct", "union",
                      "typedef",   "static",  "enum",    "class",  "case",
                      "int|",      "long|",   "double|", "float|", "char|",
                      "unsigned|", "signed|", "void|",   NULL};
struct syntax HLDB[] = {
    {"//", "/*", "*/", "c", C_EXTENSIONS, C_KEYWORDS,
     HL_NUMBERS | HL_STRINGS | HL_SEPARATORS},
};

#define HLDB_SIZE (sizeof(HLDB) / sizeof(HLDB[0]))
void setstatus(const char *format, ...);
void clearscreen();
char *editorprompt(char *prompt, void (*callback)(char *, int));

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

bool isSepator(int c) {
  return isspace(c) || c == '\0' || strchr(",.()+=/*=~%<>[];", c) != NULL;
}

void updateSyntax(struct erow *row) {
  row->highlight = realloc(row->highlight, row->size);
  memset(row->highlight, NORMAL, row->size);
  if (E.syntax == NULL)
    return;
  char **keys = E.syntax->keywords;

  char *sc = E.syntax->singleCommentStart; // single Comment Start
  char *mcs = E.syntax->multicommentstart;
  char *mce = E.syntax->multicommentend;
  int scLen = sc ? strlen(sc) : 0;
  int mcsLen = mcs ? strlen(mcs) : 0;
  int mceLen = mce ? strlen(mce) : 0;

  bool prevSep = true;
  int inString = 0; // Stores the actual " or '
  bool inComment = (row->idx > 0 && E.row[row->idx - 1].openComment);

  int i = 0;
  while (i < row->rsize) {
    char c = row->render[i];
    unsigned char prevHL = (i > 0) ? row->highlight[i - 1] : NORMAL;

    if (scLen && !inString && !inComment) {
      if (!strncmp(&row->render[i], sc, scLen)) {
        memset(&row->highlight[i], COMMENT, row->size - i);
        break;
      }
    }

    if (mcsLen && mceLen && !inString) {
      if (inComment) {
        row->highlight[i] = MULTICOMMENT;
        if (strncmp(&row->render[i], mce, mceLen) == 0) {
          memset(&row->highlight[i], MULTICOMMENT, mceLen);
          i += mceLen;
          inComment = false;
          prevSep = true;
          continue;
        } else {
          i++;
          continue;
        }
      } else if (strncmp(&row->render[i], mcs, mcsLen) == 0) {
        memset(&row->highlight[i], MULTICOMMENT, mcsLen);
        i += mcsLen;
        inComment = true;
        continue;
      }
    }

    if (E.syntax->flags & HL_STRINGS) {
      if (inString) {
        row->highlight[i] = STRING;
        if (c == '\\' && i + 1 < row->size) {
          row->highlight[i + 1] = STRING;
          i += 2;
          continue;
        }
        if (c == inString)
          inString = 0;
        i++;
        prevSep = true;
        continue;
      } else {
        if (c == '"' || c == '\'') {
          inString = c;
          row->highlight[i] = STRING;
          i++;
          continue;
        }
      }
    }

    if (E.syntax->flags & HL_NUMBERS) {
      if ((isdigit(c) && (prevSep || prevHL == NUMBER)) ||
          (c == '.' && prevHL == NUMBER)) {
        row->highlight[i] = NUMBER;
        i++;
        prevSep = false;
        continue;
      }
    }

    if (prevSep) {
      int j;
      for (j = 0; keys[j]; j++) {
        int klen = strlen(keys[j]);
        bool kw2 = keys[j][klen - 1] == '|';
        if (kw2)
          klen--;

        if (!strncmp(&row->render[i], keys[j], klen) &&
            isSepator(row->render[i + klen])) {
          memset(&row->highlight[i], kw2 ? KEY2 : KEY1, klen);
          i += klen;
          break;
        }
      }
      if (keys[j] != NULL) {
        prevSep = 0;
        continue;
      }
    }

    if (E.syntax->flags & HL_SEPARATORS) {
      if (isSepator(c)) {
        row->highlight[i] = SEPARATOR;
        i++;
        prevSep = true;
        continue;
      }
    }

    prevSep = isSepator(c);
    i++;
  }
  bool diff = (row->openComment != inComment);
  row->openComment = inComment;
  if (diff && row->idx + 1 < E.numrows)
    updateSyntax(&E.row[row->idx + 1]);
}

int syntocolour(int hl) {
  switch (hl) {
  // ANSI colour codes
  case NUMBER:
    return 31;
  case MATCH:
    return 36;
  case STRING:
    return 92;
  case SEPARATOR:
    return 90;
  case COMMENT:
    return 32;
  case KEY1:
    return 94;
  case KEY2:
    return 35;
  default:
    return 37;
  }
}

void selectHL() {
  E.syntax = NULL;
  if (E.filename == NULL)
    return;

  char *ex = strchr(E.filename, '.');
  for (int j = 0; (long unsigned int)j < (HLDB_SIZE); j++) {
    struct syntax *s = &HLDB[j];
    int i = 0;
    while (s->fmatch[i]) {
      bool verified = (s->fmatch[i][0] == '.');
      if ((verified && ex && !strcmp(ex, s->fmatch[i])) ||
          (!verified && strstr(E.filename, s->fmatch[i]))) {
        E.syntax = s;
        for (int row = 0; row < E.numrows; row++)
          updateSyntax(&E.row[row]);
        return;
      }
      i++;
    }
  }
}

int cxtorx(struct erow *row, int cx) {
  int rx = 0;
  for (int i = 0; i < cx; i++) {
    if (row->line[i] == '\t')
      rx += (TAB_LENGTH - 1) - (rx % TAB_LENGTH);
    rx++;
  }
  return rx;
}

int rxtocx(struct erow *row, int rx) {
  int cx = 0, cur = 0;
  for (; cx < row->size; cx++) {
    if (row->line[cx] == '\t')
      cur += (TAB_LENGTH - 1) - (cur % TAB_LENGTH);
    cur++;
    if (cur > rx)
      return cx;
  }
  return cx;
}

void updaterow(struct erow *row) {
  int tabs = 0;
  int j;
  for (j = 0; j < row->size; j++)
    if (row->line[j] == '\t')
      tabs++;

  free(row->render);
  row->render = malloc(row->size + tabs * (TAB_LENGTH - 1) + 1);

  int idx = 0;
  for (j = 0; j < row->size; j++) {
    if (row->line[j] == '\t') {
      row->render[idx++] = ' ';
      while (idx % TAB_LENGTH != 0)
        row->render[idx++] = ' ';
    } else {
      row->render[idx++] = row->line[j];
    }
  }
  row->render[idx] = '\0';
  row->rsize = idx;
  updateSyntax(row);
}

void editorInsertRow(int at, char *s, size_t len) {
  if (at < 0 || at > E.numrows)
    return;
  E.row = realloc(E.row, sizeof(struct erow) * (E.numrows + 1));
  memmove(&E.row[at + 1], &E.row[at], sizeof(struct erow) * (E.numrows - at));
  for (int i = at + 1; i <= E.numrows; i++)
    E.row[i].idx++;

  E.row[at].idx = at;

  E.row[at].size = len;
  E.row[at].line = malloc(len + 1);
  memcpy(E.row[at].line, s, len);
  E.row[at].line[len] = '\0';

  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  E.row[at].highlight = NULL;
  E.row[at].openComment = false;
  updaterow(&E.row[at]);
  E.numrows++;
  E.dirty = true;
}

void editorFreeRow(struct erow *row) {
  free(row->render);
  free(row->line);
  free(row->highlight);
}

void editorDelRow(int at) {
  if (at < 0 || at >= E.numrows)
    return;
  editorFreeRow(&E.row[at]);
  memmove(&E.row[at], &E.row[at + 1],
          sizeof(struct erow) * (E.numrows - at - 1));
  for (int i = at; i < E.numrows - 1; i++)
    E.row[i].idx--;
  E.numrows--;
  E.dirty = true;
}

void rowinsertchar(struct erow *row, int at, int c) {
  if (at < 0 || at > row->size)
    at = row->size;
  row->line = realloc(row->line, row->size + 2);
  memmove(&row->line[at + 1], &row->line[at], row->size - at + 1);
  row->size++;
  row->line[at] = c;
  updaterow(row);
  E.dirty = true;
}

void rowdeletechar(struct erow *row, int at) {
  if (at < 0 || at >= row->size)
    return;
  memmove(&row->line[at], &row->line[at + 1], row->size - at);
  row->size--;
  updaterow(row);
  E.dirty = true;
}

void insertchar(int c) {
  if (E.cy == E.numrows)
    editorInsertRow(E.numrows, "", 0);
  rowinsertchar(&E.row[E.cy], E.cx, c);
  E.cx++;
}

void insertnewline() {
  if (E.cx == 0) {
    editorInsertRow(E.cy, "", 0);
  } else {
    struct erow *row = &E.row[E.cy];
    editorInsertRow(E.cy + 1, &row->line[E.cx], row->size - E.cx);
    row = &E.row[E.cy];
    row->size = E.cx;
    row->line[row->size] = '\0';
    updaterow(row);
  }
  E.cy++;
  E.cx = 0;
}

void rowinsertstring(struct erow *row, char *s, size_t len) {
  row->line = realloc(row->line, row->size + len + 1);
  memcpy(&row->line[row->size], s, len);
  row->size += len;
  row->line[row->size] = '\0';
  updaterow(row);
  E.dirty = true;
}

void deletechar() {
  if (E.cy >= E.numrows)
    return;
  if (E.cx == 0 && E.cy == 0)
    return;

  struct erow *row = &E.row[E.cy];
  if (E.cx > 0) {
    rowdeletechar(row, E.cx - 1);
    E.cx--;
  } else {
    E.cx = E.row[E.cy - 1].size;
    rowinsertstring(&E.row[E.cy - 1], row->line, row->size);
    editorDelRow(E.cy);
    E.cy--;
  }
}

char *rowstostring(int *len) {
  int total = 0;
  for (int j = 0; j < E.numrows; j++)
    total += E.row[j].size + 1;
  *len = total;

  char *buf = malloc(total);
  char *p = buf;
  for (int j = 0; j < E.numrows; j++) {
    memcpy(p, E.row[j].line, E.row[j].size);
    p += E.row[j].size;
    *p = '\n';
    ++p;
  }

  return buf;
}

void editorOpen(char *filename) {
  free(E.filename);
  E.filename = strdup(filename);
  selectHL();
  FILE *fp = fopen(filename, "r");
  if (!fp)
    kill("fopen");

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 &&
           (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
      linelen--;
    editorInsertRow(E.numrows, line, linelen);
  }
  E.dirty = false;
  free(line);
  fclose(fp);
}

void save() {
  if (E.filename == NULL) {
    E.filename = editorprompt("Save as: %s (press ESC to cancel) ", NULL);
    if (E.filename == NULL) {
      setstatus("Save Aborted");
      return;
    }
    selectHL();
  }

  int len;
  char *buf = rowstostring(&len);
  int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
  if (fd != -1) {
    if (ftruncate(fd, len) != -1) {
      if (write(fd, buf, len) == len) {
        close(fd);
        free(buf);
        E.dirty = false;
        setstatus("%d bytes written to disk", len);
        return;
      }
    }
    close(fd);
  }
  free(buf);
  setstatus("I/O error: %s", strerror(errno));
}

void findCallback(char *query, int key) {
  static int last = -1;
  static int dir = 1;
  static int savedLine;
  static char *savedHL = NULL;
  if (savedHL) {
    memcpy(E.row[savedLine].highlight, savedHL, E.row[savedLine].rsize);
    free(savedHL);
    savedHL = NULL;
  }

  if (key == '\r' || key == '\x1b') {
    last = -1;
    dir = 1;
    return;
  } else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
    dir = 1;
  } else if (key == ARROW_LEFT || ARROW_UP) {
    dir = -1;
  } else {
    last = -1;
    dir = 1;
  }

  if (last == -1)
    dir = 1;
  int cur = last;
  for (int i = 0; i < E.numrows; i++) {
    cur += dir;
    if (cur == -1)
      cur = E.numrows - 1;
    else if (cur == E.numrows)
      cur = 0;
    struct erow *row = &E.row[cur];
    char *match = strstr(row->render, query);
    if (match) {
      last = cur;
      E.cy = cur;
      E.cx = rxtocx(row, match - row->render);
      E.rowoff = E.numrows;

      savedLine = cur;
      savedHL = malloc(row->rsize);
      memcpy(savedHL, row->highlight, row->rsize);
      memset(&row->highlight[match - row->render], MATCH, strlen(query));
      break;
    }
  }
}

void find() {
  int initcx = E.cx;
  int initcy = E.cy;
  int initcoloff = E.coloff;
  int initrowoff = E.rowoff;

  char *query = editorprompt("Search: %s (Esc to cancel)", findCallback);
  if (query)
    free(query);
  else {
    E.cx = initcx;
    E.cy = initcy;
    E.coloff = initcoloff;
    E.rowoff = initrowoff;
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

void scroll() {
  E.rx = 0;
  if (E.cy < E.numrows)
    E.rx = cxtorx(&E.row[E.cy], E.cx);
  if (E.cy < E.rowoff)
    E.rowoff = E.cy;
  if (E.cy >= E.rowoff + E.rows) {
    E.rowoff = E.cy - E.rows + 1;
  }
  if (E.rx < E.coloff)
    E.coloff = E.rx;
  if (E.rx >= E.coloff + E.cols)
    E.coloff = E.rx - E.cols + 1;
}

void drawrows(struct abuf *ab) {
  for (int y = 0; y < E.rows; y++) {
    int filerow = y + E.rowoff;
    if (filerow >= E.numrows) {
      if (y == E.rows / 3 && E.numrows == 0) {
        char message[80];
        int messagelen = snprintf(message, sizeof(message),
                                  "Batata -- version %s", editor_version);
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
    } else {
      // Prints the line Number
      int padding = (E.numrows > 0) ? (int)log10(E.numrows) + 1 : 1;
      char lineNum[16];
      int number = 0;
      if (RELATIVE_LINE_NUMBERS)
        number = (filerow == E.cy) ? filerow + 1 : abs(filerow - E.cy);
      else
        number = filerow + 1;

      int wlen = snprintf(lineNum, sizeof(lineNum), "%d", number);
      padding -= wlen;

      for (; padding > 0; padding--)
        abAdd(ab, " ", 1);

      if (filerow == E.cy) {
        abAdd(ab, "\x1b[32m", 5);
        abAdd(ab, lineNum, wlen);
        abAdd(ab, "\x1b[39m", 5);

      } else {
        abAdd(ab, lineNum, wlen);
      }

      abAdd(ab, " ", 1);

      int len = E.row[filerow].rsize - E.coloff;
      if (len < 0)
        len = 0;
      if (len > E.cols)
        len = E.cols;

      char *c = &E.row[filerow].render[E.coloff];
      unsigned char *hl = &E.row[filerow].highlight[E.coloff];
      int curColour = -1;
      for (int j = 0; j < len; j++) {
        if (iscntrl(c[j])) {
          char sym = (c[j] <= 26) ? '@' + c[j] : '?';
          abAdd(ab, "\x1b[7m", 4);
          abAdd(ab, &sym, 1);
          abAdd(ab, "\x1b[m", 3);
          if (curColour != -1) {
            char buf[16];
            int len = snprintf(buf, sizeof(buf), "\x1b[%dm", curColour);
            abAdd(ab, buf, len);
          }
        } else if (hl[j] == NORMAL) {
          if (curColour != -1) {
            abAdd(ab, "\x1b[39m", 5);
            curColour = -1;
          }
          abAdd(ab, &c[j], 1);
        } else {
          int colour = syntocolour(hl[j]);
          if (curColour != colour) {
            curColour = colour;
            char buf[16];
            int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", colour);
            abAdd(ab, buf, clen);
          }
          abAdd(ab, &c[j], 1);
        }
      }
      abAdd(ab, "\x1b[39m", 5);
    }

    abAdd(ab, "\x1b[K", 3);
    abAdd(ab, "\r\n", 2);
  }
}

void DrawStatusBar(struct abuf *ab) {
  abAdd(ab, "\x1b[7m", 4);
  char status[80], rstatus[80];

  int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
                     E.filename ? E.filename : "[No Name]", E.numrows,
                     E.dirty ? "(modified)" : "");
  int rlen =
      snprintf(rstatus, sizeof(rstatus), "%s | %d/%d",
               E.syntax ? E.syntax->ftype : "no filetype", E.cy + 1, E.numrows);
  if (len > E.cols)
    len = E.cols;
  abAdd(ab, status, len);
  while (len < E.cols) {
    if (E.cols - len == rlen) {
      abAdd(ab, rstatus, rlen);
      break;
    } else {
      abAdd(ab, " ", 1);
      len++;
    }
  }
  abAdd(ab, "\x1b[m", 3);
  abAdd(ab, "\r\n", 2);
}

void DrawMessageBar(struct abuf *ab) {
  abAdd(ab, "\x1b[K", 3);
  int msglen = strlen(E.status);
  if (msglen > E.cols)
    msglen = E.cols;
  if (msglen && time(NULL) - E.statusmsg_time < 5)
    abAdd(ab, E.status, msglen);
}

void setstatus(const char *format, ...) {
  va_list arglist;
  va_start(arglist, format);
  vsnprintf(E.status, sizeof(E.status), format, arglist);
  va_end(arglist);
  E.statusmsg_time = time(NULL);
}

char *editorprompt(char *prompt, void (*callback)(char *, int)) {
  size_t bufsize = 128;
  char *buf = malloc(bufsize);

  size_t buflen = 0;
  buf[0] = '\0';

  while (1) {
    setstatus(prompt, buf);
    clearscreen();

    int c = readkey();
    if (c == DEL || c == CTRL_KEY('h') || c == BACKSPACE) {
      if (buflen != 0)
        buf[--buflen] = '\0';
    } else if (c == '\x1b') {
      setstatus("");
      if (callback)
        callback(buf, c);
      free(buf);
      return NULL;
    } else if (c == '\r') {
      if (buflen != 0) {
        setstatus("");
        if (callback)
          callback(buf, c);
        return buf;
      }
    } else if (!iscntrl(c) && c < 128) {
      if (buflen == bufsize - 1) {
        bufsize += buflen / 2;
        buf = realloc(buf, bufsize);
      }
      buf[buflen++] = c;
      buf[buflen] = '\0';
    }

    if (callback)
      callback(buf, c);
  }
}

void clearscreen() {
  scroll();
  struct abuf ab = ABUF_INIT;

  abAdd(&ab, "\x1b[?25l", 6);
  abAdd(&ab, "\x1b[H", 3);
  abAdd(&ab, "\x1b[2J", 4);

  drawrows(&ab);
  DrawStatusBar(&ab);
  DrawMessageBar(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy - E.rowoff + 1,
           E.rx - E.coloff + 2 +
               ((E.numrows > 0) ? (int)log10(E.numrows) + 1 : 1));
  abAdd(&ab, buf, strlen(buf));

  abAdd(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

void movecursor(int key) {
  struct erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  switch (key) {
  case ARROW_LEFT:
    if (E.cx != 0)
      E.cx--;
    else if (E.cy > 0) {
      E.cy--;
      E.cx = E.row[E.cy].size;
    }
    break;
  case ARROW_DOWN:
    if (E.cy < E.numrows)
      E.cy++;
    break;
  case ARROW_UP:
    if (E.cy != 0)
      E.cy--;
    break;
  case ARROW_RIGHT:
    if (row && E.cx < row->size)
      E.cx++;
    else if (row && E.cx == row->size) {
      E.cy++;
      E.cx = 0;
    }
    break;
  }

  row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  int rowlen = row ? row->size : 0;
  if (E.cx > rowlen)
    E.cx = rowlen;
}

void processkey() {
  int c = readkey();
  switch (c) {
  case '\r':
    insertnewline();
    break;

  case CTRL_KEY('q'):
    if (E.dirty) {
      setstatus("Warning!! The file has unsaved changes, press 'y or Y' to "
                "confirm and quit:");
      clearscreen();
      int c = readkey();
      if (c != 'y' && c != 'Y') {
        setstatus("Quitting Cancelled");
        clearscreen();
        break;
      }
    }
    write(STDOUT_FILENO, "\x1b[2j", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    exit(0);

    break;

  case CTRL_KEY('s'):
    save();
    break;

  case CTRL_KEY('f'):
    find();
    break;

  case PG_UP:
  case PG_DN: {
    if (c == PG_UP)
      E.cy = E.rowoff;
    else if (c == PG_DN) {
      E.cy = E.rowoff + E.rows - 1;
      if (E.cy > E.numrows)
        E.cy = E.numrows;
    }

    int times = E.rows;
    while (times--)
      movecursor(c == PG_UP ? ARROW_UP : ARROW_DOWN);
  } break;

  case HOME:
    E.cx = 0;
    break;

  case END: {
    if (E.cy < E.numrows)
      E.cx = E.row[E.cy].size;
  } break;

  case BACKSPACE:
  case CTRL_KEY('h'):
  case DEL:
    if (c == DEL)
      movecursor(ARROW_RIGHT);
    deletechar();
    break;

  case ARROW_LEFT:
  case ARROW_DOWN:
  case ARROW_UP:
  case ARROW_RIGHT:
    movecursor(c);
    break;

  case CTRL_KEY('l'):
  case '\x1b':
    break;

  default:
    insertchar(c);
    break;
  }
}

void geteditor() {
  E.cx = 0;
  E.cy = 0;
  E.rx = 0;
  E.rowoff = 0;
  E.coloff = 0;
  E.numrows = 0;
  E.row = NULL;
  E.filename = NULL;
  E.status[0] = '\0';
  E.statusmsg_time = 0;
  E.dirty = false;
  E.syntax = NULL;
  if (windowsize(&E.rows, &E.cols) == -1)
    kill("GetWindowSize");
  E.rows -= 2;
}

int main(int argc, char *argv[]) {
  rawmode();
  geteditor();
  if (argc >= 2) {
    editorOpen(argv[1]);
  }

  setstatus("TIP: Ctrl-S to save | Ctrl-Q to quit | Ctrl-F to find");

  while (1) {
    clearscreen();
    processkey();
  }

  return 0;
}
