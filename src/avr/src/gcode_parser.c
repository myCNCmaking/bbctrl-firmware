/******************************************************************************\

                This file is part of the Buildbotics firmware.

                  Copyright (c) 2015 - 2017 Buildbotics LLC
                  Copyright (c) 2010 - 2015 Alden S. Hart, Jr.
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

#include "gcode_parser.h"

#include "gcode_expr.h"
#include "machine.h"
#include "plan/arc.h"
#include "axis.h"
#include "util.h"

#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>


parser_t parser = {{0}};


#define SET_MODAL(m, parm, val) \
  {parser.gn.parm = val; parser.gf.parm = true; parser.modals[m] += 1; break;}
#define SET_NON_MODAL(parm, val) \
  {parser.gn.parm = val; parser.gf.parm = true; break;}
#define EXEC_FUNC(f, parm) if (parser.gf.parm) f(parser.gn.parm)


// NOTE Nested comments are not allowed.  E.g. (msg (hello))
static char *_parse_gcode_comment(char *p) {
  char *msg = 0;

  p++; // Skip leading paren

  while (isspace(*p)) p++; // skip whitespace

  // Look for "(MSG"
  if (tolower(*(p + 0)) == 'm' &&
      tolower(*(p + 1)) == 's' &&
      tolower(*(p + 2)) == 'g') {
      p += 3;
      while (isspace(*p)) p++; // skip whitespace
      if (*p && *p != ')') msg = p;
  }

  // Find end
  while (*p && *p != ')') p++;
  *p = 0; // Terminate string

  // Queue message
  if (msg) mach_message(msg);

  return p;
}


static stat_t _parse_gcode_value(char **p, float *value) {
  while (isspace(**p)) (*p)++; // skip whitespace

  if (**p == '[') *value = parse_gcode_expression(p);
  else *value = parse_gcode_number(p);

  return parser.error;
}


/// Isolate the decimal point value as an integer
static uint8_t _point(float value) {return value * 10 - trunc(value) * 10;}


#if 0
static bool _axis_changed() {
  for (int axis = 0; axis < AXES; axis++)
    if (parser.gf.target[axis]) return true;
  return false;
}
#endif


/// Check for some gross Gcode block semantic violations
static stat_t _validate_gcode_block() {
  // Check for modal group violations. From NIST, section 3.4 "It
  // is an error to put a G-code from group 1 and a G-code from
  // group 0 on the same line if both of them use axis words. If an
  // axis word-using G-code from group 1 is implicitly in effect on
  // a line (by having been activated on an earlier line), and a
  // group 0 G-code that uses axis words appears on the line, the
  // activity of the group 1 G-code is suspended for that line. The
  // axis word-using G-codes from group 0 are G10, G28, G30, and G92"

  if (parser.modals[MODAL_GROUP_G0] && parser.modals[MODAL_GROUP_G1])
    return STAT_MODAL_GROUP_VIOLATION;

#if 0 // TODO This check fails for arcs which may have offsets but no axis word
  // look for commands that require an axis word to be present
  if (parser.modals[MODAL_GROUP_G0] || parser.modals[MODAL_GROUP_G1])
    if (!_axis_changed()) return STAT_GCODE_AXIS_IS_MISSING;
#endif

  return STAT_OK;
}


/* Execute parsed block
 *
 * Conditionally (based on whether a flag is set in gf) call the
 * machining functions in order of execution as per RS274NGC_3 table 8
 * (below, with modifications):
 *
 *   0. record the line number
 *   1. comment (includes message) [handled during block normalization]
 *   2. set feed rate mode (G93, G94 - inverse time or per minute)
 *   3. set feed rate (F)
 *   3a. set feed override rate (M50)
 *   4. set spindle speed (S)
 *   4a. set spindle override rate (M51)
 *   5. select tool (T)
 *   6. change tool (M6)
 *   7. spindle on or off (M3, M4, M5)
 *   8. coolant on or off (M7, M8, M9)
 *   9. enable or disable overrides (M48, M49)
 *   10. dwell (G4)
 *   11. set active plane (G17, G18, G19)
 *   12. set length units (G20, G21)
 *   13. cutter radius compensation on or off (G40, G41, G42)
 *   14. cutter length compensation on or off (G43, G49)
 *   15. coordinate system selection (G54, G55, G56, G57, G58, G59)
 *   16. set path control mode (G61, G61.1, G64)
 *   17. set distance mode (G90, G91, G90.1, G91.1)
 *   18. set retract mode (G98, G99)
 *   19a. homing functions (G28.2, G28.3, G28.1, G28, G30) // TODO update this
 *   19b. update system data (G10)
 *   19c. set axis offsets (G92, G92.1, G92.2, G92.3)
 *   20. perform motion (G0 to G3, G80-G89) as modified (possibly) by G53
 *   21. stop and end (M0, M1, M2, M30, M60)
 *
 * Values in gn are in original units and should not be unit converted prior
 * to calling the machine functions (which does the unit conversions)
 */
