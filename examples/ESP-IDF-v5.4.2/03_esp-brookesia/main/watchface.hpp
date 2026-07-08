#pragma once

#include "lvgl.h"
#include <time.h>
#include "driver/gpio.h"

class Watchface {
public:
    Watchface(lv_obj_t* parent);
    ~Watchface();

    void updateTime();
    void toggleVisibility();
    void showSettings();
    void hideSettings();
    void saveTime();

private:
    lv_obj_t* main_container;
    lv_obj_t* time_label;
    lv_obj_t* date_label;
    
    lv_timer_t* time_timer;
    lv_timer_t* button_timer;

    bool last_button_state;

    // Componentes da Tela de Ajustes
    lv_obj_t* settings_container;
    lv_obj_t* roller_hour;
    lv_obj_t* roller_minute;
    lv_obj_t* roller_day;
    lv_obj_t* roller_month;
    lv_obj_t* roller_year;

    bool readTimeFromRTC(struct tm* timeinfo); 

    static void time_timer_cb(lv_timer_t * t);
    static void button_timer_cb(lv_timer_t * t);
    static void settings_event_cb(lv_event_t * e);
    static void save_btn_event_cb(lv_event_t * e);
};