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
#define sbi(port, pin)          (port |= (1 << pin))
#define cbi(port, pin)          (port &= ~(1 << pin))
#define getbit(port, pin)       (port & (1 << pin))
#define setbit(port, pin, val) \
	if (val) sbi(port, pin);   \
	    else cbi(port, pin);

// Bit masks that define leds and buttons for set_leds and get_buttons
#define LED1 1
#define LED2 2
#define LED3 4
#define LED4 8

uint32_t rand_seed; // current seed for random

// Initializes hardware abstraction layer
inline void hal_init() {
	// 1 = output, 0 = input
	DDRB = 0b11111100;  // button 2,3 on PB0,1
	DDRD = 0b00111110;  // LEDs, buttons, buzzer, TX/RX

	PORTB = 0b00000011; // Enable pull-ups on buttons 2,3
	PORTD = 0b11000000; // Enable pull-ups on buttons 0,1

    // Init timer 1 for random number generation
    TCCR1B = (1<<CS10); // No prescaler -- run timer 1:1 with CPU clock
}

// Lights leds according to bitmask
void set_leds(uint8_t mask) {
	setbit(PORTB, 2, mask & LED1);
	setbit(PORTD, 2, mask & LED2);
	setbit(PORTB, 5, mask & LED3);
	setbit(PORTD, 5, mask & LED4);
}

// Returns bitmask of buttons pressed
uint8_t get_buttons() {
	uint8_t mask = 0;
	if (getbit(PINB, 0)) mask |= LED1;
	if (getbit(PINB, 1)) mask |= LED2;
	if (getbit(PIND, 7)) mask |= LED3;
	if (getbit(PIND, 6)) mask |= LED4;
	return mask;
}

// Flips the buzzer between two states -- 0 and 1
inline void set_buzzer(uint8_t val) {
	setbit(PORTD, 3, val);
	setbit(PORTD, 4, !val);
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
	if (mask & LED1) cnt++;
	if (mask & LED2) cnt++;
	if (mask & LED3) cnt++;
	if (mask & LED4) cnt++;
	return cnt;
}

// Plays note of specified length (ms) and tone half-period (us)
inline static void play_note(uint16_t length_ms, uint16_t half_period_us) {
	uint32_t length_us = length_ms * (uint32_t) 1000;
	uint8_t val = 0;
	while (length_us >= half_period_us) {
		length_us -= half_period_us;
		set_buzzer(val); // Toggle the buzzer
		delay_us(half_period_us);
		val = !val;
	}
}

uint8_t game_string[64];
uint8_t game_string_position;
uint8_t game_level = 5; // default game level if game starts with single button press.

//Define functions
//======================

void welcome();
void play_loser();
void play_winner();
void play_string();
void add_to_string();
void toner(uint8_t, uint16_t);

//======================

int main(void) {
	uint8_t choice;
	uint8_t current_pos;

	hal_init(); //Setup IO pins and defaults

BEGIN_GAME:

	welcome();

	game_string_position = 0; //Start new game

	while (1) {
		add_to_string(); //Add the first button to the string
		play_string(); //Play the current contents of the game string back for the player

		//Wait for user to input buttons until they mess up, reach the end of the current string, or time out
		for (current_pos = 0; current_pos < game_string_position; current_pos++) {
			choice = wait_buttons(3000); //Wait at most 3 sec
			if (buttons_count(choice) == 1)
				toner(choice, 150); //Fire the button and play the button tone
			if (choice != game_string[current_pos]) {
				play_loser(); //Play anoying loser tones
				goto BEGIN_GAME;
			}
		}//End user input loop

		//If user reaches the game length of X, they win!
		if (current_pos == game_level) {
			play_winner(); //Play winner tones
			game_level++; // next level
			goto BEGIN_GAME;
		}

		//Otherwise, we need to wait just a hair before we play back the last string
		_delay_ms(1000);
	}

	return (0);
}

//Display fancy LED pattern waiting for any button to be pressed
//Wait for user to begin game
void welcome() {
	uint8_t choice = 0;
	uint8_t buttons;
	uint8_t cnt;
	uint8_t led = LED1;

	do {
		set_leds(led);
		choice = wait_buttons(100); // 100ms max wait
		led = led == LED4 ? LED1 : led << 1; // next led
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

	//Indicate the start of game play
	set_leds(LED1 | LED2 | LED3 | LED4);
	_delay_ms(1000);
	set_leds(0);
	_delay_ms(250);
}


//Waits until button pressed and released or timeout (in millis) passes
//Plays the loser sounds
void play_loser(void) {
	set_leds(LED1 | LED2);
	toner(0, 255);
	set_leds(LED3 | LED4);
	toner(0, 255);
	set_leds(LED1 | LED2);
	toner(0, 255);
	set_leds(LED3 | LED4);
	toner(0, 255);
}

//Plays the winner sounds
void play_winner(void) {
	uint8_t x, y, z;

	set_leds(LED1 | LED2 | LED3 | LED4);
	for (z = 0; z < 4; z++) {
		if (z == 0 || z == 2) {
			set_leds(LED2 | LED3);
		} else {
			set_leds(LED1 | LED4);
		}

		for (x = 250; x > 70; x--) {
			for (y = 0; y < 3; y++) {
				//Toggle the buzzer at various speeds
				set_buzzer(0);
				delay_us(x);
				set_buzzer(1);
				delay_us(x);
			}
		}
	}
}

//Plays the current contents of the game string
void play_string(void) {
	uint8_t string_pos;
	uint8_t button;

	for (string_pos = 0; string_pos < game_string_position; string_pos++) {
		button = game_string[string_pos];
		toner(button, 150);
		_delay_ms(150);
	}
}

//Adds a new random button to the game sequence based on the current timer elapsed
void add_to_string(void) {
	uint8_t new_button = 1 << (random() & 3);
	game_string[game_string_position] = new_button;
	game_string_position++;
}

//Tone generator
//(red, upper left) - 440Hz - 2.272ms - 1.136ms pulse
//(green, upper right, an octave higher than the upper right) - 880Hz - 1.136ms - 0.568ms pulse
//(blue, lower left, a perfect fourth higher than the upper left) - 587.33Hz - 1.702ms - 0.851ms pulse
//(yellow, lower right, a perfect fourth higher than the lower left) - 784Hz - 1.276ms - 0.638ms pulse
void toner(uint8_t tone, uint16_t length_ms) {
	uint16_t half_period_us;

	switch (tone) {
	case LED1:
		half_period_us = 1136; //440Hz = 2272us Upper left, Red
		set_leds(LED1);
		break;

	case LED2:
		half_period_us = 568; //Upper right, Green
		set_leds(LED2);
		break;

	case LED3:
		half_period_us = 851; //Lower left, Blue
		set_leds(LED3);
		break;

	case LED4:
		half_period_us = 638; //Lower right, Yellow
		set_leds(LED4);
		break;
	default:
		// Failure tone
		half_period_us = 1500;
	}

	play_note(length_ms, half_period_us);

	//Turn off all LEDs
	set_leds(0);
}
