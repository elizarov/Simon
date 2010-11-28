/*---------------------------------------------------------------------------*
  SIMON GAME

  6-19-2007
  Copyright Spark Fun Electronics© 2009
  Nathan Seidle
  nathan at sparkfun.com

  Generates random sequence, plays music, and displays button lights.

  Simon tones from Wikipedia
  * A (red, upper left) - 440Hz
  * a (green, upper right, an octave higher than the upper right) - 880Hz
  * D (blue, lower left, a perfect fourth higher than the upper left) - 587.33Hz
  * G (yellow, lower right, a perfect fourth higher than the lower left) - 784Hz

 *---------------------------------------------------------------------------*
  Cleaned up, added debounce and level control by Roman Elizarov, 2010.
  This version of Simon code does not depend on the clock source.
  It can work with the internal default 1MHz oscillator or a higher external one,
  provided that F_CPU is set property during compilation and fuzes are right.
  It uses timer1 to generate tones to get a perfectly accurate frequency.
 *---------------------------------------------------------------------------*/

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#include "buzzer.h"

/*---------------------------------------------------------------------------*
  HARDWARE ABSTRACTION LAYER (HAL)
  These methods hide and abstract all the hardware details of the specific
  Simon board: ports where leds and buttons, plus hardware-specific approach
  to random number generation. All ports are hard-coded in these methods.
  Methods that are used only once are hand-marked as inline.
 *---------------------------------------------------------------------------*/

// Utility macros to work with ports
#define sbi(reg, bit)          (reg |= _BV(bit))
#define cbi(reg, bit)          (reg &= ~_BV(bit))
#define getbit(reg, bit)       (reg & _BV(bit))
#define setbit(reg, bit, val) \
	if (val) sbi(reg, bit);   \
	    else cbi(reg, bit);

// Bit masks that define leds and buttons for set_leds and get_buttons
#define LED0 _BV(0)
#define LED1 _BV(1)
#define LED2 _BV(2)
#define LED3 _BV(3)

// Initializes hardware abstraction layer
inline void hal_init() {
	// 1 = output, 0 = input
	DDRB = 0b11111100;  // buttons 2,3 on PB0,1
	DDRD = 0b00111110;  // LEDs, buttons, buzzer, TX/RX

	PORTB = 0b00000011; // Enable pull-ups on buttons 2,3
	PORTD = 0b11000000; // Enable pull-ups on buttons 0,1

	// Use timer0 & timer2 for random number generation (see random method)
	// Together they will act like a 16bit timer
	TCCR0B = _BV(CS00);             // Run timer0 1:1   with CPU clock (no prescaler)
	TCCR2B = _BV(CS22) | _BV(CS21); // Run timer2 1:256 with CPU clock

	// Enable global interrupts (it is required for buzzer)
	sei();
}

// Lights leds according to bitmask
void set_leds(uint8_t mask) {
	setbit(PORTB, 2, mask & LED0);
	setbit(PORTD, 2, mask & LED1);
	setbit(PORTB, 5, mask & LED2);
	setbit(PORTD, 5, mask & LED3);
}

// Returns bitmask of buttons pressed
uint8_t get_buttons() {
	uint8_t mask = 0;
	if (!getbit(PINB, 0)) mask |= LED0;
	if (!getbit(PINB, 1)) mask |= LED1;
	if (!getbit(PIND, 7)) mask |= LED2;
	if (!getbit(PIND, 6)) mask |= LED3;
	return mask;
}

// Typedef for random seed
typedef union {
	uint32_t value;
	uint8_t bytes[4];
} rand_seed_t;

// Current seed for random()
rand_seed_t rand_seed;

// Generates random byte using timer0&2 as randomness source
inline uint8_t random() {
	rand_seed_t cur = rand_seed;
	cur.bytes[0] ^= TCNT0; // mangle seed based on timer0
	cur.bytes[1] ^= TCNT2; // mangle seed based on timer2
	cur.value = (cur.value * 22695477L + 1); // linear congruent PRNG
	rand_seed = cur;
	return cur.bytes[3]; // use most significant byte as random value
}

/*---------------------------------------------------------------------------*
  UTILITY METHODS FOR BUTTONS
  These methods provide utility function for button debouncing and counting.
 *---------------------------------------------------------------------------*/

#define DEBOUNCE_MS 5  // debounce delay in ms

// Waits for button(s) press and release until timeout (ms) with debounce
uint8_t wait_buttons(uint16_t time_ms) {
	uint8_t res = 0; // resulting buttons mask
	uint8_t cur = 0; // currently pressed buttons
	do {
		cur = get_buttons();
		res |= cur;
		_delay_ms(DEBOUNCE_MS); // this delay de-bounces read
		time_ms -= DEBOUNCE_MS;
	} while (time_ms >= DEBOUNCE_MS && (res == 0 || cur != 0));
	return res;
}

// Counts the number of button(s) pressed
inline uint8_t buttons_count(uint8_t mask) {
	uint8_t cnt = 0;
	if (mask & LED0) cnt++;
	if (mask & LED1) cnt++;
	if (mask & LED2) cnt++;
	if (mask & LED3) cnt++;
	return cnt;
}

/*---------------------------------------------------------------------------*
  AUDIO-VISUAL EFFECTS
  These methods provide various effects like winner and loose sequences,
  as well as button lights and tones
 *---------------------------------------------------------------------------*/

