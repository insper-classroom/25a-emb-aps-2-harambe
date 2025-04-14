#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <queue.h>

#include "ssd1306.h"
#include "gfx.h"

#include "pico/stdlib.h"
#include "hardware/adc.h"
#include <stdio.h>

#include "hardware/uart.h"
#include "pico/binary_info.h"

#define UART_ID uart0
#define BAUD_RATE 115200

#define UART_TX_PIN 0
#define UART_RX_PIN 1

#define ENCODER_CLK_PIN 15 // A ou CLK
#define ENCODER_DT_PIN 14  // B ou DT

QueueHandle_t xQueueADC;

typedef struct encoder {
    bool clk;
    bool dt;
} encoder_data;

void init_uart() {
    uart_init(UART_ID, BAUD_RATE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
}

void encoder_task(void *p) {
    encoder_data encoder_data;    
    while (1) {
        bool clk_state = gpio_get(ENCODER_CLK_PIN);
        bool dt_state = gpio_get(ENCODER_DT_PIN);

        encoder_data.clk = clk_state;
        encoder_data.dt = dt_state;

        if (clk_state != dt_state){
            xQueueSend(xQueueADC, &encoder_data, 100);
        }
        // xQueueSend(xQueueADC, &encoder_data, 100);


        vTaskDelay(pdMS_TO_TICKS(1));  // Evita busy-loop
    }
}

void uart_task(void *p) {
    encoder_data data;

    while (true) {
        if (xQueueReceive(xQueueADC, &data, 100)) {
            uint8_t pacote[2];

            // Pacote[0] → bits com CLK e DT: 
            // clk ocupa o bit 1 (<< 1), dt ocupa o bit 0
            pacote[0] = (data.clk << 1) | (data.dt & 0x01);

            // Byte de fim (pode ser um marcador, como 0xFF)
            pacote[1] = 0xFF;

            uart_write_blocking(UART_ID, pacote, 2);  // Envia os 3 bytes pela UART
        }
    }
}

int main() {
    stdio_init_all();
    init_uart();

    // Configura os pinos do encoder
    gpio_init(ENCODER_CLK_PIN);
    gpio_set_dir(ENCODER_CLK_PIN, GPIO_IN);
    gpio_pull_up(ENCODER_CLK_PIN);

    gpio_init(ENCODER_DT_PIN);
    gpio_set_dir(ENCODER_DT_PIN, GPIO_IN);
    gpio_pull_up(ENCODER_DT_PIN);

    xQueueADC = xQueueCreate(32, sizeof(encoder_data));

    xTaskCreate(encoder_task, "encoderTask", 4096, NULL, 1, NULL);
    xTaskCreate(uart_task, "uartTask", 4096, NULL, 1, NULL);

    vTaskStartScheduler();

    while (true) {}
}
