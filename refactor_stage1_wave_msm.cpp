#include <iostream>

// Boost Meta State Machine library (for tracking preprocessor "stack")
#include <boost/msm/front/state_machine_def.hpp>
#include <boost/msm/front/functor_row.hpp>
#include <boost/msm/back/state_machine.hpp>
// for the Not_ operator
#include <boost/msm/front/euml/operator.hpp>
// for the ActionSequence_ operator
#include <boost/msm/front/functor_row.hpp>

// Boost Wave preprocessor library
#include <boost/wave.hpp>
#include <boost/wave/token_ids.hpp>
#include <boost/wave/preprocessing_hooks.hpp>
#include <boost/wave/cpplexer/cpp_lex_token.hpp>
#include <boost/wave/cpplexer/cpp_lex_iterator.hpp>

using namespace boost;
using boost::msm::front::euml::Not_;
using boost::msm::front::ActionSequence_;
using boost::msm::front::Row;

// events - currently expecting these to be external (Wave tokens?)
struct condtrue  {};    // if or ifdef applying to our condition==true, OR "else" of opposite
struct condfalse {};    // if or ifdef applying to our condition==false or "else" of opposite
struct tok_if    {};    // if or ifdef unrelated to our condition
struct tok_else  {};    // encountered else
struct tok_endif {};    // encountered endif

struct pp_state : msm::front::state_machine_def<pp_state> {

    pp_state() : m_stack_depth(0) {}

    // states
    struct inactive : msm::front::state<> {};
    typedef inactive initial_state;

    struct condtrue_code : msm::front::state<> {};

    // inside the body of a target hunk whose condition was false
    struct condfalse_code : msm::front::state<> {};

    // inside the ELSE clause of a target hunk whose condition was true
    struct condtrue_else : msm::front::state<> {};

    // actions
    // can make the incr/decr a simple lambda?
    struct push_stack {
        template<class Event, class Source, class Target>
        void operator()(Event const&, pp_state& fsm, Source const&, Target const&) {
            fsm.m_stack_depth++;
        }
    };

    struct pop_stack {
        template<class Source, class Target>
        void operator()(tok_endif const&, pp_state& fsm, Source const&, Target const&) {
            fsm.m_stack_depth--;
        }
    };

    struct begin_lambda {
        template<class Source, class Target, class Event>
        void operator()(Event const&, pp_state const&, Source const&, Target const&) {
            // output start of lambda
            std::cout << "BEGIN LAMBDA\n";
        }
    };

    struct end_lambda {
        template<class Source, class Target, class Event>
        void operator()(Event const&, pp_state const&, Source const&, Target const&) {
            // output start of lambda
            std::cout << "END LAMBDA\n";
        }
    };

    // define composite actions
    typedef ActionSequence_<mpl::vector<push_stack, begin_lambda> > enter_target;
    typedef ActionSequence_<mpl::vector<pop_stack,  end_lambda> >   leave_target;
    typedef ActionSequence_<mpl::vector<pop_stack, begin_lambda, end_lambda> > empty_target;

    // guards
    struct first_level {
        template<class Event, class Source, class Target>
        bool operator()(Event const&, pp_state const& fsm, Source const&, Target const&) {
            return fsm.m_stack_depth == 1;
        }
    };

    size_t m_stack_depth;   // counter to remember where we are in the nested PP directives

    // transition table
    // We need to include internal transitions with counter increment/decrement actions
    // This table assumes we transition into "inactive" on tok_endif from stack depth 1
    typedef boost::msm::front::none none;
    struct transition_table : mpl::vector<
        //    State           Event      Next            Action        Guard
        Row < inactive,       condtrue,  condtrue_code,  enter_target, none              >,
        Row < condtrue_code,  tok_if,    condtrue_code,  push_stack,   none              >,
        Row < condtrue_code,  tok_endif, condtrue_code,  pop_stack,    Not_<first_level> >,
        Row < condtrue_code,  tok_endif, inactive,       leave_target, first_level       >,
        Row < condtrue_code,  tok_else,  condtrue_else,  end_lambda,   first_level       >,

        // transitions for if/ifdef/ifndef of target hunk with false condition
        Row < inactive,       condfalse, condfalse_code, push_stack,   none              >,
        Row < condfalse_code, tok_if,    condfalse_code, push_stack,   none              >,
        Row < condfalse_code, tok_endif, inactive,       empty_target, first_level       >,
        Row < condfalse_code, tok_endif, condfalse_code, pop_stack,    Not_<first_level> >,
        Row < condfalse_code, tok_else,  condtrue_code,  begin_lambda, first_level       >,

        // transitions for ELSE clause of target hunk with true condition
        Row < condtrue_else,  tok_if,    condtrue_else,  push_stack,   none              >,
        Row < condtrue_else,  tok_endif, inactive,       pop_stack,    first_level       >,
        Row < condtrue_else,  tok_endif, condtrue_else,  pop_stack,    Not_<first_level> >,
        Row < condtrue_else,  tok_else,  condtrue_else,  none,         none              >,

        // transitions for inactive state (discard everything except our target)
        Row < inactive,       tok_if,    inactive,       none,         none              >,
        Row < inactive,       tok_endif, inactive,       none,         none              >,
        Row < inactive,       tok_else,  inactive,       none,         none              >

        > {};
        
};

typedef msm::back::state_machine<pp_state> pp_fsm;

struct pp_hooks : wave::context_policies::default_preprocessing_hooks {
    pp_hooks(std::string const & macro_name) :
        m_macro_name(macro_name) {
        m_fsm.start();
    }