static stat_t _execute_gcode_block() {
  stat_t status = STAT_OK;

  mach_set_line(parser.gn.line);
  EXEC_FUNC(mach_set_feed_mode, feed_mode);
  EXEC_FUNC(mach_set_feed_rate, feed_rate);
  EXEC_FUNC(mach_feed_override_enable, feed_override_enable);
  EXEC_FUNC(mach_set_spindle_speed, spindle_speed);
  EXEC_FUNC(mach_spindle_override_enable, spindle_override_enable);
  EXEC_FUNC(mach_select_tool, tool);
  EXEC_FUNC(mach_change_tool, tool_change);
  EXEC_FUNC(mach_set_spindle_mode, spindle_mode);
  EXEC_FUNC(mach_mist_coolant_control, mist_coolant);
  EXEC_FUNC(mach_flood_coolant_control, flood_coolant);
  EXEC_FUNC(mach_override_enables, override_enables);

  if (parser.gn.next_action == NEXT_ACTION_DWELL) // G4 - dwell
    RITORNO(mach_dwell(parser.gn.parameter));

  EXEC_FUNC(mach_set_plane, plane);
  EXEC_FUNC(mach_set_units, units);
  //--> cutter radius compensation goes here
  //--> cutter length compensation goes here
  EXEC_FUNC(mach_set_coord_system, coord_system);
  EXEC_FUNC(mach_set_path_mode, path_mode);
  EXEC_FUNC(mach_set_distance_mode, distance_mode);
  EXEC_FUNC(mach_set_arc_distance_mode, arc_distance_mode);
  //--> set retract mode goes here

  switch (parser.gn.next_action) {
  case NEXT_ACTION_SET_G28_POSITION: // G28.1
    mach_set_g28_position();
    break;
  case NEXT_ACTION_GOTO_G28_POSITION: // G28
    status = mach_goto_g28_position(parser.gn.target, parser.gf.target);
    break;
  case NEXT_ACTION_SET_G30_POSITION: // G30.1
    mach_set_g30_position();
    break;
  case NEXT_ACTION_GOTO_G30_POSITION: // G30
    status = mach_goto_g30_position(parser.gn.target, parser.gf.target);
    break;
  case NEXT_ACTION_CLEAR_HOME: // G28.2
    mach_clear_home(parser.gf.target);
    break;
  case NEXT_ACTION_SET_HOME: // G28.3
    mach_set_home(parser.gn.target, parser.gf.target);
    break;
  case NEXT_ACTION_SET_COORD_DATA:
    mach_set_coord_offsets(parser.gn.parameter, parser.gn.target,
                           parser.gf.target);
    break;
  case NEXT_ACTION_SET_ORIGIN_OFFSETS:
    mach_set_origin_offsets(parser.gn.target, parser.gf.target);
    break;
  case NEXT_ACTION_RESET_ORIGIN_OFFSETS:
    mach_reset_origin_offsets();
    break;
  case NEXT_ACTION_SUSPEND_ORIGIN_OFFSETS:
    mach_suspend_origin_offsets();
    break;
  case NEXT_ACTION_RESUME_ORIGIN_OFFSETS:
    mach_resume_origin_offsets();
    break;
  case NEXT_ACTION_DWELL: break; // Handled above

  case NEXT_ACTION_DEFAULT:
    // apply override setting to gm struct
    mach_set_absolute_mode(parser.gn.absolute_mode);

    switch (parser.gn.motion_mode) {
    case MOTION_MODE_CANCEL_MOTION_MODE:
      mach_set_motion_mode(parser.gn.motion_mode);
      break;
    case MOTION_MODE_RAPID:
      status = mach_rapid(parser.gn.target, parser.gf.target);
      break;
    case MOTION_MODE_FEED:
      status = mach_feed(parser.gn.target, parser.gf.target);
      break;
    case MOTION_MODE_CW_ARC: case MOTION_MODE_CCW_ARC:
      // gf.radius sets radius mode if radius was collected in gn
      status = mach_arc_feed(parser.gn.target, parser.gf.target,
                             parser.gn.arc_offset, parser.gf.arc_offset,
                             parser.gn.arc_radius, parser.gf.arc_radius,
                             parser.gn.parameter, parser.gf.parameter,
                             parser.modals[MODAL_GROUP_G1],
                             parser.gn.motion_mode);
      break;
    case MOTION_MODE_STRAIGHT_PROBE_CLOSE_ERR: // G38.2
      status = mach_probe(parser.gn.target, parser.gf.target,
                          parser.gn.motion_mode);
      break;
    case MOTION_MODE_STRAIGHT_PROBE_CLOSE:     // G38.3
      status = mach_probe(parser.gn.target, parser.gf.target,
                          parser.gn.motion_mode);
      break;
    case MOTION_MODE_STRAIGHT_PROBE_OPEN_ERR:  // G38.4
      status = mach_probe(parser.gn.target, parser.gf.target,
                          parser.gn.motion_mode);
      break;
    case MOTION_MODE_STRAIGHT_PROBE_OPEN:      // G38.5
      status = mach_probe(parser.gn.target, parser.gf.target,
                          parser.gn.motion_mode);
      break;
    case MOTION_MODE_SEEK_CLOSE_ERR:           // G38.6
      status = mach_seek(parser.gn.target, parser.gf.target,
                         parser.gn.motion_mode);
      break;
    case MOTION_MODE_SEEK_CLOSE:               // G38.7
      status = mach_seek(parser.gn.target, parser.gf.target,
                         parser.gn.motion_mode);
      break;
    case MOTION_MODE_SEEK_OPEN_ERR:            // G38.8
      status = mach_seek(parser.gn.target, parser.gf.target,
                         parser.gn.motion_mode);
      break;
    case MOTION_MODE_SEEK_OPEN:                // G38.9
      status = mach_seek(parser.gn.target, parser.gf.target,
                         parser.gn.motion_mode);
      break;
    default: break; // Should not get here
    }
  }

  // un-set absolute override once the move is planned
  mach_set_absolute_mode(false);

  // do the program stops and ends : M0, M1, M2, M30, M60
  if (parser.gf.program_flow)
    switch (parser.gn.program_flow) {
    case PROGRAM_STOP: mach_program_stop(); break;
    case PROGRAM_OPTIONAL_STOP: mach_optional_program_stop(); break;
    case PROGRAM_PALLET_CHANGE_STOP: mach_pallet_change_stop(); break;
    case PROGRAM_END: mach_program_end(); break;
    }

  return status;
}


