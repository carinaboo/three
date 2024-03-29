#include <emmintrin.h>
#include <string.h>
#include <stdio.h>
// string.h for memset
#define KERNX 3 // this is the x-size of the kernel. It will always be odd.
#define KERNY 3 // this is the y-size of the kernel. It will always be odd.
#define STRIDE 8
#define FLOOR_MULTIPLE( N, FACTOR ) (N)/(FACTOR)*(FACTOR)

typedef struct {
    int width;
    int height;
    float *array;
} array2d;

// SSE memcopy
void memcopyFloats(float *dest, float *src, unsigned int count) {
    __m128 buf1, buf2;
    int i;
    for (i = 0; i < ((count >> 4) << 4); i+=8) {
        buf1 = _mm_loadu_ps(src + i);
        buf2 = _mm_loadu_ps(src + i + 4);
        _mm_storeu_ps(dest + i, buf1);
        _mm_storeu_ps(dest + i + 4, buf2);
    }
    for ( ; i < count; i++) {
        dest[i] = src[i];
    }
}

// add some number of zeros as padding around a given strided-array
// for a 3x3 kernel, the pad_size should be one
// in general pad_size = max((KERNX - 1)/2, (KERNY - 1)/2);
// note that we'll need to free whatever memory is in *padded elsewhere!
array2d zeroPad(array2d in, int pad_size) {
    array2d retval;
    int pad_x = in.width + pad_size*2;
    int pad_y = in.height + pad_size*2;

    size_t p_arr_size = pad_x*pad_y*sizeof(float);
    size_t line_size = in.width*sizeof(float);

    float *padded = (float*) malloc(p_arr_size);

    // zero out the whole dingus
    memset(padded, 0, p_arr_size);

    // copy the original data into the zero-padded array
    for (int i = in.height-1; i != -1; i--) {
        //     0,0      y                   x         
        memcpy(padded + pad_x*(i+pad_size) + pad_size,
                // 0,y
                in.array + i*in.width, 
                line_size);
    }

    retval.array = padded;
    retval.width = pad_x;
    retval.height = pad_y;

    return retval;
}

// unpads padded.array by pad_size from all size
// writes result into out.array (which should be *out from 
// conv2d :)
void unPad(array2d padded, array2d out, int pad_size) {
    // copy data out of the zero-padded array
    float *src = padded.array;
    float *dst = out.array;
    size_t line_size = out.width*sizeof(float);
    for (int i = out.height-1; i != -1; i--) {
        memcpy(dst + i*out.width,
               src + padded.width*(i+pad_size) + pad_size,
               line_size);
    }
}

// tool for debugging zeroPad
void printArray(array2d array) {
    for (int y = 0; y < array.height; y++) {
        for (int x = 0; x < array.width; x++) {
            printf("%f, ", array.array[x + y*array.width]);
        }
        printf("\n");
    }
}

// test the above functions on arrays
void test_array2d(int pad_size) {
    float data[9] = {1,2,3,4,5,6,7,8,9};
    float res[9];
    array2d padded;
    array2d out;
    array2d test;

    test.array = data;
    test.width = 3;
    test.height = 3;

    out.array = res;
    out.width = 3;
    out.height = 3;


    printf("Original data\n");
    printArray(test);

    printf("Padded with %i 0s on each side\n", pad_size);
    padded = zeroPad(test, pad_size);
    printArray(padded);

    printf("Padding removed!\n");
    unPad(padded, out, pad_size);
    printArray(out);
}

