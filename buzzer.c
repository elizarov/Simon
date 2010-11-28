/******************************************************************************
 * Interrupt-driven code for playing tones with piezoelectric buzzer.
 * (C) Roman Elizarov, 2010
 *****************************************************************************/

#include <avr/io.h>
#include <avr/interrupt.h>

#include "buzzer.h"

#define sbi(reg, bit)  (reg |= _BV(bit))
#define cbi(reg, bit)  (reg &= ~_BV(bit))

// Remaining number of half periods for buzzer time1 ISR
volatile uint16_t buzzer_count;

// Current buzzer callback routine (call it when done)
volatile buzzer_callback_t buzzer_done_callback;

// Interrupt Service Routine for timer overflow to flip buzzer
ISR(TIMER1_OVF_vect) {
	// flip both buzzer legs
	sbi(BUZZER_PIN1, BUZZER_BIT1);
	sbi(BUZZER_PIN2, BUZZER_BIT2);
	// decrement remaining counter
	uint16_t remaining = buzzer_count - 1;
	buzzer_count = remaining;
	// invoke callback when done
	if (remaining == 0)
		(*buzzer_done_callback)();
}

// Starts buzzer with a specified tone, counter of half-periods, and callback when done
// You can start_buzzer again in callback to play next tone or stop_buzzer to finish
void start_buzzer(uint16_t tone, uint16_t cnt, buzzer_callback_t done_callback) {
	if (!is_buzzer_working()) {
		// use timer1 in Fast PWM mode 15 for buzzer, no prescaler
		TCCR1A = _BV(WGM11) | _BV(WGM10);
		TCCR1B = _BV(WGM13) | _BV(WGM12) | _BV(CS10);
		// reset timer counter to zero
		TCNT1 = 0;
		// set one buzzer pin low and the other high
		cbi(BUZZER_PORT1, BUZZER_BIT1);
		sbi(BUZZER_PORT2, BUZZER_BIT2);
	}
	cbi(TIMSK1, TOIE1);                   // disable overflow interrupt
	OCR1A = tone;                         // set top to tone (half period of sound wave)
	buzzer_count = cnt;                   // set count of half periods
	buzzer_done_callback = done_callback; // set callback
	sbi(TIFR1, TOV1);                     // clear pending overflow interrupt flag
	sbi(TIMSK1, TOIE1);                   // enable overflow interrupt
}

// Stops buzzer
void stop_buzzer() {
	// disable overflow interrupt
	cbi(TIMSK1, TOIE1);
	// set both buzzer pins low so that it does not consume power
	cbi(BUZZER_PORT1, BUZZER_BIT1);
	cbi(BUZZER_PORT2, BUZZER_BIT2);
}

// Starts buzzer with a specified tone and counter of half-periods, and waits until it finishes
void buzzer_wait(uint16_t tone, uint16_t cnt) {
	start_buzzer(tone, cnt, &stop_buzzer);
	while (is_buzzer_working());
}
