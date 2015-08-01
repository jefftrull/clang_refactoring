#include <iostream>
#include <string>

int main() {
    std::string str("blah blah blah");   // to be used const
    double      d;                       // to be used ref
    int         i;                       // not used
    

    auto expression_capture_0 = [&]() -> void {
        std::cout << str << "\n";        // const usage
        d = 1.1;                         // non-const usage
    };
    expression_capture_0();

    std::cout << d << "\n";

}
