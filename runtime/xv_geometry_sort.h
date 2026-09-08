#ifndef XV_GEOMETRY_SORT_H
#define XV_GEOMETRY_SORT_H
#include <stdint.h>

/* Signed triangle numbers use Halo 3925's 0x53F60 ordering. */
static inline void xv_sort_sift(int32_t *a, unsigned root, unsigned n)
{
    int32_t value = a[root];
    while (root < n / 2) {
        unsigned child = root * 2 + 1;
        if (child + 1 < n && a[child] < a[child + 1]) child++;
        if (a[child] <= value) break;
        a[root] = a[child]; root = child;
    }
    a[root] = value;
}
static inline void xv_sort_triangles(int32_t *a, unsigned n)
{
    if (n < 2) return;
    for (unsigned i = n / 2; i; i--) xv_sort_sift(a, i - 1, n);
    for (unsigned end = n - 1; end; end--) {
        int32_t v = a[0]; a[0] = a[end]; a[end] = v;
        xv_sort_sift(a, 0, end);
    }
}
static inline void xv_merge_triangles(const int32_t *a, unsigned mid,
                                      unsigned n, int32_t *out)
{
    unsigned i = 0, j = mid, k = 0;
    while (i < mid && j < n) out[k++] = a[i] <= a[j] ? a[i++] : a[j++];
    while (i < mid) out[k++] = a[i++];
    while (j < n) out[k++] = a[j++];
}

#endif
