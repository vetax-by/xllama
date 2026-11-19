#pragma once
#include <cstdint>
#include "ggml.h"
#include <string_view>
namespace xllama::capture {
struct GraphPoint {const char*point;const char*semantic;uint32_t op,n0,n1;bool i32,layer;uint32_t mapping=0;};
inline constexpr GraphPoint graph_points[]={
 {"embedding","model.embed_tokens",40,2048,1,false,false},
 {"operator_norm","model.layers.{}.operator_norm",7,2048,1,false,true},
 {"operator_conv_out","model.layers.{}.conv.out_proj",29,2048,1,false,true},
 {"operator_attn_out","model.layers.{}.self_attn.out_proj",29,2048,1,false,true},
 {"pre_ffn_residual","model.layers.{}.pre_ffn_residual",2,2048,1,false,true},
 {"ffn_norm","model.layers.{}.ffn_norm",7,2048,1,false,true},
 {"router_logits","ffn_moe_logits",29,64,1,false,true},
 {"router_logits_biased","ffn_moe_logits_biased",2,64,1,false,true},
 {"router_probs","ffn_moe_probs",46,64,1,false,true},
 {"router_probs_biased","ffn_moe_probs_biased",2,64,1,false,true},
 {"router_probs_masked","ffn_moe_probs_masked",36,64,1,false,true},
 {"router_rank","ffn_moe_argsort",69,64,1,true,true},
 {"router_topk","ffn_moe_topk",37,4,1,true,true},
 {"router_weights","ffn_moe_weights",40,1,4,false,true},
 {"router_weights_norm","ffn_moe_weights_norm",8,4,1,false,true},
 {"router_weights_scaled","ffn_moe_weights_scaled",32,1,4,false,true},
 {"moe_gate","ffn_moe_gate",30,1536,4,false,true},
 {"moe_up","ffn_moe_up",30,1536,4,false,true},
 {"moe_activation","ffn_moe_swiglu",100,1536,4,false,true},
 {"moe_down","ffn_moe_down",30,2048,4,false,true},
 {"moe_weighted","ffn_moe_weighted",7,2048,4,false,true},
 {"moe_aggregate","ffn_moe_out",2,2048,1,false,true},
 {"dense_ffn_out","model.layers.{}.dense_ffn_out",29,2048,1,false,true},
 {"post_ffn_residual","model.layers.{}.post_ffn_residual",2,2048,1,false,true},
 {"result_norm","result_norm",7,2048,1,false,false},
 {"result_output","result_output",29,128000,1,false,false},
 {"conv_in_proj","model.layers.{}.conv.in_proj",GGML_OP_MUL_MAT,6144,1,false,true},
 {"conv_preinput","@capture.lfm2.conv_preinput",GGML_OP_CONCAT,64,2048,false,true,1},
 {"conv_inner_out","model.layers.{}.conv.conv",GGML_OP_SSM_CONV,2048,1,false,true},
 {"q_norm","model.layers.{}.self_attn.q_layernorm",GGML_OP_MUL,2048,1,false,true,2},
 {"k_norm","model.layers.{}.self_attn.k_layernorm",GGML_OP_MUL,2048,1,false,true,3},
 {"q_post_rope","@capture.lfm2.q_post_rope",GGML_OP_ROPE,2048,1,false,true,2},
 {"k_post_rope","@capture.lfm2.k_post_rope",GGML_OP_ROPE,2048,1,false,true,3},
 {"attention_scores","kq",GGML_OP_MUL_MAT,512,64,false,true,4},
 {"attention_probabilities","kq_soft_max",GGML_OP_SOFT_MAX,512,64,false,true,4},
 {"attention_weighted_v","kqv",GGML_OP_MUL_MAT,2048,1,false,true,5}
};
inline bool graph_shape(const GraphPoint&p,const ggml_tensor*t){
 if(!t)return false;for(int d=0;d<4;++d)if(t->ne[d]<=0)return false;
 if(!p.mapping)return t->ne[0]==p.n0&&t->ne[1]==p.n1&&t->ne[2]==1&&t->ne[3]==1;
 if(p.mapping==1)return t->ne[0]>=2&&t->ne[0]<=64&&t->ne[1]==2048&&t->ne[2]==1&&t->ne[3]==1;
 if(p.mapping==2||p.mapping==3)return t->ne[0]>=32&&t->ne[0]<=256&&t->ne[1]<=64&&t->ne[2]==1&&t->ne[3]==1&&(p.mapping==2?t->ne[0]*t->ne[1]==2048:t->ne[0]*t->ne[1]<=2048);
 if(p.mapping==4)return t->ne[0]<=512&&t->ne[1]==1&&t->ne[2]<=64&&t->ne[3]==1;
 if(p.mapping==5)return t->ne[0]<=256&&t->ne[1]==1&&t->ne[2]<=64&&t->ne[3]==1&&t->ne[0]*t->ne[2]==2048;
 return false;
}
inline const GraphPoint*graph_point(std::string_view name){for(const auto&p:graph_points)if(name==p.point)return &p;return nullptr;}
}
