#include <libopencm3/cm3/nvic.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/stm32/dma.h>
#include <libopencm3/stm32/exti.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/spi.h>
#include <libopencm3/stm32/usart.h>
#include <stdlib.h>
#include <strings.h>
#include "delay.h"
#include "uart.h"

volatile uint32_t millis;
volatile uint8_t activ = 0;
uint32_t ul[3];
volatile uint16_t u[3];
volatile uint16_t up;
char bufC[64];

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define abs(x) ((x) > 0 ? (x) : -(x))
#define constrain(amt, low, high) \
    ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#define round(x) ((x) >= 0 ? (long)((x) + 0.5) : (long)((x)-0.5))
long map(long x, long in_min, long in_max, long out_min, long out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

union {
    struct {
        int16_t speed;
        uint16_t light;
        uint16_t sysa;
        uint16_t sysb;
    } msg;
    uint8_t buf[64];
} packMotor;            // Пакет для релейного блока

union {
    struct {
        int16_t angle;
        uint16_t sysa;
    } msg;
    uint8_t buf[64];
} packSteering;         // Пакает для веска

void clock_setup() {
    rcc_clock_setup_in_hse_8mhz_out_72mhz();
    delayInit();
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_GPIOB);
    rcc_periph_clock_enable(RCC_GPIOC);
    rcc_periph_clock_enable(RCC_AFIO);
    rcc_periph_clock_enable(RCC_DMA1);
    rcc_periph_clock_enable(RCC_USART1);
    rcc_periph_clock_enable(RCC_USART2);
    rcc_periph_clock_enable(RCC_USART3);
}

void systick_setup() {
    nvic_set_priority(NVIC_SYSTICK_IRQ, 0x10);
    systick_set_reload(89999);
    systick_interrupt_enable();
    systick_counter_enable();
}

void gpio_setup() {
    gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_50_MHZ,
                  GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO_USART2_TX);
    gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_50_MHZ,
                  GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO_USART3_TX);
    gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL,
                  GPIO12);
}

void extiInit() {
    gpio_set_mode(GPIOB, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN,
                  GPIO12 | GPIO13 | GPIO14);
    exti_select_source(GPIO12 | GPIO13 | GPIO14, GPIOB);
    exti_set_trigger(GPIO12 | GPIO13 | GPIO14, EXTI_TRIGGER_BOTH);
    exti_enable_request(GPIO12 | GPIO13 | GPIO14);
    nvic_enable_irq(NVIC_EXTI15_10_IRQ);
}

void sendPacketMotor() { // Передача моторному блоку
    uint16_t crc = 211;
    uint8_t data[sizeof(packMotor.msg) + 3] = {0x55, 0x55};
    for (uint16_t i = 0; i < sizeof(packMotor.msg); i++) {
        data[i + 2] = packMotor.buf[i];
        crc += packMotor.buf[i] * 211;
        crc ^= crc >> 8;
    }
    data[sizeof(data) - 1] = crc;

    DMA_CCR(DMA1, DMA_CHANNEL7) &= ~DMA_CCR_EN;
    DMA_CMAR(DMA1, DMA_CHANNEL7) = (uint32_t)data;
    DMA_CNDTR(DMA1, DMA_CHANNEL7) = sizeof(data);
    DMA_CCR(DMA1, DMA_CHANNEL7) |= DMA_CCR_EN;
    while (DMA_CNDTR(DMA1, DMA_CHANNEL7))  // Ожидание окончания передачи чтобы не накладывалось (хз есть польза или нет)
        ;
}

void sendPacketSteering() { // Передача вескному блоку
    uint16_t crc = 211;
    uint8_t data[sizeof(packSteering.msg) + 3] = {0x55, 0x55};
    for (uint16_t i = 0; i < sizeof(packSteering.msg); i++) {
        data[i + 2] = packSteering.buf[i];
        crc += packSteering.buf[i] * 211;
        crc ^= crc >> 8;
    }
    data[sizeof(data) - 1] = crc;

    DMA_CCR(DMA1, DMA_CHANNEL2) &= ~DMA_CCR_EN;
    DMA_CMAR(DMA1, DMA_CHANNEL2) = (uint32_t)data;
    DMA_CNDTR(DMA1, DMA_CHANNEL2) = sizeof(data);
    DMA_CCR(DMA1, DMA_CHANNEL2) |= DMA_CCR_EN;
    while (DMA_CNDTR(DMA1, DMA_CHANNEL2)) // Ожидание окончания передачи чтобы не накладывалось (хз есть польза или нет)
        ;
}

