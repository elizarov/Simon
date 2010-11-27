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
	PORTD = 0b11001000; // Enable pull-ups on buttons 0,1, enable buzzer initial state

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

// Flips the buzzer between two states
inline void toggle_buzzer() {
	sbi(PIND, 3);
	sbi(PIND, 4);
}

// Generates random byte
inline uint8_t random() {
	rand_seed ^= TCNT1; // adds physical timing randomness
	rand_seed = (rand_seed * 22695477L + 1); // linear congruent PRNG
	return rand_seed >> 16;
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
inline static void play_note(uint16_t length_ms, uint16_t half_period_us) {
	uint32_t length_us = length_ms * (uint32_t) 1000;
	while (length_us >= half_period_us) {
		length_us -= half_period_us;
		toggle_buzzer(); // Toggle the buzzer
		delay_us(half_period_us);
	}
}

/*---------------------------------------------------------------------------*
  AUDIO-VISUAL EFFECTS
  These methods provide various effects like winner and loose sequences,
  as well as button lights and tones
 *---------------------------------------------------------------------------*/

// Tone generator
// (red, upper left) - 440Hz - 2.272ms - 1.136ms pulse
// (green, upper right, an octave higher than the upper right) - 880Hz - 1.136ms - 0.568ms pulse
// (blue, lower left, a perfect fourth higher than the upper left) - 587.33Hz - 1.702ms - 0.851ms pulse
// (yellow, lower right, a perfect fourth higher than the lower left) - 784Hz - 1.276ms - 0.638ms pulse
inline static void toner(uint8_t mask, uint16_t length_ms) {
	uint16_t half_period_us = 0;
	set_leds(mask);
	switch (mask) {
	case LED0:
		half_period_us = 1136; //440Hz = 2272us Upper left, Red
		break;
	case LED1:
		half_period_us = 568; //Upper right, Green
		break;
	case LED2:
		half_period_us = 851; //Lower left, Blue
		break;
	case LED3:
		half_period_us = 638; //Lower right, Yellow
		break;
	}
	play_note(length_ms, half_period_us);
	set_leds(0); // Turn off all LEDs
}

// Plays the loser sounds
inline static void play_loser(void) {
	set_leds(LED0 | LED1);
	play_note(250, 1500);
	set_leds(LED2 | LED3);
	play_note(250, 1500);
	set_leds(LED0 | LED1);
	play_note(250, 1500);
	set_leds(LED2 | LED3);
	play_note(250, 1500);
}

// Plays the winner sounds
inline static void play_winner(void) {
	uint8_t x, y, z;
	for (z = 0; z < 4; z++) {
		if ((z & 1) == 0) {
			set_leds(LED1 | LED2);
		} else {
			set_leds(LED0 | LED3);
		}

		// Toggle the buzzer at various speeds
		for (x = 250; x > 70; x--) {
			for (y = 0; y < 6; y++) {
				toggle_buzzer();
				delay_us(x);
			}
		}
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
  These methods generate game string and play it back
 *---------------------------------------------------------------------------*/

#define MAX_GAME_LEVEL 64

uint8_t game_string[MAX_GAME_LEVEL];
uint8_t game_string_position;
uint8_t game_level = 5; // default game level if game starts with single button press.

// Starts new game
inline void new_game() {
	game_string_position = 0;
}

// Adds a new random button to the game sequence
inline void add_to_game_string(void) {
	uint8_t new_button = 1 << (random() & 3);
	game_string[game_string_position] = new_button;
	game_string_position++;
}

// Plays the current contents of the game string
inline static void play_game_string(void) {
	uint8_t string_pos;
	uint8_t button;
	for (string_pos = 0; string_pos < game_string_position; string_pos++) {
		button = game_string[string_pos];
		toner(button, 150);
		_delay_ms(150);
	}
}

// Display fancy LED pattern waiting for any button to be pressed
// Wait for user to begin game
inline static void wait_start() {
	uint8_t choice = 0;
	uint8_t buttons;
	uint8_t cnt;
	uint8_t led = LED0;

	do {
		set_leds(led);
		choice = wait_buttons(100); // 100ms max wait
		led = led == LED3 ? LED0 : led << 1; // next led
	} while (choice == 0);

	// wait more until all buttons are released
	set_leds(0);
	buttons = choice;
	do {
		choice = get_buttons();
		buttons |= choice;
	} while (choice != 0);

	// configure game level depending on number of buttons pressed
	cnt = buttons_count(buttons);
	if (cnt == 2)
		game_level = 15;
	else if (cnt == 3)
		game_level = 20;
	else if (cnt == 4)
		game_level = 25;
}

/*---------------------------------------------------------------------------*
  MAIN
  Brings it all together
 *---------------------------------------------------------------------------*/

int main(void) {
	uint8_t choice;
	uint8_t current_pos;

	hal_init(); //Setup IO pins and defaults

BEGIN_GAME:

	wait_start();
	play_start();
	new_game();

	while (1) {
		add_to_game_string(); // Add the first button to the string
		play_game_string(); // Play the current contents of the game string back for the player

		// Wait for user to input buttons until they mess up, reach the end of the current string, or time out
		for (current_pos = 0; current_pos < game_string_position; current_pos++) {
			choice = wait_buttons(3000); // Wait at most 3 sec
			if (buttons_count(choice) == 1)
				toner(choice, 150); // Fire the button and play the button tone
			if (choice != game_string[current_pos]) {
				play_loser(); // Play anoying loser tones
				goto BEGIN_GAME;
			}
		}// End user input loop

		// If user reaches the game length of X, they win!
		if (current_pos == game_level) {
			play_winner(); //Play winner tones
			game_level++; // next level
			goto BEGIN_GAME;
		}

		// Otherwise, we need to wait just a hair before we play back the last string
		_delay_ms(1000);
	}
	return (0);
}
