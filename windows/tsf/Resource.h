//
//  Resource.h — fire_tsf.dll 资源 ID
//
//  仅含输入法图标资源。该图标同时用于：
//    1. TSF 注册表 IconFile/IconIndex（输入法列表、设置面板图标）；
//    2. 语言栏按钮 GetIcon()（任务栏托盘 / 输入指示器运行时图标）。
//
#pragma once

// 输入法主图标。注册表 IconIndex 从 0 起算，DLL 内首个 ICON 资源即为 index 0。
// 注意：不要改名为 IDI_WINFIRE（与 fire_config 工程的 Resource.h 同名宏共用 ID 101，
// 但二者是独立工程、独立资源段，此处 DLL 用专属 ID 1 以符合 DLL 图标惯例）。
#define IDI_FIRE_TSF_ICON       1
