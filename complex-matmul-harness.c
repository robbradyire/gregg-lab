/* Test and timing harness program for developing a dense matrix
   multiplication routine for the CS3014 module */

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <assert.h>
#include <omp.h>
#include <x86intrin.h>




/* the following two definitions of DEBUGGING control whether or not
   debugging information is written out. To put the program into
   debugging mode, uncomment the following line: */
/*#define DEBUGGING(_x) _x */
/* to stop the printing of debugging information, use the following line: */
#define DEBUGGING(_x)

struct complex {
  float real;
  float imag;
};

/* write matrix to stdout */
void write_out(struct complex ** a, int dim1, int dim2)
{
  int i, j;

  for ( i = 0; i < dim1; i++ ) {
    for ( j = 0; j < dim2 - 1; j++ ) {
      printf("%.3f + %.3fi ", a[i][j].real, a[i][j].imag);
    }
    printf("%.3f + %.3fi\n", a[i][dim2-1].real, a[i][dim2-1].imag);
  }
}


/* create new empty matrix */
struct complex ** new_empty_matrix(int dim1, int dim2)
{
  struct complex ** result = malloc(sizeof(struct complex*) * dim1);
  struct complex * new_matrix = malloc(sizeof(struct complex) * dim1 * dim2);
  int i;

  for ( i = 0; i < dim1; i++ ) {
    result[i] = &(new_matrix[i*dim2]);
  }

  return result;
}

void free_matrix(struct complex ** matrix) {
  free (matrix[0]); /* free the contents */
  free (matrix); /* free the header */
}

/* take a copy of the matrix and return in a newly allocated matrix */
struct complex ** copy_matrix(struct complex ** source_matrix, int dim1, int dim2)
{
  int i, j;
  struct complex ** result = new_empty_matrix(dim1, dim2);

  for ( i = 0; i < dim1; i++ ) {
    for ( j = 0; j < dim2; j++ ) {
      result[i][j] = source_matrix[i][j];
    }
  }

  return result;
}

/* create a matrix and fill it with random numbers */
struct complex ** gen_random_matrix(int dim1, int dim2)
{
  const int random_range = 512; // constant power of 2
  struct complex ** result;
  int i, j;
  struct timeval seedtime;
  int seed;

  result = new_empty_matrix(dim1, dim2);

  /* use the microsecond part of the current time as a pseudorandom seed */
  gettimeofday(&seedtime, NULL);
  seed = seedtime.tv_usec;
  srandom(seed);

  /* fill the matrix with random numbers */
  for ( i = 0; i < dim1; i++ ) {
    for ( j = 0; j < dim2; j++ ) {
      /* evenly generate values in the range [0, random_range-1)*/
      result[i][j].real = (float)(random() % random_range);
      result[i][j].imag = (float)(random() % random_range);

      /* at no loss of precision, negate the values sometimes */
      /* so the range is now (-(random_range-1), random_range-1)*/
      if (random() & 1) result[i][j].real = -result[i][j].real;
      if (random() & 1) result[i][j].imag = -result[i][j].imag;
    }
  }

  return result;
}

/* check the sum of absolute differences is within reasonable epsilon */
/* returns number of differing values */
void check_result(struct complex ** result, struct complex ** control, int dim1, int dim2)
{
  int i, j;
  double sum_abs_diff = 0.0;
  const double EPSILON = 0.0625;

  for ( i = 0; i < dim1; i++ ) {
    for ( j = 0; j < dim2; j++ ) {
      double diff;
      diff = abs(control[i][j].real - result[i][j].real);
      sum_abs_diff = sum_abs_diff + diff;

      diff = abs(control[i][j].imag - result[i][j].imag);
      sum_abs_diff = sum_abs_diff + diff;
    }
  }

  if ( sum_abs_diff > EPSILON ) {
    fprintf(stderr, "WARNING: sum of absolute differences (%f) > EPSILON (%f)\n",
      sum_abs_diff, EPSILON);
  }
}

/* multiply matrix A times matrix B and put result in matrix C */
void matmul(struct complex ** A, struct complex ** B, struct complex ** C, int a_dim1, int a_dim2, int b_dim2)
{
  int i, j, k;

  for ( i = 0; i < a_dim1; i++ ) {
    for( j = 0; j < b_dim2; j++ ) {
      struct complex sum;
      sum.real = 0.0;
      sum.imag = 0.0;
      for ( k = 0; k < a_dim2; k++ ) {
        // the following code does: sum += A[i][k] * B[k][j];
        struct complex product;
        product.real = A[i][k].real * B[k][j].real - A[i][k].imag * B[k][j].imag;
        product.imag = A[i][k].real * B[k][j].imag + A[i][k].imag * B[k][j].real;
        sum.real += product.real;
        sum.imag += product.imag;
      }
      C[i][j] = sum;
    }
  }
}

// 90  ~ 1x
// 500 ~ 29x
// 1000 ~ 62x

