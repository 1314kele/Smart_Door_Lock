#include "uart.h"
#include "flash.h"
#include "led.h"
#include "sys.h"
#include "as608.h"
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

extern QueueHandle_t msgQueue;

volatile u8 uart_buf[64] = {0};
volatile u8 uart_len = 0;
volatile u8 uart_flag = 0;

volatile u8 bl_buf[64] = {0};
volatile u8 bl_len = 0;
volatile u8 bl_flag = 0;

char wifi_ssid[32] = "yxq";
char wifi_pass[32] = "123456789";

volatile u8 tmp_buf[6] = {0};
volatile u8 tmp_cnt = 0;

extern volatile int co2_auto_static;

#pragma import(__use_no_semihosting)
struct __FILE { int handle; };
FILE __stdout;
FILE __stdin;
void _sys_exit(int x) { x = x; }

int fputc(int ch, FILE *f)
{
	while(USART_GetFlagStatus(USART1, USART_FLAG_TXE) != SET);
	USART_SendData(USART1, ch);
	return ch;
}

void uart1_init(uint32_t baudrate)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA | RCC_AHB1Periph_GPIOG, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOG, &GPIO_InitStructure);
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9 | GPIO_Pin_10;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource9, GPIO_AF_USART1);
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource10, GPIO_AF_USART1);
	USART_InitStructure.USART_BaudRate = baudrate;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;
	USART_InitStructure.USART_Parity = USART_Parity_No;
	USART_InitStructure.USART_StopBits = USART_StopBits_1;
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_Init(USART1, &USART_InitStructure);
	USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
	NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0x1;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0x2;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);
	USART_Cmd(USART1, ENABLE);
}

void uart1_putc(char ch) {
	while(USART_GetFlagStatus(USART1, USART_FLAG_TXE) != SET);
	USART_SendData(USART1, ch);
}
void uart1_puts(const char *s) { while(*s) uart1_putc(*s++); }

void uart2_init(uint32_t baudrate)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2 | GPIO_Pin_3;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource2, GPIO_AF_USART2);
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource3, GPIO_AF_USART2);
	USART_InitStructure.USART_BaudRate = baudrate;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;
	USART_InitStructure.USART_Parity = USART_Parity_No;
	USART_InitStructure.USART_StopBits = USART_StopBits_1;
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_Init(USART2, &USART_InitStructure);
	USART_Cmd(USART2, ENABLE);
}

void uart2_putc(char ch) {
	while(USART_GetFlagStatus(USART2, USART_FLAG_TXE) != SET);
	USART_SendData(USART2, ch);
}
void uart2_puts(const char *s) { while(*s) uart2_putc(*s++); }

uint8_t uart2_test_loopback(void) {
    uint8_t test_data[] = "UART2 Test";
    uint8_t received[32] = {0};
    uint16_t i, timeout;
    
    printf("[UART2] Testing TX...\r\n");
    uart2_puts((const char*)test_data);
    
    printf("[UART2] Testing RX (loopback required)...\r\n");
    for(i = 0; i < sizeof(test_data)-1; i++) {
        timeout = 0;
        while(USART_GetFlagStatus(USART2, USART_FLAG_RXNE) == RESET) {
            if(timeout++ > 100000) {
                printf("[UART2] RX timeout!\r\n");
                return 0;
            }
        }
        received[i] = USART_ReceiveData(USART2);
    }
    
    printf("[UART2] Sent: %s\r\n", test_data);
    printf("[UART2] Received: %s\r\n", received);
    
    if(memcmp(test_data, received, sizeof(test_data)-1) == 0) {
        printf("[UART2] Loopback test PASSED!\r\n");
        return 1;
    } else {
        printf("[UART2] Loopback test FAILED!\r\n");
        return 0;
    }
}

void uart3_putc(char ch) {
	while(USART_GetFlagStatus(USART3, USART_FLAG_TXE) != SET);
	USART_SendData(USART3, ch);
}
void uart3_puts(const char *s) { while(*s) uart3_putc(*s++); }

void uart3_init(uint32_t baudrate)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10 | GPIO_Pin_11;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &GPIO_InitStructure);
	GPIO_PinAFConfig(GPIOB, GPIO_PinSource10, GPIO_AF_USART3);
	GPIO_PinAFConfig(GPIOB, GPIO_PinSource11, GPIO_AF_USART3);
	USART_InitStructure.USART_BaudRate = baudrate;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;
	USART_InitStructure.USART_Parity = USART_Parity_No;
	USART_InitStructure.USART_StopBits = USART_StopBits_1;
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_Init(USART3, &USART_InitStructure);
	USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);
	NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0x8;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0x2;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);
	USART_Cmd(USART3, ENABLE);
}