/// Load the state values in gn (next model state) and set flags in gf (model
/// state flags).
static stat_t _process_gcode_word(char letter, float value) {
  switch (letter) {
  case 'G':
    switch ((uint8_t)value) {
    case 0:
      SET_MODAL(MODAL_GROUP_G1, motion_mode, MOTION_MODE_RAPID);
    case 1:
      SET_MODAL(MODAL_GROUP_G1, motion_mode, MOTION_MODE_FEED);
    case 2: SET_MODAL(MODAL_GROUP_G1, motion_mode, MOTION_MODE_CW_ARC);
    case 3: SET_MODAL(MODAL_GROUP_G1, motion_mode, MOTION_MODE_CCW_ARC);
    case 4: SET_NON_MODAL(next_action, NEXT_ACTION_DWELL);
    case 10:
      SET_MODAL(MODAL_GROUP_G0, next_action, NEXT_ACTION_SET_COORD_DATA);
    case 17: SET_MODAL(MODAL_GROUP_G2, plane, PLANE_XY);
    case 18: SET_MODAL(MODAL_GROUP_G2, plane, PLANE_XZ);
    case 19: SET_MODAL(MODAL_GROUP_G2, plane, PLANE_YZ);
    case 20: SET_MODAL(MODAL_GROUP_G6, units, INCHES);
    case 21: SET_MODAL(MODAL_GROUP_G6, units, MILLIMETERS);
    case 28:
      switch (_point(value)) {
      case 0:
        SET_MODAL(MODAL_GROUP_G0, next_action, NEXT_ACTION_GOTO_G28_POSITION);
      case 1:
        SET_MODAL(MODAL_GROUP_G0, next_action, NEXT_ACTION_SET_G28_POSITION);
      case 2: SET_NON_MODAL(next_action, NEXT_ACTION_CLEAR_HOME);
      case 3: SET_NON_MODAL(next_action, NEXT_ACTION_SET_HOME);
      default: return STAT_GCODE_COMMAND_UNSUPPORTED;
      }
      break;

    case 30:
      switch (_point(value)) {
      case 0:
        SET_MODAL(MODAL_GROUP_G0, next_action, NEXT_ACTION_GOTO_G30_POSITION);
      case 1:
        SET_MODAL(MODAL_GROUP_G0, next_action, NEXT_ACTION_SET_G30_POSITION);
      default: return STAT_GCODE_COMMAND_UNSUPPORTED;
      }
      break;

    case 38:
      switch (_point(value)) {
      case 2: SET_MODAL(MODAL_GROUP_G1, motion_mode,
                        MOTION_MODE_STRAIGHT_PROBE_CLOSE_ERR);
      case 3: SET_MODAL(MODAL_GROUP_G1, motion_mode,
                        MOTION_MODE_STRAIGHT_PROBE_CLOSE);
      case 4: SET_MODAL(MODAL_GROUP_G1, motion_mode,
                        MOTION_MODE_STRAIGHT_PROBE_OPEN_ERR);
      case 5: SET_MODAL(MODAL_GROUP_G1, motion_mode,
                        MOTION_MODE_STRAIGHT_PROBE_OPEN);
      case 6: SET_MODAL(MODAL_GROUP_G1, motion_mode,
                        MOTION_MODE_SEEK_CLOSE_ERR);
      case 7: SET_MODAL(MODAL_GROUP_G1, motion_mode, MOTION_MODE_SEEK_CLOSE);
      case 8: SET_MODAL(MODAL_GROUP_G1, motion_mode,
                        MOTION_MODE_SEEK_OPEN_ERR);
      case 9: SET_MODAL(MODAL_GROUP_G1, motion_mode, MOTION_MODE_SEEK_OPEN);
      default: return STAT_GCODE_COMMAND_UNSUPPORTED;
      }
      break;

    case 40: break; // ignore cancel cutter radius compensation
    case 49: break; // ignore cancel tool length offset comp.
    case 53: SET_NON_MODAL(absolute_mode, true);
    case 54: SET_MODAL(MODAL_GROUP_G12, coord_system, G54);
    case 55: SET_MODAL(MODAL_GROUP_G12, coord_system, G55);
    case 56: SET_MODAL(MODAL_GROUP_G12, coord_system, G56);
    case 57: SET_MODAL(MODAL_GROUP_G12, coord_system, G57);
    case 58: SET_MODAL(MODAL_GROUP_G12, coord_system, G58);
    case 59: SET_MODAL(MODAL_GROUP_G12, coord_system, G59);
    case 61:
      switch (_point(value)) {
      case 0: SET_MODAL(MODAL_GROUP_G13, path_mode, PATH_EXACT_PATH);
      case 1: SET_MODAL(MODAL_GROUP_G13, path_mode, PATH_EXACT_STOP);
      default: return STAT_GCODE_COMMAND_UNSUPPORTED;
      }
      break;

    case 64: SET_MODAL(MODAL_GROUP_G13, path_mode, PATH_CONTINUOUS);
    case 80: SET_MODAL(MODAL_GROUP_G1, motion_mode,
                       MOTION_MODE_CANCEL_MOTION_MODE);
    case 90:
      switch (_point(value)) {
      case 0: SET_MODAL(MODAL_GROUP_G3, distance_mode, ABSOLUTE_MODE);
      case 1: SET_MODAL(MODAL_GROUP_G3, arc_distance_mode, ABSOLUTE_MODE);
      default: return STAT_GCODE_COMMAND_UNSUPPORTED;
      }
      break;

    case 91:
      switch (_point(value)) {
      case 0: SET_MODAL(MODAL_GROUP_G3, distance_mode, INCREMENTAL_MODE);
      case 1: SET_MODAL(MODAL_GROUP_G3, arc_distance_mode, INCREMENTAL_MODE);
      default: return STAT_GCODE_COMMAND_UNSUPPORTED;
      }
      break;

    case 92:
      switch (_point(value)) {
      case 0: SET_MODAL(MODAL_GROUP_G0, next_action,
                        NEXT_ACTION_SET_ORIGIN_OFFSETS);
      case 1: SET_NON_MODAL(next_action, NEXT_ACTION_RESET_ORIGIN_OFFSETS);
      case 2: SET_NON_MODAL(next_action, NEXT_ACTION_SUSPEND_ORIGIN_OFFSETS);
      case 3: SET_NON_MODAL(next_action, NEXT_ACTION_RESUME_ORIGIN_OFFSETS);
      default: return STAT_GCODE_COMMAND_UNSUPPORTED;
      }
      break;

    case 93: SET_MODAL(MODAL_GROUP_G5, feed_mode, INVERSE_TIME_MODE);
    case 94: SET_MODAL(MODAL_GROUP_G5, feed_mode, UNITS_PER_MINUTE_MODE);
      // case 95:
      // SET_MODAL(MODAL_GROUP_G5, feed_mode, UNITS_PER_REVOLUTION_MODE);
      // case 96: // Spindle Constant Surface Speed (not currently supported)
    case 97: break; // Spindle RPM mode (only mode curently supported)
    default: return STAT_GCODE_COMMAND_UNSUPPORTED;
    }
    break;

  case 'M':
    switch ((uint8_t)value) {
    case 0:
      SET_MODAL(MODAL_GROUP_M4, program_flow, PROGRAM_STOP);
    case 1:
      SET_MODAL(MODAL_GROUP_M4, program_flow, PROGRAM_OPTIONAL_STOP);
    case 60:
      SET_MODAL(MODAL_GROUP_M4, program_flow, PROGRAM_PALLET_CHANGE_STOP);
    case 2: case 30:
      SET_MODAL(MODAL_GROUP_M4, program_flow, PROGRAM_END);
    case 3: SET_MODAL(MODAL_GROUP_M7, spindle_mode, SPINDLE_CW);
    case 4: SET_MODAL(MODAL_GROUP_M7, spindle_mode, SPINDLE_CCW);
    case 5: SET_MODAL(MODAL_GROUP_M7, spindle_mode, SPINDLE_OFF);
    case 6: SET_NON_MODAL(tool_change, true);
    case 7: SET_MODAL(MODAL_GROUP_M8, mist_coolant, true);
    case 8: SET_MODAL(MODAL_GROUP_M8, flood_coolant, true);
    case 9: SET_MODAL(MODAL_GROUP_M8, flood_coolant, false); // Also mist
    case 48: SET_MODAL(MODAL_GROUP_M9, override_enables, true);
    case 49: SET_MODAL(MODAL_GROUP_M9, override_enables, false);
    case 50: SET_MODAL(MODAL_GROUP_M9, feed_override_enable, true);
    case 51: SET_MODAL(MODAL_GROUP_M9, spindle_override_enable, true);
    default: return STAT_MCODE_COMMAND_UNSUPPORTED;
    }
    break;

  case 'T': SET_NON_MODAL(tool, (uint8_t)trunc(value));
  case 'F': SET_NON_MODAL(feed_rate, value);
    // used for dwell time, G10 coord select, rotations
  case 'P': SET_NON_MODAL(parameter, value);
  case 'S': SET_NON_MODAL(spindle_speed, value);
  case 'X': SET_NON_MODAL(target[AXIS_X], value);
  case 'Y': SET_NON_MODAL(target[AXIS_Y], value);
  case 'Z': SET_NON_MODAL(target[AXIS_Z], value);
  case 'A': SET_NON_MODAL(target[AXIS_A], value);
  case 'B': SET_NON_MODAL(target[AXIS_B], value);
  case 'C': SET_NON_MODAL(target[AXIS_C], value);
    // case 'U': SET_NON_MODAL(target[AXIS_U], value); // reserved
    // case 'V': SET_NON_MODAL(target[AXIS_V], value); // reserved
    // case 'W': SET_NON_MODAL(target[AXIS_W], value); // reserved
  case 'I': SET_NON_MODAL(arc_offset[0], value);
  case 'J': SET_NON_MODAL(arc_offset[1], value);
  case 'K': SET_NON_MODAL(arc_offset[2], value);
  case 'R': SET_NON_MODAL(arc_radius, value);
  case 'N': SET_NON_MODAL(line, (uint32_t)value); // line number
  case 'L': break; // not used for anything
  case 0: break;
  default: return STAT_GCODE_COMMAND_UNSUPPORTED;
  }

  return STAT_OK;
}


