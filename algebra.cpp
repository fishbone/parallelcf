#include "algebra.h"
#include "util.h"
#include <boost/numeric/ublas/vector.hpp>
#include <boost/numeric/ublas/matrix.hpp>
#include <immintrin.h>
#include <string.h>
#include "inverse.h"

int ompthread = 24;
void set_algebra_ompthread(int t){
    ompthread = t;
}
static __inline void __attribute__((__always_inline__))
_mm256_storeu2_m128(float *addr_hi, float *addr_lo, __m256 a)
{
    __m128 v128;
    
    v128 = _mm256_castps256_ps128(a);
    _mm_store_ps(addr_lo, v128);
    v128 = _mm256_extractf128_ps(a, 1);
    _mm_store_ps(addr_hi, v128);
}

const int WIDTH = 8;
inline float vec_sum(__m256 sum){
    __m128 h = _mm256_castps256_ps128(sum);
    __m128 l = _mm256_extractf128_ps(sum, 1);
    __m128 s = l + h;
    return s[0] + s[1] + s[2] + s[3];
    
}

inline __m256i gen_mask(int v, int m){
    // Set the value
    __m256i value = _mm256_set_epi32(v + 7  , v + 6, v + 5, v + 4,
                                     v + 3  , v + 2, v + 1, v);
    // Set the max value
    __m256i max_value = _mm256_set1_epi32(m);
    // Get the mask by comparing
    __m256i mask = _mm256_cmpgt_epi32(max_value, value);
    
    return mask;
}

inline __m256 mul_vec(const float *arr1, const float *arr2, __m256 sum, int pos, int size){
    if(pos > size)
        return _mm256_setzero_ps();
    if(pos + WIDTH > size){
        __m256i mask = gen_mask(pos, size);
        __m256 lsh = _mm256_maskload_ps(arr1, mask);
        __m256 rsh = _mm256_maskload_ps(arr2, mask);
        return  _mm256_fmadd_ps(lsh, rsh, sum);
    }else {
        __m256 lsh = _mm256_load_ps(arr1);
        __m256 rsh = _mm256_load_ps(arr2);
        return  _mm256_fmadd_ps(lsh, rsh, sum);
    }
}

float array_diff_norm2(const float *arr1, const float *arr2, int size){
    int i = 0;
    __m256 lsh, rsh, r;
    __m256i mask;
    __m256 sum = _mm256_setzero_ps();
    while(i < size - WIDTH){
        lsh = _mm256_load_ps(arr1 + i);
        rsh = _mm256_load_ps(arr2 + i);
        r = lsh - rsh;
        sum += r * r;
        i += WIDTH;
    }
    
    mask = gen_mask(i, size);
    lsh = _mm256_maskload_ps(arr1 + i, mask);
    rsh = _mm256_maskload_ps(arr2 + i, mask);
    r = lsh - rsh;
    sum += r * r;
    i += WIDTH;
    return vec_sum(sum);
}

void array_weighted_add(const float *arr1, float w1, const float *arr2, float w2, float *out, int size){
    int i = 0;
    __m256 lsh, rsh, r;
    __m256i mask;
    __m256 sum = _mm256_setzero_ps();
    __m256 mw1 = _mm256_set1_ps(w1);
    __m256 mw2 = _mm256_set1_ps(w2);
    
    while(i < size - WIDTH){
        lsh = mw1 * _mm256_load_ps(arr1 + i);
        rsh = mw2 * _mm256_load_ps(arr2 + i);
        r = lsh + rsh;
        _mm256_store_ps(out + i, r);
        i += WIDTH;
    }
    mask = gen_mask(i, size);
    lsh = _mm256_maskload_ps(arr1 + i, mask);
    rsh = _mm256_maskload_ps(arr2 + i, mask);

    r = mw1 * lsh + mw2 * rsh;
    
    _mm256_maskstore_ps(out + i, mask, r);
}

