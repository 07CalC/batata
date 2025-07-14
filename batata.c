#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <asm-generic/errno-base.h>
#include <asm-generic/ioctls.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
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
#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
// Now with undo
#define editor_version "0.0.3"

// Defaults to be ovverwritten by .batatarc
int TAB_LENGTH = 4;
int RELATIVE_LINE_NUMBERS = 0;
int UNDO_STACK_SIZE = 100;

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
  DEL,
  MOUSE_EVENT
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

typedef enum { EDITNONE, EDITINSERT, EDITDELETE } ActionType;

#define HL_NUMBERS (1 << 0)
#define HL_STRINGS (1 << 1)
#define HL_SEPARATORS (1 << 2)

static struct {
  ActionType type;
  int row;
  int lastcol;
  bool active;
} coalesce_state = {.active = false};

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

// For Undo and Redo
struct action {
  struct erow oldrow;
  int at;
  ActionType type;
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
  struct action *UndoStack;
  int undotop;
  struct action *RedoStack;
  int redotop;
  char mode;
  int sel_x;
  int sel_y;
};

struct editor E;

char *C_EXTENSIONS[] = {".c", ".h", ".cpp", NULL};
char *C_KEYWORDS[] = {"switch",    "if",      "while",   "for",      "break",
                      "continue",  "return",  "else",    "struct",   "union",
                      "typedef",   "static",  "enum",    "class",    "case",
                      "int|",      "long|",   "double|", "float|",   "char|",
                      "unsigned|", "signed|", "void|",   "include|", "define|",
                      NULL};
struct syntax HLDB[] = {
    {"//", "/*", "*/", "c", C_EXTENSIONS, C_KEYWORDS,
     HL_NUMBERS | HL_STRINGS | HL_SEPARATORS},
};

#define HLDB_SIZE (sizeof(HLDB) / sizeof(HLDB[0]))
void setstatus(const char *format, ...);
void clearscreen();
char *editorprompt(char *prompt, void (*callback)(char *, int));
void handlemouse(int btn, int x, int y, char type);

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

void enableMouse() {
  write(STDOUT_FILENO, "\x1b[?1000h", 8);
  write(STDOUT_FILENO, "\x1b[?1006h", 8);
}

void disableMouse() {
  write(STDOUT_FILENO, "\x1b[?10001", 8);
  write(STDOUT_FILENO, "\x1b[?10061", 8);
}