void parse_cmd(void)
{
	if(uart_flag){
		if(strstr((const char *)uart_buf,"test")){
			printf("test command!\n");
		}
		else if(strstr((const char *)uart_buf,"unlock")){
			printf("Bluetooth Unlock Success!\n");
			D1 = 0;
			{ uint32_t cmd = 4; xQueueSend(msgQueue, &cmd, 10); }
		}
		else if(strstr((const char *)uart_buf,"lock")){
			printf("Bluetooth Lock Success!\n");
			D1 = 1;
		}
		else if(strstr((const char *)uart_buf,"sync_finger")){
			printf("Syncing fingerprints to APP...\n");
		}
		else if(strstr((const char *)uart_buf,"ssid ")){
			char *p = (char *)uart_buf + 5;
			int n = 0;
			while(*p && *p != '*' && n < 31) wifi_ssid[n++] = *p++;
			wifi_ssid[n] = 0;
			printf("WiFi SSID set: %s\r\n", wifi_ssid);
		}
		else if(strstr((const char *)uart_buf,"pass ")){
			char *p = (char *)uart_buf + 5;
			int n = 0;
			while(*p && *p != '*' && n < 31) wifi_pass[n++] = *p++;
			wifi_pass[n] = 0;
			printf("WiFi Pass set: %s\r\n", wifi_pass);
		}
		else if(strstr((const char *)uart_buf,"at ")){
			char *p = (char *)uart_buf + 3;
			while(*p && *p != '*') { uart3_putc(*p); p++; }
			uart3_putc('\r'); uart3_putc('\n');
		}
		else if(strstr((const char *)uart_buf,"wifi_test")){
			printf("WiFi test: sending AT...\r\n");
			uart3_puts("+++");
			vTaskDelay(pdMS_TO_TICKS(2000));
			uart3_puts("AT\r\n");
			vTaskDelay(pdMS_TO_TICKS(2000));
			printf("WiFi test done.\r\n");
		}
		else if(strstr((const char *)uart_buf,"wifi")){
			wifi_auto_connect();
		}
		else if(strstr((const char *)uart_buf,"finger")){
			printf("Testing fingerprint module...\n");
			as608_handshake();
		}
		else if(strstr((const char *)uart_buf,"uart2_echo")){
			printf("[UART2] Testing echo...\r\n");
			uart2_puts("Hello from UART2!\r\n");
			printf("[UART2] Echo test done.\r\n");
		}
		else if(strstr((const char *)uart_buf,"send ")){
			char *p = (char *)uart_buf + 5;
			printf("[WiFi] Sending to WiFi: %s\r\n", p);
			while(*p && *p != '*') { uart3_putc(*p++); }
			uart3_putc('\r');
			uart3_putc('\n');
			printf("[WiFi] Message sent!\r\n");
		}
		else if(strstr((const char *)uart_buf,"save_card")){
			extern uint32_t last_card_uid;
			if(last_card_uid == 0) {
				printf("No card scanned yet! Scan a card first.\r\n");
			} else {
				flash_save_card_uid(last_card_uid);
				printf("Card %08X saved to Flash!\r\n", last_card_uid);
			}
		}
		else if(strstr((const char *)uart_buf,"show_card")){
			extern uint32_t saved_card_uid;
			if(saved_card_uid) printf("Saved card: %08X\r\n", saved_card_uid);
			else printf("No card saved.\r\n");
		}
		else if(strstr((const char *)uart_buf,"enroll")){
			uint16_t id = 1;
			char *p = (char *)uart_buf;
			while(*p && !(*p >= '0' && *p <= '9')) p++;
			if(*p >= '0' && *p <= '9') {
				id = 0;
				while(*p >= '0' && *p <= '9') { id = id * 10 + (*p - '0'); p++; }
				if(id == 0) id = 1;
			}
			printf("Start enrolling fingerprint ID=%d...\n", id);
			as608_enroll_finger(id);
		}
		else if(strstr((const char *)uart_buf,"clear_finger")){
			printf("Clearing all fingerprints...\n");
			as608_clear_all();
		}
		else {
			printf("unknow command = %s\n", uart_buf);
		}
		memset((char *)uart_buf, 0, sizeof(uart_buf));
		uart_len = 0;
		uart_flag = 0;
	}
}