int conv2D(float* in, float* out, int data_size_X, int data_size_Y, float* kernel){
    size_t float_size = sizeof(float);
    // the x coordinate of the kernel's center
    int kern_cent_X = (KERNX - 1)/2;
    // the y coordinate of the kernel's center
    int kern_cent_Y = (KERNY - 1)/2;

    /**********************************************************************
     * pad the input array
     * *******************************************************************/
    int padded_width = data_size_X + 2;
    int padded_height = data_size_Y + 2;

    size_t p_arr_size = padded_width*padded_height*sizeof(float);
    float padded[padded_width*padded_height];

    // zero top line
    memset(padded, 0, sizeof(float)*padded_width);
    // copy the original data into the zero-padded array
    int y;
    for (y = 0; y < ((data_size_Y >> 1) << 1); y+=2) {
        // zero start of line
        padded[(y+1)*padded_width] = 0;
        memcopyFloats(padded + 1 + (y+1)*padded_width,
                in+ y*data_size_X,
                data_size_X);      // y + pad_width, hwich is 1 for kernel size
        // zero end of line
        padded[(y+1)*padded_width + (padded_width -1)] = 0;

        // zero start of line
        padded[(y+2)*padded_width] = 0;
        memcopyFloats(padded + 1 + (y+1+1)*padded_width,
                in + (y+1)*data_size_X,
                data_size_X);
        // zero end of line
        padded[(y+2)*padded_width + (padded_width -1)] = 0;
    }
    for (; y < data_size_Y; y++) {
        // zero start of line
        padded[(y+1)*padded_width] = 0;
        memcopyFloats(padded + 1 + (y+1)*padded_width,
                in+ y*data_size_X,
                data_size_X);
        // zero end of line
        padded[(y+1)*padded_width + (padded_width -1)] = 0;
    }
    // zero last line
    memset(padded + (padded_height-1)*padded_width, 0, sizeof(float)*padded_width);


    /********************************************************************
     * Do a convolution
     * assuming KERNX = 3 and KERNY = 3,
     * we can create something defined in KERNX and KERNY using C preprocessor
     *
     * save the whole kernel in variables so we don't
     * do any memory access semantics for it
     *
     *
     * this is kinda loop unrolling, and definitly register blocking
     *
     *      0  1  2
     *    ----------
     * a  |__|__|__|
     * b  |__|__|__|
     * c  |  |  |  |
     *    ----------
     */
    
    // hardcoded unflipped kernel array, fix later
    float kernel_unflipped[9] = {kernel[8], kernel[7], kernel[6], kernel[5], kernel[4], kernel[3], kernel[2], kernel[1], kernel[0]};

    __m128  kv_0, kv_1, kv_2;
            // inv_0, inv_1, inv_2, inv_4, inv_5, inv_6,
            // outv_0, outv_4;

    int x_max_stride = FLOOR_MULTIPLE(data_size_X, STRIDE);
    char needs_help  = (FLOOR_MULTIPLE(data_size_X, STRIDE) != data_size_X);

    // main convolution loop
    // avg 14 Gflops with STRIDE 8
    for (int j = 0; j < KERNY; j++){ // deal with one row of kernel at a time
        
        // load 4 copies of each column value in current kernel row into vectors
        kv_0 = _mm_load1_ps(kernel_unflipped + j*KERNX + 0); // [j0, j0, j0, j0]
        kv_1 = _mm_load1_ps(kernel_unflipped + j*KERNX + 1); // [j1, j1, j1, j1]
        kv_2 = _mm_load1_ps(kernel_unflipped + j*KERNX + 2); // [j2, j2, j2, j2]

        for(int y = j; y < data_size_Y+j; y++){ // the row of padded input
            // start y = kernel row, so 2nd kernel row isn't multiplied by 1st img row, and 3rd kernel row isn't multiplied by 1st or 2nd img row
            int x = 0;
            for( ; x < x_max_stride; x+=STRIDE){ // x coordinate of padded input

                // load corresponding output block we'll sum with; all 3 input blocks sum to same output block
                __m128 outv_0 = _mm_loadu_ps(out + (y-j)*data_size_X + x+0);
                __m128 outv_4 = _mm_loadu_ps(out + (y-j)*data_size_X + x+4);

                // multiply

                // sum
                outv_0 = _mm_add_ps(_mm_mul_ps(kv_0, _mm_loadu_ps(padded + y*padded_width + x+0)), outv_0);
                outv_0 = _mm_add_ps(_mm_mul_ps(kv_1, _mm_loadu_ps(padded + y*padded_width + x+1)), outv_0);
                outv_0 = _mm_add_ps(_mm_mul_ps(kv_2, _mm_loadu_ps(padded + y*padded_width + x+2)), outv_0);
                outv_4 = _mm_add_ps(_mm_mul_ps(kv_0, _mm_loadu_ps(padded + y*padded_width + x+4)), outv_4);
                outv_4 = _mm_add_ps(_mm_mul_ps(kv_1, _mm_loadu_ps(padded + y*padded_width + x+5)), outv_4);
                outv_4 = _mm_add_ps(_mm_mul_ps(kv_2, _mm_loadu_ps(padded + y*padded_width + x+6)), outv_4);


                // store into output image array
                _mm_storeu_ps(out + (y-j)*data_size_X + x+0, outv_0);
                _mm_storeu_ps(out + (y-j)*data_size_X + x+4, outv_4);
            }

            // handle tail when (data_size_X % STRIDE) != 0
            if (needs_help) {
                for( ; x < data_size_X; x++) { 
                    float *out_index = out + (y-j)*data_size_X + x;
                    *out_index += kernel_unflipped[j*KERNX + 0] * padded[y*padded_width + x+0];
                    *out_index += kernel_unflipped[j*KERNX + 1] * padded[y*padded_width + x+1];
                    *out_index += kernel_unflipped[j*KERNX + 2] * padded[y*padded_width + x+2];

                }
            }
		}
	}

	return 1;
}
