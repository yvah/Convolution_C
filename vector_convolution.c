// Authors: Jamie Taylor, Yeva Huseva, Mylana Bulat


/* Test and timing harness program for developing a multichannel
   multikernel convolution (as used in deep learning networks)

   Note there are some simplifications around this implementation,
   in particular with respect to computing the convolution at edge
   pixels of the image.

   Author: David Gregg
   Date:   March 2022

   Version 1.7 : Adjusted types for mixed-type computation

   Version 1.6 : Modified the code so that the input tensor is float

   Version 1.5 : Modified the code so that the input and kernel
                 are tensors of 16-bit integer values

   Version 1.4 : Modified the random generator to reduce the range
                 of generated values;

   Version 1.3 : Fixed which loop variables were being incremented
                 in write_out();
                 Fixed dimensions of output and control_output
                 matrices in main function

   Version 1.2 : Changed distribution of test data to (hopefully)
                 eliminate random walk of floating point error;
                 Also introduced checks to restrict kernel-order to
                 a small set of values

   Version 1.1 : Fixed bug in code to create 4d matrix
*/
#include <immintrin.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <assert.h>
#include <omp.h>
#include <math.h>
#include <stdint.h>
#include <x86intrin.h>


/* the following two definitions of DEBUGGING control whether or not
   debugging information is written out. To put the program into
   debugging mode, uncomment the following line: */
/*#define DEBUGGING(_x) _x */
/* to stop the printing of debugging information, use the following line: */
#define DEBUGGING(_x)

/* write 3d matrix to stdout */
void write_out(int16_t ***a, int dim0, int dim1, int dim2)
{
    int i, j, k;

    for (i = 0; i < dim0; i++)
    {
        printf("Outer dimension number %d\n", i);
        for (j = 0; j < dim1; j++)
        {
            for (k = 0; k < dim2 - 1; k++)
            {
                printf("%d, ", a[i][j][k]);
            }
            // print end of line
            printf("%d\n", a[i][j][dim2 - 1]);
        }
    }
}

/* create new empty 4d float matrix */
float ****new_empty_4d_matrix_float(int dim0, int dim1, int dim2, int dim3)
{
    float ****result = malloc(dim0 * sizeof(float ***));
    float ***mat1 = malloc(dim0 * dim1 * sizeof(float **));
    float **mat2 = malloc(dim0 * dim1 * dim2 * sizeof(float *));
    float *mat3 = malloc(dim0 * dim1 * dim2 * dim3 * sizeof(float));
    int i, j, k;

    for (i = 0; i < dim0; i++)
    {
        result[i] = &(mat1[i * dim1]);
        for (j = 0; j < dim1; j++)
        {
            result[i][j] = &(mat2[i * dim1 * dim2 + j * dim2]);
            for (k = 0; k < dim2; k++)
            {
                result[i][j][k] = &(mat3[i * dim1 * dim2 * dim3 + j * dim2 * dim3 + k * dim3]);
            }
        }
    }

    return result;
}

/* create new empty 3d matrix */
float ***new_empty_3d_matrix_float(int dim0, int dim1, int dim2)
{
    float ****mat4d;
    float ***mat3d;

    // create a 4d matrix with single first dimension
    mat4d = new_empty_4d_matrix_float(1, dim0, dim1, dim2);
    // now throw away out first dimension
    mat3d = mat4d[0];
    free(mat4d);
    return mat3d;
}

/* create new empty 4d int16_t matrix */
int16_t ****new_empty_4d_matrix_int16(int dim0, int dim1, int dim2, int dim3)
{
    int16_t ****result = malloc(dim0 * sizeof(int16_t ***));
    int16_t ***mat1 = malloc(dim0 * dim1 * sizeof(int16_t **));
    int16_t **mat2 = malloc(dim0 * dim1 * dim2 * sizeof(int16_t *));
    int16_t *mat3 = malloc(dim0 * dim1 * dim2 * dim3 * sizeof(int16_t));
    int i, j, k;

    for (i = 0; i < dim0; i++)
    {
        result[i] = &(mat1[i * dim1]);
        for (j = 0; j < dim1; j++)
        {
            result[i][j] = &(mat2[i * dim1 * dim2 + j * dim2]);
            for (k = 0; k < dim2; k++)
            {
                result[i][j][k] = &(mat3[i * dim1 * dim2 * dim3 + j * dim2 * dim3 + k * dim3]);
            }
        }
    }

    return result;
}

/* create new empty 3d matrix */
int16_t ***new_empty_3d_matrix_int16(int dim0, int dim1, int dim2)
{
    int16_t ****mat4d;
    int16_t ***mat3d;

    // create a 4d matrix with single first dimension
    mat4d = new_empty_4d_matrix_int16(1, dim0, dim1, dim2);
    // now throw away out first dimension
    mat3d = mat4d[0];
    free(mat4d);
    return mat3d;
}

