#pragma once
#include "bounded_capture.h"
#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
namespace xllama::capture {
struct CaptureSink {
 virtual ~CaptureSink()=default;
 virtual bool write(const void*,size_t)=0;
 virtual bool flush()=0;
 // False means UNKNOWN. The sink and capture anchors must remain alive.
 virtual bool close()noexcept=0;
};
class Publication;
inline std::atomic<uint32_t>&publication_pending(){static std::atomic<uint32_t> count{0};return count;}
inline bool publication_resources_closed(){return publication_pending().load(std::memory_order_acquire)==0;}
/* Final index name is published ONLY after binary and temporary index handles
 * are proved closed. A failed/unknown close is never converted into a gap or
 * destructor-driven release. This registry retains those actual anchors. */
class Publication:public std::enable_shared_from_this<Publication> {
 std::shared_ptr<CaptureSink>binary_,index_;std::shared_ptr<void>anchor_;bool binary_closed_=false,index_closed_=false,binary_close_attempted_=false,index_close_attempted_=false,registered_=false,attempted_=false,committed_=false;
 static auto&mutex(){static std::mutex m;return m;}
 static auto&retained(){static auto*owners=new std::vector<std::shared_ptr<Publication>>;return *owners;}
 void clear_if_closed(){if((!binary_||binary_closed_)&&(!index_||index_closed_)){std::lock_guard<std::mutex>lock(mutex());if(registered_){registered_=false;publication_pending().fetch_sub(1,std::memory_order_release);auto&v=retained();v.erase(std::remove_if(v.begin(),v.end(),[&](const auto&p){return p.get()==this;}),v.end());}anchor_.reset();}}
 void close_known()noexcept{if(binary_&&!binary_close_attempted_){binary_close_attempted_=true;binary_closed_=binary_->close();}if(index_&&!index_close_attempted_){index_close_attempted_=true;index_closed_=index_->close();}try{clear_if_closed();}catch(...){}}
public:
 static std::shared_ptr<Publication>start(std::shared_ptr<void>anchor){need(bool(anchor),"capture publication anchor required");auto p=std::make_shared<Publication>();std::lock_guard<std::mutex>lock(mutex());need(retained().size()<16,"capture publication owner cap");p->anchor_=std::move(anchor);retained().push_back(p);p->registered_=true;publication_pending().fetch_add(1,std::memory_order_release);return p;}
 template<class Open,class Publish,class Guard>void commit(const std::vector<unsigned char>&binary,const std::string&index,Open&&open,Publish&&publish,Guard&&guard){
  need(!attempted_,"capture publication no replay");attempted_=true;
  try{need(binary.size()<=1081344&&index.size()<=262144,"capture publication hard caps");guard();binary_=open(false);need(bool(binary_),"capture binary CREATE_NEW");guard();need(binary_->write(binary.data(),binary.size()),"capture binary write");guard();need(binary_->flush(),"capture binary flush");binary_close_attempted_=true;binary_closed_=binary_->close();need(binary_closed_,"capture binary close UNKNOWN");guard();
   index_=open(true);need(bool(index_),"capture index CREATE_NEW");need(index_->write(index.data(),index.size()),"capture index write");guard();need(index_->flush(),"capture index flush");index_close_attempted_=true;index_closed_=index_->close();need(index_closed_,"capture index close UNKNOWN");guard();need(publish(),"capture closed index publication");guard();committed_=true;clear_if_closed();
  }catch(...){close_known();throw;}
 }
 bool committed()const{return committed_;}bool pending()const{return registered_;}
};
}
