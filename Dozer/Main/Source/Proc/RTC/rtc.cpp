/*
	Модуль часов реального времени RTC
*/

#include "Global.h"
#include "i2c.h"
// Структура хранения времени/даты в микросхеме RTC
struct rtc_time_struct {
	uint8_t	seconds;
	uint8_t minutes;
	uint8_t	hours;
	uint8_t	day;
	uint8_t	date;
	uint8_t	month;
	uint8_t	year;
	uint8_t alarms[7];
	uint8_t control;
	uint8_t status;
	uint8_t aging;
	uint8_t temp_hi;
	uint8_t temp_lo;
} __packed;

static struct rtc_time_struct rtc, rtcw;	// структура даты/времени для записи в микросхему

struct dozer_action next_action = {0, 0, 0, 0, 0.0};

/*
	Функция проверки устанавливаемой даты на корректность.
	Она не должна быть меньше 14:31 12.01.2019
*/
BaseType_t is_rtc_time_valid(struct rtc_time_struct *rtc)
{
	if (rtc->year < 0x19) return pdFAIL;
	else {//return pdPASS;
		if (rtc->year == 0x19 && rtc->month < 0x01) return pdFAIL;
		else {//return pdPASS;
			if (rtc->month == 0x01 && rtc->date < 0x12) return pdFAIL;
			else {//return pdPASS;
				if (rtc->date == 0x12 && rtc->hours < 0x14) return pdFAIL;
				else {//return pdPASS;
					if (rtc->hours == 0x14 && rtc->minutes < 0x31) return pdFAIL;
					else return pdPASS;
				}
			}
		}
	}
}

/*
	Функция конвертирования числа из двоичного формата в двоично-десятичный
*/
__STATIC_INLINE uint8_t bin2bcd(uint16_t arg)
{
	return (uint8_t)(((arg/10)<<4) + arg%10);
}

/*
	Функция преобразования текущей даты и времени из формата микросхемы в формат системы
*/
void get_sys_date(struct date_time *dest)
{
	dest->year =	(uint16_t)(2000 + ((rtc.month & 0x80) >> 7)*100 + ((rtc.year & 0xF0) >> 4)*10 + (rtc.year & 0x0F));
	dest->month =	(uint8_t)(((rtc.month & 0x10) >> 4)*10 + (rtc.month & 0x0F));
	dest->day =		(uint8_t)(((rtc.date & 0xF0) >> 4)*10 + (rtc.date & 0x0F));
	dest->hours =	(uint8_t)(((rtc.hours & 0x30) >> 4)*10 + (rtc.hours & 0x0F));
	dest->minutes =	(uint8_t)(((rtc.minutes & 0xF0) >> 4)*10 + (rtc.minutes & 0x0F));
	dest->seconds =	(uint8_t)(((rtc.seconds & 0xF0) >> 4)*10 + (rtc.seconds & 0x0F));
}

/*
	Запись новой даты/времени в RTC по команде из BLE
*/
void ble_update_rtc(const struct date_time *arg)
{
	uint16_t pyear = arg->year - 2000;
	uint8_t centure = (uint8_t)(pyear/100);	// Вычисляем номер столения
	
//	pyear = arg->year - 2000;			
//	centure = (uint8_t)(pyear/100);
// Преобразуем дату и время в формат BCD
	rtcw.year = bin2bcd(pyear%100);
	rtcw.month = (centure << 7) + bin2bcd(arg->month);
	rtcw.date = bin2bcd(arg->day);
	rtcw.hours = bin2bcd(arg->hours);
	rtcw.minutes = bin2bcd(arg->minutes);
	rtcw.seconds = bin2bcd(arg->seconds);
// Записываем новые дату и время
	i2c(RTC_WRITE, (uint8_t)0, offsetof(rtc_time_struct, alarms), &rtcw);
//	i2c(RTC_READ, (uint8_t)0, offsetof(rtc_time_struct, aging), &rtc);
	next_action.actual = 0;
	next_action.day = arg->day;
	next_action.hours = arg->hours;
	next_action.minutes = arg->minutes;
}

/*
Функция увеличивает дату на 1 с учётом количества дней в месяцах и високосных годов
*/
uint8_t day_inc(uint8_t day)
{
	int year;
	uint8_t retval = day;
	retval++;				// если никаких исключений нет, то просто увеличиваем дату
	switch (rtc.month) {	// в этих месяцах 31 день
		case 0x01:
		case 0x03:
		case 0x05:
		case 0x07:
		case 0x08:
		case 0x10:
		case 0x12:
			if (day == 31) retval = 1;
			break;
		case 0x04:				// в этих месяцах 30 дней
		case 0x06:
		case 0x09:
		case 0x11:
			if (day == 30) retval = 1;
			break;
		case 0x02:				// а это вообще февраль
			year = 2000 + ((rtc.month & 0x80) >> 7)*100 + ((rtc.year & 0xF0) >> 4)*10 + (rtc.year & 0x0F);
			if (year%4 == 0) {		// если год високосный, то 29 дней
				if (day == 29) retval = 1;
			} else {					// иначе 28
				if (day == 28) retval = 1;
			}
	}
	return retval;
}