int main(void) {
    clock_setup();
    gpio_setup();
    usart_setup();
    extiInit();

    // sprintf(bufC, "\n");
    uPD("\nTarantaika start...\n");// Для определения что за прошивка

    USART_BRR(USART2) = rcc_apb1_frequency / 19200 + 1; // Скорость юарта 
    USART_CR1(USART2) |= USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
    USART_CR3(USART2) |= USART_CR3_DMAT;
    DMA_CPAR(DMA1, DMA_CHANNEL7) = (uint32_t)&USART_DR(USART2);
    DMA_CCR(DMA1, DMA_CHANNEL7) = DMA_CCR_DIR | DMA_CCR_MINC | DMA_CCR_EN;

    USART_BRR(USART3) = rcc_apb1_frequency / 19200 + 1; // Скорость юарта 
    USART_CR1(USART3) |= USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
    USART_CR3(USART3) |= USART_CR3_DMAT;
    DMA_CPAR(DMA1, DMA_CHANNEL2) = (uint32_t)&USART_DR(USART3);
    DMA_CCR(DMA1, DMA_CHANNEL2) = DMA_CCR_DIR | DMA_CCR_MINC | DMA_CCR_EN;

    // sprintf(bufC, "WHOAMI : 0x%X\n");
    // uPD(bufC);
    systick_setup();
    while (1) {
        memset(packMotor.buf, 0, 64);                                     // По умолчанию пакет пуст и все в нулях
        memset(packSteering.buf, 0, 64);
        if (activ) {                                                      // Если пульт активен
            packMotor.msg.sysa |= (1 << 0);                               // Говорит релейке что пульт жив
            if (u[1] > 1550) {                                            // Если газ больше мз товключить фары и подать скорость
                packMotor.msg.light |= (1 << 0);
                packMotor.msg.speed =
                    constrain(map(u[1], 1550, 2000, 0, 10000), 0, 10000);
            }
            if (u[1] < 1400) packMotor.msg.sysb |= (1 << 1);              // Включение рекупирации
            if (u[1] < 1200) {                                            // Включение тормоза
                packMotor.msg.sysb |= (1 << 0);
                packMotor.msg.light |= (1 << 3);
            }
            if (u[2] > 1500) packMotor.msg.speed = -packMotor.msg.speed;  // Если назад то инвертируем скорость

            if (u[0] > 1700) packMotor.msg.light |= (1 << 2);             // Поворотники
            if (u[0] < 1300) packMotor.msg.light |= (1 << 1);
            packSteering.msg.sysa |= (1 << 0);                            // Говорит веску что пульт жив 
            packSteering.msg.angle =
                constrain(map(u[0], 1050, 1950, -10000, 10000), -10000, 10000);  // Значения на пульт
        }
        sendPacketMotor();  // Отправка
        sendPacketSteering();
        // sprintf(bufC, "%d %d %d\n", u[0], u[1], u[2]);  // Тут значния с пульта можно дебажить юарт 1
        // uPD(bufC);
    }
}

void sys_tick_handler() {
    millis++;

    if (!(((up & 0x000F) == 0x000F) || ((up & 0x00F0) == 0x00F0) ||
          ((up & 0x0F00) == 0x0F00))) {
        up += 0x111;
        activ = 1;

    } else {
        activ = 0;
        u[0] = 0;
        u[1] = 0;
        u[2] = 0;
    }
}

void exti15_10_isr() {
    uint32_t sysTime = DWT_CYCCNT;
    if (exti_get_flag_status(EXTI12)) {
        exti_reset_request(EXTI12);
        if (!gpio_get(GPIOB, GPIO12)) {
            uint16_t uz = (sysTime - ul[0]) / 72;
            if (uz > 800 && uz < 2200) {          // Страшный хрень которая вроде фиксит то что система сходит сума от шумов
                u[0] = uz;
                up &= 0xFFF0;
            }
        } else
            ul[0] = sysTime;
    }

    if (exti_get_flag_status(EXTI13)) {
        exti_reset_request(EXTI13);
        if (!gpio_get(GPIOB, GPIO13)) {
            uint16_t uz = (sysTime - ul[1]) / 72;
            if (uz > 800 && uz < 2200) {
                u[1] = uz;
                up &= 0xFF0F;
            }
        } else
            ul[1] = sysTime;
    }

    if (exti_get_flag_status(EXTI14)) {
        exti_reset_request(EXTI14);
        if (!gpio_get(GPIOB, GPIO14)) {
            uint16_t uz = (sysTime - ul[2]) / 72;
            if (uz > 800 && uz < 2200) {
                u[2] = uz;
                up &= 0xF0FF;
            }
        } else
            ul[2] = sysTime;
    }
}