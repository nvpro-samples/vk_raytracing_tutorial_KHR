/*
 * Copyright (c) 2023-2026, NVIDIA CORPORATION.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-FileCopyrightText: Copyright (c) 2023-2026, NVIDIA CORPORATION.
 * SPDX-License-Identifier: Apache-2.0
 */


#pragma once

#include "nvapi.h"
#include "NvApiDriverSettings.h"
#include <iostream>

class NVAPIManager
{
public:
  NVAPIManager()
      : hSession(nullptr)
      , hProfile(nullptr)
  {
  }

  void init()
  {
    NvAPI_Status status = NvAPI_Initialize();
    checkNvapiStatus(status);

    status = NvAPI_DRS_CreateSession(&hSession);
    checkNvapiStatus(status);

    status = NvAPI_DRS_LoadSettings(hSession);
    checkNvapiStatus(status);

    status = NvAPI_DRS_GetBaseProfile(hSession, &hProfile);
    checkNvapiStatus(status);
  }

  void pushSetting(const NVDRS_SETTING& setting)
  {
    NVDRS_SETTING oldSetting = setting;
    NvAPI_Status  status     = NvAPI_DRS_GetSetting(hSession, hProfile, setting.settingId, &oldSetting);
    checkNvapiStatus(status);
    oldSettings.push_back(oldSetting);

    setSetting(setting);
  }

  void popSettings()
  {
    for(auto& setting : oldSettings)
    {
      setSetting(setting);
    }
  }

  void setSetting(const NVDRS_SETTING& setting)
  {
    NVDRS_SETTING nonConstSetting = setting;
    NvAPI_Status  status          = NvAPI_DRS_SetSetting(hSession, hProfile, &nonConstSetting);
    checkNvapiStatus(status);

    status = NvAPI_DRS_SaveSettings(hSession);
    checkNvapiStatus(status);
  }

  void deinit()
  {
    NvAPI_Status status = NvAPI_DRS_DestroySession(hSession);
    checkNvapiStatus(status);

    NvAPI_Unload();
  }

private:
  void checkNvapiStatus(NvAPI_Status status)
  {
    if(status != NVAPI_OK)
    {
      NvAPI_ShortString errorMessage;
      NvAPI_GetErrorMessage(status, errorMessage);
      std::cerr << "NVAPI error: " << errorMessage << std::endl;
      assert(false);
    }
  }

  NvDRSSessionHandle         hSession;
  NvDRSProfileHandle         hProfile;
  std::vector<NVDRS_SETTING> oldSettings;
};