/*
функция возвращает количество минут во входных часах и минутах
*/
int get_minutes(uint8_t hours, uint8_t minutes)
{
	int retval = 60*hours + minutes;
	if (retval > 1440) retval = 1440;
	return retval;
}

/*
функция добавляет ко времени события кол-во минут mins
*/
void add_minutes(struct dozer_action *action, int mins)
{
	uint32_t sum;
	int pmins = mins;
	
	if (pmins > 1440) pmins = 1440;
	sum = 60*(action->hours) + action->minutes + pmins;
	if ((sum/60)/24 > 0) action->day = day_inc(action->day);
	action->hours = (sum/60)%24;
	action->minutes = sum%60;
}

/*
функция возвращает true когда время action больше времени now
*/
bool is_action_trigged(struct dozer_action *action, struct date_time *now)
{
	if (action->day > now->day) return true;
	if (action->day < now->day) return false;
	if (action->hours > now->hours) return true;
	if (action->hours < now->hours) return false;
	if (action->minutes > now->minutes) return true;
	return false;
}

/*
функция возвращает true когда время action совпадает со временем now
*/
static bool is_alarm_time(struct dozer_action *action, struct date_time *now)
{
	if (action->day == now->day)
		if (action->hours == now->hours)
			if (action->minutes == now->minutes) return true;
	return false;
}

/*
функция сравнивает время события act1 со временем события act2.
Времена этих событий не должны отличаться более чем на сутки,
иначе правильный результат не гарантирован
*/
int action_compare(struct dozer_action *act1, struct dozer_action *act2)
{
	if (act1->day == day_inc(act2->day)) return 1;
	if (act2->day == day_inc(act1->day)) return -1;
	if (act1->day == act2->day) {
		if (act1->hours < act2->hours) return -1;
		if (act1->hours > act2->hours) return 1;
		if (act1->minutes > act2->minutes) return 1;
		if (act1->minutes < act2->minutes) return -1;
		return 0;
	}
	printf("is_action_gt: ERROR: these are not the next days!\r\n");
	printf("day1: %d day2: %d\r\n", act1->day, act2->day);
	if (act1->day > act2->day) return 1;
	return -1;
}

/*
	Процесс вычитывает дату и время из RTC, и каждую секунду
	отправляет их в BLE
*/
void RTCProc(void *Param)
{
	struct date_time ble;
	
	i2c(RTC_READ, (uint8_t)0, offsetof(rtc_time_struct, aging), &rtc);	// читаем текущее время из микросхемы

	rtc.status = 0;

	if (is_rtc_time_valid(&rtc) == pdFAIL) {	// если время неверно, записываем в микросхему значение по умолчанию
		rtc.seconds = 0x00;
		rtc.minutes = 0x31;
		rtc.hours = 0x14;
		rtc.day = 0x6;
		rtc.date = 0x12;
		rtc.month = 0x01;
		rtc.year = 0x19;
		i2c(RTC_WRITE, (uint8_t)0, offsetof(rtc_time_struct, alarms), &rtc);
	}

	i2c(RTC_WRITE, (uint8_t)offsetof(struct rtc_time_struct, status), sizeof(rtc.status), &rtc.status);		// возможно избыточно

	get_sys_date(&ble);
	
	next_action.day = ble.day;
	next_action.hours = ble.hours;
	next_action.minutes = ble.minutes;
	
	printf("start: %02d:%02d:%02d\r\n", next_action.day, next_action.hours, next_action.minutes);
	
	xTaskCreate(httpsProc, "", configMINIMAL_STACK_SIZE + 500, 0, TASK_PRI_LED, &gsmTaskHandle);
	
	for (;;) {
		static uint8_t seconds;	// переменная для определения момента истечения секунды

		seconds = rtc.seconds;	// присваиваем текущее значение секунд

		i2c(RTC_READ, (uint8_t)0, offsetof(rtc_time_struct, alarms), &rtc);	// читаем текущие дату и время

		if (seconds != rtc.seconds) {	// если значение секунд изменилось, отправляем новое значение даты/времени в очередь сообщений для BLE и двигателя заслонки
			get_sys_date(&ble);
			if (RTC_Queue != NULL) xQueueSendToBack(RTC_Queue, &ble, 0);
			
			if ( is_alarm_time(&next_action, &ble) && (next_action.actual != 0) ) {	// если наступило время события
				set_doze(next_action.doze);		// устанавливаем дозу выдачи
				printf("action on %d:%d doze: %.3f\r\n", next_action.hours, next_action.minutes, next_action.doze);
				motor1_on = 0xFF;	// запускаем процесс выдачи
				next_action.actual = 0;	// помечаем событие не актуальным
			}
		}
		vTaskDelay(MS_TO_TICK(100));	// проверяем секунды каждые 100мс.
	}
}
