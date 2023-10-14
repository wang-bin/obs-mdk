/*
  Copyright 2019 - 2023, WangBin wbsecg1 at gmail dot com and the obs-mdk contributors
  SPDX-License-Identifier: MIT
*/
#include <obs-module.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("mdk-video", "en-US")
MODULE_EXPORT const char *obs_module_description(void)
{
    return "mdk video source";
}

extern void register_mdkvideo();

bool obs_module_load()
{
    register_mdkvideo();
    return true;
}

void obs_module_unload()
{}
