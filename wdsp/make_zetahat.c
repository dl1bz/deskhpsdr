/*
 * make_zetahat
 *
 * This program reads the contents of the binary WDSP file "zetaHat.bin"
 * and prints the data.
 *
 * The output is intended to be part of the file "zetaHat.c" which
 * initializes these arrays (static data) for use with "memcpy"
 * in emnr.c.
 *
 * Should the WDSP file "zetaHat.bin" be changed, "zetaHat.c" must
 * be re-generated using this program.
 *
 * return values of main()
 *
 *  0  all OK
 * -1  sizeof(double) is not 8
 * -2  error opening file "calculus"
 * -3  read error
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

int main() {
  int fd;
  int i,j;
  double d;
  const size_t dsize = sizeof(double);
  const size_t isize = sizeof(int);
  int rows, cols;
  double gmin, gmax, ximin, ximax;
  double *zetaDouble;
  int    *zetaInt;


  if (dsize != 8) {
    printf("Data type DOUBLE is not 8-byte. Please check!\n");
    return -1;
  }
  if (isize != 4) {
    printf("Data type INT is not 4-byte. Please check!\n");
    return -1;
  }
  fd=open ("zetaHat.bin", O_RDONLY);
  if (fd < 0) {
    printf("Could not open file 'zetaHat.bin'\n");
    return -2;
  }

  if (read(fd, &rows,isize) != isize) {
    printf("READ ERROR rows\n");
    return -3;
  }
  printf("int    zetaHatDefaultRows = %d;\n", rows);

  if (read(fd, &cols,isize) != isize) {
    printf("READ ERROR cols\n");
    return -3;
  }
  printf("int    zetaHatDefaultCols = %d;\n", cols);

  if (read(fd, &gmin, dsize) != dsize) {
    printf("READ ERROR gmin\n");
    return -3;
  }
  printf("double zetaHatDefaultGmin = %30.25f;\n", gmin);

  if (read(fd, &gmax, dsize) != dsize) {
    printf("READ ERROR gmax\n");
    return -3;
  }
  printf("double zetaHatDefaultGmax = %30.25f;\n", gmax);

  if (read(fd, &ximin, dsize) != dsize) {
    printf("READ ERROR ximin\n");
    return -3;
  }
  printf("double zetaHatDefaultXimin = %30.25f;\n", ximin);

  if (read(fd, &ximax, dsize) != dsize) {
    printf("READ ERROR ximax\n");
    return -3;
  }
  printf("double zetaHatDefaultXimax = %30.25f;\n", ximax);

  zetaDouble = malloc(rows*cols*dsize);
  if (zetaDouble == NULL) {
    printf("MALLOC ERROR Double\n");
  }
  zetaInt    = malloc(rows*cols*isize);
  if (zetaInt == NULL) {
    printf("MALLOC ERROR Int\n");
  }

  if (read(fd, zetaDouble, rows*cols*dsize) != rows*cols*dsize) {
    printf("READ ERROR in zetaHatDouble\n");
  }
  if (read(fd, zetaInt, rows*cols*isize) != rows*cols*isize) {
    printf("READ ERROR in zetaHatInt\n");
  }

  //
  // ZetaDouble data is only valid where ZetaInt is non-negative.
  // So report a zero where this is the case
  //
  for (i=0; i< rows*cols; i++) {
    if (zetaInt[i] < 0) zetaDouble[i]=0.0;
  }

  printf("double zetaHatDefaultData[%d]={\n", rows*cols);
  for (i=0; i<rows*cols-1; i++) {
    printf("%30.25f,", zetaDouble[i]);
    if (i % 4 == 3) printf("\n");
  }
  printf("%30.25f\n};\n", zetaDouble[rows*cols-1]);

  printf("int    zetaHatDefaultValid[%d]={\n", rows*cols);
  for (i=0; i<rows*cols-1; i++) {
    printf("%3d,", zetaInt[i]);
    if (i % 30 == 29) printf("\n");
  }
  printf("%3d\n};\n", zetaInt[rows*cols-1]);

  return 0;
}



