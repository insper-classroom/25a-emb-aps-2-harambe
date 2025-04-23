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

#define BUTTON1_GPIO 10
#define BUTTON2_GPIO 11
#define BUTTON3_GPIO 12
#define BUTTON4_GPIO 13

#define UART_ID uart0
#define BAUD_RATE 115200
#define UART_TX_PIN 0
#define UART_RX_PIN 1

#define I2C_SDA_GPIO 4
#define I2C_SCL_GPIO 5
#define MPU_ADDRESS 0x68

#define ANALOG_TOLERANCE 10
#define TOLERANCIA 1

typedef enum {
    BUTTON_1,
    BUTTON_2,
    BUTTON_3,
    BUTTON_4
} ButtonId;

QueueHandle_t xQueueSteer;
QueueHandle_t xQueueAccel;
QueueHandle_t xQueueBreak;

void init_uart() {
    uart_init(UART_ID, BAUD_RATE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
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

        int8_t delta_x = (int8_t)(roll * 2);

        if (delta_x > 127) delta_x = 127;
        if (delta_x < -127) delta_x = -127;

        // Envia só se houve variação relevante
        if (abs(delta_x - last_delta_x) >= TOLERANCIA) {
            xQueueSend(xQueueSteer, &delta_x, 0);
            last_delta_x = delta_x;
        }

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

    uint8_t pacote[6];

    pacote[0] = 0;
    pacote[1] = 0;
    pacote[2] = 0;
    pacote[3] = 0;
    pacote[4] = 0;
    pacote[5] = 0xFF;
    
    while (true) {

        bool has_data = false;

        if (xQueueReceive(xQueueSteer, &data_steer, pdMS_TO_TICKS(5))) {
            pacote[0] = (uint8_t) data_steer;
            has_data = true;
        }

        if (xQueueReceive(xQueueAccel, &data_accel, pdMS_TO_TICKS(0))) {
            pacote[1] = (data_accel >> 8) & 0xFF;
            pacote[2] = data_accel & 0xFF;
            has_data = true;
        }

        if (xQueueReceive(xQueueBreak, &data_break, pdMS_TO_TICKS(0))) {
            pacote[3] = (data_break >> 8) & 0xFF;
            pacote[4] = data_break & 0xFF;
            has_data = true;
        }

        if (has_data) {
            uart_write_blocking(UART_ID, pacote, 6);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

int main() {
    stdio_init_all();
    init_uart();

    adc_init();
    adc_gpio_init(27);
    adc_gpio_init(26);

    xQueueSteer = xQueueCreate(32, sizeof(int8_t));
    xQueueAccel = xQueueCreate(32, sizeof(int16_t));
    xQueueBreak = xQueueCreate(32, sizeof(int16_t));

    xTaskCreate(mpu6050_task, "mpu6050_Task", 8192, NULL, 1, NULL);
    xTaskCreate(accel_task, "accel_Task", 8192, NULL, 1, NULL);
    xTaskCreate(break_task, "break_Task", 8192, NULL, 1, NULL);
    xTaskCreate(uart_task, "uart_Task", 8192, NULL, 1, NULL);

    vTaskStartScheduler();

    while (true);
}
