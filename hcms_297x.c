/** \file hcms-297x.c - модуль библиотеки поддержки HCMS-29xx

 * \brief Основные функции для работы с модулем HCMS-29xx

 * \par

 * Большинство низкоуровневых функций сделаны локальными, но ничто не мешает

 * сделать их публичными.

 * \par \author ARV \par

 * \note Для свободного использования без ограничений

 * \n Схема:

 * \n \date	27 нояб. 2016 г.

 * \par

 * \version 0.01.	\par

 * Copyright 2015 © ARV. All rights reserved.

 * \par

 * Для компиляции требуется:\n

 * 	-# WinAVR-20100110 или более новая версия

 *

 */



#include <avr/io.h>

#include <avr_helper.h>

#include <avr/pgmspace.h>

#include <util/delay.h>

#include <string.h>



#include "hcms_297x.h"



// макрос для совместимости кода с новыми версиями GCC, поддерживающими __flash

#if (__GNUC__ > 4) || ((__GNUC__ == 4) && (__GNUC_MINOR__ > 3))

#define p_get(x)	(x)

#else

#define __flash		PROGMEM

#define p_get(x)	pgm_read_byte(&x)

#endif



// шрифт надо включать именно после предыдущего макроса, иначе будет ошибка

#include "font5x7.h"



// экранная область

char hcms_screen[SCR_SIZE];



#if defined(USE_CURSOR)

static uint8_t cursor_pos = NO_CURSOR;

#endif



// управляющие регистры

static ctrl_reg0_t def_ctrl_r0;

static ctrl_reg1_t def_ctrl_r1;



/// макрос включения режима записи в регистры управления

#define hcms_ctrl_mode()	PORT(PORT_RS) |= _BV(PIN_RS)

/// макрос включения режима записи данных

#define hcms_data_mode()	PORT(PORT_RS) &= ~_BV(PIN_RS)

/// макрос активации чипов дисплея

#define hcms_enable()		PORT(PORT_CE) &= ~_BV(PIN_CE)

/// макрос деактивации чипов дисплея

#define hcms_disable()		PORT(PORT_CE) |= _BV(PIN_CE)

#if !defined(USE_SPI)

/// макрос установки высокого уровня в линии CLK

#define hcms_clk_hi()		PORT(PORT_CLK) |= _BV(PIN_CLK)

/// макрос установки низкого уровня в линии CLK

#define hcms_clk_lo()		PORT(PORT_CLK) &= ~_BV(PIN_CLK)

/// макрос установки высокого уровня в линии данных

#define hcms_di_hi()		PORT(PORT_DI) |= _BV(PIN_DI)

/// макрос установки низкого уровня в линии данных

#define hcms_di_lo()		PORT(PORT_DI) &= ~_BV(PIN_DI)

#else

/// макрос установки высокого уровня в линии CLK

#define hcms_clk_hi()

/// макрос установки низкого уровня в линии CLK

#define hcms_clk_lo()

/// макрос установки высокого уровня в линии данных

#define hcms_di_hi()

/// макрос установки низкого уровня в линии данных

#define hcms_di_lo()

#endif



/** сброс дисплея

 * выполняется единственный раз - при инициализации

 */

static void hcms_reset(void){

	PORT(PORT_RST) &= ~_BV(PIN_RST);

	PORT(PORT_RST) |= _BV(PIN_RST);

}



/** вывод байта в дисплей

 * реализует последовательный вывод байта в зависимости от значения #USE_SPI

 * либо программно, либо аппаратно

 * \note аппаратный вывод примерно в 5 раз быстрее программного

 * @param c

 */

#if !defined(USE_SPI)

static void hcms_put_byte(uint8_t c){

	for(uint8_t mask = 0x80; mask; mask >>= 1){

		hcms_clk_lo();

		if(c & mask)	hcms_di_hi();

		else			hcms_di_lo();

		hcms_clk_hi();

	}

}

#else

static void hcms_put_byte(uint8_t c){

	SPDR = c;

	while(!(SPSR & _BV(SPIF)));

}

#endif



/** обновление регистра управления

 *

 * @param data состояние регистра управления в виде одного байта

 */