    template <typename ContextT, typename TokenT, typename ContainerT>
    bool
    evaluated_conditional_expression(ContextT const&, 
                                     TokenT const& directive,
                                     ContainerT const& expression, 
                                     bool expression_value) {
        using namespace boost::wave;

        // determine what sort of event, if any, to give to the state machine
        auto expr = util::impl::as_string(expression);
        // BOZO use std compare algorithm here
        std::string expr_s(expr.begin(), expr.end());
        if (expr_s == m_macro_name) {
            if ((expression_value && (token_id(directive) == T_PP_IFDEF)) ||
                (!expression_value && (token_id(directive) == T_PP_IFNDEF))) {
                // start handling of "true" hunk
                m_fsm.process_event(condtrue());
            } else {
                // enter "false" hunk handling
                m_fsm.process_event(condfalse());
            }
        } else if ((token_id(directive) == T_PP_IFDEF) ||
                   (token_id(directive) == T_PP_IFNDEF) ||
                   (token_id(directive) == T_PP_IF)) {
            // enter some nested ifdef/if/ifndef
            m_fsm.process_event(tok_if());
        }

        return false;  // means "do not re-evaluate expression"
    }

    template <typename ContextT, typename TokenT>
    void
    skipped_token(ContextT const&, TokenT const& token)
    {
        using namespace boost::wave;
        switch (token_id(token)) {

        case T_PP_IFDEF:
        case T_PP_IFNDEF:
        case T_PP_IF:
            m_fsm.process_event(tok_if());
            break;

        case T_PP_ELSE:
            m_fsm.process_event(tok_else());
            break;

        case T_PP_ENDIF:
            m_fsm.process_event(tok_endif());
            break;

        default:
            break;    // not handling anything else (e.g. elif)
        }
    }

    std::string m_macro_name;    // the PP definition whose usage we are trying to track
    pp_fsm      m_fsm;           // our PP state tracking FSM
};

typedef boost::wave::cpplexer::lex_token<> token_type;
typedef boost::wave::cpplexer::lex_iterator<token_type> lex_iterator_type;
typedef boost::wave::context<std::string::const_iterator, lex_iterator_type,
            boost::wave::iteration_context_policies::load_file_to_string,
            pp_hooks
        > context_type;

void try_out_pp(std::string const& corpus) {
    pp_hooks hooks("TEST_PP_CONDITIONAL");
    context_type ctx_defined (corpus.begin(), corpus.end(),
                              "<Unknown>", hooks);
    
    // before we parse, set up a few things:
    // retain comments
    ctx_defined.set_language(boost::wave::enable_preserve_comments(ctx_defined.get_language()));
    // enable test ifdef
    ctx_defined.add_macro_definition("TEST_PP_CONDITIONAL");

    // iterate over the non-skipped tokens
    // this process will execute our code:
    for (token_type const& t : ctx_defined) {
        std::cout << t.get_value();  // this one was not skipped and we copy it to the output
    }
}

int main() {
    // emulate some interesting preprocessor behavior

    // PP events that are "not for us"
    std::cout << "Ignored PP code:\n";
    try_out_pp( "#ifdef SOME_UNKNOWN_MACRO\n"
                "some code here we do NOT want\n"
                "#else\n"
                "code here we DO want\n"
                "#endif  // SOME_UNKNOWN_MACRO\n"
        );

    // PP event for us
    std::cout << "\nsimple matching PP code:\n";
    try_out_pp( "#ifdef TEST_PP_CONDITIONAL\n"
                "some code here we DO want\n"
                "#endif\n"
        );

    // PP event for us, two branches (true/false)
    std::cout << "\nmatching PP if/else/endif:\n";
    try_out_pp( "#ifdef TEST_PP_CONDITIONAL\n"
                "some code here we want\n"
                "#else\n"
                "some other code here we do NOT want\n"
                "#endif  // TEST_PP_CONDITIONAL\n"
        );

    // PP event for us, two branches (false/true)
    std::cout << "\nmatching PP if/else/endif, condition false:\n";
    try_out_pp( "#ifndef TEST_PP_CONDITIONAL\n"
                "some code here that we do NOT want\n"
                "#else\n"
                "some other code here, that we DO want\n"
                "#endif  // TEST_PP_CONDITIONAL\n"
        );

    // two branches with embedded if/then/else
    std::cout << "\nmatching PP if/else/endif with nested ifdefs:\n";
    try_out_pp( "#ifdef TEST_PP_CONDITIONAL\n"
                "code we DO want here\n"
                "#ifdef UNDEFINED_MACRO\n"
                "#endif  // UNDEFINED_MACRO\n"
                "some other code we DO want\n"
                "#else\n"
                "code we do NOT want\n"
                "#ifdef OTHER_UNDEFINED_MACRO\n"
                "other code we do NOT want\n"
                "#else\n"
                "yet more code we do NOT want\n"
                "#endif  // OTHER_UNDEFINED_MACRO\n"
                "still more code we do NOT want\n"
                "#endif  // TEST_PP_CONDITIONAL\n"
        );

    std::cout << "\nifndef where we want an empty lambda:\n";
    try_out_pp( "#ifndef TEST_PP_CONDITIONAL\n"
                "code we do NOT want\n"
                "#endif\n"
                "^there should be an empty lambda above me^\n"
        );

    std::cout << "\nifdef unrelated to us, should be no lambda anywhere\n";
    try_out_pp( "#ifdef UNDEFINED_MACRO\n"
                "code we do NOT want\n"
                "#endif\n"
                "^there should be NO lambda above me^\n"
        );

}