// Plays the loser sounds
inline void play_loser(void) {
	uint8_t i;
	for (i = 0; i < 4; i++) {
		set_leds((i & 1) ? (LED2 | LED3) : (LED0 | LED1));
		buzzer_wait(FREQLEN2TONECNT(333.33, 250));
	}
}

// Next tone for play_winner
volatile uint8_t winner_tone;

// Plays current winner tone and decrements it for a higher note next
void next_winner_tone() {
	uint8_t tone = winner_tone;
	if (tone > 70) {
		start_buzzer(tone * (F_CPU / 1000000), 6, &next_winner_tone);
		winner_tone = tone - 1;
	} else {
		stop_buzzer();
	}
}

// Plays the winner sounds
inline void play_winner(void) {
	uint8_t i;
	for (i = 0; i < 4; i++) {
		set_leds((i & 1) ? (LED0 | LED3) : (LED1 | LED2));
		winner_tone = 250;
		next_winner_tone();
		while (is_buzzer_working());
	}
}

// Indicate the start of game play
inline static void play_start() {
	set_leds(LED0 | LED1 | LED2 | LED3);
	_delay_ms(1000);
	 set_leds(0);
	_delay_ms(250);
}

/*---------------------------------------------------------------------------*
  GAMEPLAY UTILITIES
  These methods generate game string, play it back, test pressed button
  sequence, and perform overall logic of a single game.
 *---------------------------------------------------------------------------*/

#define MAX_GAME_LEVEL 64
#define WINNER 1
#define LOSER  0

// Play button tone for 150 ms
#define BUTTON_LENGTH_MS 150

// Button Tone and Count array entries generation macro
#define BTC(freq) FREQLEN2TONECNT(freq, BUTTON_LENGTH_MS)

// Current game variables
uint8_t game_sequence[MAX_GAME_LEVEL]; // contains 0..3 button numbers for a game
uint8_t game_position;                 // current game position from 0
uint8_t game_level = 5;                // default game level if game starts with single button press.

uint16_t BUTTONS[8] = {
		BTC(440.00), // (red, upper left) - 440Hz
		BTC(880.00), // (green, upper right, an octave higher than the upper right) - 880Hz
		BTC(587.33), // (blue, lower left, a perfect fourth higher than the upper left) - 587.33Hz
		BTC(784.00)  // (yellow, lower right, a perfect fourth higher than the lower left) - 784Hz
};

// Generates button tone and highlights the corresponding button
void button_tone(uint8_t button) {
	set_leds(_BV(button)); // Turn on button led
	uint16_t *btc = &BUTTONS[2 * button]; // Pointer to BTC entry for the button
	buzzer_wait(*btc, *(btc + 1));
	set_leds(0);           // Turn off all LEDs
}

// Starts new game
inline void new_game_sequence() {
	game_position = 0;
}

// Adds a new random button to the game sequence
inline void add_to_game_sequence(void) {
	game_sequence[game_position++] = random() & 3;
}

// Plays the current contents of the game sequence
inline static void play_game_sequence(void) {
	uint8_t pos;
	for (pos = 0; pos < game_position; pos++) {
		button_tone(game_sequence[pos]);
		_delay_ms(150);
	}
}

// Display fancy LED pattern waiting for any button to be pressed
// Wait for user to begin game
inline static void wait_start() {
	uint8_t mask = 0;
	uint8_t buttons;
	uint8_t cnt;
	uint8_t led = LED0;

	do {
		set_leds(led);
		mask = wait_buttons(100); // 100ms max wait
		led = led == LED3 ? LED0 : led << 1; // next led
	} while (mask == 0);

	// wait more until all buttons are released
	set_leds(0);
	buttons = mask;
	do {
		mask = get_buttons();
		buttons |= mask;
	} while (mask != 0);

	// configure game level depending on number of buttons pressed
	cnt = buttons_count(buttons);
	if (cnt == 2)
		game_level = 15;
	else if (cnt == 3)
		game_level = 20;
	else if (cnt == 4)
		game_level = 25;
}

// Tests if game sequence is pressed correctly, returns WINNER or LOSER
inline static uint8_t test_game_sequence() {
	uint8_t mask;
	uint8_t pos;
	for (pos = 0; pos < game_position; pos++) {
		mask = wait_buttons(3000); // Wait at most 3 sec for button press
		if (mask != _BV(game_sequence[pos]))
			return LOSER;
		// Fire the button and play the button tone
		button_tone(game_sequence[pos]);
	}
	return WINNER;
}

// Plays a single game and returns WINNER or LOSER
inline static uint8_t single_game() {
	new_game_sequence();
	while (1) {
		add_to_game_sequence();    // Add the button to the game sequence
		play_game_sequence();      // Play the current contents of the game sequence back for the player
		if (!test_game_sequence()) // Player presses buttons to repeat sequence
			return LOSER;
		// If user reaches the game level, they win!
		if (game_position == game_level)
			return WINNER;
		// Otherwise, we need to wait just a hair before we play back longer sequence again
		_delay_ms(1000);
	}
}

/*---------------------------------------------------------------------------*
  MAIN
  Brings it all together
 *---------------------------------------------------------------------------*/

void main() __attribute__ ((noreturn));

void main() {
	hal_init();  // Setup IO pins and defaults
	while (1) {  // Repeatedly play games
		wait_start();
		play_start();
		if (single_game()) {
			play_winner();
			game_level++; // Next level
		} else {
			play_loser();
		}
	}
}
