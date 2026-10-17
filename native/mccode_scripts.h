#pragma once
// MCCode script library: ready-made programs for the Code view's side panel. Each one compiles as is
// (tests/mccode_complete_test.cpp checks); part names are examples the player renames to their own parts.
#include <vector>

namespace mccode {

struct Script { const char* name; const char* help; const char* code; };

inline const std::vector<Script>& script_library() {
    static const std::vector<Script> s = {
        {"Traction control", "Cuts power while the driven wheels spin faster than the others",
         "# Traction control: eases power off while the rear wheels spin faster than the front ones.\n"
         "# Parts: rename FrontLeft, FrontRight, RearLeft, RearRight and Actuator_Stepper_Motor to yours.\n"
         "# The stepper sets the power cut: 0.0 is full power, 1.0 is fully cut.\n"
         "define var fractional slip\n"
         "define var fractional cut\n"
         "\n"
         "on_tick {\n"
         "\tslip = (RearLeft.rps + RearRight.rps) - (FrontLeft.rps + FrontRight.rps)\n"
         "\tif slip > 2.0 {\n"
         "\t\tcut = cut + 0.05   # how fast power comes off while spinning\n"
         "\t} else {\n"
         "\t\tcut = cut - 0.02   # how fast it comes back once they grip\n"
         "\t}\n"
         "\tif cut > 1.0 {\n"
         "\t\tcut = 1.0\n"
         "\t}\n"
         "\tif cut < 0.0 {\n"
         "\t\tcut = 0.0\n"
         "\t}\n"
         "\tActuator_Stepper_Motor.input = cut\n"
         "}\n"},
        {"ABS", "Lets a wheel's brake off when it locks up",
         "# ABS: a wheel turning much slower than the average is locking, so its brake eases off.\n"
         "# Parts: rename FrontLeft, FrontRight, RearLeft and RearRight to your wheels.\n"
         "# brake_factor 1.0 is full brake, lower lets the brake off.\n"
         "define var fractional avg\n"
         "define var fractional lock_at\n"
         "\n"
         "on_tick {\n"
         "\tavg = (FrontLeft.rps + FrontRight.rps + RearLeft.rps + RearRight.rps) / 4.0\n"
         "\tlock_at = avg * 0.8   # a wheel below 80% of the average counts as locking\n"
         "\tFrontLeft.brake_factor = 1.0\n"
         "\tFrontRight.brake_factor = 1.0\n"
         "\tRearLeft.brake_factor = 1.0\n"
         "\tRearRight.brake_factor = 1.0\n"
         "\tif avg > 1.0 {   # only while rolling\n"
         "\t\tif FrontLeft.rps < lock_at {\n"
         "\t\t\tFrontLeft.brake_factor = 0.3\n"
         "\t\t}\n"
         "\t\tif FrontRight.rps < lock_at {\n"
         "\t\t\tFrontRight.brake_factor = 0.3\n"
         "\t\t}\n"
         "\t\tif RearLeft.rps < lock_at {\n"
         "\t\t\tRearLeft.brake_factor = 0.3\n"
         "\t\t}\n"
         "\t\tif RearRight.rps < lock_at {\n"
         "\t\t\tRearRight.brake_factor = 0.3\n"
         "\t\t}\n"
         "\t}\n"
         "}\n"},
        {"Cruise control", "Holds the speed you had when the switch went on",
         "# Cruise control: flip the switch and it holds that wheel speed.\n"
         "# Parts: rename RearLeft, CruiseSwitch.state and Throttle.input to your parts and channels\n"
         "# (This MC lists them). The output is 0.0 to 1.0 throttle.\n"
         "define var bool was_on\n"
         "define var fractional target\n"
         "define var fractional err\n"
         "define var fractional boost\n"
         "define var fractional throttle\n"
         "\n"
         "on_tick {\n"
         "\tif CruiseSwitch.state {\n"
         "\t\tif not was_on {\n"
         "\t\t\ttarget = RearLeft.rps   # lock in the current speed\n"
         "\t\t\tboost = 0.0\n"
         "\t\t}\n"
         "\t\terr = target - RearLeft.rps\n"
         "\t\tboost = boost + err * 0.005   # slowly learns how much throttle the hill needs\n"
         "\t\tif boost > 1.0 {\n"
         "\t\t\tboost = 1.0\n"
         "\t\t}\n"
         "\t\tif boost < 0.0 {\n"
         "\t\t\tboost = 0.0\n"
         "\t\t}\n"
         "\t\tthrottle = boost + err * 0.2   # raise 0.2 for a harder push\n"
         "\t\tif throttle > 1.0 {\n"
         "\t\t\tthrottle = 1.0\n"
         "\t\t}\n"
         "\t\tif throttle < 0.0 {\n"
         "\t\t\tthrottle = 0.0\n"
         "\t\t}\n"
         "\t} else {\n"
         "\t\tthrottle = 0.0\n"
         "\t}\n"
         "\tThrottle.input = throttle\n"
         "\twas_on = CruiseSwitch.state\n"
         "}\n"},
        {"Launch control", "Holds the revs with the two-step while stopped and the switch is on",
         "# Launch control: while stopped with the switch on, the Turbo Wastegate's two-step holds the revs.\n"
         "# Parts: rename LaunchSwitch.state, RearLeft and Turbo_Wastegate to yours.\n"
         "on_tick {\n"
         "\tif LaunchSwitch.state and RearLeft.rps < 1.0 {\n"
         "\t\tTurbo_Wastegate.two_step = 4000.0   # launch RPM\n"
         "\t} else {\n"
         "\t\tTurbo_Wastegate.two_step = 0.0   # off once you roll\n"
         "\t}\n"
         "}\n"},
        {"Shift light", "Turns a lamp on above a set engine speed",
         "# Shift light: the lamp comes on above the shift point.\n"
         "# Parts: rename Engine.rps and ShiftLamp.input to your engine and lamp channels.\n"
         "on_tick {\n"
         "\tShiftLamp.input = Engine.rps > 100.0   # shift point in engine rps\n"
         "}\n"},
        {"Blinker", "Flashes a lamp on and off while a switch is on",
         "# Blinker: flashes the lamp while the switch is on.\n"
         "# Parts: rename BlinkSwitch.state and Lamp.input to yours.\n"
         "define var integer ticks\n"
         "\n"
         "on_tick {\n"
         "\tif BlinkSwitch.state {\n"
         "\t\tticks = (ticks + 1) % 40   # ticks per flash cycle\n"
         "\t\tLamp.input = ticks < 20\n"
         "\t} else {\n"
         "\t\tticks = 0\n"
         "\t\tLamp.input = false\n"
         "\t}\n"
         "}\n"},
    };
    return s;
}

}  // namespace mccode
