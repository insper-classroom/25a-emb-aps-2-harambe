#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <queue.h>

#include "pico/stdlib.h"
#include <stdio.h>
#include <stdlib.h>
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "mpu6050.h"
#include "hardware/adc.h"
#include "pico/stdlib.h"
#include "Fusion.h"
#include "hardware/gpio.h"
#include "hc06.h"

#define UART_ID uart0
#define BAUD_RATE 115200
#define UART_TX_PIN 0
#define UART_RX_PIN 1

#define I2C_SDA_GPIO 20 // ALTERADO
#define I2C_SCL_GPIO 21 //EU ALTEREI ISSO!
#define MPU_ADDRESS 0x68

#define ANALOG_TOLERANCE 100
#define TOLERANCIA 1

QueueHandle_t xQueueSteer;
QueueHandle_t xQueueAccel;
QueueHandle_t xQueueBreak;
QueueHandle_t xQueueButtons;

typedef struct button {
    int square;
    int x;
    int triangle;
    int circle;
} button_id;

const int BTN_TRIANGLE = 10;
const int BTN_X = 11;
const int BTN_SQUARE = 12;
const int BTN_CIRCLE = 13;

void btn_callback(uint gpio, uint32_t events) {
    button_id button;
    button.circle = 0;
    button.triangle = 0;
    button.square = 0;
    button.x = 0;
    
    if (gpio == BTN_TRIANGLE && events == 0x4) {     
        button.triangle = 1;
    } if (gpio == BTN_X && events == 0x4) {
        button.x = 1;  
    } if (gpio == BTN_SQUARE && events == 0x4) {
        button.square = 1;
    } if (gpio == BTN_CIRCLE && events == 0x4) {
        button.circle = 1;
    }
    
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(xQueueButtons, &button, &xHigherPriorityTaskWoken);    
}

void init_buttons() {
    const uint buttons[] = {BTN_TRIANGLE, BTN_X, BTN_SQUARE, BTN_CIRCLE};
    for (int i = 0; i < 4; i++) {
        gpio_init(buttons[i]);
        gpio_set_dir(buttons[i], GPIO_IN);
        gpio_pull_up(buttons[i]);
        gpio_set_irq_enabled_with_callback(buttons[i], GPIO_IRQ_EDGE_FALL, true, &btn_callback);
    }
}

static void mpu6050_reset() {
    uint8_t buf[] = {0x6B, 0x00};
    i2c_write_blocking(i2c_default, MPU_ADDRESS, buf, 2, false);
}

static void mpu6050_read_raw(int16_t accel[3], int16_t gyro[3], int16_t *temp) {
    uint8_t buffer[6];
    uint8_t val;

    val = 0x3B;
    i2c_write_blocking(i2c_default, MPU_ADDRESS, &val, 1, true);
    i2c_read_blocking(i2c_default, MPU_ADDRESS, buffer, 6, false);
    for (int i = 0; i < 3; i++) {
        accel[i] = (buffer[i * 2] << 8 | buffer[(i * 2) + 1]);
    }

    val = 0x43;
    i2c_write_blocking(i2c_default, MPU_ADDRESS, &val, 1, true);
    i2c_read_blocking(i2c_default, MPU_ADDRESS, buffer, 6, false);
    for (int i = 0; i < 3; i++) {
        gyro[i] = (buffer[i * 2] << 8 | buffer[(i * 2) + 1]);
    }

    val = 0x41;
    i2c_write_blocking(i2c_default, MPU_ADDRESS, &val, 1, true);
    i2c_read_blocking(i2c_default, MPU_ADDRESS, buffer, 2, false);
    *temp = buffer[0] << 8 | buffer[1];
}