int readkey() {
  int n;
  char c;
  while ((n = read(STDIN_FILENO, &c, 1)) != 1) {
    if (n == -1 && errno == EAGAIN)
      kill("read");
  }

  if (c == '\x1b') {
    char sq[32];
    sq[0] = '\x1b';

    if (read(STDIN_FILENO, &sq[1], 1) != 1)
      return '\x1b';
    if (read(STDIN_FILENO, &sq[2], 1) != 1)
      return '\x1b';

    // Mouse Support
    if (sq[1] == '[' && sq[2] == '<') {
      int i = 3;
      while (i < (int)sizeof(sq) - 1) {
        if (read(STDIN_FILENO, &sq[i], 1) != 1)
          break;
        if (sq[i] == 'm' || sq[i] == 'M') {
          sq[++i] = '\0';
          break;
        }
        i++;
      }
      int btn, x, y;
      char type;
      if (sscanf(sq, "\x1b[<%d;%d;%d%c", &btn, &x, &y, &type) == 4) {
        handlemouse(btn, x, y, type);
        return MOUSE_EVENT;
      } else {
        write(STDOUT_FILENO, "\nChud Gaye\n", 11);
      }
    }

    if (sq[1] == '[') {
      if (sq[2] >= '0' && sq[2] <= '9') {
        if (read(STDIN_FILENO, &sq[3], 1) != 1)
          return '\x1b';
        if (sq[3] == '~') {
          switch (sq[2]) {
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
        switch (sq[2]) {
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
    } else if (sq[1] == '0') {
      switch (sq[2]) {
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

int isSepator(int c) {
  return isspace(c) || c == '\0' ||
         strchr(",.()+=/*=~%<>[];<>#-_\n\r", c) != NULL;
}

int isWhitespace(int c) { return c == ' ' || c == '\t'; }

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

void pushUndo(ActionType type, int rowidx, int col) {
  E.redotop = 0;
  if (rowidx < 0 || rowidx >= E.numrows)
    return;

  bool coalesce = coalesce_state.active && coalesce_state.type == type &&
                  coalesce_state.row == rowidx &&
                  ((type == EDITINSERT && col == coalesce_state.lastcol + 1) ||
                   (type == EDITDELETE && col == coalesce_state.lastcol - 1));

  if (coalesce) {
    coalesce_state.lastcol = col;
    return;
  }

  struct erow *src = &E.row[rowidx];
  struct action edit;
  edit.at = rowidx;
  edit.type = type;
  edit.oldrow.size = src->size;
  edit.oldrow.rsize = src->rsize;
  edit.oldrow.openComment = src->openComment;
  edit.oldrow.line = strdup(src->line);
  edit.oldrow.idx = src->idx;
  edit.oldrow.render = strdup(src->render);
  edit.oldrow.highlight = malloc(sizeof(unsigned char) * src->size);
  if (edit.oldrow.highlight)
    memcpy(edit.oldrow.highlight, src->highlight,
           sizeof(unsigned char) * src->size);
  else
    edit.oldrow.highlight = NULL;

  if (E.undotop == UNDO_STACK_SIZE - 1) {
    free(E.UndoStack[0].oldrow.line);
    free(E.UndoStack[0].oldrow.render);
    free(E.UndoStack[0].oldrow.highlight);
    memmove(&E.UndoStack[0], &E.UndoStack[1],
            sizeof(struct action) * (UNDO_STACK_SIZE - 1));
    E.undotop--;
  }

  E.UndoStack[E.undotop++] = edit;

  coalesce_state.type = type;
  coalesce_state.row = rowidx;
  coalesce_state.lastcol = col;
  coalesce_state.active = true;
}

void applyUndo() {
  if (E.undotop == 0)
    return;

  struct action *edit = &E.UndoStack[--E.undotop];
  int row = edit->at;
  if (row < 0 || row > E.numrows)
    return;

  struct erow *cur = &E.row[row];

  struct action redo;
  redo.at = row;
  redo.type = edit->type;
  redo.oldrow.size = cur->size;
  redo.oldrow.rsize = cur->rsize;
  redo.oldrow.idx = cur->idx;
  redo.oldrow.openComment = cur->openComment;
  redo.oldrow.line = strdup(cur->line);
  redo.oldrow.render = strdup(cur->render);
  redo.oldrow.highlight = malloc(sizeof(unsigned char) * cur->size);
  if (redo.oldrow.highlight)
    memcpy(redo.oldrow.highlight, cur->highlight,
           sizeof(unsigned char) * cur->size);

  if (E.redotop == UNDO_STACK_SIZE) {
    free(E.RedoStack[0].oldrow.line);
    free(E.RedoStack[0].oldrow.render);
    free(E.RedoStack[0].oldrow.highlight);
    memmove(&E.RedoStack[0], &E.RedoStack[1],
            sizeof(struct action) * UNDO_STACK_SIZE - 1);
    E.redotop--;
  }

  E.RedoStack[E.redotop++] = redo;

  free(cur->line);
  free(cur->render);
  free(cur->highlight);

  cur->size = edit->oldrow.size;
  cur->rsize = edit->oldrow.rsize;
  cur->idx = edit->oldrow.idx;
  cur->openComment = edit->oldrow.openComment;

  cur->line = strdup(edit->oldrow.line);
  cur->render = strdup(edit->oldrow.render);

  if (edit->oldrow.highlight && cur->size > 0) {
    cur->highlight = malloc(sizeof(unsigned char) * cur->size);
    memcpy(cur->highlight, edit->oldrow.highlight,
           sizeof(unsigned char) * cur->size);
  } else
    cur->highlight = NULL;

  E.cy = row;
  if (E.cx > cur->size)
    E.cx = cur->size;
  E.dirty = true;

  coalesce_state.active = false;

  free(edit->oldrow.line);
  free(edit->oldrow.render);
  free(edit->oldrow.highlight);
}

void applyRedo() {
  if (E.redotop == 0)
    return;

  struct action *act = &E.RedoStack[--E.redotop];
  int row = act->at;
  if (row < 0 || row >= E.numrows)
    return;

  struct erow *dst = &E.row[row];

  struct action undo;
  undo.at = row;
  undo.type = act->type;
  undo.oldrow.size = dst->size;
  undo.oldrow.rsize = dst->rsize;
  undo.oldrow.idx = dst->idx;
  undo.oldrow.openComment = dst->openComment;
  undo.oldrow.line = strdup(dst->line);
  undo.oldrow.render = strdup(dst->render);
  undo.oldrow.highlight = malloc(sizeof(unsigned char) * dst->size);
  if (undo.oldrow.highlight)
    memcpy(undo.oldrow.highlight, dst->highlight,
           sizeof(unsigned char) * dst->size);

  if (E.undotop == UNDO_STACK_SIZE) {
    free(E.UndoStack[0].oldrow.line);
    free(E.UndoStack[0].oldrow.render);
    free(E.UndoStack[0].oldrow.highlight);
    memmove(&E.UndoStack[0], &E.UndoStack[1],
            sizeof(struct action) * (UNDO_STACK_SIZE - 1));
    E.undotop--;
  }

  E.UndoStack[E.undotop++] = undo;

  free(dst->line);
  free(dst->render);
  free(dst->highlight);

  dst->size = act->oldrow.size;
  dst->rsize = act->oldrow.rsize;
  dst->idx = act->oldrow.idx;
  dst->openComment = act->oldrow.openComment;
  dst->line = strdup(act->oldrow.line);
  dst->render = strdup(act->oldrow.render);
  dst->highlight = malloc(sizeof(unsigned char) * dst->size);
  if (dst->highlight)
    memcpy(dst->highlight, act->oldrow.highlight,
           sizeof(unsigned char) * dst->size);

  E.cy = row;
  if (E.cx > dst->size)
    E.cx = dst->size;

  E.dirty = true;
  coalesce_state.active = false;

  free(act->oldrow.line);
  free(act->oldrow.render);
  free(act->oldrow.highlight);
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
  pushUndo(EDITDELETE, at, 0);
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
  if (!coalesce_state.active)
    pushUndo(EDITINSERT, E.cy, E.cx);
}

void rowdeletechar(struct erow *row, int at) {
  if (at < 0 || at >= row->size)
    return;
  memmove(&row->line[at], &row->line[at + 1], row->size - at);
  row->size--;
  updaterow(row);
  E.dirty = true;
  if (!coalesce_state.active) {
    if (E.cx > 0)
      pushUndo(EDITDELETE, E.cy, E.cx - 1);
    else
      pushUndo(EDITDELETE, E.cy - 1, E.row[E.cy - 1].size);
  }
}

void insertchar(int c) {
  if (!coalesce_state.active)
    pushUndo(EDITINSERT, E.cy, E.cx);
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

  if (!coalesce_state.active) {
    if (E.cx > 0)
      pushUndo(EDITDELETE, E.cy, E.cx - 1);
    else
      pushUndo(EDITDELETE, E.cy - 1, E.row[E.cy - 1].size);
  }

  struct erow *row = &E.row[E.cy];
  if (E.cx > 0) {
    rowdeletechar(row, E.cx - 1);
    E.cx--;
  } else {
    E.cx = E.row[E.cy - 1].size;
    rowinsertstring(&E.row[E.cy - 1], row->line, row->size);
    editorDelRow(E.cy);
    E.cy--;
    coalesce_state.active = false;
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

bool inSelection(int x, int y) {
  // int lineNumGutter = (E.numrows > 0) ? (int)log10(E.numrows) + 1 : 1;
  // x = x - 1 - (lineNumGutter + 1) + E.coloff;

  int starty = E.sel_y;
  int endy = E.cy;
  int startx = E.sel_x;
  int endx = E.cx;

  if (starty > endy || (starty == endy && startx > endx)) {
    int tmpy = starty;
    int tmpx = startx;
    starty = endy;
    startx = endx;
    endy = tmpy;
    endx = tmpx;
  }

  if (y < starty || y > endy)
    return false;

  if (y == starty && y == endy)
    return x >= startx && x <= endx;
  else if (y == starty)
    return x >= startx;
  else if (y == endy)
    return x <= endx;
  else
    return true;
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
          if (E.mode == 'v' && inSelection(j, filerow))
            abAdd(ab, "\x1b[100m", 6);
          abAdd(ab, &c[j], 1);
          if (E.mode == 'v' && inSelection(j, filerow))
            abAdd(ab, "\x1b[49m", 5);
        } else {
          int colour = syntocolour(hl[j]);
          if (curColour != colour) {
            curColour = colour;
            char buf[16];
            int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", colour);
            abAdd(ab, buf, clen);
          }
          if (E.mode == 'v' && inSelection(j, filerow))
            abAdd(ab, "\x1b[100m", 6);
          abAdd(ab, &c[j], 1);
          if (E.mode == 'v' && inSelection(j, filerow))
            abAdd(ab, "\x1b[49m", 6);
        }
      }
      abAdd(ab, "\x1b[39;49m", 8);
    }

    abAdd(ab, "\x1b[K", 3);
    abAdd(ab, "\r\n", 2);
  }
}

void DrawStatusBar(struct abuf *ab) {
  abAdd(ab, "\x1b[7m", 4);
  const char *mode = NULL;
  switch (E.mode) {
  case 'i':
    mode = "INSERT";
    break;
  case 'n':
    mode = "NORMAL";
    break;
  case 'v':
    mode = "VISUAL";
    break;
  }
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), " [%s] %.20s - %d lines %s", mode,
                     E.filename ? E.filename : "[No Name]", E.numrows,
                     E.dirty ? "(modified)" : "");
  int rlen =
      snprintf(rstatus, sizeof(rstatus), "%s | %d/%d",
               E.syntax ? E.syntax->ftype : "no filetype", E.cy + 1, E.numrows);
  int total = len + rlen;
  if (total > E.cols) {
    len -= (total - E.cols);
    if (len < 0)
      len = 0;
  }
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
  return;
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

  if (E.mode == 'i') {
    abAdd(&ab, "\x1b[6 q", 5);
  } else {
    abAdd(&ab, "\x1b[2 q", 5);
  }
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
  coalesce_state.active = false;
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

void handlemouse(int btn, int x, int y, char type) {
  if (type == 'M') {
    switch (btn) {
    case 0: {
      int lineNumGutter = (E.numrows > 0) ? (int)log10(E.numrows) + 1 : 1;

      E.cy = y - 1 + E.rowoff;
      E.cx = x - 1 - (lineNumGutter + 1) + E.coloff;

      if (E.cy < 0)
        E.cy = 0;
      if (E.cx < 0)
        E.cx = 0;
      if (E.cy >= E.numrows)
        E.cy = E.numrows - 1;
      if (E.cx >= E.row[E.cy].size)
        E.cx = E.row[E.cy].size - 1;

      if (E.cy < E.numrows)
        E.rx = cxtorx(&E.row[E.cy], E.cx);
      else
        E.rx = 0;
      break;
    }

    case 2: {
      // TODO : Right Click8
      break;
    }

    case 64: {
      int scroll = 3;
      if (E.rowoff >= scroll)
        E.rowoff -= scroll;
      else
        E.rowoff = 0;
      if (E.cy >= E.rowoff + E.rows)
        for (int i = 0; i < scroll; i++)
          movecursor(ARROW_UP);
      if (E.rowoff >= scroll)
        E.rowoff -= scroll;
      if (E.cy < E.rowoff)
        E.cy = E.rowoff;

      if (E.cy >= E.numrows)
        E.cy = E.numrows - 1;
      break;
    }

    case 65: {
      int scroll = 3;
      if (E.rowoff < E.numrows - scroll)
        E.rowoff += scroll;
      else
        E.rowoff = E.numrows - 1;
      if (E.cy == E.rowoff - scroll) {
        for (int i = 0; i < scroll; i++)
          movecursor(ARROW_DOWN);
        E.rowoff += scroll;
      }
      if (E.cy <= 0)
        movecursor(ARROW_DOWN);
      break;
    }
    }
  }
}

void nextWord(char key) {
  if (E.mode == 'i')
    return;
  int (*fptr)(int) = ((key == 'w') ? &isSepator : &isWhitespace);
  int cx = E.cx;
  int cy = E.cy;
  while (cy < E.numrows) {
    struct erow *row = &E.row[cy];
    if (fptr(row->line[cx])) {
      while (cx < row->size && fptr(row->line[cx]))
        cx++;
      if (cx < row->size) {
        E.cx = cx;
        E.cy = cy;
        return;
      }
    } else {
      while (cx < row->size && isWhitespace(row->line[cx]))
        cx++;
      while (cx < row->size && !fptr(row->line[cx]))
        cx++;
      while (cx < row->size && isWhitespace(row->line[cx]))
        cx++;
    }
    if (cx < row->size) {
      E.cx = cx;
      E.cy = cy;
      return;
    } else {
      cx = 0;
      cy++;
      E.cx = cx;
      E.cy = cy;
      while (isWhitespace(E.row[E.cy].line[E.cx]))
        E.cx++;
      return;
    }
  }
  E.cx = 0;
  E.cy = E.numrows - 1;
}

void prevWord(char key) {
  if (E.mode == 'i')
    return;
  int (*fptr)(int) = ((key == 'b') ? &isSepator : &isWhitespace);
  int cx = E.cx;
  int cy = E.cy;
  while (cy >= 0) {
    struct erow *row = &E.row[cy];
    if (fptr(row->line[cx])) {
      while (cx >= 0 && fptr(row->line[cx]))
        cx--;
      while (cx >= 0 && !fptr(row->line[cx]))
        cx--;
      if (cx >= 0) {
        E.cx = cx + 1;
        E.cy = cy;
        return;
      }
    } else {
      while (cx > -1 && isWhitespace(row->line[cx]))
        cx--;
      while (cx > -1 && !fptr(row->line[cx]))
        cx--;
      while (cx > -1 && isWhitespace(row->line[cx]))
        cx--;
    }
    if (cx > -1) {
      E.cx = cx;
      E.cy = cy;
      return;
    } else {
      cy = ((cy > 0) ? cy - 1 : 0);
      cx = E.row[cy].size - 1;
      E.cx = cx;
      E.cy = cy;
      while (isWhitespace(E.row[E.cy].line[E.cx]))
        E.cx--;
      // if (isSepator(E.row[E.cy].line[E.cx + 1]))
      //   E.cx++;
      return;
    }
  }
  E.cx = E.row[0].size;
  E.cy = 0;
}

void wordend() {
  if (E.mode == 'i')
    return;

  struct erow *row = &E.row[E.cy];
  int i = E.cx + 1;

  while (i < row->size && isWhitespace(row->line[i]))
    i++;

  if (i >= row->size) {
    if (E.cy + 1 < E.numrows) {
      E.cy++;
      E.cx = 0;
    }
    return;
  }

  bool in_separator = isSepator(row->line[i]);

  while (i < row->size) {
    char c = row->line[i];
    if (isWhitespace(c))
      break;
    if (in_separator && !isSepator(c))
      break;
    if (!in_separator && isSepator(c))
      break;
    i++;
  }

  if (i > 0)
    i--;

  E.cx = i;
}

// Vim motion directions
void processmotion(int key) {
  if (E.mode == 'i')
    return;

  switch (key) {
  case 'h':
    movecursor(ARROW_LEFT);
    break;
  case 'j':
    movecursor(ARROW_DOWN);
    break;
  case 'k':
    movecursor(ARROW_UP);
    break;
  case 'l':
    movecursor(ARROW_RIGHT);
    break;
  case 'w':
  case 'W':
    nextWord(key);
    break;
  case 'b':
  case 'B':
    prevWord(key);
    break;
  case 'e':
  case 'E':
    wordend();
    break;

  case '0':
    E.cx = 0;
    clearscreen();
    break;

  case '$':
    E.cx = E.row[E.cy].size - 1;
    clearscreen();
  }
}

void deleteSelection() {
  for (int i = MIN(E.sel_y, E.cy); i <= MAX(E.cy, E.sel_y); i++)
    pushUndo(EDITDELETE, i, -1);

  for (int i = MIN(E.sel_y, E.cy); i <= MAX(E.sel_y, E.cy); i++) {
    struct erow *row = &E.row[i];
    for (int j = row->size; j >= 0; j--) {
      if (inSelection(j, i)) {
        rowdeletechar(row, j);
      }
    }
  }
  for (int i = MAX(E.sel_y, E.cy); i >= MIN(E.sel_y, E.cy); i--) {
    struct erow *row = &E.row[i];
    if (row->size == 0)
      editorDelRow(i);
  }
  E.cx = E.sel_y;
  E.cx = E.sel_x;
  E.mode = 'n';
}

bool openParen(int x, int y, int *outx, int *outy) {
  int depth = 0;
  while (true) {
    char c = E.row[y].line[x];
    if (c == ')')
      depth++;
    else if (c == '(') {
      if (depth == 0) {
        *outx = x;
        *outy = y;
        return true;
      } else {
        depth--;
      }
    }

    if (--x < 0) {
      if (--y < 0)
        return false;
      x = strlen(E.row[y].line) - 1;
    }
  }
}

bool matchingParen(int x, int y, int *outx, int *outy) {
  int depth = 0;
  int len;
  while (true) {
    char c = E.row[y].line[x];
    if (c == '(')
      depth++;
    else if (c == ')') {
      depth--;
      if (depth == 0) {
        *outx = x;
        *outy = y;
        return true;
      }
    }

    x++;
    len = strlen(E.row[y].line);
    if (x >= len) {
      x = 0;
      if (++y >= E.numrows)
        return false;
    }
  }
}

bool insideParens(int x, int y) {
  int tempx = x, tempy = y;
  while (tempx >= 0) {
    if (E.row[tempy].line[tempx] == ')')
      return false;
    if (E.row[tempy].line[tempx] == '(') {
      int matchx, matchy;
      if (matchingParen(tempx, tempy, &matchx, &matchy)) {
        if ((matchy > y) || (matchy == y && matchx >= x))
          return true;
        else
          return false;
      }
    }
    tempx--;
  }
  return false;
}

void processSelection() {
  if (E.mode != 'v')
    return;

  int c = readkey();
  switch (c) {
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
  case CTRL_KEY('z'):
    applyUndo();
    break;
  case CTRL_KEY('y'):
    applyRedo();
    break;
    return;

  case '\x1b':
    E.mode = 'n';
    break;

  case ARROW_LEFT:
  case ARROW_DOWN:
  case ARROW_UP:
  case ARROW_RIGHT:
    movecursor(c);
    break;

  case 'h':
  case 'j':
  case 'k':
  case 'l':
  case 'w':
  case 'W':
  case 'b':
  case 'B':
  case '0':
  case '$':
  case 'e':
  case 'E':
    processmotion(c);
    break;

  case 'd':
    deleteSelection();
    break;
  case 'c':
    deleteSelection();
    E.mode = 'i';
    break;
  case 'i': {
    int k = readkey();
    switch (k) {
    case 'W':
    case 'w': {
      int (*fptr)(int) = ((k == 'w') ? &isSepator : &isWhitespace);
      if (fptr(E.row[E.cx].line[E.cx])) {
        return;
      } else {
        while (!fptr(E.row[E.sel_y].line[E.sel_x]))
          E.sel_x--;
        if (fptr(E.row[E.sel_y].line[E.sel_x]))
          E.sel_x++;
        while (!fptr(E.row[E.cy].line[E.cx]))
          E.cx++;
        if (fptr(E.row[E.cy].line[E.cx]))
          E.cx--;
      }
      break;
    }
    case '(': {
      int x = E.cx, y = E.cy;

      if (insideParens(x, y)) {
        int open_x, open_y, close_x, close_y;
        if (!openParen(x, y, &open_x, &open_y))
          return;
        if (!matchingParen(open_x, open_y, &close_x, &close_y))
          return;
        E.sel_x = open_x + 1;
        E.sel_y = open_y;
        E.cx = close_x - 1;
        E.cy = close_y;
        return;
      }

      int len = strlen(E.row[y].line);
      for (int i = x; i < len; i++) {
        if (E.row[y].line[i] == '(') {
          int close_x, close_y;
          if (!matchingParen(i, y, &close_x, &close_y))
            return;
          E.sel_x = i + 1;
          E.sel_y = y;
          E.cx = close_x - 1;
          E.cy = close_y;
          return;
        }
      }
      return;
    }
    }
  }
  }
}

void Normalgomove() {
  int c = readkey();
  switch (c) {
  case 'g':
    E.cy = 0;
    if (E.cx >= E.row[0].size)
      E.cx = E.row[0].size - 1;
    break;
  default:
    processmotion(c);
    break;
  }
  return;
}

void NormalDelete(char lmao) {
  int c;
  if (lmao != '\0')
    c = lmao;
  else
    c = readkey();
  int count = 1;
  int motion = c;
  if (isdigit(c) && c != '0') {
    count = c - '0';
    while (1) {
      int k = readkey();
      if (isdigit(k)) {
        count = count * 10 + (k - '0');
      } else {
        motion = k;
        break;
      }
    }
  }
  switch (motion) {
  case 'd':
    editorDelRow(E.cy);
    break;
  case '0': {
    int idx = E.cx;
    for (int i = 0; i < idx; i++)
      deletechar();
    break;
  }
  case '$': {
    int times = E.row[E.cy].size - E.cx;
    for (int i = 0; i < times; i++) {
      movecursor(ARROW_RIGHT);
      deletechar();
    }
    break;
  }
  case 'h':
    for (int i = 0; i < count; i++)
      deletechar();
    break;
  case 'l':
    for (int i = 0; i < count; i++) {
      movecursor(ARROW_RIGHT);
      deletechar();
    }
    break;
  case 'j':
    for (int i = 0; i <= count; i++)
      editorDelRow(E.cy);
    break;
  case 'k':
    for (int i = 0; i <= count; i++) {
      editorDelRow(E.cy);
      movecursor(ARROW_UP);
    }
    break;
  case 'W':
  case 'w': {
    int (*fptr)(int) = ((motion == 'w') ? &isSepator : &isWhitespace);
    for (int i = 0; i < count; i++) {
      if (isWhitespace(E.row[E.cy].line[E.cx])) {
        while (isWhitespace(E.row[E.cy].line[E.cx])) {
          movecursor(ARROW_RIGHT);
          deletechar();
        }
        continue;
      } else if (fptr(E.row[E.cy].line[E.cx])) {
        movecursor(ARROW_RIGHT);
        deletechar();
        continue;
      }

      while (1) {
        if (!(E.cy < E.numrows && E.cy >= 0 && E.cx < E.row[E.cx].size &&
              E.cx >= 0))
          break;
        if (!fptr(E.row[E.cy].line[E.cx])) {
          movecursor(ARROW_RIGHT);
          deletechar();
        } else
          break;
      }
      while (isWhitespace(E.row[E.cy].line[E.cx])) {
        movecursor(ARROW_RIGHT);
        deletechar();
      }
      continue;
    }
    break;
  }

  case 'i': {
    int k = readkey();
    switch (k) {
    case 'W':
    case 'w': {
      E.sel_x = E.cx;
      E.sel_y = E.cy;
      int (*fptr)(int) = ((k == 'w') ? &isSepator : &isWhitespace);
      if (fptr(E.row[E.cy].line[E.cx])) {
        return;
      } else {
        while (!fptr(E.row[E.sel_y].line[E.sel_x]))
          E.sel_x--;
        if (fptr(E.row[E.sel_y].line[E.sel_x]))
          E.sel_x++;
        while (!fptr(E.row[E.cy].line[E.cx]))
          E.cx++;
        if (fptr(E.row[E.cy].line[E.cx]))
          E.cx--;
      }
      deleteSelection();
      break;
    }
    case '(': {
      int x = E.cx, y = E.cy;

      if (insideParens(x, y)) {
        int open_x, open_y, close_x, close_y;
        if (!openParen(x, y, &open_x, &open_y))
          return;
        if (!matchingParen(open_x, open_y, &close_x, &close_y))
          return;
        E.sel_x = open_x + 1;
        E.sel_y = open_y;
        E.cx = close_x - 1;
        E.cy = close_y;
        deleteSelection();
        return;
      }

      int len = strlen(E.row[y].line);
      for (int i = x; i < len; i++) {
        if (E.row[y].line[i] == '(') {
          int close_x, close_y;
          if (!matchingParen(i, y, &close_x, &close_y))
            return;
          E.sel_x = i + 1;
          E.sel_y = y;
          E.cx = close_x - 1;
          E.cy = close_y;
          deleteSelection();
          return;
        }
      }
      break;
    }
    }
  }
  }
}

void toggleCase() {
  char changed;
  char c = E.row[E.cy].line[E.cx];
  if (c >= 'a' && c <= 'z')
    changed = c - ('a' - 'A');
  else if (c >= 'A' && c <= 'Z')
    changed = c + ('a' - 'A');
  else
    changed = c;
  rowdeletechar(&E.row[E.cy], E.cx);
  rowinsertchar(&E.row[E.cy], E.cx, changed);
  E.cx = MIN(E.cx + 1, E.row[E.cy].size - 1);
}

// Ctrl-a
void incrementOrDecrement(char c) {
  // Detetct the entire word you are in
  E.sel_x = E.cx;
  E.sel_y = E.cy;
  if (isSepator(E.row[E.cy].line[E.cx])) {
    return;
  } else {
    while (E.sel_x > 0 && isdigit(E.row[E.sel_y].line[E.sel_x - 1]))
      E.sel_x--;
    if (!isdigit(E.row[E.sel_y].line[E.sel_x]))
      E.sel_x++;
    if (E.sel_x > 0 && E.row[E.sel_y].line[E.sel_x - 1] == '-')
      E.sel_x--;
    while (E.cx < E.row[E.cy].size && isdigit(E.row[E.cy].line[E.cx]))
      E.cx++;
    if (E.cx > 0 && !isdigit(E.row[E.cy].line[E.cx]))
      E.cx--;
  }
  int len = E.cx - E.sel_x + 1;
  char *buf = (char *)malloc(len + 1);
  if (!buf) {
    kill("Malloc");
    return;
  }
  strncpy(buf, &E.row[E.sel_y].line[E.sel_x], len);
  buf[len] = '\0';

  // convert to int
  char *endptr;
  int num = (int)strtol(buf, &endptr, 10);
  free(buf);

  if (c == 'i')
    num += 1;
  else if (c == 'd')
    num -= 1;
  else
    return;
  char bufn[32];
  snprintf(bufn, sizeof(bufn), "%d", num);
  len = strlen(bufn);
  bufn[len] = '\0';

  // deleteSelection();
  movecursor(ARROW_RIGHT);
  for (int i = E.cx; i > E.sel_x; i--)
    deletechar();
  for (int i = 0; i < len; i++) {
    insertchar(bufn[i]);
  }
  E.cx--;
}

// proecess normal mode keypresses
void processcommands() {
  if (E.mode != 'n')
    return;
  int c = readkey();
  switch (c) {
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
  case CTRL_KEY('z'):
    applyUndo();
    break;

  case ARROW_LEFT:
  case ARROW_DOWN:
  case ARROW_UP:
  case ARROW_RIGHT:
    movecursor(c);
    break;

  case '/':
    find();
    break;

  case 'u':
    applyUndo();
    break;

  case CTRL_KEY('r'):
    applyRedo();
    break;

  case 'd':
    NormalDelete('\0');
    break;
  case 'D':
    editorDelRow(E.cy);
    break;
  case 'c': {
    int k = readkey();
    switch (k) {
    case 'c':
      E.cx = E.row[E.cy].size;
      while (E.cx > 0)
        deletechar();
      E.mode = 'i';
      break;
    default:
      NormalDelete(k);
      E.mode = 'i';
      break;
    }
    break;
  }
  case 'C': {
    int times = E.row[E.cy].size - E.cx;
    for (int i = 0; i < times; i++) {
      movecursor(ARROW_RIGHT);
      deletechar();
    }
    E.mode = 'i';
    break;
  }

  case 'i':
    E.mode = 'i';
    break;
  case 'I':
    E.cx = 0;
    while (isWhitespace(E.row[E.cy].line[E.cx]))
      E.cx++;
    E.mode = 'i';
    break;
  case 'a':
    movecursor(ARROW_RIGHT);
    E.mode = 'i';
    break;
  case 'A':
    if (E.cy < E.numrows)
      E.cx = E.row[E.cy].size;
    E.mode = 'i';
    break;
  case 'o':
    E.cx = E.row[E.cy].size - 1;
    movecursor(ARROW_RIGHT);
    clearscreen();
    E.mode = 'i';
    insertnewline();
    break;
  case 'O':
    movecursor(ARROW_LEFT);
    E.cx = E.row[E.cy].size - 1;
    movecursor(ARROW_RIGHT);
    clearscreen();
    E.mode = 'i';
    insertnewline();
    break;

  case 'v':
    E.sel_x = E.rx;
    E.sel_y = E.cy;
    E.mode = 'v';
    break;

  case 'h':
  case 'j':
  case 'k':
  case 'l':
  case 'w':
  case 'W':
  case 'b':
  case 'B':
  case '0':
  case '$':
  case 'e':
  case 'E':
    processmotion(c);
    break;
  case 'x':
    movecursor(ARROW_RIGHT);
    deletechar();
    break;
  case 'r':
    write(STDOUT_FILENO, "\x1b[4 q", 5);
    int k = readkey();
    rowdeletechar(&E.row[E.cy], E.cx);
    rowinsertchar(&E.row[E.cy], E.cx, k);
    write(STDOUT_FILENO, "\x1b[6 q", 5);
    break;
  case 's':
    write(STDOUT_FILENO, "\x1b[4 q", 5);
    int l = readkey();
    rowdeletechar(&E.row[E.cy], E.cx);
    rowinsertchar(&E.row[E.cy], E.cx, l);
    E.cx++;
    E.mode = 'i';
    write(STDOUT_FILENO, "\x1b[6 q", 5);
    break;

  case 'g':
    Normalgomove();
    break;
  case 'G':
    E.cy = E.numrows - 1;
    if (E.cx > E.row[E.numrows - 1].size)
      E.cx = E.row[E.numrows - 1].size - 1;
    break;

  case '~':
    toggleCase();
    break;

  case CTRL_KEY('a'):
    incrementOrDecrement('i');
    break;
  case CTRL_KEY('x'):
    incrementOrDecrement('d');
    break;

  // Scroll down
  case CTRL_KEY('e'):
    if (E.rowoff == E.cy) {
      movecursor(ARROW_DOWN);
      E.rowoff++;
    } else
      E.rowoff++;
    break;

  // Scroll up
  case CTRL_KEY('y'):
    if (E.rowoff >= 1)
      E.rowoff--;
    else
      E.rowoff = 0;
    if (E.cy >= E.rowoff + E.rows)
      movecursor(ARROW_UP);
    if (E.cy < E.rowoff)
      E.cy = E.rowoff;
    if (E.cy >= E.numrows)
      E.cy = E.numrows - 1;
    break;

  // Scroll down a page
  case CTRL_KEY('b'):
    E.cy += E.rows - 1;
    E.cy = MIN(E.cy, E.numrows);
    break;
  // Scroll up a page
  case CTRL_KEY('f'):
    E.cy -= E.rows - 1;
    E.cy = MAX(E.cy, 0);
    break;
  // Scroll down half a page
  case CTRL_KEY('d'):
    E.cy -= (E.rows - 1) / 2;
    E.cy = MAX(E.cy, 0);
    break;

  // Scroll up half a page
  case CTRL_KEY('u'):
    E.cy += (E.rows - 1) / 2;
    E.cy = MIN(E.cy, E.numrows);
    break;

  case MOUSE_EVENT:
    clearscreen();
    break;
  }
}

// Process insert mode keypresses
void processkey() {
  if (E.mode != 'i') {
    if (E.mode == 'n') {
      processcommands();
      return;
    } else if (E.mode == 'v') {
      processSelection();
      return;
    }
  }
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

  case CTRL_KEY('z'):
    applyUndo();
    break;

  case CTRL_KEY('r'):
    applyRedo();
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
    E.mode = 'n';
    coalesce_state.active = false;
    break;

  case MOUSE_EVENT:
    clearscreen();
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
  E.UndoStack = malloc(sizeof(struct action) * UNDO_STACK_SIZE);
  E.undotop = 0;
  E.RedoStack = malloc(sizeof(struct action) * UNDO_STACK_SIZE);
  E.redotop = 0;
  E.mode = 'n';
  E.sel_x = 0;
  E.sel_y = 0;
  if (windowsize(&E.rows, &E.cols) == -1)
    kill("GetWindowSize");
  E.rows -= 2;
}

void getConfig(char *filename) {
  FILE *fp = fopen(filename, "r");
  if (!fp)
    return;

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 &&
           (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
      linelen--;
    line[strcspn(line, "\n")] = 0;
    char *eq = strchr(line, '=');
    if (!eq)
      continue;
    *eq = '\0';
    char *key = line;
    char *value = eq + 1;

    while (*key == ' ')
      key++;
    while (*value == ' ')
      value++;

    if (strcmp(key, "TAB_LENGTH") == 0)
      TAB_LENGTH = atoi(value);
    else if (strcmp(key, "RELATIVE_LINE_NUMBERS") == 0)
      RELATIVE_LINE_NUMBERS = atoi(value);
    else if (strcmp(key, "UNDO_STACK_SIZE") == 0)
      UNDO_STACK_SIZE = atoi(value);
  }
  free(line);
  fclose(fp);
}

int main(int argc, char *argv[]) {
  enableMouse();
  rawmode();
  geteditor();
  getConfig(".batatarc");
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
