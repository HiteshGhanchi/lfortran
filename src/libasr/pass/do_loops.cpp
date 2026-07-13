#include <libasr/asr.h>
#include <libasr/containers.h>
#include <libasr/exception.h>
#include <libasr/asr_utils.h>
#include <libasr/asr_verify.h>
#include <libasr/pass/replace_do_loops.h>
#include <libasr/pass/stmt_walk_visitor.h>
#include <libasr/pass/pass_utils.h>

namespace LCompilers {

using ASR::down_cast;
using ASR::is_a;

/*

This ASR pass replaces do loops with while loops. The function
`pass_replace_do_loops` transforms the ASR tree in-place.

Converts:

    do i = a, b, c
        ...
    end do

to:

    i = a-c
    do while (i+c <= b)
        i = i+c
        ...
    end do

The comparison is >= for c<0.
*/
class DoLoopVisitor : public ASR::StatementWalkVisitor<DoLoopVisitor>
{
public:
    bool use_loop_variable_after_loop = false;
    PassOptions pass_options;
    DoLoopVisitor(Allocator &al, PassOptions pass_options_) :
        StatementWalkVisitor(al), pass_options(pass_options_) { }

    void visit_DoLoop(const ASR::DoLoop_t &x) {
        pass_result = PassUtils::replace_doloop(al, x, -1, use_loop_variable_after_loop, this->current_scope);
    }

    void visit_DoConcurrentLoop(const ASR::DoConcurrentLoop_t &x) {
        if (pass_options.enable_gpu_offloading) {
            return;
        }

        SymbolTable* parent_scope = this->current_scope;
        SymbolTable* loop_scope = x.m_symtab;
        Vec<ASR::stmt_t*> body;body.reserve(al,1);
        for (int i = 0; i < static_cast<int>(x.n_body); i++) {
            body.push_back(al,x.m_body[i]);
        }
        for (int i = static_cast<int>(x.n_head) - 1; i >= 0; i--) {
            ASR::asr_t* do_loop = ASR::make_DoLoop_t(al, x.base.base.loc, s2c(al, ""), x.m_head[i], body.p, body.n, nullptr, 0);
            
            const ASR::DoLoop_t &do_loop_ref = (const ASR::DoLoop_t&)(*do_loop);
            body = PassUtils::replace_doloop(al, do_loop_ref, -1, use_loop_variable_after_loop, loop_scope);
        }
        std::string block_name = parent_scope->get_unique_name("do_concurrent_block");
        ASR::asr_t* block = ASR::make_Block_t(al, x.base.base.loc, loop_scope, s2c(al, block_name), body.p, body.n);
        ASR::symbol_t* block_symbol =ASR::down_cast<ASR::symbol_t>(block);
        parent_scope->add_symbol(block_name, block_symbol);
        ASR::stmt_t* block_call = ASRUtils::STMT(ASR::make_BlockCall_t(al, x.base.base.loc, -1, block_symbol));
        Vec<ASR::stmt_t*> result;
        result.reserve(al, 1);
        result.push_back(al, block_call);
        pass_result = result;        
    }
};

void pass_replace_do_loops(Allocator &al, ASR::TranslationUnit_t &unit,
                           const LCompilers::PassOptions& pass_options) {
    DoLoopVisitor v(al, pass_options);
    // Each call transforms only one layer of nested loops, so we call it twice
    // to transform doubly nested loops:
    v.asr_changed = true;
    v.use_loop_variable_after_loop = pass_options.use_loop_variable_after_loop;
    while( v.asr_changed ) {
        v.asr_changed = false;
        v.visit_TranslationUnit(unit);
    }
}


} // namespace LCompilers