void mpu6050_task(void *p) {
    i2c_init(i2c_default, 400 * 1000);
    gpio_set_function(I2C_SDA_GPIO, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_GPIO, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_GPIO);
    gpio_pull_up(I2C_SCL_GPIO);

    mpu6050_reset();

    int16_t acceleration[3], gyro[3], temp;
    TickType_t lastTick = xTaskGetTickCount();
    int8_t last_delta_x = 0;

    FusionAhrs ahrs;
    FusionAhrsInitialise(&ahrs);

    while (1) {
        TickType_t now = xTaskGetTickCount();
        float sample_rate = (now - lastTick) / 1000.0f;
        lastTick = now;

        mpu6050_read_raw(acceleration, gyro, &temp);

        FusionVector gyroscope = {
            .axis.x = gyro[0] / 131.0f,
            .axis.y = gyro[1] / 131.0f,
            .axis.z = gyro[2] / 131.0f,
        };

        FusionVector accelerometer = {
            .axis.x = acceleration[0] / 16384.0f,
            .axis.y = acceleration[1] / 16384.0f,
            .axis.z = acceleration[2] / 16384.0f,
        };

        FusionAhrsUpdateNoMagnetometer(&ahrs, gyroscope, accelerometer, sample_rate);

        const FusionEuler euler = FusionQuaternionToEuler(FusionAhrsGetQuaternion(&ahrs));
        float roll = euler.angle.roll;
        // float pitch = euler.angle.pitch;

        int8_t delta_x = (int8_t)(roll * 1);

        // if (delta_x < 6 && delta_x > -6) {
        //     delta_x = delta_x * 0.95;
        // }

        // if (delta_x > 40) delta_x = 40;
        // if (delta_x < -40) delta_x = -40;

        // printf("delta: %d\n", delta_x);
        // Envia só se houve variação relevante
        if (abs(delta_x - last_delta_x) >= TOLERANCIA) {
            // printf("delta: %d\n", delta_x);

            xQueueSend(xQueueSteer, &delta_x, 0);
        }
        last_delta_x = delta_x;

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void accel_task(void *p) {
    uint16_t data;
    uint16_t accel_antigo = 0;

    while (1) {
        adc_select_input(1); // GPIO27

        const float conversion_factor = 3.3f / (1 << 12);
        uint16_t result = adc_read();
        data = (uint16_t)((result * conversion_factor / 3.3f) * 1000);

        if (abs((int)data - (int)accel_antigo) >= ANALOG_TOLERANCE) {
            xQueueSend(xQueueAccel, &data, pdMS_TO_TICKS(100));
            accel_antigo = data;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void break_task(void *p) {
    uint16_t data;
    uint16_t break_antigo = 0;

    while (1) {
        adc_select_input(0); // GPIO26

        const float conversion_factor = 3.3f / (1 << 12);
        uint16_t result = adc_read();
        data = (uint16_t)((result * conversion_factor / 3.3f) * 1000);

        if (abs((int)data - (int)break_antigo) >= ANALOG_TOLERANCE) {
            xQueueSend(xQueueBreak, &data, pdMS_TO_TICKS(100));
            break_antigo = data;
        }
        
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void uart_task(void *p) {
    int8_t data_steer = 0;
    int16_t data_accel = 0;
    int16_t data_break = 0;
    button_id btn;

    uint8_t pacote[10] = {0};
    pacote[9] = 0xFF; // delimitador

    // Inicializar UART do HC-06
    uart_init(HC06_UART_ID, HC06_BAUD_RATE);
    gpio_set_function(HC06_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(HC06_RX_PIN, GPIO_FUNC_UART);

    gpio_init(HC06_STATE_PIN);
    gpio_set_dir(HC06_STATE_PIN, GPIO_IN);

    const uint LED_PIN_RED = 16; 
    gpio_init(LED_PIN_RED);
    gpio_set_dir(LED_PIN_RED, GPIO_OUT);
    gpio_put(LED_PIN_RED, 1);

    const uint LED_PIN_YELLOW = 18; 
    gpio_init(LED_PIN_YELLOW);
    gpio_set_dir(LED_PIN_YELLOW, GPIO_OUT);
    
    const uint LED_PIN_BLUE = 17; 
    gpio_init(LED_PIN_BLUE);
    gpio_set_dir(LED_PIN_BLUE, GPIO_OUT);
    gpio_put(LED_PIN_BLUE, 0);

    vTaskDelay(pdMS_TO_TICKS(2000)); // Espera 2 segundos depois de configurar o HC-06

    // Inicializar o HC-06 (nome/senha)
    hc06_init("forza7", "1234");
    
    bool paired = false;
    
    while (true) {
        gpio_put(LED_PIN_RED, 0);
        bool has_data = false;
        
        bool connected = gpio_get(HC06_STATE_PIN);
        if (!paired) {
            // printf("yellow\n");
            gpio_put(LED_PIN_YELLOW, 1);
        }
        
        if (uart_is_readable(UART_ID)) {
            int status_led = uart_getc(UART_ID);
            paired = true;
            gpio_put(LED_PIN_YELLOW, 0);
            // printf("status: %d\n", status_led);
            gpio_put(LED_PIN_BLUE, status_led);
        }

        if (xQueueReceive(xQueueSteer, &data_steer, pdMS_TO_TICKS(5))) {
            pacote[0] = (uint8_t)data_steer;
            has_data = true;
        }

        if (xQueueReceive(xQueueAccel, &data_accel, pdMS_TO_TICKS(5))) {
            pacote[1] = (data_accel >> 8) & 0xFF;
            pacote[2] = data_accel & 0xFF;
            has_data = true;
        }

        if (xQueueReceive(xQueueBreak, &data_break, pdMS_TO_TICKS(5))) {
            pacote[3] = (data_break >> 8) & 0xFF;
            pacote[4] = data_break & 0xFF;
            has_data = true;
        }

        if (xQueueReceive(xQueueButtons, &btn, pdMS_TO_TICKS(5))) {
            if (btn.x) pacote[5] = (uint8_t)btn.x;
            if (btn.triangle) pacote[6] = (uint8_t)btn.triangle;
            if (btn.circle) pacote[7] = (uint8_t)btn.circle;
            if (btn.square) pacote[8] = (uint8_t)btn.square;
            has_data = true;
        }

        if (has_data) {
            uart_write_blocking(HC06_UART_ID, pacote, 10); //atualizado com bluetooth
        }

        pacote[5] = 0;
        pacote[6] = 0;
        pacote[7] = 0;
        pacote[8] = 0;

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

int main() {
    stdio_init_all();
    init_buttons();
    adc_init();
    adc_gpio_init(27);
    adc_gpio_init(26);

    xQueueSteer = xQueueCreate(32, sizeof(int8_t));
    xQueueAccel = xQueueCreate(32, sizeof(int16_t));
    xQueueBreak = xQueueCreate(32, sizeof(int16_t));
    xQueueButtons = xQueueCreate(32, sizeof(button_id));
    
    xTaskCreate(mpu6050_task, "mpu6050_Task", 8192, NULL, 1, NULL);
    xTaskCreate(accel_task, "accel_Task", 8192, NULL, 1, NULL);
    xTaskCreate(break_task, "break_Task", 8192, NULL, 1, NULL);
    xTaskCreate(uart_task, "uart_Task", 8192, NULL, 1, NULL);
    vTaskStartScheduler();

    while (true);
}
