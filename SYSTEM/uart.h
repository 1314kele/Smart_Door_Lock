#ifndef _UART_H_
#define _UART_H_

#include "stm32f4xx.h"

void uart1_init(uint32_t baudrate);
void uart1_putc(char ch);
void uart1_puts(const char *s);
void uart2_init(uint32_t baudrate);
void uart2_putc(char ch);
void uart2_puts(const char *s);
uint8_t uart2_test_loopback(void);
void uart3_init(uint32_t baudrate);
void uart3_putc(char ch);
void uart3_puts(const char *s);

void parse_cmd(void);
void parse_bl_cmd(void);
void wifi_auto_connect(void);

#endif
