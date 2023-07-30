// This file was generated by SquareLine Studio
// SquareLine Studio version: SquareLine Studio 1.3.1
// LVGL version: 8.3.6
// Project name: display_ball

#ifndef _DISPLAY_BALL_UI_H
#define _DISPLAY_BALL_UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

#include "ui_helpers.h"
#include "ui_events.h"
// SCREEN: ui_Clock
void ui_Clock_screen_init(void);
void ui_event_Clock(lv_event_t * e);
extern lv_obj_t * ui_Clock;
void ui_event_left_panel_1(lv_event_t * e);
extern lv_obj_t * ui_left_panel_1;
void ui_event_right_panel_1(lv_event_t * e);
extern lv_obj_t * ui_right_panel_1;
extern lv_obj_t * ui_clock_label;
extern lv_obj_t * ui_clock_seconds;
// SCREEN: ui_Demo
void ui_Demo_screen_init(void);
extern lv_obj_t * ui_Demo;
void ui_event_left_panel_2(lv_event_t * e);
extern lv_obj_t * ui_left_panel_2;
void ui_event_right_panel_2(lv_event_t * e);
extern lv_obj_t * ui_right_panel_2;
extern lv_obj_t * ui_Spinner1;
// SCREEN: ui_Demo1
void ui_Demo1_screen_init(void);
extern lv_obj_t * ui_Demo1;
void ui_event_left_panel_3(lv_event_t * e);
extern lv_obj_t * ui_left_panel_3;
void ui_event_right_panel_3(lv_event_t * e);
extern lv_obj_t * ui_right_panel_3;
extern lv_obj_t * ui_Colorwheel1;
// SCREEN: ui_Demo2
void ui_Demo2_screen_init(void);
extern lv_obj_t * ui_Demo2;
void ui_event_left_panel_4(lv_event_t * e);
extern lv_obj_t * ui_left_panel_4;
void ui_event_right_panel_4(lv_event_t * e);
extern lv_obj_t * ui_right_panel_4;
extern lv_obj_t * ui_Label2;
extern lv_obj_t * ui_Chart2;
extern lv_obj_t * ui____initial_actions0;

void ui_init(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