/* take a copy of the matrix and return in a newly allocated matrix */
int16_t ****copy_4d_matrix(int16_t ****source_matrix, int dim0,
                           int dim1, int dim2, int dim3)
{
    int i, j, k, l;
    int16_t ****result = new_empty_4d_matrix_int16(dim0, dim1, dim2, dim3);

    for (i = 0; i < dim0; i++)
    {
        for (j = 0; j < dim1; j++)
        {
            for (k = 0; k < dim2; k++)
            {
                for (l = 0; l < dim3; l++)
                {
                    result[i][j][k][l] = source_matrix[i][j][k][l];
                }
            }
        }
    }
    return result;
}

/* create a matrix and fill it with random numbers */
int16_t ****gen_random_4d_matrix_int16(int dim0, int dim1, int dim2, int dim3)
{
    int16_t ****result;
    int i, j, k, l;
    struct timeval seedtime;
    int seed;

    result = new_empty_4d_matrix_int16(dim0, dim1, dim2, dim3);

    /* use the microsecond part of the current time as a pseudorandom seed */
    gettimeofday(&seedtime, NULL);
    seed = seedtime.tv_usec;
    srandom(seed);

    /* fill the matrix with random numbers */
    const int range = 1 << 10; // 2^10
    // const int bias = 1 << 16; // 2^16
    int16_t offset = 0.0;
    for (i = 0; i < dim0; i++)
    {
        for (j = 0; j < dim1; j++)
        {
            for (k = 0; k < dim2; k++)
            {
                for (l = 0; l < dim3; l++)
                {
                    // generate uniform random integer with mean of zero
                    long long rand = random();
                    // now cut down the range and bias the mean to reduce
                    // the likelihood of large floating point round-off errors
                    int reduced_range = (rand % range);
                    result[i][j][k][l] = reduced_range;
                }
            }
        }
    }

    return result;
}

/* create a matrix and fill it with random numbers */
float ****gen_random_4d_matrix_float(int dim0, int dim1, int dim2, int dim3)
{
    float ****result;
    int i, j, k, l;
    struct timeval seedtime;
    int seed;

    result = new_empty_4d_matrix_float(dim0, dim1, dim2, dim3);

    /* use the microsecond part of the current time as a pseudorandom seed */
    gettimeofday(&seedtime, NULL);
    seed = seedtime.tv_usec;
    srandom(seed);

    /* fill the matrix with random numbers */
    const int range = 1 << 12; // 2^12
    const int bias = 1 << 10;  // 2^16
    int16_t offset = 0.0;
    for (i = 0; i < dim0; i++)
    {
        for (j = 0; j < dim1; j++)
        {
            for (k = 0; k < dim2; k++)
            {
                for (l = 0; l < dim3; l++)
                {
                    // generate uniform random integer with mean of zero
                    long long rand = random();
                    // now cut down the range and bias the mean to reduce
                    // the likelihood of large floating point round-off errors
                    int reduced_range = (rand % range);
                    result[i][j][k][l] = reduced_range + bias;
                }
            }
        }
    }

    return result;
}

/* create a matrix and fill it with random numbers */
float ***gen_random_3d_matrix_float(int dim0, int dim1, int dim2)
{
    float ****mat4d;
    float ***mat3d;

    // create a 4d matrix with single first dimension
    mat4d = gen_random_4d_matrix_float(1, dim0, dim1, dim2);
    // now throw away out first dimension
    mat3d = mat4d[0];
    free(mat4d);
    return mat3d;
}

/* create a matrix and fill it with random numbers */
int16_t ***gen_random_3d_matrix_int16(int dim0, int dim1, int dim2)
{
    int16_t ****mat4d;
    int16_t ***mat3d;

    // create a 4d matrix with single first dimension
    mat4d = gen_random_4d_matrix_int16(1, dim0, dim1, dim2);
    // now throw away out first dimension
    mat3d = mat4d[0];
    free(mat4d);
    return mat3d;
}

/* check the sum of absolute differences is within reasonable epsilon */
void check_result(float ***result, float ***control,
                  int dim0, int dim1, int dim2)
{
    int i, j, k;
    double sum_abs_diff = 0.0;
    const double EPSILON = 0.0625;

    // printf("SAD\n");

    for (i = 0; i < dim0; i++)
    {
        for (j = 0; j < dim1; j++)
        {
            for (k = 0; k < dim2; k++)
            {
                double diff = fabs(control[i][j][k] - result[i][j][k]);
                assert(diff >= 0.0);
                sum_abs_diff = sum_abs_diff + diff;
            }
        }
    }

    if (sum_abs_diff > EPSILON)
    {
        fprintf(stderr, "WARNING: sum of absolute differences (%f) > EPSILON (%f)\n",
                sum_abs_diff, EPSILON);
    }
    else
    {
        printf("COMMENT: sum of absolute differences (%f)  within acceptable range (%f)\n", sum_abs_diff, EPSILON);
    }
}