/* the fast version of matmul written by the team */
void team_matmul(struct complex ** A, struct complex ** B, struct complex ** C, int a_rows, int a_cols, int b_cols) {
  int i, j, k;

  struct complex tB [a_cols][b_cols];

  if (a_rows + a_cols + b_cols < 360) {
    for (i = 0; i < a_cols; i++) {
      for(j = 0; j < b_cols; j++) {
        tB[i][j] = B[j][i];
      }
    }

    for (i = 0; i < a_rows; i++) {
      for(j = 0; j < b_cols; j++) {
        struct complex sum;
        sum.real = 0.0;
        sum.imag = 0.0;
        float aReal, aImag, bReal, bImag;
        for (k = 0; k < a_cols; k++) {
          // the following code does: sum += A[i][k] * B[k][j];
          aReal = A[i][k].real;
          aImag = A[i][k].imag;
          bReal = B[j][k].real;
          bImag = B[j][k].imag;
          sum.real += aReal * bReal - aImag * bImag;
          sum.imag += aReal * bImag + aImag * bReal;
        }
        C[i][j] = sum;
      }
    }
  } else {
    #pragma omp parallel for schedule(dynamic)
    for (i = 0; i < a_cols; i++) {
      for(j = 0; j < b_cols; j++) {
        tB[i][j] = B[j][i];
      }
    }

    #pragma omp parallel for schedule(dynamic) if (a_rows + a_cols + b_cols > 360)
    for (i = 0; i < a_rows; i++) {
      for(j = 0; j < b_cols; j++) {
        struct complex sum;
        sum.real = 0.0;
        sum.imag = 0.0;
        float aReal, aImag, bReal, bImag;
        for (k = 0; k < a_cols; k++) {
          // the following code does: sum += A[i][k] * B[k][j];
          aReal = A[i][k].real;
          aImag = A[i][k].imag;
          bReal = tB[j][k].real;
          bImag = tB[j][k].imag;
          sum.real += aReal * bReal - aImag * bImag;
          sum.imag += aReal * bImag + aImag * bReal;
        }
        C[i][j] = sum;
      }
    }
  }
}

long long time_diff(struct timeval * start, struct timeval * end) {
  return (end->tv_sec - start->tv_sec) * 1000000L + (end->tv_usec - start->tv_usec);
}

int main(int argc, char ** argv)
{
  struct complex ** A, ** B, ** C;
  struct complex ** control_matrix;
  long long control_time, mul_time;
  double speedup;
  int a_dim1, a_dim2, b_dim1, b_dim2, errs;
  struct timeval pre_time, start_time, stop_time;

  if ( argc != 5 ) {
    fprintf(stderr, "Usage: matmul-harness <A nrows> <A ncols> <B nrows> <B ncols>\n");
    exit(1);
  }
  else {
    a_dim1 = atoi(argv[1]);
    a_dim2 = atoi(argv[2]);
    b_dim1 = atoi(argv[3]);
    b_dim2 = atoi(argv[4]);
  }

  /* check the matrix sizes are compatible */
  if ( a_dim2 != b_dim1 ) {
    fprintf(stderr,
      "FATAL number of columns of A (%d) does not match number of rows of B (%d)\n",
      a_dim2, b_dim1);
    exit(1);
  }

  /* allocate the matrices */
  A = gen_random_matrix(a_dim1, a_dim2);
  B = gen_random_matrix(b_dim1, b_dim2);
  C = new_empty_matrix(a_dim1, b_dim2);
  control_matrix = new_empty_matrix(a_dim1, b_dim2);

  DEBUGGING( {
    printf("matrix A:\n");
    write_out(A, a_dim1, a_dim2);
    printf("\nmatrix B:\n");
    write_out(A, a_dim1, a_dim2);
    printf("\n");
  } )

  /* record control start time */
  gettimeofday(&pre_time, NULL);

  /* use a simple matmul routine to produce control result */
  matmul(A, B, control_matrix, a_dim1, a_dim2, b_dim2);

  /* record starting time */
  gettimeofday(&start_time, NULL);

  /* perform matrix multiplication */
  team_matmul(A, B, C, a_dim1, a_dim2, b_dim2);

  /* record finishing time */
  gettimeofday(&stop_time, NULL);

  /* compute elapsed times and speedup factor */
  control_time = time_diff(&pre_time, &start_time);
  mul_time = time_diff(&start_time, &stop_time);
  speedup = (float) control_time / mul_time;

  printf("Matmul time: %lld microseconds\n", mul_time);
  printf("control time : %lld microseconds\n", control_time);
  if (mul_time > 0 && control_time > 0) {
    printf("speedup: %.2fx\n", speedup);
  }

  /* now check that the team's matmul routine gives the same answer
     as the known working version */
  check_result(C, control_matrix, a_dim1, b_dim2);

  /* free all matrices */
  free_matrix(A);
  free_matrix(B);
  free_matrix(C);
  free_matrix(control_matrix);

  return 0;
}

