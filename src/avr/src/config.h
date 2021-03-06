/******************************************************************************\

                This file is part of the Buildbotics firmware.

                  Copyright (c) 2015 - 2017 Buildbotics LLC
                            All rights reserved.

     This file ("the software") is free software: you can redistribute it
     and/or modify it under the terms of the GNU General Public License,
      version 2 as published by the Free Software Foundation. You should
      have received a copy of the GNU General Public License, version 2
     along with the software. If not, see <http://www.gnu.org/licenses/>.

     The software is distributed in the hope that it will be useful, but
          WITHOUT ANY WARRANTY; without even the implied warranty of
      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
               Lesser General Public License for more details.

       You should have received a copy of the GNU Lesser General Public
                License along with the software.  If not, see
                       <http://www.gnu.org/licenses/>.

                For information regarding this software email:
                  "Joseph Coffland" <joseph@buildbotics.com>

\******************************************************************************/

#pragma once

#include "pins.h"

#ifdef __AVR__
#include <avr/interrupt.h>
#endif


// Pins
enum {
  STALL_X_PIN = PORT_A << 3,
  STALL_Y_PIN,
  STALL_Z_PIN,
  STALL_A_PIN,
  TOOL_DIR_PIN,
  TOOL_ENABLE_PIN,
  ANALOG_PIN,
  PROBE_PIN,

  MIN_X_PIN = PORT_B << 3,
  MAX_X_PIN,
  MIN_A_PIN,
  MAX_A_PIN,
  MIN_Y_PIN,
  MAX_Y_PIN,
  MIN_Z_PIN,
  MAX_Z_PIN,

  SDA_PIN = PORT_C << 3,
  SCL_PIN,
  SERIAL_RX_PIN,
  SERIAL_TX_PIN,
  SERIAL_CTS_PIN,
  SPI_CLK_PIN,
  SPI_MISO_PIN,
  SPI_MOSI_PIN,

  STEP_X_PIN = PORT_D << 3,
  SPI_CS_X_PIN,
  SPI_CS_A_PIN,
  SPI_CS_Z_PIN,
  SPIN_PWM_PIN,
  SWITCH_1_PIN,
  RS485_RO_PIN,
  RS485_DI_PIN,

  STEP_Y_PIN = PORT_E << 3,
  SPI_CS_Y_PIN,
  DIR_X_PIN,
  DIR_Y_PIN,
  STEP_A_PIN,
  SWITCH_2_PIN,
  DIR_Z_PIN,
  DIR_A_PIN,

  STEP_Z_PIN = PORT_F << 3,
  RS485_RW_PIN,
  FAULT_PIN,
  ESTOP_PIN,
  MOTOR_FAULT_PIN,
  MOTOR_ENABLE_PIN,
  NC_0_PIN,
  NC_1_PIN,
};

#define SPI_SS_PIN SERIAL_CTS_PIN // Needed for SPI configuration


#define AXES                     6 // number of axes
#define MOTORS                   4 // number of motors on the board
#define COORDS                   6 // number of supported coordinate systems
#define SWITCHES                10 // number of supported switches
#define OUTS                     5 // number of supported pin outputs


// Switch settings.  See switch.c
#define SWITCH_INTLVL            PORT_INT0LVL_MED_gc


// Motor ISRs
#define STALL_ISR_vect PORTA_INT1_vect
#define FAULT_ISR_vect PORTF_INT1_vect


/* Interrupt usage:
 *
 *    HI    Stepper timers                       (set in stepper.h)
 *    LO    Segment execution SW interrupt       (set in stepper.h)
 *   MED    GPIO1 switch port                    (set in gpio.h)
 *   MED    Serial RX                            (set in usart.c)
 *   MED    Serial TX                            (set in usart.c) (* see note)
 *    LO    Real time clock interrupt            (set in xmega_rtc.h)
 *
 *    (*) The TX cannot run at LO level or exception reports and other prints
 *        called from a LO interrupt (as in prep_line()) will kill the system
 *        in a permanent loop call in usart_putc() (usart.c).
 */

// Timer assignments - see specific modules for details
// TCC1 free
#define TIMER_STEP             TCC0 // Step timer    (see stepper.h)
#define TIMER_PWM              TCD1 // PWM timer     (see pwm_spindle.c)

#define M1_TIMER               TCD0
#define M2_TIMER               TCE0
#define M3_TIMER               TCF0
#define M4_TIMER               TCE1

#define M1_DMA_CH              DMA.CH0
#define M2_DMA_CH              DMA.CH1
#define M3_DMA_CH              DMA.CH2
#define M4_DMA_CH              DMA.CH3

#define M1_DMA_TRIGGER         DMA_CH_TRIGSRC_TCD0_CCA_gc
#define M2_DMA_TRIGGER         DMA_CH_TRIGSRC_TCE0_CCA_gc
#define M3_DMA_TRIGGER         DMA_CH_TRIGSRC_TCF0_CCA_gc
#define M4_DMA_TRIGGER         DMA_CH_TRIGSRC_TCE1_CCA_gc


// Timer setup for stepper and dwells
#define STEP_TIMER_DISABLE     0
#define STEP_TIMER_ENABLE      TC_CLKSEL_DIV4_gc
#define STEP_TIMER_DIV         4
#define STEP_TIMER_FREQ        (F_CPU / STEP_TIMER_DIV)
#define STEP_TIMER_POLL        ((uint16_t)(STEP_TIMER_FREQ * 0.001))
#define STEP_TIMER_WGMODE      TC_WGMODE_NORMAL_gc // count to TOP & rollover
#define STEP_TIMER_ISR         TCC0_OVF_vect
#define STEP_TIMER_INTLVL      TC_OVFINTLVL_HI_gc
#define STEP_LOW_LEVEL_ISR     ADCB_CH0_vect

