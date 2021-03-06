/******************************************************************************\

                This file is part of the Buildbotics firmware.

                  Copyright (c) 2015 - 2017 Buildbotics LLC
                  Copyright (c) 2010 - 2013 Alden S. Hart Jr.
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

#include "rtc.h"

#include "switch.h"
#include "huanyang.h"
#include "motor.h"
#include "lcd.h"

#include <avr/io.h>
#include <avr/interrupt.h>

#include <string.h>


static uint32_t ticks;


ISR(RTC_OVF_vect) {
  ticks++;

  switch_rtc_callback();
  huanyang_rtc_callback();
  if (!(ticks & 255)) motor_rtc_callback();
  lcd_rtc_callback();
}


/// Initialize and start the clock
/// This routine follows the code in app note AVR1314.
void rtc_init() {
  ticks = 0;

  OSC.CTRL |= OSC_RC32KEN_bm;                         // enable internal 32kHz.
  while (!(OSC.STATUS & OSC_RC32KRDY_bm));            // 32kHz osc stabilize
  while (RTC.STATUS & RTC_SYNCBUSY_bm);               // wait RTC not busy

  CLK.RTCCTRL = CLK_RTCSRC_RCOSC32_gc | CLK_RTCEN_bm; // 32kHz clock as RTC src
  while (RTC.STATUS & RTC_SYNCBUSY_bm);               // wait RTC not busy

  // the following must be in this order or it doesn't work
  RTC.PER = 33;                        // overflow period ~1ms
  RTC.INTCTRL = RTC_OVFINTLVL_LO_gc;   // overflow LO interrupt
  RTC.CTRL = RTC_PRESCALER_DIV1_gc;    // no prescale
}


uint32_t rtc_get_time() {return ticks;}
bool rtc_expired(uint32_t t) {return 0 <= (int32_t)(ticks - t);}