/* the slow but correct version of matmul written by David*/
void multichannel_conv(float ***image, int16_t ****kernels,
                       float ***output, int width, int height,
                       int nchannels, int nkernels, int kernel_order)
{
    int h, w, x, y, c, m;

    for (m = 0; m < nkernels; m++)
    {
        for (w = 0; w < width; w++)
        {
            for (h = 0; h < height; h++)
            {
                double sum = 0.0;
                for (c = 0; c < nchannels; c++)
                {
                    for (x = 0; x < kernel_order; x++)
                    {
                        for (y = 0; y < kernel_order; y++)
                        {
                            sum += image[w + x][h + y][c] * kernels[m][c][x][y];
                        }
                    }
                    output[m][w][h] = (float)sum;
                }
            }
        }
    }
} 
/* TODO the fast version of matmul written by the student */


void student_conv(float ***image, int16_t ****kernels, float ***output,
                  int width, int height, int nchannels, int nkernels,
                  int kernel_order)
{
    int h, w, x, y, c, m;
    float temp[] = {0.0, 0.0, 0.0, 0.0};

    int16_t *kernel_pointer = ***kernels;
    float *output_pointer = **output;
    //float *image_pointer = **image;

    int kernel_order_squared = kernel_order * kernel_order;
    int m_index, c_index, x_index, mo_index, w_index, h_index;
    int m_mult = kernel_order_squared * nchannels;
    int mo_mult = width * height;
    __m128 sum4;

   #pragma omp parallel for
    for (m = 0; m < nkernels; m++)
    {
        mo_index = m * mo_mult;
        m_index = m * m_mult;
        for (w = 0; w < width; w++)
        {
            w_index = w * height + mo_index;
            for (h = 0; h < height; h++)
            {
                h_index = h + w_index;
                double sum = 0.0;
                for (c = 0; c < nchannels; c++)
                {
                    int c_index = c * kernel_order_squared + m_index;
                    if (kernel_order == 1)
                    {
                        #pragma omp parallel for private(x, sum)
                        for (x = 0; x < kernel_order; x++)
                        {
                            x_index = x * kernel_order + c_index;
                            sum += image[w + x][h][c] * kernel_pointer[x_index];
                            //sum += image_pointer[c + nchannels * (h+y + (height+kernel_order) * (w+x))] * kernel_pointer[y + x_index];
                        }
                    }
                    else if (kernel_order == 3)
                    {
                        for (x = 0; x < kernel_order; x++)
                        {
                             x_index = x * kernel_order + c_index;
                            __m128 img = _mm_set_ps(0.0, image[w + x][h + 2][c], image[w + x][h + 1][c], image[w + x][h][c]);
                            __m128 kern = _mm_set_ps(0.0, kernel_pointer[2 + x_index], kernel_pointer[1 + x_index], kernel_pointer[x_index]);
                            __m128 mul = _mm_mul_ps(img, kern);
                            sum += _mm_cvtss_f32(_mm_dp_ps(mul, _mm_set1_ps(1.0), 0x71));
                        }
                    }
                    else if (kernel_order == 5)
                    {
                         for (x = 0; x < kernel_order; x++)
                        {
                            x_index = x * kernel_order + c_index;
                            __m128d img1 = _mm_set_pd(image[w + x][h + 4][c], image[w + x][h + 3][c]);
                            __m128d img2 = _mm_set_pd(image[w + x][h + 2][c], image[w + x][h + 1][c]);
                            __m128d img3 = _mm_set_sd(image[w + x][h][c]);

                            __m128d kern1 = _mm_set_pd(kernel_pointer[4 + x_index], kernel_pointer[3 + x_index]);
                            __m128d kern2 = _mm_set_pd(kernel_pointer[2 + x_index], kernel_pointer[1 + x_index]);
                            __m128d kern3 = _mm_set_sd(kernel_pointer[x_index]);

                            __m128d mul1 = _mm_mul_pd(img1, kern1);
                            __m128d mul2 = _mm_mul_pd(img2, kern2);
                            __m128d mul3 = _mm_mul_sd(img3, kern3);

                            __m128d sum2_1 = _mm_add_pd(mul1, mul2);
                            __m128d sum2_2 = _mm_add_sd(mul3, _mm_unpackhi_pd(sum2_1, sum2_1));
                            sum2_1 = _mm_add_sd(sum2_1, sum2_2);

                            double temp;
                            _mm_store_sd(&temp, sum2_1);
                            sum += temp;
                        }
                    }
                    else if (kernel_order == 7)
                    {
                        for (x = 0; x < kernel_order; x++)
                        {
                            x_index = x * kernel_order + c_index;
                            sum += image[w + x][h][c] * kernel_pointer[x_index];
                            sum += image[w + x][h + 1][c] * kernel_pointer[1 + x_index];
                            sum += image[w + x][h + 2][c] * kernel_pointer[2 + x_index];
                            sum += image[w + x][h + 3][c] * kernel_pointer[3 + x_index];
                            sum += image[w + x][h + 4][c] * kernel_pointer[4 + x_index];
                            sum += image[w + x][h + 5][c] * kernel_pointer[5 + x_index];
                            sum += image[w + x][h + 6][c] * kernel_pointer[6 + x_index];
                        
                        }
                    }
                    // _mm_storeu_ps(&temp[0], sum4);
                    // sum = temp[0] + temp[1] + temp[2] + temp[3];
                }
                output_pointer[h_index] = (float)sum;
            }
        }
    }
}

