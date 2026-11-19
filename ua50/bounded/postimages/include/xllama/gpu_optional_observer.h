#pragma once
#include <utility>

namespace xllama::gpu_proof {
// The ggml scheduler treats a false callback return as a stop request.
// Optional diagnostics may latch unavailability, but must never supply that
// return value or hide a required observer's refusal.
template<class Observe,class Fail>
void optional_observe(Observe&&observe,Fail&&fail)noexcept{
 try{std::forward<Observe>(observe)();}
 catch(...){std::forward<Fail>(fail)();}
}
}