float array_dot(const float *arr1, const float *arr2, int size){
    __m256 sum = _mm256_setzero_ps();
    register const float *rarr1 = arr1;
    register const float *rarr2 = arr2;

    for(int i = 0; i < size; i += WIDTH){
        sum = mul_vec(rarr1, rarr2, sum, i, size);
        rarr1 += WIDTH;
        rarr2 += WIDTH;
    }
    return vec_sum(sum);
}


inline void matrix_transpose8x8_simd(__m256 *a, __m256 *b){
    b[0] = _mm256_unpacklo_ps(a[0], a[1]);
    b[1] = _mm256_unpackhi_ps(a[0], a[1]);
    b[2] = _mm256_unpacklo_ps(a[2], a[3]);
    b[3] = _mm256_unpackhi_ps(a[2], a[3]);
    b[4] = _mm256_unpacklo_ps(a[4], a[5]);
    b[5] = _mm256_unpackhi_ps(a[4], a[5]);
    b[6] = _mm256_unpacklo_ps(a[6], a[7]);
    b[7] = _mm256_unpackhi_ps(a[6], a[7]);
    a[0] = _mm256_shuffle_ps(b[0], b[2], _MM_SHUFFLE(1,0,1,0));
    a[1] = _mm256_shuffle_ps(b[0], b[2], _MM_SHUFFLE(3,2,3,2));
    a[2] = _mm256_shuffle_ps(b[1], b[3], _MM_SHUFFLE(1,0,1,0));
    a[3] = _mm256_shuffle_ps(b[1], b[3], _MM_SHUFFLE(3,2,3,2));
    a[4] = _mm256_shuffle_ps(b[4], b[6], _MM_SHUFFLE(1,0,1,0));
    a[5] = _mm256_shuffle_ps(b[4], b[6], _MM_SHUFFLE(3,2,3,2));
    a[6] = _mm256_shuffle_ps(b[5], b[7], _MM_SHUFFLE(1,0,1,0));
    a[7] = _mm256_shuffle_ps(b[5], b[7], _MM_SHUFFLE(3,2,3,2));

    b[0] = _mm256_permute2f128_ps(a[0], a[4], 0x20);
    b[1] = _mm256_permute2f128_ps(a[1], a[5], 0x20);
    b[2] = _mm256_permute2f128_ps(a[2], a[6], 0x20);
    b[3] = _mm256_permute2f128_ps(a[3], a[7], 0x20);
    b[4] = _mm256_permute2f128_ps(a[0], a[4], 0x31);
    b[5] = _mm256_permute2f128_ps(a[1], a[5], 0x31);
    b[6] = _mm256_permute2f128_ps(a[2], a[6], 0x31);
    b[7] = _mm256_permute2f128_ps(a[3], a[7], 0x31);
}

void matrix_transpose(float *mat1, int row, int col, float *out){
    int align_row = align_32(row);
    int align_col = align_32(col);
    __m256 a[WIDTH];
    __m256 b[WIDTH];
    __m256i mask;
    for(int r = 0; r < row; r += WIDTH){
        for(int c = 0; c < col; c += WIDTH){
            for(int i = 0; i < WIDTH; ++i){
                a[i] = _mm256_load_ps(mat1 + (r + i) * align_col + c);
            }
            
            matrix_transpose8x8_simd(a, b);
            int o_r = c;
            int o_c = r;

            for(int i = 0; i < WIDTH; ++i){
                _mm256_store_ps(out + (o_r + i) * align_row + o_c, b[i]);
            }
        }
    }
}