static void hcms_ctrl_register(uint8_t data){

	hcms_ctrl_mode();

	hcms_enable();

	hcms_put_byte(data);

	hcms_clk_lo();

	hcms_disable();

}



// вывод "сырых" байтов

void hcms_raw_pixels(uint8_t *buf, buf_size_t sz){

	hcms_data_mode();

	hcms_enable();

	for(buf_size_t i=0; i < sz; i++){

		hcms_put_byte(buf[i]);

	}

	hcms_clk_lo();

	hcms_disable();

}



static void rpad_strcpy(char *d, char *s){

	uint8_t l = strlen(s);

	for(uint8_t i=0; i<SCR_SIZE; i++)

		if(i >= l)

			d[i] = ' ';

		else

			d[i] = s[i];

}



// вывод строки

void hcms_puts(char *s){

	rpad_strcpy(hcms_screen, s);

	hcms_update_scr();

}



// вывод строки из flash

void hcms_puts_P(const char *s){

	char tmp[SCR_SIZE];

	strncpy_P(tmp, s, SCR_SIZE);

	hcms_puts(tmp);

}



/** вычисление смещения начала битмапа символа в шрифте

 *

 * @param c символ

 * @return смещение начала битмапа в массиве шрифта

 */

static uint16_t get_offset_char(char c){

	if(c){

		// проверка наличия символа в шрифте

		if((c < FONT_FIRST_CHAR) || (c > FONT_LAST_CHAR)) c = FONT_UNKNOWN_CHAR;

	} else {

		// если символ '\0' - заменяем его на пробел

		c = ' ';

	}



	return (c - FONT_FIRST_CHAR) * FONT_COLS;

}



#if defined(USE_CURSOR)

static uint8_t inverce;

#endif



static void hcms_put_char(char c){

	uint16_t idx; // смещение начала символа в шрифте



	// вычисляем смещение

	idx  = get_offset_char(c);

	// вывод "столбиков" символа

	for(uint8_t i = 0; i < FONT_COLS; i++){

		// для загрузки байта из flash используется макрос совместимости #p_get

		hcms_put_byte(p_get(font5x7[idx++])

#if defined(USE_CURSOR)

			// если требуется курсор, то в нужной позиции инверсия символа

			^ (inverce ? 0xFF : 0)

#endif

		);

	}

}



// очистка дисплея

void hcms_clrscr(void){

#if defined(USE_CURSOR)

	cursor_pos = NO_CURSOR;

#endif

	hcms_puts_P(PSTR(""));

}



// обновление дисплея содержимым экранной области

void hcms_update_scr(void){

	hcms_data_mode();

	hcms_enable();



	for(uint8_t pos = 0; pos < SCR_SIZE; pos++){

#if defined(USE_CURSOR)

		// если требуется курсор, то в нужной позиции инверсия символа

		inverce = (pos == cursor_pos);

#endif

		hcms_put_char(hcms_screen[pos]);

	}

	hcms_clk_lo();

	hcms_disable();

}



#if defined(USE_CURSOR)

// установка позиции курсора

void hcms_cursor_pos(uint8_t pos){

	cursor_pos = pos;

	hcms_update_scr());

}

#endif



// управление яркостью

void hcms_bright(pwm_brightness_t br){

	def_ctrl_r0.brightness = br;

	hcms_ctrl_register(def_ctrl_r0.byte);

}



// включение-отключение дисплея

void hcms_on(uint8_t on){

	def_ctrl_r0.sleep_mode = !on;

	hcms_ctrl_register(def_ctrl_r0.byte);

}



// управление пиковым током

void hcms_peak_current(peak_current_t pc){

	def_ctrl_r0.peak_current = pc;

	hcms_ctrl_register(def_ctrl_r0.byte);

}



// инициализация модуля

