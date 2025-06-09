#include <stdio.h>
#include "devicetree.h"

void demo_i2c_app_run(void) {
    printf("I2C demo running. Sensor is detected at address 0x%X\n",
            DT_N_S_i2c_40003000_S_sensor_76_REG_IDX_0_VAL_ADDRESS);
    // In Zephyr, we would use the devicetree.h API to query the address, which uses
    // preprocessor defines to resolve this:
    //       DT_REG_ADDR(DT_NODELABEL(sensor_76)));
    // which then resolves into something that like DT_N_S_i2c_40003000_S_sensor_76_P_reg.
}
