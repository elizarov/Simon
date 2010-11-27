/*---------------------------------------------------------------------------*
  SIMON GAME

  6-19-2007
  Copyright Spark Fun Electronics© 2009
  Nathan Seidle
  nathan at sparkfun.com

  Cleaned up, added debounce and level control by Roman Elizarov, 2010.

  Generates random sequence, plays music, and displays button lights.

  Simon tones from Wikipedia

  * A (red, upper left) - 440Hz - 2.272ms - 1.136ms pulse
  * a (green, upper right, an octave higher than the upper right) - 880Hz - 1.136ms - 0.568ms pulse
  * D (blue, lower left, a perfect fourth higher than the upper left) - 587.33Hz - 1.702ms - 0.851ms pulse
  * G (yellow, lower right, a perfect fourth higher than the lower left) - 784Hz - 1.276ms - 0.638ms pulse

  The tones are close, but probably off a bit, but they sound all right.

  This version of Simon relies on the internal default 1MHz oscillator.
  Do not set the external fuses.
 *---------------------------------------------------------------------------*/

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

/*---------------------------------------------------------------------------*
  HARDWARE ABSTRACTION LAYER (HAL)
  These methods hide and abstract all the hardware details of the specific
  Simon board: ports where leds, buttons, and buzzer are connected, plus
  hardware-specific approach to random number generation.
  This is the only piece of code that works directly with hardware ports.
  All ports are hard-coded in these methods.
  Methods that are used only once are hand-marked as inline.
 *---------------------------------------------------------------------------*/

// Utility macros to work with ports
#define sbi(port, pin)          (port |= _BV(pin))
#define cbi(port, pin)          (port &= ~_BV(pin))
#define getbit(port, pin)       (port & _BV(pin))
#define setbit(port, pin, val) \
	if (val) sbi(port, pin);   \
	    else cbi(port, pin);

// Bit masks that define leds and buttons for set_leds and get_buttons
#define LED0 _BV(0)
#define LED1 _BV(1)
#define LED2 _BV(2)
#define LED3 _BV(3)

uint32_t rand_seed; // current seed for random

// Initializes hardware abstraction layer
inline void hal_init() {
	// 1 = output, 0 = input
	DDRB = 0b11111100;  // buttons 2,3 on PB0,1
	DDRD = 0b00111110;  // LEDs, buttons, buzzer, TX/RX

	PORTB = 0b00000011; // Enable pull-ups on buttons 2,3
	PORTD = 0b11000000; // Enable pull-ups on buttons 0,1

	// Init timer 1 for random number generation
	TCCR1B = _BV(CS10); // No prescaler -- run timer 1:1 with CPU clock
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

// Enables buzzer and sets it to initial state
inline void enable_buzzer() {
	cbi(PORTD, 3);
	sbi(PORTD, 4);
}

// Disables buzzer, so that it does not consume power
inline void disable_buzzer() {
	cbi(PORTD, 3);
	cbi(PORTD, 4);
}

// Flips the buzzer between two states
inline void toggle_buzzer() {
	sbi(PIND, 3);
	sbi(PIND, 4);
}

// Generates random byte
inline uint8_t random() {
	rand_seed ^= TCNT1; // adds physical timing randomness
	rand_seed = (rand_seed * 22695477L + 1); // linear congruent PRNG
	return rand_seed >> 24;
}

// Waits a specified number of microseconds, assumes 1 MHz clock (1us tick)
inline static void delay_us(uint16_t time_us) {
	_delay_loop_2(time_us >> 2); // delay_loop_2 has 4 ticks per loop
}

/*---------------------------------------------------------------------------*
  UTILITY METHODS
  These methods provide various utility function like button debouncing,
  playing tones, etc.
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
uint8_t buttons_count(uint8_t mask) {
	uint8_t cnt = 0;
	if (mask & LED0) cnt++;
	if (mask & LED1) cnt++;
	if (mask & LED2) cnt++;
	if (mask & LED3) cnt++;
	return cnt;
}

// Plays note of specified length (ms) and tone half-period (us)
inline static void play_tone(uint16_t length_ms, uint16_t half_period_us) {
	uint32_t length_us = length_ms * (uint32_t) 1000;
	enable_buzzer();
	while (length_us >= half_period_us) {
		length_us -= half_period_us;
		toggle_buzzer(); // Toggle the buzzer
		delay_us(half_period_us);
	}
	disable_buzzer();
}

/*---------------------------------------------------------------------------*
  AUDIO-VISUAL EFFECTS
  These methods provide various effects like winner and loose sequences,
  as well as button lights and tones
 *---------------------------------------------------------------------------*/

// Plays the loser sounds
inline static void play_loser(void) {
	uint8_t i;
	for (i = 0; i < 4; i++) {
		set_leds((i & 1) ? (LED2 | LED3) : (LED0 | LED1));
		play_tone(250, 1500);
	}
}

// Plays the winner sounds
inline static void play_winner(void) {
	uint8_t x, y, z;
	enable_buzzer();
	for (z = 0; z < 4; z++) {
		set_leds((z & 1) ? (LED0 | LED3) : (LED1 | LED2));
		// Toggle the buzzer at various speeds
		for (x = 250; x > 70; x--) {
			for (y = 0; y < 6; y++) {
				toggle_buzzer();
				delay_us(x);
			}
		}
	}
	disable_buzzer();
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

uint8_t game_sequence[MAX_GAME_LEVEL]; // contains 0..3 button numbers for a game
uint8_t game_position;                 // current game position from 0
uint8_t game_level = 5;                // default game level if game starts with single button press.

// (red, upper left) - 440Hz - 2.272ms - 1.136ms pulse
// (green, upper right, an octave higher than the upper right) - 880Hz - 1.136ms - 0.568ms pulse
// (blue, lower left, a perfect fourth higher than the upper left) - 587.33Hz - 1.702ms - 0.851ms pulse
// (yellow, lower right, a perfect fourth higher than the lower left) - 784Hz - 1.276ms - 0.638ms pulse
uint16_t BUTTON_TONES[4] = { 1136, 568, 851, 638 };

// Generates button tone and highlights the corresponding button
void button_tone(uint8_t button) {
	set_leds(_BV(button));
	play_tone(150, BUTTON_TONES[button]);
	set_leds(0); // Turn off all LEDs
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
