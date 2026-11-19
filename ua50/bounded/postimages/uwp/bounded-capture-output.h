#pragma once
#include "dev-run-input.h"
#include "xllama/bounded_publication.h"
#include <fileapifromapp.h>
namespace xllama::bridge {
class BoundedCaptureSink final:public capture::CaptureSink {
 HANDLE handle_=INVALID_HANDLE_VALUE;bool attempted_=false;
public:
 explicit BoundedCaptureSink(const std::filesystem::path&path){handle_=CreateFile2(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,CREATE_NEW,nullptr);if(handle_==INVALID_HANDLE_VALUE)throw std::runtime_error("bounded capture CREATE_NEW");}
 // Unknown close is deliberately NOT retried by the destructor. Publication's
 // process-lifetime registry retains this sink and its capture anchors.
 bool write(const void*data,size_t bytes)override{if(attempted_||handle_==INVALID_HANDLE_VALUE||bytes>1081344)return false;DWORD n=0;return WriteFile(handle_,data,DWORD(bytes),&n,nullptr)&&n==bytes;}
 bool flush()override{return !attempted_&&handle_!=INVALID_HANDLE_VALUE&&FlushFileBuffers(handle_);}
 bool close()noexcept override{if(attempted_)return handle_==INVALID_HANDLE_VALUE;attempted_=true;if(handle_==INVALID_HANDLE_VALUE)return true;if(!CloseHandle(handle_))return false;handle_=INVALID_HANDLE_VALUE;return true;}
};
template<class Guard>inline void publish_bounded_capture(const std::filesystem::path&root,const std::string&run,const std::vector<unsigned char>&binary,const std::string&index,std::shared_ptr<void>anchor,Guard&&guard){
 dev_run::need(dev_run::run_id(run)&&binary.size()<=1081344&&index.size()<=262144,"bounded publication identity/caps");
 const std::wstring prefix(run.begin(),run.end());const auto b=root/(prefix+L".bounded-diagnostics-v1.bin"),temp=root/(prefix+L".bounded-diagnostics-v1.index.pending"),final=root/(prefix+L".bounded-diagnostics-v1.index.json");
 auto publication=capture::Publication::start(std::move(anchor));
 publication->commit(binary,index,[&](bool idx){return std::make_shared<BoundedCaptureSink>(idx?temp:b);},[&]{return MoveFileFromAppW(temp.c_str(),final.c_str())!=FALSE;},std::forward<Guard>(guard));
}
}
