#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace xllama::ingress_reason_v2 {
enum class Stage : uint8_t { identity, proof, cpu_dev, generation };
inline const char* stage_name(Stage s){switch(s){case Stage::identity:return "identity";case Stage::proof:return "proof";case Stage::cpu_dev:return "cpu_dev";case Stage::generation:return "generation";}return "unknown";}
enum Mask : uint32_t { none=0, limit_event=1u<<0, limit_changed=1u<<1, steady_deadline=1u<<2, utc_expired=1u<<3, abort_requested=1u<<4, quota_unstable=1u<<5, pressure_or_headroom=1u<<6, clock_unknown=1u<<7, file_state_unknown=1u<<8 };
// Instrumentation observes the existing stage predicates. It never grants an
// admission or replaces the predicate evaluated by a stage.
inline bool limit_callback_stops(Stage s,uint64_t initial,uint64_t new_limit){return s==Stage::identity||new_limit!=initial;}
struct Event {uint64_t sequence=0,steady_elapsed_ns=0,device_utc_ms=0,old_limit=0,new_limit=0,usage=0;uint32_t mask=0;bool steady_available=false,utc_available=false,limit_available=false,usage_available=false,pressure=false,stopped=false;};
struct Snapshot {uint64_t steady_elapsed_ns=0,device_utc_ms=0,expires_at_ms=0,initial_limit=0,current_limit=0,usage=0;uint32_t predicate_mask=0;bool steady_available=false,utc_available=false,quota_available=false;};
struct Recorder {
 static constexpr size_t event_cap=32;
 static constexpr size_t serializer_capacity_cap=32768;
 static constexpr size_t descriptor_charge=4096;
 std::array<Event,event_cap> events{};std::array<std::atomic<bool>,event_cap> ready{};std::atomic<uint64_t> issued{0};std::atomic<uint32_t> observed_mask{0};std::atomic<uint32_t> stop_observed_mask{0};std::atomic<uint32_t> first_mask{0};std::atomic<bool> truncated{false};
 // The first stopping event has a separate bounded slot. Keep the lowest issued
 // sequence even if concurrent callbacks finish out of order after the ring fills.
 mutable std::atomic_flag stop_lock=ATOMIC_FLAG_INIT;std::atomic<bool> first_stop_ready{false};Event first_stop{};
 void record(Event e) noexcept {const uint64_t n=issued.fetch_add(1,std::memory_order_relaxed);e.sequence=n+1;observed_mask.fetch_or(e.mask,std::memory_order_relaxed);if(e.stopped){stop_observed_mask.fetch_or(e.mask,std::memory_order_relaxed);while(stop_lock.test_and_set(std::memory_order_acquire)){}if(!first_stop_ready.load(std::memory_order_relaxed)||e.sequence<first_stop.sequence){first_stop=e;first_stop_ready.store(true,std::memory_order_release);}stop_lock.clear(std::memory_order_release);}if(n<event_cap){events[size_t(n)]=e;if(n==0)first_mask.store(e.mask,std::memory_order_release);ready[size_t(n)].store(true,std::memory_order_release);}else truncated.store(true,std::memory_order_relaxed);}
 bool get(size_t n,Event& out)const noexcept {if(n>=event_cap||!ready[n].load(std::memory_order_acquire))return false;out=events[n];return true;}
 bool get_first_stop(Event& out)const noexcept {while(stop_lock.test_and_set(std::memory_order_acquire)){}const bool ready=first_stop_ready.load(std::memory_order_relaxed);if(ready)out=first_stop;stop_lock.clear(std::memory_order_release);return ready;}
 size_t stored()const noexcept {size_t n=0;for(;n<event_cap&&ready[n].load(std::memory_order_acquire);++n){}return n;}
};
inline std::string render(const Recorder& r,Stage stage,const std::string& run,const std::string& ticket_sha,const std::string& output_sha,uint64_t expires){
 // Run and hashes have already passed the exact v2 parsers; no arbitrary text,
 // credentials, paths, model bytes or Windows error strings enter this record.
 std::string s; s.reserve(12288);
 s="{\"schema\":\"xllama-ingress-reason-v2\",\"diagnostic_only\":true,\"stage\":\""+std::string(stage_name(stage))+"\",\"run_id\":\""+run+"\",\"ticket_sha256\":\""+ticket_sha+"\",\"output_sha256\":\""+output_sha+"\",\"expires_at_epoch_ms\":"+std::to_string(expires)+",\"events_total\":"+std::to_string(r.issued.load())+",\"observed_mask\":"+std::to_string(r.observed_mask.load())+",\"stop_observed_mask\":"+std::to_string(r.stop_observed_mask.load())+",\"truncated\":"+(r.truncated.load()?"true":"false")+",\"events\":[";
 auto event_json=[](const Event&e){return "{\"sequence\":"+std::to_string(e.sequence)+",\"mask\":"+std::to_string(e.mask)+",\"stopped\":"+(e.stopped?std::string("true"):"false")+",\"steady_available\":"+(e.steady_available?std::string("true"):"false")+",\"steady_elapsed_ns\":"+(e.steady_available?std::to_string(e.steady_elapsed_ns):"null")+",\"device_utc_available\":"+(e.utc_available?std::string("true"):"false")+",\"device_utc_ms\":"+(e.utc_available?std::to_string(e.device_utc_ms):"null")+",\"limit_available\":"+(e.limit_available?std::string("true"):"false")+",\"old_limit\":"+(e.limit_available?std::to_string(e.old_limit):"null")+",\"new_limit\":"+(e.limit_available?std::to_string(e.new_limit):"null")+",\"usage_available\":"+(e.usage_available?std::string("true"):"false")+",\"usage\":"+(e.usage_available?std::to_string(e.usage):"null")+",\"pressure\":"+(e.usage_available?(e.pressure?"true":"false"):"null")+"}";};
 size_t count=0;for(size_t n=0;n<Recorder::event_cap;++n){Event e;if(!r.get(n,e))break;if(count++)s+=",";
  s+=event_json(e);
 }
 Event first_stop; s+="],\"first_stop\":";if(r.get_first_stop(first_stop))s+=event_json(first_stop);else s+="null";s+="}";if(s.size()>16384||s.capacity()>Recorder::serializer_capacity_cap)throw std::length_error("ingress reason sidecar cap");return s;
}
inline uint32_t stop_mask(const Snapshot& s,bool event_latched,bool abort_latched,bool steady_expired,bool quota_bad){uint32_t mask=none;if(event_latched)mask|=limit_event;if(abort_latched)mask|=abort_requested;if(steady_expired)mask|=steady_deadline;if(!s.utc_available)mask|=clock_unknown;else if(s.device_utc_ms>=s.expires_at_ms)mask|=utc_expired;if(!s.quota_available)mask|=quota_unstable;else if(quota_bad)mask|=pressure_or_headroom;if(s.quota_available&&s.current_limit!=s.initial_limit)mask|=limit_changed;return mask;}
}
