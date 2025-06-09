#include <stdio.h>
#include <autoconf.h>
#include "devicetree.h"

extern void demo_i2c_app_run(void);

int main(void) {
    printf("System booting...\n");

#ifdef CONFIG_DEMO_I2C_APP
    demo_i2c_app_run();
#endif

    return 0;
}
