#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void trim(char *s) {
  // remove trailing blanks and newlines
  char *p = s + strlen(s) - 1;

  while (p >= s && (*p == ' ' || *p == '\n')) {
    *p-- = 0;
  }
}

void replacexy(char *str, int flag) {
  //
  // Replaces x -> P$_1$
  //          y -> P$_2$
  //
  // etc. (order is: xyzsbcdefghikmnop)  (letter a replaced by s!)
  //
  // If flag == 1, replace unconditionally
  // If flag == 0, replace if the replaced letter is NOT either preceeded or followed
  //               by another letter a-z or A-Z
  //
  // Furthermore, a vertical bar is unconditionally converted to a line break
  //
  char *rep;
  char res[1024];
  int j = 0;
  int len = strlen(str);

  for (int i = 0; i < len; i++) {
    int isolated;
    int replace;

    if (str[i] == '|') {
      res[j++] = '\n';
      continue;
    }

    isolated = 1;
    replace = 0;

    if (i > 0 && isalpha(str[i - 1])) { isolated = 0; }

    if (isalpha(str[i + 1])) { isolated = 0; }

    if (flag == 0 && isolated == 0) {
      res[j++] = str[i];
      continue;
    }

    switch (str[i]) {
    case 'x':
      replace = 1;
      break;

    case 'y':
      replace = 2;
      break;

    case 'z':
      replace = 3;
      break;

    case 's':
      replace = 4;
      break;

    case 'b':
      replace = 5;
      break;

    case 'c':
      replace = 6;
      break;

    case 'd':
      replace = 7;
      break;

    case 'e':
      replace = 8;
      break;

    case 'f':
      replace = 9;
      break;

    case 'g':
      replace = 10;
      break;

    case 'h':
      replace = 11;
      break;

    case 'i':
      replace = 12;
      break;

    case 'k':
      replace = 13;
      break;

    case 'l':
      replace = 14;
      break;

    case 'm':
      replace = 15;
      break;

    case 'n':
      replace = 16;
      break;

    default:
      replace = 0;
    }

    if (replace == 0) {
      res[j++] = str[i];
    } else if (replace < 10) {
      res[j++] = 'p';
      res[j++] = '$';
      res[j++] = '_';
      res[j++] = '0' + replace;
      res[j++] = '$';
    } else if (replace < 20) {
      res[j++] = 'p';
      res[j++] = '$';
      res[j++] = '_';
      res[j++] = '{';
      res[j++] = '1';
      res[j++] = '0' + (replace - 10);
      res[j++] = '}';
      res[j++] = '$';
    }
  }

  res[j++] = 0;
  strcpy(str, res);
}

int main(int argc, char **argv) {
  char catcmd[8];
  char catdescr[1024];
  char catset[1024];
  char catread[1024];
  char catresp[1024];
  int notenum;
  char notes[16][128];
  char *line;
  size_t linecap;
  char *pos;
  linecap = 1024;
  line = malloc(linecap);

  while (getline(&line, &linecap, stdin) > 0) {
    if ((pos = strstr(line, "//"))  != NULL) {
      pos += 2;

      while (*pos != ' ' && *pos != 0) { pos++; }

      while (*pos == ' ' && *pos != 0) { pos++; }

      trim(pos);
    }

    if (strstr(line, "//CATDEF")  != NULL) {
      strcpy(catcmd, pos);
      notenum = 0;
      catdescr[0] = 0;
      catset[0] = 0;
      catread[0] = 0;
      catresp[0] = 0;
      continue;
    }

    if (strstr(line, "//DESCR")  != NULL) {
      strcpy(catdescr, pos);
    }

    if (strstr(line, "//SET")  != NULL) {
      strcpy(catset, pos);
    }

    if (strstr(line, "//READ")  != NULL) {
      strcpy(catread, pos);
    }

    if (strstr(line, "//RESP")  != NULL) {
      strcpy(catresp, pos);
    }

    if (strstr(line, "//NOTE")  != NULL) {
      strcpy(notes[notenum], pos);
      notenum++;
    }

    if (strstr(line, "//ENDDEF")  != NULL) {
      // ship out
      printf("\\begin{center}\n");
      printf("\\begin{tabular}{|p{2cm}|p{11cm}|}\n");
      printf("\\toprule\n");
      printf("$\\phantom{\\Big|}$\\textbf{\\large %s} & %s \\\\\\cline{1-2}\n", catcmd, catdescr);

      if (*catset) {
        replacexy(catset, 1);
        printf("$\\phantom{\\Big|}${\\large Set} & {%s} \\\\\\hline\n", catset);
      }

      if (*catread) {
        replacexy(catread, 1);
        printf("$\\phantom{\\Big|}${\\large Read} & {%s} \\\\\\hline\n", catread);
      }

      if (*catresp) {
        replacexy(catresp, 1);
        printf("$\\phantom{\\Big|}${\\large Response} & {%s} \\\\\\hline\n", catresp);
      }

      if (notenum > 0) {
        replacexy(notes[0], 0);
        printf("$\\phantom{\\Big|}${\\large Notes} & \\multicolumn{1}{|p{11cm}|}{%s} \\\\\n", notes[0]);
      }

      for (int i = 1; i < notenum; i++) {
        replacexy(notes[i], 0);
        printf(" & \\multicolumn{1}{|p{11cm}|}{%s} \\\\\n", notes[i]);
      }

      printf("\\bottomrule\n");
      printf("\\end{tabular}\n");
      printf("\\end{center}\n");
      printf("\n");
    }
  }

  return 0;
}
