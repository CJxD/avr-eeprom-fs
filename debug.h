/*  Author: Steve Gunn
 * Licence: This work is licensed under the Creative Commons Attribution License.
 *          View this license at http://creativecommons.org/about/licenses/
 *   Notes: F_CPU must be defined to match the clock frequency
 *   Usage: Include in your main file and call init_debug_uart0() (or init_debug_uart1())
 *          from the beginning of main to initialise redirection of stdout, stderr 
 *          and stdin to UART0 (or UART1).
 */

#define __ASSERT_USE_STDERR
#include <assert.h>
#include <stdio.h>
#include <avr/io.h>

#define DEBUG_BAUD  9600

int uputchar0(char c, FILE *stream)
{
	if (c == '\n') uputchar0('\r', stream);
	while (!(UCSR0A & _BV(UDRE0)));
	UDR0 = c;
	return c;
}

int ugetchar0(FILE *stream)
{
	while(!(UCSR0A & _BV(RXC0)));
	return UDR0;
}

void init_debug_uart0(void)
{
	/* Configure UART0 baud rate, one start bit, 8-bit, no parity and one stop bit */
	UBRR0H = (F_CPU/(DEBUG_BAUD*16L)-1) >> 8;
	UBRR0L = (F_CPU/(DEBUG_BAUD*16L)-1);
	UCSR0B = _BV(RXEN0) | _BV(TXEN0);
	UCSR0C = _BV(UCSZ00) | _BV(UCSZ01);

	/* Setup new streams for input and output */
	static FILE uout = FDEV_SETUP_STREAM(uputchar0, NULL, _FDEV_SETUP_WRITE);
	static FILE uin = FDEV_SETUP_STREAM(NULL, ugetchar0, _FDEV_SETUP_READ);

	/* Redirect all standard streams to UART0 */
	stdout = &uout;
	stderr = &uout;
	stdin = &uin;
}



int uputchar1(char c, FILE *stream)
{
	if (c == '\n') uputchar1('\r', stream);
	while (!(UCSR1A & _BV(UDRE1)));
	UDR1 = c;
	return c;
}

int ugetchar1(FILE *stream)
{
	while(!(UCSR1A & _BV(RXC1)));
	return UDR1;
}

void init_debug_uart1(void)
{
	/* Configure UART1 baud rate, one start bit, 8-bit, no parity and one stop bit */
	UBRR1H = (F_CPU/(DEBUG_BAUD*16L)-1) >> 8;
	UBRR1L = (F_CPU/(DEBUG_BAUD*16L)-1);
	UCSR1B = _BV(RXEN1) | _BV(TXEN1);
	UCSR1C = _BV(UCSZ10) | _BV(UCSZ11);

	/* Setup new streams for input and output */
	static FILE uout = FDEV_SETUP_STREAM(uputchar1, NULL, _FDEV_SETUP_WRITE);
	static FILE uin = FDEV_SETUP_STREAM(NULL, ugetchar1, _FDEV_SETUP_READ);

	/* Redirect all standard streams to UART0 */
	stdout = &uout;
	stderr = &uout;
	stdin = &uin;
}


