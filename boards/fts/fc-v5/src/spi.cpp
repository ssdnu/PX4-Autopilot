/****************************************************************************
 *
 *   fc-v5 SPI bus description generated from the original ArduPilot hwdef.dat
 *
 *   Active SPI devices in hwdef:
 *     SPI1: ICM20689, ICM20649, BMI088 gyro, BMI088 accel
 *     SPI2: FRAM/RAMTRON
 *     SPI4: MS5611 barometer
 *
 ****************************************************************************/

#include <px4_arch/spi_hw_description.h>
#include <drivers/drv_sensor.h>
#include <nuttx/spi/spi.h>

constexpr px4_spi_bus_t px4_spi_buses[SPI_BUS_MAX_BUS_ITEMS] = {
	/* SPI1 - internal sensors
	 * hwdef pins:
	 *   PG11 SPI1_SCK, PA6 SPI1_MISO, PD7 SPI1_MOSI
	 * CS/DRDY:
	 *   ICM20689   CS PF2,  DRDY PB4
	 *   ICM20649   CS PF3,  DRDY PC5
	 *   BMI088_G   CS PF4,  DRDY PB14
	 *   BMI088_A   CS PG10, DRDY PB15
	 * Sensor power enable: PE3 VDD_3V3_SENSORS_EN
	 */
	initSPIBus(SPI::Bus::SPI1, {
		initSPIDevice(DRV_IMU_DEVTYPE_ICM20689, SPI::CS{GPIO::PortF, GPIO::Pin2},  SPI::DRDY{GPIO::PortB, GPIO::Pin4}),
		initSPIDevice(DRV_IMU_DEVTYPE_ICM20649, SPI::CS{GPIO::PortF, GPIO::Pin3},  SPI::DRDY{GPIO::PortC, GPIO::Pin5}),
		initSPIDevice(DRV_GYR_DEVTYPE_BMI088,   SPI::CS{GPIO::PortF, GPIO::Pin4},  SPI::DRDY{GPIO::PortB, GPIO::Pin14}),
		initSPIDevice(DRV_ACC_DEVTYPE_BMI088,   SPI::CS{GPIO::PortG, GPIO::Pin10}, SPI::DRDY{GPIO::PortB, GPIO::Pin15}),
	}, {GPIO::PortE, GPIO::Pin3}),

	/* SPI2 - FRAM/RAMTRON
	 * hwdef pins: PI1 SPI2_SCK, PI2 SPI2_MISO, PI3 SPI2_MOSI
	 * CS: PF5 FRAM_CS
	 */
	initSPIBus(SPI::Bus::SPI2, {
		initSPIDevice(SPIDEV_FLASH(0), SPI::CS{GPIO::PortF, GPIO::Pin5}),
	}),

	/* SPI4 - barometer
	 * hwdef pins: PE2 SPI4_SCK, PE13 SPI4_MISO, PE6 SPI4_MOSI
	 * CS: PF10 MS5611_CS
	 */
	initSPIBus(SPI::Bus::SPI4, {
		initSPIDevice(DRV_BARO_DEVTYPE_MS5611, SPI::CS{GPIO::PortF, GPIO::Pin10}),
	}),
};

static constexpr bool unused = validateSPIConfig(px4_spi_buses);
