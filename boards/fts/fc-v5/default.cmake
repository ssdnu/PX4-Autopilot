# PX4 board configuration for yourcompany_yourfc
# Generated from ArduPilot hwdef.dat for STM32F765/F767 custom FC.
# Baseline: PX4 FMUv5-class NuttX target.

px4_add_board(
	PLATFORM nuttx
	VENDOR fts
	MODEL fc-v5
	LABEL default
	TOOLCHAIN arm-none-eabi
	ARCHITECTURE cortex-m7
	ROMFSROOT px4fmu_common
	IO px4_io-v2_default
	SRCS
        src/board_identity.c
	DRIVERS
		adc/board_adc
		barometer/ms5611
		batt_smbus
		camera_capture
		camera_trigger
		differential_pressure
		distance_sensor
		dshot
		frsky_telemetry
		gps
		imu/bmi088
		imu/icm20689
		imu/icm20649
		magnetometer/ist8310
		mixer_module
		osd
		pca9685
		pwm_out
		px4io
		rc_input
		rpm
		safety_button
		tone_alarm
		uavcan
	MODULES
		airspeed_selector
		attitude_estimator_q
		battery_status
		commander
		dataman
		ekf2
		events
		flight_mode_manager
		fw_att_control
		fw_pos_control_l1
		gyro_calibration
		land_detector
		landing_target_estimator
		load_mon
		logger
		mavlink
		mc_att_control
		mc_hover_thrust_estimator
		mc_pos_control
		mc_rate_control
		navigator
		rc_update
		rover_pos_control
		sensors
		temperature_compensation
		uuv_att_control
		uuv_pos_control
		vtol_att_control
	SYSTEMCMDS
		bl_update
		dmesg
		dumpfile
		hardfault_log
		i2cdetect
		led_control
		mixer
		motor_ramp
		mtd
		nshterm
		param
		perf
		pwm
		reboot
		top
		topic_listener
		tune_control
		ver
	EXAMPLES
)