void matrix_slice(float *mat, int col, const int *offset, int num, float *out){
    int out_align = align_32(col);
    for(int i = 0; i < num; ++i){
        memcpy(out + i * out_align, mat + offset[i] * out_align, sizeof(float) * col);
    }
}
inline __m256 hsum(__m256 *data){
    /*
    data[0] = _mm256_unpacklo_ps(data[0], data[4]) + _mm256_unpackhi_ps(data[0], data[4]);
    data[1] = _mm256_unpacklo_ps(data[1], data[5]) + _mm256_unpackhi_ps(data[1], data[5]);
    data[2] = _mm256_unpacklo_ps(data[2], data[6]) + _mm256_unpackhi_ps(data[2], data[6]);
    data[3] = _mm256_unpacklo_ps(data[3], data[7]) + _mm256_unpackhi_ps(data[3], data[7]);

    data[0] = _mm256_unpacklo_ps(data[0], data[2]) + _mm256_unpackhi_ps(data[0], data[2]);
    data[1] = _mm256_unpacklo_ps(data[1], data[3]) + _mm256_unpackhi_ps(data[1], data[3]);

    data[2] = _mm256_unpacklo_ps(data[0], data[1]);
    data[3] = _mm256_unpackhi_ps(data[0], data[1]);

    // a b c d a b c d
    // e f g h e f g h
        
    data[0] = _mm256_permute2f128_ps(data[2], data[3], 0x20);
    data[1] = _mm256_permute2f128_ps(data[2], data[3], 0x31);
    data[2] = data[0] + data[1];
    */
    data[0] =  _mm256_hadd_ps(data[0], data[1]);
    data[1] =  _mm256_hadd_ps(data[2], data[3]);
    data[2] =  _mm256_hadd_ps(data[4], data[5]);
    data[3] =  _mm256_hadd_ps(data[6], data[7]);

    data[0] =  _mm256_hadd_ps(data[0], data[1]);
    data[1] =  _mm256_hadd_ps(data[2], data[3]);

    data[2] = _mm256_permute2f128_ps(data[0], data[1], 0x20);
    data[3] = _mm256_permute2f128_ps(data[0], data[1], 0x31);
    return data[2] + data[3];
}
// a = 4x8
// b = 4x8
inline void matrix_prod_transpose4x8(__m256 *a, __m256 *b, __m256 *out){
    __m256 c[WIDTH];
    c[0] = a[0] * b[0];
    c[1] = a[0] * b[1];
    c[2] = a[0] * b[2];
    c[3] = a[0] * b[3];
    c[4] = a[1] * b[0];
    c[5] = a[1] * b[1];
    c[6] = a[1] * b[2];
    c[7] = a[1] * b[3];
    out[0] = hsum(c);
    
    c[0] = a[2] * b[0];
    c[1] = a[2] * b[1];
    c[2] = a[2] * b[2];
    c[3] = a[2] * b[3];
    c[4] = a[3] * b[0];
    c[5] = a[3] * b[1];
    c[6] = a[3] * b[2];
    c[7] = a[3] * b[3];
    out[1] = hsum(c);
}
inline __m256 loadMatrix(float *m, int row, int col, int crow, int ccol){
    if(crow >= row){
        return _mm256_setzero_ps();
    }
    if(ccol + WIDTH >= col){
        __m256i mask = gen_mask(ccol, col);
        return _mm256_maskload_ps(m, mask);
    }else{
        return _mm256_load_ps(m);
    }
}