void USART1_IRQHandler(void)
{
	uint32_t ulReturn;
	ulReturn = taskENTER_CRITICAL_FROM_ISR();
	if(USART_GetITStatus(USART1, USART_IT_RXNE) == SET){
		uart_buf[uart_len++] = USART_ReceiveData(USART1);
		if(uart_buf[uart_len-1] == '*' || uart_len >= sizeof(uart_buf)-1){
			uart_flag = 1;
		}
		USART_ClearITPendingBit(USART1, USART_IT_RXNE);
	}
	taskEXIT_CRITICAL_FROM_ISR(ulReturn);
}

void USART3_IRQHandler(void)
{
	uint32_t ulReturn;
	ulReturn = taskENTER_CRITICAL_FROM_ISR();
	if(USART_GetITStatus(USART3, USART_IT_RXNE) == SET){
		uint8_t ch = USART_ReceiveData(USART3);
		if(bl_len < sizeof(bl_buf) - 1) {
			bl_buf[bl_len++] = ch;
			if(ch == '*' || ch == '\r' || ch == '\n' || bl_len >= sizeof(bl_buf) - 2) {
				bl_flag = 1;
			}
		}
		USART_ClearITPendingBit(USART3, USART_IT_RXNE);
	}
	taskEXIT_CRITICAL_FROM_ISR(ulReturn);
}

static int32_t wifi_find_str(char *str, uint32_t timeout_ms)
{
	uint32_t t;
	for(t = 0; t < timeout_ms; t++) {
		if(strstr((const char *)bl_buf, str)) return 0;
		vTaskDelay(pdMS_TO_TICKS(1));
	}
	return -1;
}

static void wifi_clear_buf(void)
{
	memset((char *)bl_buf, 0, sizeof(bl_buf));
	bl_len = 0;
	bl_flag = 0;
}

static int32_t wifi_wait_response(char *expected, uint32_t timeout_ms, char *error_str)
{
	int32_t ret;
	ret = wifi_find_str(expected, timeout_ms);
	if(ret == 0) {
		printf("[WiFi] Found: %s\r\n", expected);
		return 0;
	} else {
		printf("[WiFi] %s (expected: %s)\r\n", error_str, expected);
		printf("[WiFi] Buffer content: %s\r\n", bl_buf);
		return -1;
	}
}

static void wifi_send_at(char *str)
{
	wifi_clear_buf();
	uart3_puts(str);
}

void wifi_notify(const char *msg)
{
	if(msg == NULL) return;
	
	printf("[WiFi] Notifying: %s\r\n", msg);
	
	uart3_puts(msg);
	uart3_putc('\r');
	uart3_putc('\n');
	
	printf("[WiFi] Notification sent!\r\n");
}

