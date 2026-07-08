#include "watchface.hpp"
#include "systems/phone/assets/esp_brookesia_phone_assets.h"
#include "driver/i2c.h" 
#include <sys/time.h>   
#include <cstring> 
#include <cstdio>  

#define BOOT_BUTTON_PIN GPIO_NUM_0
#define PCF85063_I2C_ADDR 0x51 

static uint8_t bcd2dec(uint8_t val) { return (val >> 4) * 10 + (val & 0x0f); }
static uint8_t dec2bcd(uint8_t val) { return ((val / 10) << 4) + (val % 10); }

bool Watchface::readTimeFromRTC(struct tm* timeinfo) {
    i2c_port_t i2c_num = I2C_NUM_0; 
    uint8_t reg_addr = 0x04; 
    uint8_t data[7]; 

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (PCF85063_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (PCF85063_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, data, 7, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    
    esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);

    if (ret == ESP_OK) {
        timeinfo->tm_sec  = bcd2dec(data[0] & 0x7F);
        timeinfo->tm_min  = bcd2dec(data[1] & 0x7F);
        timeinfo->tm_hour = bcd2dec(data[2] & 0x3F);
        timeinfo->tm_mday = bcd2dec(data[3] & 0x3F);
        timeinfo->tm_mon  = bcd2dec(data[5] & 0x1F) - 1; 
        timeinfo->tm_year = bcd2dec(data[6]) + 100;      
        return true;
    }
    return false;
}

Watchface::Watchface(lv_obj_t* parent) {
    struct tm rtc_time = {0};
    if (readTimeFromRTC(&rtc_time)) {
        struct timeval tv;
        tv.tv_sec = mktime(&rtc_time);
        tv.tv_usec = 0;
        settimeofday(&tv, NULL);
    }

    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << BOOT_BUTTON_PIN);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);
    
    last_button_state = true; 

    main_container = lv_obj_create(parent);
    lv_obj_set_size(main_container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(main_container, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_border_width(main_container, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(main_container, 0, LV_PART_MAIN);

    lv_obj_add_flag(main_container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(main_container, settings_event_cb, LV_EVENT_LONG_PRESSED, this);

    time_label = lv_label_create(main_container);
    lv_obj_set_style_text_font(time_label, &esp_brookesia_font_maison_neue_book_48, LV_PART_MAIN); 
    lv_obj_set_style_text_color(time_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_transform_scale(time_label, 600, LV_PART_MAIN);
    lv_obj_set_style_text_align(time_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(time_label, "00:00");
    lv_obj_align(time_label, LV_ALIGN_CENTER, -40, -40); 

    date_label = lv_label_create(main_container);
    lv_obj_set_style_text_font(date_label, &esp_brookesia_font_maison_neue_book_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(date_label, lv_color_hex(0xAAAAAA), LV_PART_MAIN);
    lv_label_set_text(date_label, "01/01/2026");
    lv_obj_align(date_label, LV_ALIGN_CENTER, 0, 90); 

    // --- TELA DE AJUSTES ---
    settings_container = lv_obj_create(parent);
    lv_obj_set_size(settings_container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(settings_container, lv_color_hex(0x0A0A0A), 0);
    lv_obj_set_style_border_width(settings_container, 0, 0);
    lv_obj_add_flag(settings_container, LV_OBJ_FLAG_HIDDEN); 

    lv_obj_t * title = lv_label_create(settings_container);
    lv_label_set_text(title, "Ajustar Data e Hora");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // Aumento agressivo dos buffers para evitar overflow
    static char opts_d[120], opts_mo[50], opts_y[150];
    static char opts_h[100], opts_m[200];
    opts_d[0] = '\0'; opts_mo[0] = '\0'; opts_y[0] = '\0'; opts_h[0] = '\0'; opts_m[0] = '\0';

    for(int i=1; i<=31; i++) sprintf(opts_d + strlen(opts_d), "%02d\n", i);
    for(int i=1; i<=12; i++) sprintf(opts_mo + strlen(opts_mo), "%02d\n", i);
    for(int i=2024; i<=2050; i++) sprintf(opts_y + strlen(opts_y), "%04d\n", i);
    for(int i=0; i<24; i++) sprintf(opts_h + strlen(opts_h), "%02d\n", i);
    for(int i=0; i<60; i++) sprintf(opts_m + strlen(opts_m), "%02d\n", i);
    
    opts_d[strlen(opts_d)-1] = '\0'; opts_mo[strlen(opts_m)-1] = '\0';
    opts_y[strlen(opts_y)-1] = '\0'; opts_h[strlen(opts_h)-1] = '\0'; opts_m[strlen(opts_m)-1] = '\0';

    // Seção de Data
    roller_day = lv_roller_create(settings_container);
    lv_roller_set_options(roller_day, opts_d, LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(roller_day, 3);
    lv_obj_align(roller_day, LV_ALIGN_CENTER, -100, -80);
    lv_obj_t * lbl_d = lv_label_create(settings_container);
    lv_label_set_text(lbl_d, "Dia");
    lv_obj_align_to(lbl_d, roller_day, LV_ALIGN_OUT_TOP_MID, 0, -5);

    roller_month = lv_roller_create(settings_container);
    lv_roller_set_options(roller_month, opts_mo, LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(roller_month, 3);
    lv_obj_align(roller_month, LV_ALIGN_CENTER, 0, -80);
    lv_obj_t * lbl_mo = lv_label_create(settings_container);
    lv_label_set_text(lbl_mo, "Mes");
    lv_obj_align_to(lbl_mo, roller_month, LV_ALIGN_OUT_TOP_MID, 0, -5);

    roller_year = lv_roller_create(settings_container);
    lv_roller_set_options(roller_year, opts_y, LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(roller_year, 3);
    lv_obj_align(roller_year, LV_ALIGN_CENTER, 100, -80);
    lv_obj_t * lbl_y = lv_label_create(settings_container);
    lv_label_set_text(lbl_y, "Ano");
    lv_obj_align_to(lbl_y, roller_year, LV_ALIGN_OUT_TOP_MID, 0, -5);

    // Seção de Hora
    roller_hour = lv_roller_create(settings_container);
    lv_roller_set_options(roller_hour, opts_h, LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(roller_hour, 3);
    lv_obj_align(roller_hour, LV_ALIGN_CENTER, -50, 80);
    lv_obj_t * lbl_h = lv_label_create(settings_container);
    lv_label_set_text(lbl_h, "Hora");
    lv_obj_align_to(lbl_h, roller_hour, LV_ALIGN_OUT_TOP_MID, 0, -5);

    roller_minute = lv_roller_create(settings_container);
    lv_roller_set_options(roller_minute, opts_m, LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(roller_minute, 3);
    lv_obj_align(roller_minute, LV_ALIGN_CENTER, 50, 80);
    lv_obj_t * lbl_m = lv_label_create(settings_container);
    lv_label_set_text(lbl_m, "Min");
    lv_obj_align_to(lbl_m, roller_minute, LV_ALIGN_OUT_TOP_MID, 0, -5);

    // Botão de Salvar
    lv_obj_t * save_btn = lv_btn_create(settings_container);
    lv_obj_set_size(save_btn, 120, 50);
    lv_obj_align(save_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_t * save_label = lv_label_create(save_btn);
    lv_label_set_text(save_label, "Salvar");
    lv_obj_center(save_label);
    lv_obj_add_event_cb(save_btn, save_btn_event_cb, LV_EVENT_CLICKED, this);

    time_timer = lv_timer_create(time_timer_cb, 1000, this);
    button_timer = lv_timer_create(button_timer_cb, 50, this); 
    
    updateTime();
}

Watchface::~Watchface() {
    if (time_timer) lv_timer_del(time_timer);
    if (button_timer) lv_timer_del(button_timer);
    if (main_container) lv_obj_del(main_container);
    if (rtc_handle) i2c_master_bus_rm_device(rtc_handle);
}

void Watchface::updateTime() {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    lv_label_set_text_fmt(time_label, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    lv_label_set_text_fmt(date_label, "%02d/%02d/%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
}

void Watchface::toggleVisibility() {
    if (lv_obj_has_flag(main_container, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_clear_flag(main_container, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(main_container, LV_OBJ_FLAG_HIDDEN);
    }
}

void Watchface::showSettings() {
    lv_obj_add_flag(main_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(settings_container, LV_OBJ_FLAG_HIDDEN);
    
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    // Atualiza todos os rollers para o tempo atual do sistema
    lv_roller_set_selected(roller_day, timeinfo.tm_mday - 1, LV_ANIM_OFF);
    lv_roller_set_selected(roller_month, timeinfo.tm_mon, LV_ANIM_OFF);
    
    int current_year = timeinfo.tm_year + 1900;
    if (current_year < 2024) current_year = 2024;
    if (current_year > 2050) current_year = 2050;
    lv_roller_set_selected(roller_year, current_year - 2024, LV_ANIM_OFF);
    
    lv_roller_set_selected(roller_hour, timeinfo.tm_hour, LV_ANIM_OFF);
    lv_roller_set_selected(roller_minute, timeinfo.tm_min, LV_ANIM_OFF);
}

void Watchface::hideSettings() {
    lv_obj_add_flag(settings_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(main_container, LV_OBJ_FLAG_HIDDEN);
}

void Watchface::saveTime() {
    int d = lv_roller_get_selected(roller_day) + 1;
    int mo = lv_roller_get_selected(roller_month); // 0-11
    int y = lv_roller_get_selected(roller_year) + 2024;
    int h = lv_roller_get_selected(roller_hour);
    int m = lv_roller_get_selected(roller_minute);

    struct tm timeinfo = {0};
    timeinfo.tm_mday = d;
    timeinfo.tm_mon = mo;
    timeinfo.tm_year = y - 1900;
    timeinfo.tm_hour = h;
    timeinfo.tm_min = m;
    timeinfo.tm_sec = 0;
    
    // Opcional: Calcular o dia da semana correto usando mktime
    mktime(&timeinfo);

    struct timeval tv;
    tv.tv_sec = mktime(&timeinfo);
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);

    if (rtc_handle != NULL) {
        uint8_t write_buf[8];
        write_buf[0] = 0x04; 
        
        write_buf[1] = dec2bcd(0);                 
        write_buf[2] = dec2bcd(timeinfo.tm_min);   
        write_buf[3] = dec2bcd(timeinfo.tm_hour);  
        write_buf[4] = dec2bcd(timeinfo.tm_mday);  
        write_buf[5] = timeinfo.tm_wday;  
        write_buf[6] = dec2bcd(timeinfo.tm_mon + 1);
        write_buf[7] = dec2bcd(y - 2000); // Ex: 2026 -> 26

        i2c_master_transmit(rtc_handle, write_buf, 8, 1000);
    }

    hideSettings();
    updateTime(); 
}

void Watchface::time_timer_cb(lv_timer_t * t) {
    Watchface *wf = (Watchface *)lv_timer_get_user_data(t);
    if (wf) wf->updateTime();
}

void Watchface::button_timer_cb(lv_timer_t * t) {
    Watchface *wf = (Watchface *)lv_timer_get_user_data(t);
    if (!wf) return;

    bool current_state = gpio_get_level(BOOT_BUTTON_PIN);
    
    // Botão BOOT foi pressionado
    if (wf->last_button_state == true && current_state == false) {
        if (!lv_obj_has_flag(wf->settings_container, LV_OBJ_FLAG_HIDDEN)) {
            // Se as configurações estiverem abertas, apenas fecha sem salvar
            wf->hideSettings();
        } else {
            // Se estiver na tela normal, alterna para o menu do Brookesia
            wf->toggleVisibility();
        }
    }
    
    wf->last_button_state = current_state;
}

void Watchface::settings_event_cb(lv_event_t * e) {
    Watchface *wf = (Watchface *)lv_event_get_user_data(e);
    wf->showSettings();
}

void Watchface::save_btn_event_cb(lv_event_t * e) {
    Watchface *wf = (Watchface *)lv_event_get_user_data(e);
    wf->saveTime();
}