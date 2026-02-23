#pragma once

// ============================================================================
// KT-WinDiagTool Resource IDs
// Shared between C++ source and Resources.rc
// ============================================================================

// --- Icons (IDI_) ---
#define IDI_APP_ICON            101

// --- Menu IDs (IDM_) ---
#define IDM_MAINMENU            200

#define IDM_FILE_OPEN_CONFIG    210
#define IDM_FILE_SET_OUTPUT     211
#define IDM_FILE_EXIT           212

#define IDM_COLLECT_RUN         220
#define IDM_COLLECT_CANCEL      221
#define IDM_COLLECT_SELECT_ALL  222
#define IDM_COLLECT_DESELECT    223

#define IDM_TOOLS_REPRO         230
#define IDM_TOOLS_ANALYZE       231
#define IDM_TOOLS_WPP_START     232
#define IDM_TOOLS_WPP_STOP      233
#define IDM_TOOLS_PERF_START    234
#define IDM_TOOLS_PERF_STOP     235

#define IDM_HELP_ABOUT          240

// --- Accelerator Table ---
#define IDR_ACCEL               250

// --- Toolbar ---
#define IDT_TOOLBAR             300

// --- Dialog IDs (IDD_) ---
#define IDD_PROGRESS            400
#define IDD_REPRO               410
#define IDD_ABOUT               420

// --- MainWindow Control IDs (IDC_) ---
#define IDC_COLLECTOR_GROUP     1000
#define IDC_COLLECTOR_FIRST     1001
#define IDC_COLLECTOR_LAST      1019
#define IDC_BTN_SELECT_ALL      1020
#define IDC_BTN_DESELECT_ALL    1021

#define IDC_CONFIG_PATH_LABEL   1100
#define IDC_CONFIG_BROWSE       1101
#define IDC_OUTPUT_PATH_EDIT    1110
#define IDC_OUTPUT_BROWSE       1111

#define IDC_CTRL_GROUP          1200
#define IDC_WPP_TOGGLE          1201
#define IDC_WPP_STATUS          1202
#define IDC_PERF_TOGGLE         1203
#define IDC_PERF_STATUS         1204
#define IDC_ETW_TOGGLE          1205
#define IDC_ETW_STATUS          1206
#define IDC_PACKET_TOGGLE       1207
#define IDC_PACKET_STATUS       1208

#define IDC_TRACE_GROUP         1300
#define IDC_WPP_LEVEL_COMBO     1301
#define IDC_WPP_LEVEL_LABEL     1302
#define IDC_PRODUCT_LEVEL_COMBO 1303
#define IDC_PRODUCT_LEVEL_LABEL 1304

#define IDC_BTN_COLLECT         1400
#define IDC_BTN_REPRO           1401
#define IDC_BTN_ANALYZE         1402

// --- ProgressDialog Control IDs ---
#define IDC_PROG_OVERALL        1500
#define IDC_PROG_STATUS_TEXT    1501
#define IDC_PROG_LISTVIEW       1502
#define IDC_PROG_CANCEL         1503

// --- ReproModeDialog Control IDs ---
#define IDC_REPRO_STEP_LABEL    1600
#define IDC_REPRO_WPP_LEVEL     1601
#define IDC_REPRO_WPP_LABEL     1602
#define IDC_REPRO_STATUS_LIST   1603
#define IDC_REPRO_ELAPSED       1604
#define IDC_REPRO_PROGRESS      1605
#define IDC_REPRO_LISTVIEW      1606
#define IDC_REPRO_START         1607
#define IDC_REPRO_STOP          1608
#define IDC_REPRO_CLOSE         1609

// --- Timer IDs ---
#define IDT_REPRO_ELAPSED       3000
