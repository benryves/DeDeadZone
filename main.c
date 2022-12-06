#include <stdlib.h>

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/eeprom.h>

#include <util/delay.h>

volatile enum operating_mode_t {
	OPERATING_MODE_INTIALISING,
	OPERATING_MODE_WAITING_MIDPOINT,
	OPERATING_MODE_FINDING_MIDPOINT,
	OPERATING_MODE_WORKING,
} operating_mode;


// Flag to determine whether we're correcting or not.
volatile uint8_t correcting = 1;
// Where do we store the correcting flag in EEPROM?
#define CORRECTING_EEPROM_ADDRESS (0)

// Where is the midpoint of an idle wheel?
volatile uint16_t midpoint = 0;

// What's the size of the input dead zone?
#define INPUT_DEAD_ZONE (5)
// When we've moved outside our input dead zone, what offset do we apply to force the wheel signal outside the hardware dead zone?
#define OUTPUT_DEAD_ZONE_OFFSET (60)

// The output is scaled by multiplying it by the MUL value then dividing it by the DIV value.
// This reduces the dead zones at the extreme ends of the scale, giving you more range of wheel motion.
#define OUTPUT_SCALE_MUL (6)
#define OUTPUT_SCALE_DIV (8)

// How many samples to take to calculate the midpoint.
#define MIDPOINT_SAMPLE_COUNT (64)
// How many samples are pending?
volatile uint8_t midpoint_samples_pending = MIDPOINT_SAMPLE_COUNT;

// How large is the rough dead zone when we're waiting for the wheel to be released?
#define MIDPOINT_WAIT_DEAD_ZONE (128)
// How long should the wheel report its position (in samples) before we assume it's settled in the middle?
#define MIDPOINT_WAIT_TIME (20000)
// Timer to count samples where the wheel is in the midpoint dead zone.
volatile uint16_t waiting_midpoint_timer = 0;

ISR(ADC_vect) {
	
	// Sample the ADC
	int16_t value = ADC;
	
	switch (operating_mode) {
		case OPERATING_MODE_INTIALISING:
			// Wait to take the midpoint.
			operating_mode = OPERATING_MODE_WAITING_MIDPOINT;
			waiting_midpoint_timer = MIDPOINT_WAIT_TIME;
			if (value < 256) {
				// If we've pulled the wheel over to the extreme left, enable correction for a small dead zone.
				correcting = 1;
			} else if (value >= 768) {
				// If we've pulled the wheel over to the extreme right, disable correction for a large dead zone.
				correcting = 0;
			} else {
				// Wheel is not at the extremities, assume it's not being touched so load from EEPROM.
				// Shorten the midpoint timer too.
				waiting_midpoint_timer /= 2;
				correcting = !!eeprom_read_byte((void*)CORRECTING_EEPROM_ADDRESS);
			}
			eeprom_update_byte((void*)CORRECTING_EEPROM_ADDRESS, correcting);
			break;
		case OPERATING_MODE_WAITING_MIDPOINT:
			// Are we near the centre?
			if (abs(value - 512) > MIDPOINT_WAIT_DEAD_ZONE) {
				waiting_midpoint_timer = MIDPOINT_WAIT_TIME;
			} else if (--waiting_midpoint_timer == 0) {
				// Start taking the midpoint.
				midpoint = 0;
				midpoint_samples_pending = MIDPOINT_SAMPLE_COUNT;
				operating_mode = OPERATING_MODE_FINDING_MIDPOINT;
			}
			break;
		case OPERATING_MODE_FINDING_MIDPOINT:
			// Capture another value to help find the midpoint.
			midpoint += value;
			if (--midpoint_samples_pending == 0) {
				// We've got all our samples, take the mean and switch to the working mode.
				midpoint /= MIDPOINT_SAMPLE_COUNT;
				operating_mode = OPERATING_MODE_WORKING;
			}
			break;
		case OPERATING_MODE_WORKING:
			// Offset via the startup-calibrated midpoint.
			value -= midpoint;
			
			// Apply our own dead zone correction.
			if (correcting) {
				
				// Apply our own input dead zone.
				if (value > 0) {
					if (value < INPUT_DEAD_ZONE) {
						value = 0;
					} else {
						value -= INPUT_DEAD_ZONE;
					}
				} else if (value < 0) {
					if (value > -INPUT_DEAD_ZONE) {
						value = 0;
					} else {
						value += INPUT_DEAD_ZONE;
					}
				}
				
				// Diminish the value slightly.
				value *= OUTPUT_SCALE_MUL;
				value /= OUTPUT_SCALE_DIV;
				
				// Now offset the value by our own output dead zone compensation.
				if (value > 0) {
					value = value + OUTPUT_DEAD_ZONE_OFFSET;
				} else if (value < 0) {
					value = value - OUTPUT_DEAD_ZONE_OFFSET;
				}
			}
			
			// Offset the value to be compatible with the PWM output and clamp if necessary.
			value += 512;
			if (value < 0) value = 0;
			if (value > 1023) value = 1023;
			
			// Update the fast PWM timer with the new analogue value.
			OCR1A = value;
			break;
		default:
			operating_mode = OPERATING_MODE_INTIALISING;
			break;
	}
	
}

int main(void) {
	
	// Initialise analogue output using PWM
	TCCR1A = 0b10000011; // COM1A1:0 = 2 (non-inverting), WGM11:0 = 3 (fast PWM, 10-bit)
	TCCR1B = 0b00001001; // WGM13:2 = 1 (fast PWM, 10-bit), CS12:0 = 1 (no clock prescaler)
	OCR1A = 512;
	TCNT1 = 0;
	DDRB |= _BV(1); // PB1 (OC1A) = output
	
	// Initialise analogue input using ADC
	
	ADMUX = 0b01000000; // REFS1:0 = 1 (AVcc ref), MUX3:0 = 0 (ADC0)
	ADCSRA = 0b10000110; // ADEN = 1 (enable), ADPS2:0 = 6 (prescaler = 64)
	
	#if !defined (__AVR_ATmega8__)
	DIDR0 = 0b00000001; // ADC0D = 1 (disable digital pin buffer 0, not available on ATmega8)
	#endif
	
	#if defined (ADATE)
	ADCSRA |= _BV(ADATE); // Free-running ADC
	#elif defined (ADFR)
	ADCSRA |= _BV(ADFR); // Free-running ADC
	#else
	#error Cannot enable free-running ADC
	#endif

	ADCSRA |= _BV(ADIE); // Generate ADC interrupts
	ADCSRA |= _BV(ADSC); // Start ADC
	
	set_sleep_mode(0b001); // SM2:0 = 001 for ADC noise reduction mode.
	for (;;) {
		sleep_enable();
		sei();
		sleep_cpu();
		sleep_disable();
	}
	
}