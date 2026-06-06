#include <stdio.h>
#include <arch/zxn.h>

int main(void)
{
    /* Pipeline proof: compile C -> .nex and run it on the Next/CSpect. */
    printf("NetHack Next - toolchain OK\n");
    printf("Z88DK + zsdcc + appmake(.nex)\n");

    /* Simple loop so the program does not return immediately. */
    for (;;) {
    }
}
