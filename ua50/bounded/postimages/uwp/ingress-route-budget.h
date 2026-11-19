#pragma once
#include "xllama/ingress_v2.h"
#include <winrt/Windows.System.h>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace xllama::bridge {
// This is a route reservation, charged before any selected model stage. The
// measured delta is sampled evidence; the source caps bound retained and
// transient scanner allocations separately. Neither is a peak measurement.
struct IngressRouteQuota {
 uint64_t limit=0,usage=0,charge=0;
 static std::pair<uint64_t,uint64_t> sample(uint64_t charge){
  using M=winrt::Windows::System::MemoryManager;
  const auto first=M::AppMemoryUsageLimit();
  const auto used=M::AppMemoryUsage();
  const auto level=M::AppMemoryUsageLevel();
  const auto second=M::AppMemoryUsageLimit();
  const bool low_or_medium=level==winrt::Windows::System::AppMemoryUsageLevel::Low||
      level==winrt::Windows::System::AppMemoryUsageLevel::Medium;
  if(!ingress_v2::route_quota_admitted(first,second,used,low_or_medium,charge))
    throw std::runtime_error("ingress route quota/headroom refused");
  return {first,used};
 }
 explicit IngressRouteQuota(uint64_t reservation):charge(reservation){
  const auto q=sample(charge);limit=q.first;usage=q.second;
 }
 void finish()const{
  const auto q=sample(charge);
  if(q.first!=limit||q.second>usage+charge)
    throw std::runtime_error("ingress route quota changed or charge exceeded");
 }
};
}
