#include "../../runtime/xv_bytes_equal.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

int main(void)
{
    assert(xv_bytes_equal(NULL,NULL,0));
    for(unsigned n=0;n<=257;n++) for(unsigned offset=0;offset<16;offset++) {
        /* Allocate exactly to the compared end so sanitizers catch tail reads. */
        size_t allocation=n+offset?n+offset:1;
        uint8_t *a=malloc(allocation),*b=malloc(allocation);assert(a&&b);
        for(unsigned i=0;i<allocation;i++)a[i]=b[i]=(uint8_t)(i*17+n);
        assert(xv_bytes_equal(a+offset,b+offset,n));
        if(n)for(unsigned k=0;k<n;k++) {
            b[offset+k]^=0x80;
            assert(!xv_bytes_equal(a+offset,b+offset,n));
            b[offset+k]^=0x80;
        }
        assert(xv_bytes_equal(a+offset,b+offset,n));
        free(a);free(b);
    }
    puts("PASS: exact cached-byte equality, all mismatch positions, unaligned prefixes and vector/tail boundaries (host fallback)");
}
