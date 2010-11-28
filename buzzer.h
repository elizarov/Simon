/******************************************************************************
 * Interrupt-driven code for playing tones with piezoelectric buzzer.
 * This code uses 16-bit Timer1 AVR hardware for precise frequency control
 * and is completely interrupt driven. One tone can be changed to the next one
 * seamlessly without any shift in phase.
 * (C) Roman Elizarov, 2010
 *****************************************************************************/

#ifndef BUZZER_H_
#define BUZZER_H_

#include <avr/io.h>

// Defines physical connection on the buzzer
#define BUZZER_PORT1 PORTD
#define BUZZER_PIN1  PIND
#define BUZZER_BIT1  3
#define BUZZER_PORT2 PORTD
#define BUZZER_PIN2  PIND
#define BUZZER_BIT2  4

// Converts frequency (in HZ) into tone value for start_buzzer
#define FREQ2TONE(freq)             ((uint16_t)(F_CPU / (freq) / 2))

// Converts tone and length (in ms) into cnt value for start_buzzer
#define TONELEN2CNT(tone, len)      ((uint16_t)(((uint32_t)1000 * (len)) / (tone)))

// Converts frequency (in HZ) and length (in ms) into cnt value for start_buzzer
#define FREQLEN2CNT(freq, len)      TONELEN2CNT(FREQ2TONE(freq), len)

// Converts frequency (in HZ) and length (in ms) into tone, cnt pair for start_buzzer
#define FREQLEN2TONECNT(freq, len)  FREQ2TONE(freq), FREQLEN2CNT(freq, len)

typedef void (*buzzer_callback_t)();

// Returns true when buzzer is working
inline uint8_t is_buzzer_working() {
	return TIMSK1 & _BV(TOIE1);
}

// Starts buzzer with a specified tone, counter of half-periods, and callback when done
// You can start_buzzer again in callback to play next tone or stop_buzzer to finish
extern void start_buzzer(uint16_t tone, uint16_t cnt, buzzer_callback_t done_callback);

// Stops buzzer
extern void stop_buzzer();

// Starts buzzer with a specified tone and counter of half-periods, and waits until it finishes
extern void buzzer_wait(uint16_t tone, uint16_t cnt);

#endif /* BUZZER_H_ */
