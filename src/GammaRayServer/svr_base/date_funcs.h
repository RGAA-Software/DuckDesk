//
// Created by RGAA on 19/08/2025.
//

#ifndef GAMMARAYPREMIUM_DATE_FUNCS_H
#define GAMMARAYPREMIUM_DATE_FUNCS_H

#pragma once
#include "redis_cache.h"

template <>
inline trantor::Date fromString<trantor::Date>(const std::string &str)
{
    return trantor::Date(std::atoll(str.data()));
}

template <>
inline std::string toString<trantor::Date>(const trantor::Date &date)
{
    return std::to_string(date.microSecondsSinceEpoch());
}

#endif //GAMMARAYPREMIUM_DATE_FUNCS_H