/// Parse a block (line) of gcode
/// Top level of gcode parser. Normalizes block and looks for special cases
stat_t gc_gcode_parser(char *block) {
#ifdef DEBUG
  printf("GCODE: %s\n", block);
#endif

  // Delete block if it starts with /
  if (*block == '/') return STAT_NOOP;

  // Set initial state for new block
  // A number of implicit things happen when the gn struct is zeroed:
  //   - inverse feed rate mode is canceled
  //   - set back to units_per_minute mode
  memset(&parser, 0, sizeof(parser)); // clear all parser values

  // get motion mode from previous block
  parser.gn.motion_mode = mach_get_motion_mode();

  // Parse words
  for (char *p = block; *p;) {
    switch (*p) {
    case ' ': case '\t': case '\r': case '\n': p++; break; // Skip whitespace
    case '(': p = _parse_gcode_comment(p); break;
    case ';': *p = 0; break; // Comment

    default: {
      char letter = toupper(*p++);
      float value = 0;
      if (!isalpha(letter)) return STAT_INVALID_OR_MALFORMED_COMMAND;
      RITORNO(_parse_gcode_value(&p, &value));
      RITORNO(_process_gcode_word(letter, value));
    }
    }
  }

  RITORNO(_validate_gcode_block());

  return _execute_gcode_block();
}
