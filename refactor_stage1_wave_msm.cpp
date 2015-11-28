#include <iostream>

// Boost Meta State Machine library (for tracking preprocessor "stack")
#include <boost/msm/front/state_machine_def.hpp>
#include <boost/msm/front/functor_row.hpp>
#include <boost/msm/back/state_machine.hpp>
// for the Not_ operator
#include <boost/msm/front/euml/operator.hpp>

using namespace boost;
using boost::msm::front::euml::Not_;
using boost::msm::front::none;
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
    struct inactive : msm::front::state<> {
        template<class Event, class FSM>
        void on_entry(Event const&, FSM&) {
            std::cout << "entered inactive state\n";
        }
        template<class Event, class FSM>
        void on_exit(Event const&, FSM&) {
            std::cout << "exited inactive state\n";
        }
    };
    typedef inactive initial_state;

    struct condtrue_code : msm::front::state<> {
        template< class Event, class FSM >
        void on_entry(Event const&, FSM&) {
            std::cout << "entered condition true state\n";
        }
        template< class Event, class FSM >
        void on_exit(Event const&, FSM&) {
            std::cout << "exited condition true state\n";
        }
    };

    struct condfalse_code : msm::front::state<> {
        template< class Event, class FSM >
        void on_entry(Event const&, FSM&) {
            std::cout << "entered condition false state\n";
        }
        template< class Event, class FSM >
        void on_exit(Event const&, FSM&) {
            std::cout << "exited condition false state\n";
        }
    };

    // actions
    // can make the incr/decr a simple lambda?
    struct push_stack {
        template<class Event, class Source, class Target>
        void operator()(Event const&, pp_state& fsm, Source const&, Target const&) {
            std::cout << "incrementing depth\n";
            fsm.m_stack_depth++;
        }
    };

    struct pop_stack {
        template<class Source, class Target>
        void operator()(tok_endif const&, pp_state& fsm, Source const&, Target const&) {
            std::cout << "decrementing depth\n";
            fsm.m_stack_depth--;
        }
    };

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
    struct transition_table : mpl::vector<
        //    State           Event      Next            Action      Guard
        Row < inactive,       condtrue,  condtrue_code,  push_stack, none              >,
        Row < condtrue_code,  tok_if,    condtrue_code,  push_stack, none              >,
        Row < condtrue_code,  tok_endif, inactive,       pop_stack,  first_level       >,
        Row < condtrue_code,  tok_endif, condtrue_code,  pop_stack,  Not_<first_level> >,
        Row < condtrue_code,  tok_else,  condfalse_code, none,       first_level       >,

        Row < inactive,       condfalse, condfalse_code, push_stack, none              >,
        Row < condfalse_code, tok_if,    condfalse_code, push_stack, none              >,
        Row < condfalse_code, tok_endif, inactive,       pop_stack,  first_level       >,
        Row < condfalse_code, tok_endif, condfalse_code, pop_stack,  Not_<first_level> >,
        Row < condfalse_code, tok_else,  condtrue_code,  none,       first_level       >

        > {};
        
    // It's my preference that the meaning of an unexpected event is "take no action", so:
    template<class Event>
    void no_transition(Event const&, pp_state const&, int) {}

};

typedef msm::back::state_machine<pp_state> pp_fsm;

int main() {

    pp_fsm fsm;
    fsm.start();

    // emulate some interesting preprocessor behavior

    // PP events that are "not for us"
    std::cout << "Ignored PP code:\n";
    fsm.process_event(tok_if());
    fsm.process_event(tok_else());
    fsm.process_event(tok_endif());

    // PP event for us
    std::cout << "\nsimple matching PP code:\n";
    fsm.process_event(condtrue());
    fsm.process_event(tok_endif());   // back to inactive

    // PP event for us, two branches (true/false)
    std::cout << "\nmatching PP if/else/endif:\n";
    fsm.process_event(condtrue());    // "true" branch
    fsm.process_event(tok_else());    // "false" branch
    fsm.process_event(tok_endif());   // back to inactive

    // PP event for us, two branches (false/true)
    std::cout << "\nmatching PP if/else/endif, condition false:\n";
    fsm.process_event(condfalse());    // "false" branch
    fsm.process_event(tok_else());    // "true" branch
    fsm.process_event(tok_endif());   // back to inactive

    // two branches with embedded if/then/else
    std::cout << "\nmatching PP if/else/endif with nested ifdefs:\n";
    fsm.process_event(condtrue());    // "true" branch
    fsm.process_event(tok_if());      // nested ifdef
    fsm.process_event(tok_endif());   // nested endif
    fsm.process_event(tok_else());    // "false" branch
    fsm.process_event(tok_if());      // nested ifdef
    fsm.process_event(tok_else());    // nested else
    fsm.process_event(tok_endif());   // nested endif
    fsm.process_event(tok_endif());   // back to inactive

}
