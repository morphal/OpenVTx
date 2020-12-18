#include "Arduino.h"
#define SKIP_CODE
#include "targets.h"
#undef SKIP_CODE
#include "printf.h"
#include <gd32f1x0_gpio.h>
#include <gd32f1x0_usart.h>
#include <gd32f1x0_rcu.h>

struct usartx {
    uint32_t usart;
    uint32_t pin_rx, pin_tx, af;
};
struct usartx usart_config[] = {
    {USART0, PA10, PA9,  1},
    {USART0, PA3,  PA2,  1},
    {USART0, PA15, PA14, 1},
    {USART0, PB7,  PB6,  0},
    //{USART1, PA3,  PA2,  1}, // same as USART0
    {USART1, PB0,  PA8,  4},
    //{USART1, PA15, PA14, 1}, // same as USART0
};

static uint32_t usart_periph_selected, usart_periph_halfduplex;

static volatile uint8_t rx_buffer[64];
static volatile uint8_t rx_head, rx_tail;

static void USARTx_IRQHandler(uint32_t usart_periph)
{
  if ((USART_CTL0(usart_periph) & USART_RECEIVE_ENABLE) &&
      (RESET != usart_interrupt_flag_get(usart_periph, USART_INT_FLAG_RBNE))) {
        /* receive data */
        uint8_t next = rx_head;
        uint8_t data = usart_data_receive(usart_periph);
        if (((next + 1) % sizeof(rx_buffer)) != rx_tail) {
        rx_buffer[next] = data;
        rx_head = (next + 1) % sizeof(rx_buffer);
        }
    }
}

void USART0_IRQHandler(void)
{
    USARTx_IRQHandler(USART0);
}

void USART1_IRQHandler(void)
{
    USARTx_IRQHandler(USART1);
}

static void config_uart(struct usartx * usart_cfg, uint32_t baud, uint8_t halfduplex)
{
    uint32_t usart_periph = usart_cfg->usart;

    if (!halfduplex)
        pinAlternateConfig(usart_cfg->pin_rx, usart_cfg->af, 1);
    pinAlternateConfig(usart_cfg->pin_tx, usart_cfg->af, 1);

    usart_periph_selected = usart_periph;
    usart_periph_halfduplex = halfduplex;

    /* enable USART and GPIOA clock */
    //rcu_periph_clock_disable(RCU_DMA);
    //rcu_periph_clock_enable(RCU_GPIOA);
    //rcu_periph_clock_enable(RCU_USART0);

    /* configure the USART0 Tx pin and USART1 Tx pin */
    //gpio_af_set(GPIOA, GPIO_AF_1, GPIO_PIN_9);
    //gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO_PIN_9);
    //gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_9);

    /* enable USART clock */
    rcu_periph_clock_enable((usart_periph == USART1) ? RCU_USART1 : RCU_USART0);

    /* USART configure */
    // usart_deinit(usart_periph);
    /* 8N1 (standard) */
    usart_baudrate_set(usart_periph, baud);
    // usart_parity_config(usart_periph, USART_PM_NONE);
    // usart_word_length_set(usart_periph, USART_WL_8BIT);
    // usart_stop_bit_set(usart_periph, USART_STB_1BIT);
    if (halfduplex) {
        usart_halfduplex_enable(usart_periph);
        usart_transmit_config(usart_periph, USART_TRANSMIT_DISABLE);
    } else {
        usart_halfduplex_disable(usart_periph);
        usart_transmit_config(usart_periph, USART_TRANSMIT_ENABLE);
    }
    usart_receive_config(usart_periph, USART_RECEIVE_ENABLE);
    usart_interrupt_enable(usart_periph, USART_INT_RBNE);
    usart_enable(usart_periph);
    nvic_irq_enable((usart_periph == USART1) ? USART1_IRQn : USART0_IRQn, 0, 0);
}

void Serial_begin(uint32_t baud)
{
    uint8_t iter, halfduplex = (UART_TX == UART_RX);

    for (iter = 0; iter < ARRAY_SIZE(usart_config); iter++)
    {
        if (usart_config[iter].pin_tx == UART_TX && (halfduplex || usart_config[iter].pin_rx == UART_RX))
        {
            config_uart(&usart_config[iter], baud, halfduplex);
            break;
        }
    }
}

uint8_t Serial_available(void)
{
    return (uint32_t)(sizeof(rx_buffer) + rx_head - rx_tail) % sizeof(rx_buffer);
}

uint8_t Serial_read(void)
{
    uint8_t data = rx_buffer[rx_tail++];
    rx_tail %= sizeof(rx_buffer);
    return data;
}

void Serial_write(uint8_t data)
{
    uint32_t usart_periph = usart_periph_selected;
    uint8_t halfduplex = usart_periph_halfduplex;
    if (halfduplex) {
        usart_transmit_config(usart_periph, USART_TRANSMIT_ENABLE);
        usart_receive_config(usart_periph, USART_RECEIVE_DISABLE);
    }

    usart_data_transmit(usart_periph, data);
    /* wait until end of transmit */
    while (RESET == usart_flag_get(usart_periph, USART_FLAG_TC))
        ;

    if (halfduplex) {
        usart_transmit_config(usart_periph, USART_TRANSMIT_DISABLE);
        usart_receive_config(usart_periph, USART_RECEIVE_ENABLE);
    }
}

void Serial_write_len(uint8_t *data, uint32_t size)
{
    uint32_t usart_periph = usart_periph_selected;
    uint8_t halfduplex = usart_periph_halfduplex;
    if (halfduplex) {
        usart_transmit_config(usart_periph, USART_TRANSMIT_ENABLE);
        usart_receive_config(usart_periph, USART_RECEIVE_DISABLE);
    }

    while(size--) {
        usart_data_transmit(usart_periph, *data++);
        while (RESET == usart_flag_get(usart_periph, USART_FLAG_TBE))
            ;
    }
    /* wait until end of transmit */
    while (RESET == usart_flag_get(usart_periph, USART_FLAG_TC))
        ;

    if (halfduplex) {
        usart_transmit_config(usart_periph, USART_TRANSMIT_DISABLE);
        usart_receive_config(usart_periph, USART_RECEIVE_ENABLE);
    }
}

void Serial_flush(void)
{
    // not needed...
}

void _putchar(char character)
{
    Serial_write((uint8_t)character);
}
