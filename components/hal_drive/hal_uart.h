#ifndef __HAL_UART__
#define __HAL_UART__

#include "hal_config.h"
#include "driver/uart.h"


typedef void (*uart_recv_callback_t)(uart_port_t uart_port, uint8_t *data, uint16_t len);
 
void hal_uart_init(uart_recv_callback_t cb_func);

void hal_uart_write(const uart_port_t uart_port, const uint8_t *data, uint16_t len);

#endif /* __HAL_UART__ END. */

