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

#define LONG_PRESS_MS 1500
#define DEBOUNCE_MS 20

#define BUTTON_PRESSED 0x01
#define BUTTON_RELEASED 0x02

TaskHandle_t mainTaskHandle = nullptr;
TaskHandle_t ledTaskHandle = nullptr;
volatile TickType_t lastEdge = 0;

struct DCCMember {
    const char* name;
    uint32_t color;
};

DCCMember MemberList[] = 
{
    {"Kitsune", 0xFF249D},
    {"Rina", 0xDB0076},
    {"Yuzu", 0x8700FF},
    {"Lullia", 0x1643c9},
    {"Nana", 0x000073}
};

const size_t MEMBER_COUNT = sizeof(MemberList) / sizeof(MemberList[0]);

static void IRAM_ATTR button_isr_handler(void* arg) {
    TickType_t now = xTaskGetTickCountFromISR();
    if((now - lastEdge) < pdMS_TO_TICKS(DEBOUNCE_MS)) { //Debounce
        return;
    }
    lastEdge = now;
    uint8_t level = gpio_get_level(BUTTON_GPIO); // 0 == press, 1 == release
    BaseType_t higherPriorityTaskWoken = pdFALSE;

    if(level == 0) {
        xTaskNotifyFromISR(mainTaskHandle, BUTTON_PRESSED, eSetValueWithOverwrite, &higherPriorityTaskWoken);
    }
    else {
        xTaskNotifyFromISR(mainTaskHandle, BUTTON_RELEASED, eSetValueWithOverwrite, &higherPriorityTaskWoken);
    }
    if(higherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
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
     mainTaskHandle = xTaskGetCurrentTaskHandle();

    // LED Config
    led_strip_handle_t led_strip = nullptr;
    led_strip_config_t strip_config = {
        .strip_gpio_num = RGB_LED_GPIO,
        .max_leds = 7,
        .led_model = LED_MODEL_SK6812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRBW,
        .flags = {
            .invert_out = false
        }
    };
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
    
    led_strip_clear(led_strip);

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
        uint32_t notification = 0;
        xTaskNotifyWait(0, UINT32_MAX, &notification, portMAX_DELAY);

        if(notification == BUTTON_PRESSED){
            //wait for 1 second for 'long press' or detect button release before 1 second
            uint32_t releaseNotif = 0;
            BaseType_t result = xTaskNotifyWait(0, UINT32_MAX, &releaseNotif, pdMS_TO_TICKS(LONG_PRESS_MS));

            if(releaseNotif == BUTTON_RELEASED && result == pdTRUE) { //button released before long press timer
                if(systemOn) {
                    memberInd = (memberInd + 1) % MEMBER_COUNT;
                    set_color(led_strip, MemberList[memberInd].color);
                    ESP_LOGI(TAG, "Member: %s", MemberList[memberInd].name);

                }
            }
            else if(result == pdFALSE) { // long press system toggle
                systemOn = !systemOn;
                if(systemOn) {
                    ESP_LOGI(TAG, "System on");
                    set_color(led_strip, MemberList[0].color);
                    memberInd = 0;
                }
                else {
                    ESP_LOGI(TAG, "System off");
                    led_strip_clear(led_strip);
                }
                // wait for user to release button
                while(true){
                    uint32_t releaseNotif = 0;
                    xTaskNotifyWait(0, UINT32_MAX, &releaseNotif, portMAX_DELAY);
                    if(releaseNotif == BUTTON_RELEASED) {
                        break;
                    }
                }
            }
        }


    }
}