INIT(7){

#if defined(ONE_PORT)

	DDR(ONE_PORT) |= _BV(PIN_CE) | _BV(PIN_RS) | _BV(PIN_RST);

	PORT(ONE_PORT) |= _BV(PIN_CE) | _BV(PIN_RS) | _BV(PIN_RST);

#else

	DDR(PORT_CE) |= _BV(PIN_CE);

	PORT(PORT_CE) |= _BV(PIN_CE);

	DDR(PORT_RS) |= _BV(PIN_RS);

	PORT(PORT_RS) |= _BV(PIN_RS);

	DDR(PORT_RST) |= _BV(PIN_RST);

#endif

	hcms_reset();



#if defined(USE_SPI)

	// инициализация аппаратного SPI

	SPCR = _BV(SPE) | _BV(MSTR) | SPI_CLK_DIV_4;

	SPSR = _BV(SPI2X);

#if defined(__AVR_ATmega32__) || defined(__AVR_ATmega32A__) || defined(__AVR_ATmega16__)

	DDRB |= _BV(PB7) | _BV(PB5) | _BV(PB4); // SS, MOSI, SCK

	DDRB &= ~ _BV(PB6); // MISO

#elif defined(__AVR_ATmega8__)

	DDRB |= _BV(PB2) | _BV(PB5) | _BV(PB3); // SS, MOSI, SCK

	DDRB &= ~_BV(PB4); // MISO

#else

#	error CONFIGURE MANUALLY SS PIN AS OUTPUT, and so MOSI and CLK.

#endif

#else

#if defined(ONE_PORT)

	DDR(ONE_PORT) |= _BV(PIN_CLK) | _BV(PIN_DI);

#else

	DDR(PORT_CLK) |= _BV(PIN_CLK);

	DDR(PORT_DI) |= _BV(PIN_DI);

#endif

	PORT(PORT_CLK) &= ~_BV(PIN_CLK);

#endif



	// задаем состояние по умолчанию регистров управления



	def_ctrl_r0.brightness = DEFAULT_BRIGHTNESS;

	def_ctrl_r0.peak_current = DEFAULT_PEAK_CURRENT;

	def_ctrl_r0.sleep_mode = 1;

	def_ctrl_r0.reg = 0;



	def_ctrl_r1.data_out_ctrl = 1;

	def_ctrl_r1.edo_prescaler = 0;

	def_ctrl_r1.reserved = 0;

	def_ctrl_r1.reg = 1;



	hcms_clrscr();

	// в регистр управления 1 надо записать бит параллельной

	// загрузки регистров управления, чтобы в последующем обновлять эти

	// регистры одновременно в одном цикле записи байта. но при инициализации

	// придется повторить запись управляющего слова в 1-й регистр столько раз,

	// сколько всего отдельных чипов.

	for(uint8_t i=0; i < CHIP_CNT+1; i++)

		hcms_ctrl_register(def_ctrl_r1.byte);

	hcms_ctrl_register(def_ctrl_r0.byte);

}



/*****************************************************************************

 * Далее следуют "презентационные" функции, практическая ценность которых

 * сомнительна, но зато они позволяют сделать красивую демонстрацию.

 *****************************************************************************/

#include <util/delay.h>

#include <stdio.h>



/** смена одного символа другим сверху вниз

 * функция сдвигает на дисплее один символ вниз, дополняя сверху знакоместо

 * строками второго символа

 * @param c0 заменяемый символ

 * @param c1 заменяющий символ

 * @param shift количество строк сдвига

 */

static void shiftdn_two_char(uint8_t c0, uint8_t c1, uint8_t shift){

	uint8_t b0, b1;

	for(uint8_t i=0; i < FONT_COLS; i++){

		b0 = p_get(font5x7[get_offset_char(c0) + i]);

		b1 = p_get(font5x7[get_offset_char(c1) + i]);

		for(uint8_t sh = shift; sh; sh--){

			b0 = (b0 << 1) | (b1 & 0x80 ? 1 : 0);

			b1 <<= 1;

		}

		hcms_put_byte(b0);

	}

}



/** смена одного символа другим снизу вверх

 * функци сдвигает на дисплее один символ вверх, дополняя снизу знакоместо

 * строками второго символа

 * @param c0 заменяемый символ

 * @param c1 заменяющий символ

 * @param shift количество строк сдвига

 */

static void shiftup_two_char(uint8_t c0, uint8_t c1, uint8_t shift){

	uint8_t b0, b1;

	for(uint8_t i=0; i < FONT_COLS; i++){

		b0 = p_get(font5x7[get_offset_char(c0) + i]);

		b1 = p_get(font5x7[get_offset_char(c1) + i]);

		for(uint8_t sh = shift; sh; sh--){

			b0 = (b0 >> 1) | (b1 & 1 ? 0x80 : 0);

			b1 >>= 1;

		}

		hcms_put_byte(b0);

	}

}



