#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "libnbfs.h"

#define SOURCE_IMAGE "../../images/test-verified.nbfs"
#define TEST_IMAGE   "../../images/test-libnbfs.nbfs"

static int fail(const char *msg)
{
fprintf(stderr, "FAIL: %s\n", msg);
return 1;
}

static int reset_test_image(void)
{
FILE *src;
FILE *dst;
unsigned char buffer[8192];
size_t n;

src = fopen(SOURCE_IMAGE, "rb");

if (!src)
{
    perror("fopen source image");
    return -1;
}

dst = fopen(TEST_IMAGE, "wb");

if (!dst)
{
    perror("fopen test image");
    fclose(src);
    return -1;
}

while ((n = fread(buffer, 1, sizeof(buffer), src)) > 0)
{
    if (fwrite(buffer, 1, n, dst) != n)
    {
        perror("write test image");
        fclose(dst);
        fclose(src);
        return -1;
    }
}

if (ferror(src))
{
    perror("read source image");
    fclose(dst);
    fclose(src);
    return -1;
}

fclose(dst);
fclose(src);

return 0;

}

int main(void)
{
nbfs_context_t *ctx;
uint64_t inode2;
uint64_t inode3;
uint64_t inode2_again;

printf("NBFS inode allocator test\n");
printf("==========================\n");

/*
 * Always start from a pristine filesystem image.
 */
if (reset_test_image() != 0)
    return fail("unable to reset test image");

ctx = nbfs_open(TEST_IMAGE);

if (!ctx)
    return fail("unable to open NBFS image");

/*
 * Allocate the first free inode.
 *
 * Root inode 1 already exists, so the allocator
 * should return inode 2.
 */
if (nbfs_allocate_inode(ctx, &inode2) != 0)
{
    nbfs_close(ctx);
    return fail("first inode allocation failed");
}

printf("PASS: allocated inode %llu\n",
       (unsigned long long)inode2);

if (inode2 != 2)
{
    nbfs_close(ctx);
    return fail("first allocated inode was not 2");
}

/*
 * Allocate another inode.
 */
if (nbfs_allocate_inode(ctx, &inode3) != 0)
{
    nbfs_close(ctx);
    return fail("second inode allocation failed");
}

printf("PASS: allocated inode %llu\n",
       (unsigned long long)inode3);

if (inode3 != 3)
{
    nbfs_close(ctx);
    return fail("second allocated inode was not 3");
}

/*
 * Free inode 2.
 */
if (nbfs_free_inode(ctx, inode2) != 0)
{
    nbfs_close(ctx);
    return fail("free inode 2 failed");
}

printf("PASS: freed inode %llu\n",
       (unsigned long long)inode2);

/*
 * The allocator should now reuse inode 2.
 */
if (nbfs_allocate_inode(ctx, &inode2_again) != 0)
{
    nbfs_close(ctx);
    return fail("re-allocation failed");
}

printf("PASS: reallocated inode %llu\n",
       (unsigned long long)inode2_again);

if (inode2_again != 2)
{
    nbfs_close(ctx);
    return fail("freed inode 2 was not reused");
}

if (nbfs_flush(ctx) != 0)
{
    nbfs_close(ctx);
    return fail("flush failed");
}

nbfs_close(ctx);

printf("\nNBFS inode allocator: OK\n");

return 0;

}