inline void matrix_prod_transpose8x8(float *A, float *B, int row, int col,
                                     int arow, int brow,
                                     float *out){
    int align_row = align_32(row);
    int align_col = align_32(col);
    float tmpRes[8*8] __attribute__((aligned (32)));
    __m256 a[8];
    __m256 b[8];
    __m256 c[8];
    __m256 d[8];
    for(int i = 0; i < 8; ++i)
        d[i] = _mm256_setzero_ps();

    for(int k = 0; k < col; k += WIDTH){
        for(int i = 0; i < 8; ++i){
            a[i] = loadMatrix(A + align_col * (i + arow) + k,
                              row, col, arow + i, k);
            b[i] = loadMatrix(B + align_col * (i + brow) + k,
                              row, col, brow + i, k);
        }
        for(int i = 0; i < 8; ++i)
            c[i] = _mm256_setzero_ps();
    
        for(int i = 0; i < 8; ++i){
            for(int j = 0; j < 8; ++j){
                c[j] = a[i] * b[j];
            }
            __m256 tmp1 = hsum(c);
            d[i] += tmp1;
        }
    }
    if(brow + WIDTH >= row){
        __m256i mask = gen_mask(brow, row);
        for(int i = 0; i < 8; ++i){
            _mm256_maskstore_ps(out + align_row * (arow + i) + brow, mask, d[i]);
        }
    }else {
        for(int i = 0; i < 8; ++i){
            _mm256_store_ps(out + align_row * (arow + i) + brow, d[i]);
        }
    }

    if(arow != brow){
        matrix_transpose8x8_simd(d, a);
        int x = brow;
        int y = arow;
        if(y + WIDTH >= row){
            __m256i mask = gen_mask(brow, row);
            for(int i = 0; i < 8; ++i){
                _mm256_maskstore_ps(out + align_row * (x + i) + y, mask, a[i]);
            }
        }else{
            for(int i = 0; i < 8; ++i){
                _mm256_store_ps(out + align_row * (x + i) + y, a[i]);
            }
        }
    }
}

void matrix_prod_transpose(float *mat, int row, int col, float *out){
    int align_row = align_32(row);
    int xi = row / WIDTH;
    if(row % WIDTH != 0)
        ++xi;
#pragma omp parallel for schedule(dynamic) num_threads(ompthread)
    for(int l = 0; l < xi * xi; ++l){
        int i = l / xi;
        int j = l % xi;
        if(j>i)
            continue;
        matrix_prod_transpose8x8(mat, mat, row, col, i * WIDTH, j * WIDTH, out);
    }
}

void matrix_add_eye(float *mat, int size, float v){
    int size_align = align_32(size);
    for(int i = 0; i < size; ++i)
        mat[i * size_align + i] += v;
}

void matrix_sub_prod_vector(const float *sub, const float *mat, int row, int col, const float *vec, float *out){
    int col_align = align_32(col);

    for(int i = 0; i < row; ++i){
        float p = array_dot(mat + col_align * i, vec, col);
        out[i] = sub[i] - p;
    }
}

