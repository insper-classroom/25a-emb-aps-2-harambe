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

#define UART_ID uart0
#define BAUD_RATE 115200
#define UART_TX_PIN 0
#define UART_RX_PIN 1

#define I2C_SDA_GPIO 4
#define I2C_SCL_GPIO 5
#define MPU_ADDRESS 0x68

#define CLICK_THRESHOLD 20
#define TOLERANCIA 1

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
    TickType_t lastClickTick = 0;

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
        float pitch = euler.angle.pitch;

        int8_t delta_x = (int8_t)(pitch * 2);

        if (delta_x > 127) delta_x = 127;
        if (delta_x < -127) delta_x = -127;

        xQueueSend(xQueueSteer, &delta_x, pdMS_TO_TICKS(sample_rate));
        vTaskDelay(pdMS_TO_TICKS(sample_rate));
    }
}

void accel_task(void *p) {
    uint16_t data;

    uint16_t accel_antigo = 0;

    while (1) {
        adc_select_input(1); //27

        const float conversion_factor = 3.3f / (1 << 12);
        uint16_t result = adc_read();
        printf("Accel: Raw value: 0x%03x, voltage: %f V\n", result, result * conversion_factor);
        data = result * conversion_factor;

        if (data != accel_antigo && accel_antigo != 0) {
            xQueueSend(xQueueAccel, &data, pdMS_TO_TICKS(100));
        }
        accel_antigo = data;
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void break_task(void *p) {
    uint16_t data;

    uint16_t break_antigo = 0;
    
    while (1) {
        adc_select_input(0); //26?

        const float conversion_factor = 3.3f / (1 << 12);
        uint16_t result = adc_read();
        printf("Break: Raw value: 0x%03x, voltage: %f V\n", result, result * conversion_factor);
        data = result * conversion_factor;

        if (data != break_antigo && break_antigo != 0) {
            xQueueSend(xQueueAccel, &data, pdMS_TO_TICKS(100));
        }
        break_antigo = data;
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void uart_task(void *p) {
    int8_t last_x = 0;
    int8_t last_accel = 0;
    int8_t last_break = 0;
    int8_t data_steer;
    int16_t data_accel;
    int16_t data_break;

    uint8_t pacote[6];
    
    while (true) {
        pacote[0] = 0;
        pacote[1] = 0;
        pacote[2] = 0;
        pacote[3] = 0;
        pacote[4] = 0;
        pacote[5] = 0xFF;

        int steer = 0;
        int accel = 0;
        int breaK = 0;
        if (xQueueReceive(xQueueSteer, &data_steer, pdMS_TO_TICKS(100))) {

            int8_t diff_x = data_steer.delta_x - last_x;

            if (abs(diff_x) >= TOLERANCIA) {
                uint8_t pacote[2];
                pacote[0] = (uint8_t) data_steer.delta_x;
                
                last_x = data_steer.delta_x;
                steer = 1;
            }
        }
        
        if (xQueueReceive(xQueueAccel, &data_accel, pdMS_TO_TICKS(100))) {
            pacote[1] = (data_accel >> 8) & 0xFF;    // accel high byte
            pacote[2] = data_accel & 0xFF;           // accel low byte
            accel = 1;
        }
        
        if (xQueueReceive(xQueueBreak, &data_break, pdMS_TO_TICKS(100))) {
            pacote[3] = (data_break >> 8) & 0xFF;    // brake high byte
            pacote[4] = data_break & 0xFF;           // brake low byte
            breaK = 1;
        }
        
        if (steer || accel || breaK) {
            uart_write_blocking(UART_ID, pacote, 6);
        }
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