void wifi_auto_connect(void)
{
	char cmd[256];
	printf("\r\n========================================\r\n");
	printf("[WiFi] Starting WiFi and TCP connection\r\n");
	printf("[WiFi] SSID: %s\r\n", wifi_ssid);
	printf("[WiFi] TCP Server: 10.184.15.53:8000\r\n");
	printf("========================================\r\n");

	wifi_clear_buf();
	printf("[WiFi] Step 1: Exit transparent mode\r\n");
	uart3_puts("+++");
	vTaskDelay(pdMS_TO_TICKS(2000));

	wifi_clear_buf();
	printf("[WiFi] Step 2: Reset module\r\n");
	uart3_puts("AT+RST\r\n");
	vTaskDelay(pdMS_TO_TICKS(3000));
	if(wifi_wait_response("ready", 5000, "Module reset failed") != 0) {
		printf("[WiFi] WARNING: No 'ready' response, continuing...\r\n");
	}

	wifi_clear_buf();
	printf("[WiFi] Step 3: Disable echo\r\n");
	uart3_puts("ATE0\r\n");
	if(wifi_wait_response("OK", 2000, "Disable echo failed") != 0) return;

	wifi_clear_buf();
	printf("[WiFi] Step 4: Set mixed mode (AP+STA)\r\n");
	uart3_puts("AT+CWMODE=3\r\n");
	if(wifi_wait_response("OK", 3000, "Set mode failed") != 0) return;

	wifi_clear_buf();
	printf("[WiFi] Step 5: Connect to WiFi\r\n");
	snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"\r\n", wifi_ssid, wifi_pass);
	uart3_puts(cmd);
	printf("[WiFi] Connecting to %s...\r\n", wifi_ssid);
	if(wifi_wait_response("WIFI GOT IP", 15000, "WiFi connection failed") != 0) {
		if(strstr((const char *)bl_buf, "FAIL")) {
			printf("[WiFi] ERROR: WiFi authentication failed or wrong password\r\n");
		} else if(strstr((const char *)bl_buf, "DISCONNECT")) {
			printf("[WiFi] ERROR: WiFi disconnected during connection\r\n");
		}
		return;
	}
	printf("[WiFi] WiFi connected successfully!\r\n");
	printf("[WiFi] Waiting for network to stabilize...\r\n");
	vTaskDelay(pdMS_TO_TICKS(3000));

	wifi_clear_buf();
	printf("[WiFi] Step 6: Close any existing TCP connection\r\n");
	uart3_puts("AT+CIPCLOSE\r\n");
	vTaskDelay(pdMS_TO_TICKS(1000));

	printf("[WiFi] Step 7: Connect to TCP server\r\n");
	snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"10.184.15.53\",8000\r\n");
	printf("[WiFi] Connecting to TCP server 10.184.15.53:8000...\r\n");

	{
		int32_t tcp_retry;
		int32_t tcp_ok = 0;
		for(tcp_retry = 0; tcp_retry < 3; tcp_retry++) {
			wifi_clear_buf();
			uart3_puts(cmd);
			printf("[WiFi] TCP attempt %ld/3\r\n", tcp_retry + 1);
			if(wifi_wait_response("CONNECT", 10000, NULL) == 0) {
				printf("[WiFi] TCP connected successfully!\r\n");
				tcp_ok = 1;
				break;
			}
			if(strstr((const char *)bl_buf, "ALREADY CONNECTED")) {
				printf("[WiFi] Already connected, continuing...\r\n");
				tcp_ok = 1;
				break;
			}
			if(strstr((const char *)bl_buf, "CONNECT FAIL")) {
				printf("[WiFi] ERROR: TCP connection refused by server\r\n");
				tcp_ok = 0;
				break;
			}
			if(strstr((const char *)bl_buf, "busy")) {
				printf("[WiFi] Module busy, retrying in 3 seconds...\r\n");
				vTaskDelay(pdMS_TO_TICKS(3000));
				continue;
			}
			printf("[WiFi] TCP connection timeout, retrying in 2 seconds...\r\n");
			vTaskDelay(pdMS_TO_TICKS(2000));
		}
		if(!tcp_ok) {
			printf("[WiFi] ERROR: TCP connection failed after %ld attempts\r\n", tcp_retry);
			printf("[WiFi] Buffer content: %s\r\n", bl_buf);
			return;
		}
	}

	wifi_clear_buf();
	printf("[WiFi] Step 8: Enable transparent mode\r\n");
	uart3_puts("AT+CIPMODE=1\r\n");
	if(wifi_wait_response("OK", 3000, "Enable transparent mode failed") != 0) return;

	wifi_clear_buf();
	printf("[WiFi] Step 9: Start sending\r\n");
	uart3_puts("AT+CIPSEND\r\n");
	vTaskDelay(pdMS_TO_TICKS(500));

	printf("\r\n========================================\r\n");
	printf("[WiFi] WiFi setup completed successfully!\r\n");
	printf("[WiFi] Ready to receive commands via TCP\r\n");
	printf("========================================\r\n");
}

void parse_bl_cmd(void)
{
	if(bl_flag){
		char buf_copy[64];
		char *start;
		uint32_t i;

		for(i = 0; i < bl_len && i < 63; i++) {
			buf_copy[i] = bl_buf[i];
		}
		buf_copy[i] = '\0';

		memset((char *)bl_buf, 0, sizeof(bl_buf));
		bl_len = 0;
		bl_flag = 0;

		printf("[WiFi] %s\r\n", buf_copy);

		start = buf_copy;
		for(i = 0; i < 64 && buf_copy[i] != '\0'; i++) {
			if(buf_copy[i] == '*' || buf_copy[i] == '\r' || buf_copy[i] == '\n') {
				buf_copy[i] = '\0';
				if(start[0] != '\0') {
					if(strstr(start, "unlock")){
						printf("WiFi Unlock!\r\n");
						D1 = 0;
						{ uint32_t cmd = 4; xQueueSend(msgQueue, &cmd, 10); }
					}
					else if(strstr(start, "lock")){
						printf("WiFi Lock!\r\n");
						D1 = 1;
					}
				}
				start = buf_copy + i + 1;
			}
		}
	}
}
