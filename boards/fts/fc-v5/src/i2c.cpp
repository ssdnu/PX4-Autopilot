/****************************************************************************
 *
 *   fc-v5 I2C bus description generated from the original ArduPilot hwdef.dat
 *
 *   hwdef I2C_ORDER: I2C3 I2C1 I2C2 I2C4
 *   Buses:
 *     I2C1: PB8/PB9
 *     I2C2: PF1/PF0
 *     I2C3: PH7/PH8 with pullups, treated as internal
 *     I2C4: PF14/PF15
 *
 ****************************************************************************/

#include <px4_arch/i2c_hw_description.h>

constexpr px4_i2c_bus_t px4_i2c_buses[I2C_BUS_MAX_BUS_ITEMS] = {
	/* Match ArduPilot hwdef I2C_ORDER: I2C3 I2C1 I2C2 I2C4 */
	initI2CBusInternal(3),
	initI2CBusExternal(1),
	initI2CBusExternal(2),
	initI2CBusExternal(4),
};