/** проверка массива байтов на "все нули"

 * вспомогательна функция

 * @param buf проверяемый массив байтов

 * @param sz количество проверяемых байтов

 * @return 0, если есть хотя бы один ненулевой байт, 1 - если все байты в

 * массиве равн нулю

 */

static uint8_t is_clear(uint8_t *buf, uint8_t sz){

	uint8_t result = 0;

	for(;sz;sz--)

		result |= *buf++;

	return result == 0;

}



/// вспомогательный буфер для реализации эффектов

static char ns[SCR_SIZE];



/** замена символа из экранной области на новый из вспомогательного буффера

 * реализует эффект замены всплытие или опускание

 * снизу (всплытие) или сверху (опускание)

 * @param up направление: 1 - вверх, -1 - вниз

 * @param pos обрабатываемый символ

 * @param shift

 */

static void hcms_rollover_chars(int8_t up, uint8_t pos, uint8_t shift){

	if(hcms_screen[pos] != ns[pos]){

		if(up < 0)	shiftdn_two_char(hcms_screen[pos], ns[pos], shift);

		else		shiftup_two_char(hcms_screen[pos], ns[pos], shift);

	} else {

		hcms_put_char(ns[pos]);

	}

}



// вывод строки с эффектом всплывания/опускания

void hcms_rollower_puts(char *s, int8_t up){

	// копируем в промежуточный буфер строку

	rpad_strcpy(ns, s);

	// цикл по количеству строк в символе

	for(uint8_t sh=1; sh <=8; sh++){

		hcms_data_mode();

		hcms_enable();

		// обработка каждого знакоместа

		for(uint8_t pos=0; pos < SCR_SIZE; pos++){

			// выполняем замену текущего символа на дисплее новым из буфера

			hcms_rollover_chars(up, pos, sh);

		}

		hcms_clk_lo();

		hcms_disable();

		// задержка для зрелищности

		_delay_ms(ANIMATE_DELAY_MS);

	}

	// перенос новой строки в экранную область

	memcpy(hcms_screen, ns, SCR_SIZE);

}



// вывод строки с эффектом последовательного всплывания/опускания

void hcms_smooth_rollower_puts(char *s, int8_t up){

	// заполнение вспомогательного буфера

	rpad_strcpy(ns, s);



	static uint8_t sh[SCR_SIZE];	// массив шагов сдвига

	memset(sh, 0, SCR_SIZE);		// изначально он обнулен,

	sh[0] = 1;						// кроме самого первого элемента



	// цикл обновления длится, пока в массиве сдвигов есть хотя бы один

	// не завершенный шаг

	while(!is_clear(sh, SCR_SIZE)){

		hcms_data_mode();

		hcms_enable();



		// каждую позицию на дисплее заменяем при помощи сдвига новым символом

		for(uint8_t pos=0; pos < SCR_SIZE; pos++){

			// сдвиг у каждой позиции имеет свою величину - массив сдвигов

			hcms_rollover_chars(up, pos, sh[pos]);

		}

		// обновляем массив сдвигов, обеспечивая плавное нарастание сдвига

		// смещаем содержимое массива на 1 позицию

		memmove(sh+1, sh, SCR_SIZE-1);

		// если первый элемент не равен нулю

		if(sh[0]){

			// увеличиваем его на 1, но не больше, чем до 8

			if(++(sh[0]) > 8){

				// если оказалось, что получился сдвиг на 9 - обнуляем,

				// т.к. сдвигать больше не надо

				sh[0] = 0;

			}

		}



		// обновляем экранную область

		for(uint8_t i=0; i < SCR_SIZE; i++){

			// перенося в нее все символы новой строки, для которых сдвиг уже

			// завершен

			if(sh[i] == 0) hcms_screen[i] = ns[i];

			else break;

		}



		hcms_clk_lo();

		hcms_disable();

		// задержка для зрелищности

		_delay_ms(ANIMATE_DELAY_MS);

	}

}

