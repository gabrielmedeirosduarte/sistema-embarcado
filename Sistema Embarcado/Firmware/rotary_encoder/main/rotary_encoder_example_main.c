// Autores: Gabriel M. Duarte e Kléber R. S. Júnior
// Data: 08 de maio de 2024

// Bibliotecas:
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/portmacro.h"
#include "esp_log.h"
#include "driver/pulse_cnt.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "driver/ledc.h"
#include "driver/uart.h"
#include "freertos/timers.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include <stdio.h>
#include <rom/ets_sys.h>
#include "soc/timer_group_struct.h"
#include "soc/timer_group_reg.h"
#include <stdlib.h>
#include <string.h>
#include "esp_timer.h"

// Criação da TAG "encoder" (leitura digital):
static const char *TAG = "encoder";

// Definição da contagem mínima e máxima da unidade PCNT:
#define EXAMPLE_PCNT_HIGH_LIMIT 10000
#define EXAMPLE_PCNT_LOW_LIMIT  -10000

// Definição dos pinos:
#define EXAMPLE_EC11_GPIO_A 16
#define EXAMPLE_EC11_GPIO_B 17
#define GPIO_CONTROL_SIGNAL 15

// Configuração do UART:
void setup_uart() {
    uart_config_t uart_config = {
        .baud_rate = 921600,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_NUM_0, &uart_config);
    uart_set_pin(UART_NUM_0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_NUM_0, 1024 * 2, 0, 0, NULL, 0);
}

// Configuração do duty cycle:
void set_duty(float duty)
{
    int max_voltage_source = 24;
    int max_voltage_cooler = 16; 
    int bits = 8191;
    int normalized_bits = (int)(bits*max_voltage_cooler/max_voltage_source);
    int real_duty_cycle = (int)(duty*normalized_bits/100);
	ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, real_duty_cycle);
	ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

// Função principal:
void app_main(void)
{
    // Inicialização do UART:
    setup_uart();

    // Configuração do GPIO da carga:
	gpio_set_direction(GPIO_CONTROL_SIGNAL, GPIO_MODE_OUTPUT);

	// Configuração do PWM (timer):
	ledc_timer_config_t pwm1c = {
		.speed_mode 		= LEDC_LOW_SPEED_MODE,
		.timer_num 			= LEDC_TIMER_0,
		.duty_resolution 	= LEDC_TIMER_13_BIT,
		.freq_hz 			= 100,
		.clk_cfg 			= LEDC_AUTO_CLK
	};
	ledc_timer_config(&pwm1c);

    // Configuração do PWM (canal):
	ledc_channel_config_t pwm1ch = {
		.speed_mode = LEDC_LOW_SPEED_MODE,
		.channel 	= LEDC_CHANNEL_0,
		.timer_sel 	= LEDC_TIMER_0,
		.intr_type 	= LEDC_INTR_DISABLE,
		.gpio_num 	= GPIO_CONTROL_SIGNAL,
		.duty		= 0,
		.hpoint		= 0,
	};
	ledc_channel_config(&pwm1ch);

	// Instalação da unidade PCNT:
    pcnt_unit_config_t unit_config = {
        .high_limit = EXAMPLE_PCNT_HIGH_LIMIT,
        .low_limit = EXAMPLE_PCNT_LOW_LIMIT,
    };
    pcnt_unit_handle_t pcnt_unit = NULL;
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &pcnt_unit));

    // Configuração do filtro da unidade PCNT:
    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = 1000,
    };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config));

    // Instalação dos canais PCNT:
    pcnt_chan_config_t chan_a_config = {
        .edge_gpio_num = EXAMPLE_EC11_GPIO_A,
        .level_gpio_num = EXAMPLE_EC11_GPIO_B,
    };

    // Configuração dos canais A e B, da unidade PCNT:
    pcnt_channel_handle_t pcnt_chan_a = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_a_config, &pcnt_chan_a));
    pcnt_chan_config_t chan_b_config = {
        .edge_gpio_num = EXAMPLE_EC11_GPIO_B,
        .level_gpio_num = EXAMPLE_EC11_GPIO_A,
    };
    pcnt_channel_handle_t pcnt_chan_b = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_b_config, &pcnt_chan_b));

    // Configuração das ações de "edge" e "level" dos canais PCNT:
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    // Configuração "default" do canal A como "low level":
    #if CONFIG_EXAMPLE_WAKE_UP_LIGHT_SLEEP
        ESP_ERROR_CHECK(gpio_wakeup_enable(EXAMPLE_EC11_GPIO_A, GPIO_INTR_LOW_LEVEL));
        ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());
        ESP_ERROR_CHECK(esp_light_sleep_start());
    #endif
    
    // Variável de contagem de pulsos e de pulsos por revolução:
    int pulse_count = 0;
    int ppr = 2500*4;

    // Variável controlada e de sinal de controle:
    float angle = 0.0;
    float duty_cycle = 0.0; 

    // Variável para controle do início e do fim do experimento:
    char input;
    
    // Variável para transmissão de dados:
    char data[30];

    // Variável contadora de iterações:
    int cont = 0;

   // Estrutura para aguardar o comando de inicialização do experimento, pela IHM:
    while (true) {
        while (uart_read_bytes(UART_NUM_0, (uint8_t*)&input, 1, portMAX_DELAY) <= 0); 
        if (input == 's') {
            break;
        }
    }

    // Aplicação do duty cycle inicial:
    set_duty(duty_cycle);

    // Variáveis de controle de tempo:
    int init_time = 0;
    uint64_t current_time;
    int current_time_ms;
    int end_time = 200000;
    current_time = esp_timer_get_time();
    current_time_ms = current_time / 1000.0;
    init_time = current_time / 1000.0;

    // Tempo de amostragem em "ms":
    int Ts = 1;
    
    // Status de inicialização da unidade PCNT:
    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));

    // Laço de repetição:
    while (1) {

        // Cálculo do tempo atual:
        current_time = esp_timer_get_time();
        current_time_ms = (current_time / 1000.0) - init_time;

        // Leitura:
        angle = -(360.0*(float)pulse_count)/(float)ppr;

        // Escrita:
        duty_cycle = 30.0;
        set_duty(duty_cycle);

        // Condição de parada (por tempo):
        if (cont>=end_time)
        {
            ESP_LOGI(TAG,"Fim");
            exit(1);
        }

        // Condição de parada (pela IHM):
        if(uart_read_bytes(UART_NUM_0, (uint8_t*)&input, 1,0.0005)>=0){
            if(input == 'e')
                exit(1);
        }
    
        // Checagem de erros:
        ESP_ERROR_CHECK(pcnt_unit_get_count(pcnt_unit, &pulse_count));
        
        // Envio de dados:
        snprintf(data, sizeof(data),"%.4f,%.4f,%d,\n", angle, duty_cycle, current_time_ms);
        uart_write_bytes(UART_NUM_0,data,strlen(data));

        // Contador de iterações:
        cont += 1;

        // Tempo de espera até a próxima iteração, assegurando o tempo de amostragem "Ts" fixo: 
        while (((current_time / 1000.0) - init_time) <= (current_time_ms+Ts))
            current_time = esp_timer_get_time();

    } 
}