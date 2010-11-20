/*
 SIMON GAME

 6-19-2007
 Copyright Spark Fun Electronics© 2009
 Nathan Seidle
 nathan at sparkfun.com

 Ported to ATmega368 and cleaned up by Roman Elizarov, 2010.

 Generates random sequence, plays music, and displays button lights.

 Simon tones from Wikipedia

 * A (red, upper left) - 440Hz - 2.272ms - 1.136ms pulse
 * a (green, upper right, an octave higher than the upper right) - 880Hz - 1.136ms - 0.568ms pulse
 * D (blue, lower left, a perfect fourth higher than the upper left) - 587.33Hz - 1.702ms - 0.851ms pulse
 * G (yellow, lower right, a perfect fourth higher than the lower left) - 784Hz - 1.276ms - 0.638ms pulse

 The tones are close, but probably off a bit, but they sound all right.

 This version of Simon relies on the internal default 1MHz oscillator. Do not set the external fuses.
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#define BUZZER1_PORT	PORTD
#define BUZZER1			3
#define BUZZER2_PORT	PORTD
#define BUZZER2			4

#define sbi(port_name,pin_number)   ((port_name) |= (1<<pin_number))
#define cbi(port_name,pin_number)   ((port_name) &= ~(1 << pin_number))

#define LED1 1
#define LED2 2
#define LED3 4
#define LED4 8

#define DEBOUNCE 5  // debounce delay in ms

uint8_t game_string[64];
uint8_t game_string_position;
uint8_t game_level = 5; // default game level if game starts with single button press.

//Define functions
//======================

void ioinit();
void welcome();
void leds(uint8_t);
uint8_t check_button();
uint8_t button_count(uint8_t);
uint8_t read_button(uint16_t);
void play_loser();
void play_winner();
void play_string();
void add_to_string();
uint8_t random();
void toner(uint8_t, uint16_t);

inline static void delay_us(uint16_t d) {
	_delay_loop_2(d >> 2); // 4 iterations per loop
}

//======================

int main(void) {
	uint8_t choice;
	uint8_t current_pos;

	ioinit(); //Setup IO pins and defaults

BEGIN_GAME:

	welcome();

	game_string_position = 0; //Start new game

	while (1) {
		add_to_string(); //Add the first button to the string
		play_string(); //Play the current contents of the game string back for the player

		//Wait for user to input buttons until they mess up, reach the end of the current string, or time out
		for (current_pos = 0; current_pos < game_string_position; current_pos++) {
			choice = read_button(3000); //Wait at most 3 sec
			if (button_count(choice) == 1)
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

void ioinit(void) {
	//1 = output, 0 = input
	DDRB = 0b11111100; //button 2,3 on PB0,1
	DDRD = 0b00111110; //LEDs, buttons, buzzer, TX/RX

	PORTB = 0b00000011; //Enable pull-ups on buttons 2,3
	PORTD = 0b11000000; //Enable pull-ups on buttons 0,1

    //Init timer 2 for random number generation
    TCCR2B = (1<<CS22); // Set prescaler to clk/64 = 64us per tick = 16ms to loop counter

	sei();
}

//Display fancy LED pattern waiting for any button to be pressed
//Wait for user to begin game
void welcome() {
	uint8_t choice = 0;
	uint8_t buttons;
	uint8_t cnt;
	uint8_t led = LED1;

	do {
		leds(led);
		choice = read_button(100); // 100ms max wait
		led = led == LED4 ? LED1 : led << 1; // next led
	} while (choice == 0);

	// wait more until all buttons are released
	leds(0);
	buttons = choice;
	do {
		choice = check_button();
		buttons |= choice;
	} while (choice != 0);

	// configure game level depending on number of buttons pressed
	cnt = button_count(buttons);
	if (cnt == 2)
		game_level = 15;
	else if (cnt == 3)
		game_level = 20;
	else if (cnt == 4)
		game_level = 25;

	//Indicate the start of game play
	leds(LED1 | LED2 | LED3 | LED4);
	_delay_ms(1000);
	leds(0);
	_delay_ms(250);
}

//Lights leds according to bitmask
void leds(uint8_t mask) {
  if (mask & LED1)
	  sbi(PORTB, 2);
  else
	  cbi(PORTB, 2);
  if (mask & LED2)
	  sbi(PORTD, 2);
  else
	  cbi(PORTD, 2);
  if (mask & LED3)
	  sbi(PORTB, 5);
  else
	  cbi(PORTB, 5);
  if (mask & LED4)
	  sbi(PORTD, 5);
  else
	  cbi(PORTD, 5);
}

//Returns a bitmask of buttons pressed
uint8_t check_button(void) {
	uint8_t choice = 0;
	if ((PINB & (1 << 0)) == 0)
		choice |= LED1;
	if ((PINB & (1 << 1)) == 0)
		choice |= LED2;
	if ((PIND & (1 << 7)) == 0)
		choice |= LED3;
	if ((PIND & (1 << 6)) == 0)
		choice |= LED4;
	return choice;
}

//Counts number of button pressed
uint8_t button_count(uint8_t choice) {
	uint8_t cnt = 0;
	if (choice & LED1) cnt++;
	if (choice & LED2) cnt++;
	if (choice & LED3) cnt++;
	if (choice & LED4) cnt++;
	return cnt;
}

//Waits until button pressed and released or timeout (in millis) passes
uint8_t read_button(uint16_t time_limit) {
	uint8_t buttons = 0;
	uint8_t choice = 0;
	do {
		choice = check_button();
		buttons |= choice;
		_delay_ms(DEBOUNCE); // this 5 ms delay also de-bounces read
		time_limit -= DEBOUNCE;
	} while (time_limit >= DEBOUNCE && (buttons == 0 || choice != 0));
	return buttons;
}

//Plays the loser sounds
void play_loser(void) {
	leds(LED1 | LED2);
	toner(0, 255);
	leds(LED3 | LED4);
	toner(0, 255);
	leds(LED1 | LED2);
	toner(0, 255);
	leds(LED3 | LED4);
	toner(0, 255);
}

//Plays the winner sounds
void play_winner(void) {
	uint8_t x, y, z;

	leds(LED1 | LED2 | LED3 | LED4);
	for (z = 0; z < 4; z++) {
		if (z == 0 || z == 2) {
			leds(LED2 | LED3);
		} else {
			leds(LED1 | LED4);
		}

		for (x = 250; x > 70; x--) {
			for (y = 0; y < 3; y++) {
				//Toggle the buzzer at various speeds
				sbi(BUZZER2_PORT, BUZZER2);
				cbi(BUZZER1_PORT, BUZZER1);

				delay_us(x);

				cbi(BUZZER2_PORT, BUZZER2);
				sbi(BUZZER1_PORT, BUZZER1);

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

// Generates random number
uint8_t random() {
	static uint32_t rand_seed;
	rand_seed += TCNT2; // add some physical randomness, so that each start is different
	rand_seed = (rand_seed * 22695477L + 1);
	return rand_seed >> 16;
}

//Tone generator
//(red, upper left) - 440Hz - 2.272ms - 1.136ms pulse
//(green, upper right, an octave higher than the upper right) - 880Hz - 1.136ms - 0.568ms pulse
//(blue, lower left, a perfect fourth higher than the upper left) - 587.33Hz - 1.702ms - 0.851ms pulse
//(yellow, lower right, a perfect fourth higher than the lower left) - 784Hz - 1.276ms - 0.638ms pulse
void toner(uint8_t tone, uint16_t buzz_length_ms) {
	uint32_t buzz_length_us = buzz_length_ms * (uint32_t) 1000;
	uint16_t buzz_delay;

	switch (tone) {
	case LED1:
		buzz_delay = 1136; //440Hz = 2272us Upper left, Red
		leds(LED1);
		break;

	case LED2:
		buzz_delay = 568; //Upper right, Green
		leds(LED2);
		break;

	case LED3:
		buzz_delay = 851; //Lower left, Blue
		leds(LED3);
		break;

	case LED4:
		buzz_delay = 638; //Lower right, Yellow
		leds(LED4);
		break;
	default:
		// Failure tone
		buzz_delay = 1500;
	}

	//Run buzzer for buzz_length_us
	while (buzz_length_us >= 2 * buzz_delay) {
		//Subtract the buzz_delay from the overall length
		buzz_length_us -= 2 * buzz_delay;

		//Toggle the buzzer at various speeds
		cbi(BUZZER1_PORT, BUZZER1);
		sbi(BUZZER2_PORT, BUZZER2);
		delay_us(buzz_delay);

		sbi(BUZZER1_PORT, BUZZER1);
		cbi(BUZZER2_PORT, BUZZER2);
		delay_us(buzz_delay);
	}

	//Turn off all LEDs
	leds(0);
}