#define SEGMENT_USEC           5000.0 // segment time
#define SEGMENT_SEC            (SEGMENT_USEC / 1000000.0)
#define SEGMENT_TIME           (SEGMENT_SEC / 60.0)
#define SEGMENT_CLOCKS         ((uint24_t)(F_CPU * SEGMENT_SEC))
#define SEGMENT_PERIOD         ((uint16_t)(STEP_TIMER_FREQ * SEGMENT_SEC))


// DRV8711 settings
#if 0 // Doug's settings
#define DRV8711_OFF   48
#define DRV8711_BLANK (0x80 | DRV8711_BLANK_ABT_bm)
#define DRV8711_DECAY (DRV8711_DECAY_DECMOD_AUTO_OPT | 6)

#else
#define DRV8711_OFF   12
#define DRV8711_BLANK (0x32 | DRV8711_BLANK_ABT_bm)
#define DRV8711_DECAY (DRV8711_DECAY_DECMOD_MIXED | 16)
#endif

#define DRV8711_STALL (DRV8711_STALL_SDCNT_2 | DRV8711_STALL_VDIV_4 | 200)
#define DRV8711_DRIVE (DRV8711_DRIVE_IDRIVEP_50 |                       \
                       DRV8711_DRIVE_IDRIVEN_100 | DRV8711_DRIVE_TDRIVEP_250 | \
                       DRV8711_DRIVE_TDRIVEN_250 | DRV8711_DRIVE_OCPDEG_2 | \
                       DRV8711_DRIVE_OCPTH_500)
#define DRV8711_TORQUE DRV8711_TORQUE_SMPLTH_200
#define DRV8711_CTRL   (DRV8711_CTRL_ISGAIN_10 | DRV8711_CTRL_DTIME_450 | \
                        DRV8711_CTRL_EXSTALL_bm)


// Huanyang settings
#define HUANYANG_PORT          USARTD1
#define HUANYANG_DRE_vect      USARTD1_DRE_vect
#define HUANYANG_TXC_vect      USARTD1_TXC_vect
#define HUANYANG_RXC_vect      USARTD1_RXC_vect
#define HUANYANG_TIMEOUT       50 // ms. response timeout
#define HUANYANG_RETRIES        4 // Number of retries before failure
#define HUANYANG_ID             1 // Default ID


// Serial settings
#define SERIAL_BAUD            USART_BAUD_115200
#define SERIAL_PORT            USARTC0
#define SERIAL_DRE_vect        USARTC0_DRE_vect
#define SERIAL_RXC_vect        USARTC0_RXC_vect


// Input
#define INPUT_BUFFER_LEN       255 // text buffer size (255 max)


// Planner
/// Should be at least the number of buffers required to support optimal
/// planning in the case of very short lines or arc segments.  Suggest no less
/// than 12.  Must leave headroom for stack.
#define PLANNER_BUFFER_POOL_SIZE 32

/// Buffers to reserve in planner before processing new input line
#define PLANNER_BUFFER_HEADROOM   4

/// Minimum number of filled buffers before timeout until execution starts
#define PLANNER_EXEC_MIN_FILL     4

/// Delay before executing new buffers unless PLANNER_EXEC_MIN_FILL is met
/// This gives the planner a chance to make a good plan before execution starts
#define PLANNER_EXEC_DELAY      250 // In ms


// I2C
#define I2C_DEV                 TWIC
#define I2C_ISR                 TWIC_TWIS_vect
#define I2C_ADDR                0x2b
#define I2C_MAX_DATA            8


// Settings ********************************************************************

// Motor settings.  See motor.c
#define MOTOR_IDLE_TIMEOUT       0.25  // secs, motor off after this time


#define MIN_HALF_STEP_CORRECTION 4
#define CHORDAL_TOLERANCE        0.01          // chordal accuracy for arcs
#define JUNCTION_DEVIATION       0.05          // default value, in mm
#define JUNCTION_ACCELERATION    100000        // centripetal corner accel
#define JOG_MIN_VELOCITY         10            // mm/min
#define CAL_ACCELERATION         500000        // mm/min^2
#define CURRENT_SENSE_RESISTOR   0.05          // ohms
#define CURRENT_SENSE_REF        2.75          // volts
#define MAX_CURRENT              10            // amps

// Arc
#define ARC_RADIUS_ERROR_MAX   1.0   // max mm diff between start and end radius
#define ARC_RADIUS_ERROR_MIN   0.005 // min mm where 1% rule applies
#define ARC_RADIUS_TOLERANCE   0.001 // 0.1% radius variance test


// Gcode defaults
#define GCODE_DEFAULT_UNITS         MILLIMETERS // MILLIMETERS or INCHES
#define GCODE_DEFAULT_PLANE         PLANE_XY    // See machine.h
#define GCODE_DEFAULT_COORD_SYSTEM  G54         // G54, G55, G56, G57, G58, G59
#define GCODE_DEFAULT_PATH_CONTROL  PATH_CONTINUOUS
#define GCODE_DEFAULT_DISTANCE_MODE ABSOLUTE_MODE
#define GCODE_DEFAULT_ARC_DISTANCE_MODE INCREMENTAL_MODE
#define GCODE_MAX_OPERATOR_DEPTH    16
#define GCODE_MAX_VALUE_DEPTH       32
