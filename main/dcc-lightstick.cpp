#include <cstdio>
#include "led_strip.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "led_strip_rmt.h"
#include <string>

static const char *TAG = "NEOPIXEL";

#define RGB_LED_GPIO 16
#define BUTTON_GPIO GPIO_NUM_6

struct DCCMember {
    const char* name;
    uint32_t color;
};

DCCMember MemberList[] = 
{
    {"Kitsune", 0xd65aa0},
    {"Rina", 0xFFFFFF},
    {"Yuzu", 0x52099c},
    {"Lullia", 0x1643c9},
    {"Nana", 0x000073}
};

const size_t MEMBER_COUNT = sizeof(MemberList) / sizeof(MemberList[0]);

#define LONG_PRESS_MS 1000
#define DEBOUNCE_MS 30

enum class ButtonEvent {
    None,
    ShortPress,
    LongPress
};

volatile ButtonEvent buttonEvent = ButtonEvent::None;
volatile TickType_t pressStart = 0;
volatile TickType_t lastEdge = 0;
static void IRAM_ATTR button_isr_handler(void* arg) {
    int level = gpio_get_level(BUTTON_GPIO);
    TickType_t now = xTaskGetTickCountFromISR();

    if ((now - lastEdge) < pdMS_TO_TICKS(DEBOUNCE_MS)) {
        return;
    }
    lastEdge = now;

    if(level == 0) {
        pressStart = now;
    }
    else {
        if(pressStart != 0) {
            TickType_t pressDuration = now - pressStart;
            if (pressDuration >= pdMS_TO_TICKS(LONG_PRESS_MS)) {
                buttonEvent = ButtonEvent::LongPress;
            }
            else {
                buttonEvent = ButtonEvent::ShortPress;
            }
            pressStart = 0;
        }
    }
    
}



// LED Helper
void set_color(led_strip_handle_t led_strip, uint32_t color)
{
    uint8_t red   = (color >> 16) & 0xFF;
    uint8_t green = (color >> 8)  & 0xFF;
    uint8_t blue  = color & 0xFF;

    for (int i = 0; i < 7; i++) {
        led_strip_set_pixel_rgbw(led_strip, i, red, green, blue, 0);
    }

    led_strip_refresh(led_strip);
}


extern "C" void app_main(void)
{
    led_strip_handle_t led_strip = nullptr;
    // Configure the onboard WS2812 RGB LED
    led_strip_config_t strip_config = {
        .strip_gpio_num = RGB_LED_GPIO,
        .max_leds = 7,
        .led_model = LED_MODEL_SK6812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRBW,
        .flags = {
            .invert_out = false
        }
    };
    // Configure the RMT peripheral
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 0,
        .flags = {
            .with_dma = false
        }
    };
    ESP_ERROR_CHECK(
        led_strip_new_rmt_device(
            &strip_config,
            &rmt_config,
            &led_strip
        )
    );
    
    // Button setup
    gpio_reset_pin(BUTTON_GPIO);
    gpio_set_direction(BUTTON_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_GPIO, GPIO_PULLUP_ONLY);

    gpio_set_intr_type(BUTTON_GPIO, GPIO_INTR_ANYEDGE);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_GPIO, button_isr_handler, nullptr);

    bool systemOn = false;
    uint32_t memberInd = 0;

    while (true) {
        ButtonEvent event = buttonEvent;
        buttonEvent = ButtonEvent::None;

        switch(event) {
            case ButtonEvent::None:
                break;
            case ButtonEvent::ShortPress:
                if(systemOn){
                    memberInd++;
                    if(memberInd >= MEMBER_COUNT) {
                        memberInd = 0;
                    }
                    set_color(led_strip, MemberList[memberInd].color);
                    ESP_LOGI(TAG, "Member: %s", MemberList[memberInd].name);
                }
                break;
            case ButtonEvent::LongPress:
                systemOn = !systemOn;
                if(systemOn){
                    ESP_LOGI(TAG, "System on");
                    memberInd++;
                    if(memberInd >= MEMBER_COUNT) {
                        memberInd = 0;
                    }
                    set_color(led_strip, MemberList[memberInd].color);
                    ESP_LOGI(TAG, "Member: %s", MemberList[memberInd].name);
                }
                else {
                    ESP_LOGI(TAG, "System off");
                    led_strip_clear(led_strip);
                }

                break;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
