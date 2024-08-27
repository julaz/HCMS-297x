/** \file main.c
 * \brief Демонстрационная программа возможностей библиотеки поддержки LED-модуля семейства HCMS-29xx
 * \par
 *
 * \par \author ARV \par
 * \note Для свободного использования без ограничений
 * \n Схема:
 * \n \date	1 дек. 2016 г.
 * \par
 * \version 1.00.	\par
 * Copyright 2015 © ARV. All rights reserved.
 * \par
 * Для компиляции требуется:\n
 * 	-# WinAVR-20100110 или более новая версия
 */

#include <avr/io.h>
#include <stdio.h>
#include <util/delay.h>
#include <avr_helper.h>
#include <avr/pgmspace.h>

#include "hcms_297x.h"

/** демо-функция "ОДОМЕТР"
 * имитирует счетчика пройденных километров автомобиля
 * @param odo значение счетчика
 * @param delta величина приращения счетчика
 * @return новое значение счетчика
 */
int32_t show_odometer(int32_t odo, int8_t delta){
	if((odo < 0) || (odo > 99999999)) return odo;
	char tmp[SCR_SIZE];
	odo += delta;
	if(odo < 0) odo = 99999999;
	if(odo > 99999999) odo = 0;
	sprintf_P(tmp, PSTR("    %08lu"), odo);
	hcms_rollower_puts(tmp, delta);
	return odo;
}

int main(void){
	// начальное значение счетчика-одометра
	int32_t odo = 99999980;
	int8_t up = 1;
	while(1){
		// демонстрация плавного вывода строк
		for(uint8_t i=0; i<2; i++){
			hcms_smooth_rollower_puts("DEMO HCMS-2915", up);
			_delay_ms(1000);
			hcms_smooth_rollower_puts("", -up);
			_delay_ms(1000);
			hcms_smooth_rollower_puts("Hello, World!", up);
			_delay_ms(1000);
			hcms_smooth_rollower_puts("", -up);
			_delay_ms(1000);
			up = -up;
		}
		// демонстрация эффекта "одометра" с одновременным увеличением яркости
		for(uint8_t br=MIN_BRIGHTNESS; br <= MAX_BRIGHTNESS; br++){
			hcms_bright(br);
			for(uint8_t i=0; i<10; i++) odo = show_odometer(odo, 1);
		}
		// теперь счетчик крутится в обратную сторону, а яркость уменьшается
		for(uint8_t br=MAX_BRIGHTNESS; br >= MIN_BRIGHTNESS; br--){
			hcms_bright(br);
			for(uint8_t i=0; i<10; i++) odo = show_odometer(odo, -1);
		}
	}
}
