#include <stdio.h>
#include "image.h"

int main(void)
{
    if (image_create("neobench.img"))
    {
        puts("Failed to create image.");
        return 1;
    }

    puts("Created neobench.img");

    return 0;
}
