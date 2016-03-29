/*
 * mixed_intersection.c
 *
 */

#include "mixed_intersection.h"
#include "bitset_util.h"
#include "array_util.h"
#include "convert.h"

/* Compute the intersection of src_1 and src_2 and write the result to
 * dst.  */
void array_bitset_container_intersection(const array_container_t *src_1,
                                         const bitset_container_t *src_2,
                                         array_container_t *dst) {
    if (dst->capacity < src_1->cardinality)
        array_container_grow(dst, src_1->cardinality, INT32_MAX, false);
    int32_t newcard = 0;  // dst could be src_1
    const int32_t origcard = src_1->cardinality;
    for (int i = 0; i < origcard; ++i) {
        // could probably be vectorized
        uint16_t key = src_1->array[i];
        // next bit could be branchless
        if (bitset_container_contains(src_2, key)) {
            dst->array[newcard++] = key;
        }
    }
    dst->cardinality = newcard;
}

/* Compute the intersection of src_1 and src_2 and write the result to
 * dst. It is allowed for dst to be equal to src_1. We assume that dst is a
 * valid container. */
void array_run_container_intersection(const array_container_t *src_1,
                                      const run_container_t *src_2,
                                      array_container_t *dst) {
    if (dst->capacity < src_1->cardinality)
        array_container_grow(dst, src_1->cardinality, INT32_MAX, false);
    if (src_2->n_runs) {
        return;
    }
    int32_t rlepos = 0;
    int32_t arraypos = 0;
    rle16_t rle = src_2->runs[rlepos];
    int32_t newcard = 0;
    while (arraypos < src_1->cardinality) {
        uint16_t arrayval = src_1->array[arraypos];
        while (rle.value + rle.length <
               arrayval) {  // this will frequently be false
            ++rlepos;
            if (rlepos == src_2->n_runs) {
                dst->cardinality = newcard;
                return;  // we are done
            }
            rle = src_2->runs[rlepos];
        }
        if (rle.value > arrayval) {
            arraypos = advanceUntil(src_1->array, arraypos, src_1->cardinality,
                                    rle.value);
        } else {
            dst->array[newcard] = arrayval;
            newcard++;
            arraypos++;
        }
    }
    dst->cardinality = newcard;
}

/* Compute the intersection of src_1 and src_2 and write the result to
 * *dst. If the result is true then the result is a bitset_container_t
 * otherwise is a array_container_t.  */
bool run_bitset_container_intersection(const run_container_t *src_1,
                                       const bitset_container_t *src_2,
                                       void **dst) {
    int32_t card = run_container_cardinality(src_1);
    if (card <= DEFAULT_MAX_SIZE) {
        // result can only be an array (assuming that we never make a
        // RunContainer)
        if (card > src_2->cardinality) {
            card = src_2->cardinality;
        }
        array_container_t *answer = array_container_create_given_capacity(card);
        *dst = answer;
        if (*dst == NULL) {
            return false;
        }
        for (int32_t rlepos = 0; rlepos < src_1->n_runs; ++rlepos) {
            rle16_t rle = src_1->runs[rlepos];
            for (uint16_t runValue = rle.value;
                 runValue <= rle.value + rle.length; ++runValue) {
                if (bitset_container_contains(src_2, runValue)) {
                    answer->array[answer->cardinality++] = runValue;
                }
            }
        }
        return false;
    }
    if (*dst == src_2) {  // we attempt in-place
        bitset_container_t *answer = (bitset_container_t *)*dst;
        uint16_t start = 0;
        for (int32_t rlepos = 0; rlepos < src_1->n_runs; ++rlepos) {
            rle16_t rle = src_1->runs[rlepos];
            uint16_t end = rle.value;
            bitset_reset_range(src_2->array, start, end);

            start = end + rle.length + 1;
        }
        bitset_reset_range(src_2->array, start, UINT32_C(1) << 16);
        answer->cardinality = bitset_container_compute_cardinality(answer);
        if (src_2->cardinality > DEFAULT_MAX_SIZE) {
            return true;
        } else {
            array_container_t *newanswer = array_container_from_bitset(src_2);
            if (newanswer == NULL) {
                *dst = NULL;
                return false;
            }
            *dst = newanswer;
            return false;
        }
    } else {  // no inplace
        // we expect the answer to be a bitmap (if we are lucky)
        bitset_container_t *answer = bitset_container_clone(src_2);

        *dst = answer;
        if (answer == NULL) {
            return true;
        }
        uint16_t start = 0;
        for (int32_t rlepos = 0; rlepos < src_1->n_runs; ++rlepos) {
            rle16_t rle = src_1->runs[rlepos];
            uint16_t end = rle.value;
            bitset_reset_range(answer->array, start, end);

            start = end + rle.length + 1;
        }
        bitset_reset_range(answer->array, start, UINT32_C(1) << 16);
        answer->cardinality = bitset_container_compute_cardinality(answer);
        if (answer->cardinality > DEFAULT_MAX_SIZE) {
            return true;
        } else {
            array_container_t *newanswer = array_container_from_bitset(answer);
            bitset_container_free(*dst);
            if (newanswer == NULL) {
                *dst = NULL;
                return false;
            }
            *dst = newanswer;
            return false;
        }
    }
}

/*
 * Compute the intersection between src_1 and src_2 and write the result
 * to *dst. If the return function is true, the result is a bitset_container_t
 * otherwise is a array_container_t.
 */
bool bitset_bitset_container_intersection(const bitset_container_t *src_1,
                                          const bitset_container_t *src_2,
                                          void **dst) {
    const int newCardinality = bitset_container_and_justcard(src_1, src_2);
    if (newCardinality > DEFAULT_MAX_SIZE) {
        *dst = bitset_container_create();
        if (*dst != NULL) {
            bitset_container_and_nocard(src_1, src_2, *dst);
            ((bitset_container_t *)*dst)->cardinality = newCardinality;
        }
        return true;  // it is a bitset
    }
    *dst = array_container_create_given_capacity(newCardinality);
    if (*dst != NULL) {
        ((array_container_t *)*dst)->cardinality = newCardinality;
        bitset_extract_intersection_setbits_uint16(
            ((bitset_container_t *)src_1)->array,
            ((bitset_container_t *)src_2)->array,
            BITSET_CONTAINER_SIZE_IN_WORDS, ((array_container_t *)*dst)->array,
            0);
    }
    return false;  // not a bitset
}

bool bitset_bitset_container_intersection_inplace(
    bitset_container_t *src_1, const bitset_container_t *src_2, void **dst) {
    const int newCardinality = bitset_container_and_justcard(src_1, src_2);
    if (newCardinality > DEFAULT_MAX_SIZE) {
        *dst = src_1;
        bitset_container_and_nocard(src_1, src_2, src_1);
        ((bitset_container_t *)*dst)->cardinality = newCardinality;
        return true;  // it is a bitset
    }
    *dst = array_container_create_given_capacity(newCardinality);
    if (*dst != NULL) {
        ((array_container_t *)*dst)->cardinality = newCardinality;
        bitset_extract_intersection_setbits_uint16(
            ((bitset_container_t *)src_1)->array,
            ((bitset_container_t *)src_2)->array,
            BITSET_CONTAINER_SIZE_IN_WORDS, ((array_container_t *)*dst)->array,
            0);
    }
    bitset_container_free(src_1);
    return false;  // not a bitset
}
