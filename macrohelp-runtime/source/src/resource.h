#pragma once

#ifndef IDC_STATIC
#define IDC_STATIC                      -1
#endif

// Icons
#define IDI_CURSOROVERLAY               101
#define IDI_SMALL                       102

// Menu items
#define IDM_EXIT                        201
#define IDM_TOGGLE_CROSSHAIR            202
#define IDM_TOGGLE_COORDS               203
#define IDM_TOGGLE_KEYS                 204
#define IDM_SAVE_CURSOR                 205
#define IDM_RECORD_KEYS                 206
#define IDM_CIRCLE_PLACER               207
#define IDM_CLICK_LEFT                  208
#define IDM_CLICK_RIGHT                 209
#define IDM_CLICK_MIDDLE                210
#define IDM_STOP_ALL_TASKET             211
#define IDM_VIEW_TOGGLES                212
#define IDM_PASTE_BUFFERS               213
#define IDM_REGISTRY_HUB                214

// Timer IDs
#define IDT_UPDATE_CURSOR               1001
#define IDT_UPDATE_COORDS               1002
#define IDT_UPDATE_KEYS                 1003

// Hotkey IDs
#define IDH_SAVE_CURSOR                 2001
#define IDH_RECORD_KEYS                 2002
#define IDH_CIRCLE_PLACER               2003
#define IDH_CLICK_LEFT                  2004
#define IDH_CLICK_RIGHT                 2005
#define IDH_CLICK_MIDDLE                2006
#define IDH_STOP_ALL_TASKET             2007
#define IDH_VIEW_TOGGLES                2008
#define IDH_PASTE_BUFFERS               2009
#define IDH_REGISTRY_HUB                2010

// Dialog IDs
#define IDD_SAVE_CURSOR                 3001
#define IDD_RECORD_KEYS                 3002
#define IDD_MANUAL_KEYS                 3003
#define IDD_CIRCLE_PLACER               3004
#define IDD_CLICK_ACTION                3005
#define IDD_VIEW_TOGGLES                3006
#define IDD_PASTE_BUFFERS               3007
#define IDD_REGISTRY_HUB                3008

// Control IDs - Save Cursor Dialog
#define IDC_EDIT_NAME                   4001
#define IDC_STATIC_COORDS               4002
#define IDC_BTN_ADD_ANOTHER_CURSOR      4003
#define IDC_BTN_ADD_CLICK_CURSOR        4004
#define IDC_STATIC_SAVE_NAME            4005
#define IDC_STATIC_SAVE_COORD_LABEL     4006
#define IDC_STATIC_SAVE_HINT            4007

// Control IDs - Record Keys Dialog
#define IDC_EDIT_SEQ_NAME               4101
#define IDC_LIST_KEYS                   4102
#define IDC_BTN_CAPTURE                 4103
#define IDC_BTN_ADD_STEP                4104
#define IDC_BTN_MANUAL                  4105
#define IDC_STATIC_STATUS               4106
#define IDC_BTN_ADD_ANOTHER_KEY         4107
#define IDC_EDIT_DELAY                  4108
#define IDC_EDIT_PASTE_TEXT             4109
#define IDC_BTN_ADD_PASTE               4110
#define IDC_STATIC_SEQ_NAME             4111
#define IDC_STATIC_SEQ_DELAY            4112
#define IDC_STATIC_RECORDED_FLOW        4113
#define IDC_STATIC_PASTE_BLOCK          4114
#define IDC_STATIC_RECORD_STATUS_LABEL  4115

// Control IDs - Manual Keys Dialog
#define IDC_COMBO_KEY1                  4201
#define IDC_COMBO_KEY2                  4202
#define IDC_COMBO_KEY3                  4203
#define IDC_BTN_ADD_KEY                 4204
#define IDC_BTN_ADD_STEP_MANUAL         4205
#define IDC_STATIC_STEP_NUM             4206
#define IDC_COMBO_KEY_CATEGORY          4207

// Control IDs - Circle Placer Dialog
#define IDC_EDIT_CIRCLE_VALUE           4301
#define IDC_STATIC_CIRCLE_PROMPT        4302
#define IDC_STATIC_CIRCLE_HINT          4303
#define IDC_STATIC_CIRCLE_STATUS        4304
#define IDC_BTN_CIRCLE_MOVE_NOW         4305
#define IDC_BTN_CIRCLE_APPEND_COORD     4306
#define IDC_BTN_CIRCLE_NEXT_CIRCLE      4307
#define IDC_BTN_CIRCLE_ZONE_ACTION      4308

// Control IDs - Click Action Dialog
#define IDC_EDIT_CLICK_VALUE            4401
#define IDC_STATIC_CLICK_PROMPT         4402
#define IDC_STATIC_CLICK_HINT           4403
#define IDC_STATIC_CLICK_STATUS         4404

// Control IDs - View Toggles Dialog
#define IDC_EDIT_VIEW_VALUE             4501
#define IDC_STATIC_VIEW_PROMPT          4502
#define IDC_STATIC_VIEW_HINT            4503
#define IDC_STATIC_VIEW_STATUS          4504

// Control IDs - Paste Buffers Dialog
#define IDC_EDIT_PASTE_BUFFER_VALUE     4601
#define IDC_STATIC_PASTE_BUFFER_PROMPT  4602
#define IDC_STATIC_PASTE_BUFFER_HINT    4603
#define IDC_STATIC_PASTE_BUFFER_STATUS  4604

// Control IDs - Registry Hub Dialog
#define IDC_EDIT_REGISTRY_HUB_VALUE     4801
#define IDC_STATIC_REGISTRY_HUB_PROMPT  4802
#define IDC_STATIC_REGISTRY_HUB_HINT    4803
#define IDC_STATIC_REGISTRY_HUB_STATUS  4804
#define IDC_BTN_REGISTRY_HUB_CLEAR      4805
#define IDC_BTN_REGISTRY_HUB_EXPORT     4806
#define IDC_BTN_REGISTRY_HUB_IMPORT     4807
#define IDC_BTN_REGISTRY_HUB_SAVE_SCHTS 4808

// Binary resource - PromptFont TTF
#define FONT_PROMPT                     5001
