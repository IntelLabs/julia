/*
 * Copyright (c) 2014 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#define DEF_C_ARRAY(ELEMENT_TYPE, TYPENAME) \
TYPENAME##_array inline new_##TYPENAME##_array_1d(ELEMENT_TYPE* _data, unsigned _N1) { \
	TYPENAME##_array a; \
	if (_data == NULL) a.data = (ELEMENT_TYPE*)malloc(sizeof(ELEMENT_TYPE)*_N1); \
	else a.data = _data; \
    a.num_dim = 1; \
	a.dims[0] = _N1; \
	return a; \
} \
TYPENAME##_array inline new_##TYPENAME##_array_2d(ELEMENT_TYPE* _data, unsigned _N1, unsigned _N2) { \
	TYPENAME##_array a; \
	if (_data == NULL) a.data = (ELEMENT_TYPE*)malloc(sizeof(ELEMENT_TYPE)*_N1*_N2); \
	else a.data = _data; \
    a.num_dim = 2; \
	a.dims[0] = _N1; \
	a.dims[1] = _N2; \
	return a; \
} \
TYPENAME##_array inline new_##TYPENAME##_array_3d(ELEMENT_TYPE* _data, unsigned _N1, unsigned _N2, unsigned _N3) { \
	TYPENAME##_array a; \
	if (_data == NULL) a.data = (ELEMENT_TYPE*)malloc(sizeof(ELEMENT_TYPE)*_N1*_N2*_N3); \
	else a.data = _data; \
    a.num_dim = 3; \
	a.dims[0] = _N1; \
	a.dims[1] = _N2; \
	a.dims[2] = _N3; \
	return a; \
} \
TYPENAME##_array inline new_##TYPENAME##_array_4d(ELEMENT_TYPE* _data, unsigned _N1, unsigned _N2, unsigned _N3, unsigned _N4) { \
	TYPENAME##_array a; \
	if (_data == NULL) a.data = (ELEMENT_TYPE*)malloc(sizeof(ELEMENT_TYPE)*_N1*_N2*_N3*_N4); \
	else a.data = _data; \
    a.num_dim = 4; \
	a.dims[0] = _N1; \
	a.dims[1] = _N2; \
	a.dims[2] = _N3; \
	a.dims[3] = _N4; \
	return a; \
} \
ELEMENT_TYPE inline sum_##TYPENAME##_array(TYPENAME##_array a) { \
    unsigned i; \
	ELEMENT_TYPE sum = 0; \
    ELEMENT_TYPE *cur = a.data; \
    unsigned len = ARRAYLEN(a); \
    for(i = 0; i < len; ++i) { \
        sum += *(cur++); \
    } \
    return sum; \
}\
ELEMENT_TYPE inline print_##TYPENAME##_array(TYPENAME##_array a) { \
    unsigned i; \
	ELEMENT_TYPE sum = 0; \
    ELEMENT_TYPE *cur = a.data; \
    unsigned len = ARRAYLEN(a); \
    for(i = 0; i < len; ++i) { \
		printf("%f ", *(cur++)); \
    } \
	printf("\n");\
}\
ELEMENT_TYPE inline fillex_##TYPENAME##_array(TYPENAME##_array a, TYPENAME x) { \
    unsigned i; \
	ELEMENT_TYPE sum = 0; \
    ELEMENT_TYPE *cur = a.data; \
    unsigned len = ARRAYLEN(a); \
    for(i = 0; i < len; ++i) { \
		*(cur++) = x; \
    } \
}\
void add(TYPENAME##_array& c, TYPENAME##_array& a, TYPENAME##_array& b) {\
    unsigned i; \
	ELEMENT_TYPE sum = 0; \
    ELEMENT_TYPE *cura = a.data; \
    ELEMENT_TYPE *curb = b.data; \
    ELEMENT_TYPE *curc = c.data; \
    unsigned len = ARRAYLEN(a); \
    assert(ARRAYLEN(b) == len); \
    assert(ARRAYLEN(c) == len); \
    for(i = 0; i < len; ++i) { \
		*(curc++) = *(cura++) + *(curb++); \
    } \
}\
void sub(TYPENAME##_array& c, TYPENAME##_array& a, TYPENAME##_array& b) {\
    unsigned i; \
	ELEMENT_TYPE sum = 0; \
    ELEMENT_TYPE *cura = a.data; \
    ELEMENT_TYPE *curb = b.data; \
    ELEMENT_TYPE *curc = c.data; \
    unsigned len = ARRAYLEN(a); \
    assert(ARRAYLEN(b) == len); \
    assert(ARRAYLEN(c) == len); \
    for(i = 0; i < len; ++i) { \
		*(curc++) = *(cura++) - *(curb++); \
    } \
}\
void mul(TYPENAME##_array& c, TYPENAME##_array& a, TYPENAME##_array& b) {\
    unsigned i; \
	ELEMENT_TYPE sum = 0; \
    ELEMENT_TYPE *cura = a.data; \
    ELEMENT_TYPE *curb = b.data; \
    ELEMENT_TYPE *curc = c.data; \
    unsigned len = ARRAYLEN(a); \
    assert(ARRAYLEN(b) == len); \
    assert(ARRAYLEN(c) == len); \
    for(i = 0; i < len; ++i) { \
		*(curc++) = *(cura++) * *(curb++); \
    } \
}

int64_t inline sub(int64_t a, int64_t b) 
{
	return a - b;
}

// Int operations with overflow check. 
// TODO: implement overlflow check.
uint64_t inline checked_uadd(uint64_t a, uint64_t b) 
{
	return a + b;
}

uint64_t inline checked_usub(uint64_t a, uint64_t b) 
{
	return a - b;
}

uint64_t inline checked_umul(uint64_t a, uint64_t b) 
{
	return a * b;
}

int64_t inline checked_sadd(int64_t a, int64_t b) 
{
	return a + b;
}

int64_t inline checked_ssub(int64_t a, int64_t b) 
{
	return a - b;
}

int64_t inline checked_smul(int64_t a, int64_t b) 
{
	return a * b;
}

#define ALLOC alloc_if(1) free_if(0) 
#define FREE alloc_if(0) free_if(1) 
#define REUSE alloc_if(0) free_if(0) 