void matrix_prod_vector(const float *mat, int row, int col, const float *vec, float *out){
    int col_align = align_32(col);
    __m256 data[WIDTH];
    __m256 trans[WIDTH];
    
    __m256 col_data;
    for(int i = 0; i < row; i += WIDTH){
        data[0] = data[1] = data[2] = data[3] = data[4] = data[5] = data[6] = data[7] = _mm256_setzero_ps();
        int j = 0;
        for(j = 0; j < col - WIDTH; j += WIDTH){
            col_data = _mm256_load_ps(vec + j);
            
            data[0] = _mm256_fmadd_ps(_mm256_load_ps(mat + col_align * i + j), col_data, data[0]);
            data[4] = _mm256_fmadd_ps(_mm256_load_ps(mat + col_align * (i+4) + j), col_data, data[4]);
            data[1] = _mm256_fmadd_ps(_mm256_load_ps(mat + col_align * (i+1) + j), col_data, data[1]);
            data[5] = _mm256_fmadd_ps(_mm256_load_ps(mat + col_align * (i+5) + j), col_data, data[5]);
            data[2] = _mm256_fmadd_ps(_mm256_load_ps(mat + col_align * (i+2) + j), col_data, data[2]);
            data[6] = _mm256_fmadd_ps(_mm256_load_ps(mat + col_align * (i+6) + j), col_data, data[6]);
            data[3] = _mm256_fmadd_ps(_mm256_load_ps(mat + col_align * (i+3) + j), col_data, data[3]);
            data[7] = _mm256_fmadd_ps(_mm256_load_ps(mat + col_align * (i+7) + j), col_data, data[7]);
        }


        __m256i mask = gen_mask(j, col);
        col_data = _mm256_maskload_ps(vec + j, mask);
        data[0] = _mm256_fmadd_ps(_mm256_maskload_ps(mat + col_align * i + j, mask), col_data, data[0]);
        data[4] = _mm256_fmadd_ps(_mm256_maskload_ps(mat + col_align * (i+4) + j, mask), col_data, data[4]);
        data[1] = _mm256_fmadd_ps(_mm256_maskload_ps(mat + col_align * (i+1) + j, mask), col_data, data[1]);
        data[5] = _mm256_fmadd_ps(_mm256_maskload_ps(mat + col_align * (i+5) + j, mask), col_data, data[5]);
        data[2] = _mm256_fmadd_ps(_mm256_maskload_ps(mat + col_align * (i+2) + j, mask), col_data, data[2]);
        data[6] = _mm256_fmadd_ps(_mm256_maskload_ps(mat + col_align * (i+6) + j, mask), col_data, data[6]);
        data[3] = _mm256_fmadd_ps(_mm256_maskload_ps(mat + col_align * (i+3) + j, mask), col_data, data[3]);
        data[7] = _mm256_fmadd_ps(_mm256_maskload_ps(mat + col_align * (i+7) + j, mask), col_data, data[7]);
        /*
        data[0] = _mm256_unpacklo_ps(data[0], data[4]) + _mm256_unpackhi_ps(data[0], data[4]);
        data[1] = _mm256_unpacklo_ps(data[1], data[5]) + _mm256_unpackhi_ps(data[1], data[5]);
        data[2] = _mm256_unpacklo_ps(data[2], data[6]) + _mm256_unpackhi_ps(data[2], data[6]);
        data[3] = _mm256_unpacklo_ps(data[3], data[7]) + _mm256_unpackhi_ps(data[3], data[7]);

        data[0] = _mm256_unpacklo_ps(data[0], data[2]) + _mm256_unpackhi_ps(data[0], data[2]);
        data[1] = _mm256_unpacklo_ps(data[1], data[3]) + _mm256_unpackhi_ps(data[1], data[3]);

        data[2] = _mm256_unpacklo_ps(data[0], data[1]);
        data[3] = _mm256_unpackhi_ps(data[0], data[1]);

        // a b c d a b c d
        // e f g h e f g h
        
        data[0] = _mm256_permute2f128_ps(data[2], data[3], 0x20);
        data[1] = _mm256_permute2f128_ps(data[2], data[3], 0x31);
        data[2] = data[0] + data[1];
        */
        _mm256_store_ps(out + i, hsum(data));
    }
}

void matrix_inverse(float *mat, int size, float *out){
    int size_align = align_32(size);
    boost::numeric::ublas::matrix<float> Ai(size, size);
    for(int i = 0; i != size; ++i){
        for(int j = 0; j != size; ++j){
            Ai(i, j) = mat[i * size_align + j];
        }
    }
    
    boost::numeric::ublas::matrix<float> invAi(size, size);
    invertMatrix(Ai, invAi);
    
    for(int i = 0; i != size; ++i){
        for(int j = 0; j != size; ++j){
            out[i * size_align + j] = invAi(i, j);
        }
    }
}

void solve_equation(const float *A, float *x, const float *b, int size, float eps){
    int align_size = align_32(size);
    float *r = align_malloc<float>(align_size);
    matrix_sub_prod_vector(b, A, size, size, x, r);
    float *p = align_malloc<float>(align_size);
    memcpy(p, r, sizeof(float) * size);
    float *tmp = align_malloc<float>(align_size);
    float rrt = array_dot(r, r, size);

    while(rrt > eps){
        matrix_prod_vector(A, size, size, p, tmp);
        float ptap = array_dot(p, tmp, size);
        float alpha = rrt / ptap;
        array_weighted_add(x, 1.0, p, alpha, x, size);
        array_weighted_add(r, 1.0, tmp, -alpha, r, size);
        float new_rrt = array_dot(r, r, size);
        float beta = new_rrt / rrt;
        array_weighted_add(r, 1.0, p, beta, p, size);
        rrt = new_rrt;
    }
    free(r);
    free(p);
    free(tmp);
}
