#include "control_loop.h"

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/atomic.h>

#include "avr-fixed.h"

#include "adc.h"
#include "addr_jumper.h"
#include "encoder.h"
#include "pwm.h"
#include "twi_state_machine.h"

unsigned char pwm_mode;
DECLARE_I2C_REGISTER_C(FIXED1616, target_speed);

void init_control_loop(void) {
  // Currently no internal state (not yet implemented).
  // TODO(rqou)
}

// configure the PWM into the correct mode (sign-magnitude/locked-antiphase,
// direction, etc)
static inline void set_pwm_mode(unsigned char pwm_mode, unsigned char fwd) {
  if ((pwm_mode & MODE_SIGN_MAG_LOCKED_ANTIPHASE) ==
      MODE_LOCKED_ANTIPHASE) {
    // TODO(rqou)
  }
  else {
    if ((pwm_mode & MODE_SM_SWITCH_MODE) == MODE_SM_GO_BRAKE) {
      if (fwd)
        set_sign_magnitude_go_brake_fwd();
      else
        set_sign_magnitude_go_brake_bck();
    }
    else {
      if (fwd)
        set_sign_magnitude_go_coast_fwd();
      else
        set_sign_magnitude_go_coast_bck();
    }
  }
}

// performs stress mode logic, returns a new pwm_val if stress is enabled
// returns the original pwm_val if stress not enabled
// may modify fwd if stress is enabled
static inline unsigned int stress_mode_logic(unsigned int pwm_val,
    unsigned char pwm_mode, FIXED1616 target_speed, unsigned char *fwd) {
  static unsigned int stress_counter = 0;

  if (pwm_mode & MODE_SPECIAL_STRESS) {
    // hack, these are the fractional bits
    unsigned int stress_period = target_speed & 0xFFFF;
    // this MUST be an unsigned number
    pwm_val = fixed_to_int(target_speed);
    if (stress_counter < stress_period) {
      *fwd = 1;
    }
    else if (stress_counter < stress_period * 2) {
      *fwd = 0;
    }
    stress_counter++;
    if (stress_counter == stress_period * 2)
      stress_counter = 0;
  }
  else {
    // switch out
    stress_counter = 0;
  }

  return pwm_val;
}

// raw mode logic. fwd is always written and is an output parameter. returns
// pwm_val
static inline unsigned int raw_mode_logic(FIXED1616 target_speed,
    unsigned char *fwd) {
  unsigned int pwm_val;

  if (target_speed >= 0) {
    *fwd = 1;
    pwm_val = fixed_to_int(target_speed);
  }
  else {
    *fwd = 0;
    pwm_val = fixed_to_int(-target_speed);
  }

  return pwm_val;
}

void run_control_loop(void) {

  FIXED1616 target_speed_copy;
  unsigned char pwm_mode_copy;

  // We need to manually copy these as one block since they go together
  ATOMIC_BLOCK(ATOMIC_FORCEON) {
    target_speed_copy = get_target_speed_dangerous();
    pwm_mode_copy = pwm_mode;
  }

  if (!(pwm_mode_copy & MODE_ENABLE_MASK)) {
    // disabled
    driver_enable(0);
    set_pwm_val(0);
  }
  else {
    unsigned int pwm_val = 0;
    unsigned char fwd = 0;

    // Compute the actual value.
    switch (pwm_mode_copy & MODE_SPEED_MASK) {
      case MODE_SPEED_RAW:
        pwm_val = raw_mode_logic(target_speed_copy, &fwd);
        break;

      case MODE_SPEED_NO_PID:
        // TODO(rqou)
        break;

      case MODE_SPEED_PID:
        // TODO(rqou)
        break;

      default:
        // Undefined mode, shutting down just in case.
        pwm_mode = 0;
        driver_enable(0);
        set_pwm_val(0);
    }

    // override the value in stress mode
    pwm_val = stress_mode_logic(pwm_val, pwm_mode_copy, target_speed_copy,
        &fwd);

    // Compute the mode.
    driver_enable(1);
    set_pwm_mode(pwm_mode_copy, fwd);
    set_pwm_val(pwm_val);
  }
}

void init_hardware(void) {
  unsigned char i2c_addr = determine_addr();
  init_i2c(i2c_addr);
  init_encoder();
  init_adc();
  init_pwm();
  init_control_loop();
}

int main(void) {
  init_hardware();

  sei();

  while (1) {
    if (TIFR4 & _BV(TOV4)) {
      TIFR4 = _BV(TOV4);

      // This code is run every time a timer overflow occurs. This currently
      // happens at an frequency of just under 1 kHz (every 1.024 ms).

      read_adc();
      run_control_loop();
    }
  }
}
