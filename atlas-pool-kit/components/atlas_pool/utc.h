#pragma once
#include "core.h"
#include <ctime>

namespace atlas_pool {
inline std::string iso8601(int64_t milliseconds) {
  if (milliseconds<1577836800000LL) return "";
  time_t seconds=milliseconds/1000;
  struct tm utc{};
  if (!gmtime_r(&seconds,&utc)) return "";
  char date[32], result[40];
  strftime(date,sizeof(date),"%Y-%m-%dT%H:%M:%S",&utc);
  snprintf(result,sizeof(result),"%s.%03dZ",date,int(milliseconds%1000));
  return result;
}
} // namespace atlas_pool