/*   for (x = 0; x < kernel_order; x++)
                    {
                        x_index = x * kernel_order + c_index;
                        for ( y = 0; y < kernel_order; y++)
                        {
                            image_vector = _mm_loadu_ps(&image[w+x][h + y][c]);
                            kernelpointer_vector = _mm_loadu_ps((float *)&kernel_pointer[y + x_index]);
                            imgkernel_vectors_mul = _mm_mul_ps(image_vector, kernelpointer_vector);
                            sum4 = _mm_add_ps(sum4, imgkernel_vectors_mul);

                            sum += image_pointer[c + nchannels * (h+y + (height+kernel_order) * (w+x))] * kernel_pointer[y + x_index];
                        }
                    }
                    */
int main(int argc, char **argv)
{
    // float image[W][H][C];
    // float kernels[M][C][K][K];
    // float output[M][W][H];

    float ***image;
    int16_t ****kernels;
    float ***control_output, ***output;
    long long mul_time, mul_time_control;
    int width, height, kernel_order, nchannels, nkernels;
    struct timeval start_time;
    struct timeval stop_time;
    struct timeval start_time_control;
    struct timeval stop_time_control;

    if (argc != 6)
    {
        fprintf(stderr, "Usage: conv-harness <image_width> <image_height> <kernel_order> <number of channels> <number of kernels>\n");
        exit(1);
    }
    else
    {
        width = atoi(argv[1]);
        height = atoi(argv[2]);
        kernel_order = atoi(argv[3]);
        nchannels = atoi(argv[4]);
        nkernels = atoi(argv[5]);
    }
    switch (kernel_order)
    {
    case 1:
    case 3:
    case 5:
    case 7:
        break;
    default:
        fprintf(stderr, "FATAL: kernel_order must be 1, 3, 5 or 7, not %d\n",
                kernel_order);
        exit(1);
    }

    /* allocate the matrices */
    image = gen_random_3d_matrix_float(width + kernel_order, height + kernel_order,
                                       nchannels);
    kernels = gen_random_4d_matrix_int16(nkernels, nchannels, kernel_order, kernel_order);
    output = new_empty_3d_matrix_float(nkernels, width, height);
    control_output = new_empty_3d_matrix_float(nkernels, width, height);

    // DEBUGGING(write_out(A, a_dim1, a_dim2));

    gettimeofday(&start_time_control, NULL);
    /* use a simple multichannel convolution routine to produce control result */
    multichannel_conv(image, kernels, control_output, width,
                      height, nchannels, nkernels, kernel_order);
    gettimeofday(&stop_time_control, NULL);
    mul_time_control = (stop_time_control.tv_sec - start_time_control.tv_sec) * 1000000L +
               (stop_time_control.tv_usec - start_time_control.tv_usec);
    printf("Control conv time: %lld microseconds\n", mul_time_control);

    /* record starting time of student's code*/
    gettimeofday(&start_time, NULL);

    /* perform student's multichannel convolution */
    student_conv(image, kernels, output, width,
                 height, nchannels, nkernels, kernel_order);

    /* record finishing time */
    gettimeofday(&stop_time, NULL);
    mul_time = (stop_time.tv_sec - start_time.tv_sec) * 1000000L +
               (stop_time.tv_usec - start_time.tv_usec);
    printf("Student conv time: %lld microseconds\n", mul_time);

    DEBUGGING(write_out(output, nkernels, width, height));

    /* now check that the student's multichannel convolution routine
       gives the same answer as the known working version */
    check_result(output, control_output, nkernels, width, height);

    return 0;
}