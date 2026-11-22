/* Bootloader hook: hold the vibration motor (GPIO8) off from the first instruction of the
 * second-stage bootloader, instead of waiting for the app's constructor. Without this the motor's
 * driver input floats through the whole boot, and through every reset if the app crashes. */
#include "hal/gpio_ll.h"
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"

#define MOTOR_GPIO 8

void bootloader_hooks_include(void) {}

void bootloader_before_init(void)
{
    esp_rom_gpio_pad_select_gpio(MOTOR_GPIO);
    gpio_ll_set_level(&GPIO, MOTOR_GPIO, 0);
    esp_rom_gpio_connect_out_signal(MOTOR_GPIO, SIG_GPIO_OUT_IDX, false, false);
    gpio_ll_pullup_dis(&GPIO, MOTOR_GPIO);
    gpio_ll_pulldown_en(&GPIO, MOTOR_GPIO);
    gpio_ll_output_enable(&GPIO, MOTOR_GPIO);
    gpio_ll_set_level(&GPIO, MOTOR_GPIO, 0);
}
