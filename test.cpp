#include <iostream>
#include <string>

void get_voltage(float & v) {
    v = 3.3;
}

int main() {
    std::string str("blah blah blah");   // to be used const
    double      d;                       // to be used in assignment
    float       f;                       // to be used as reference parameter
    int         i;                       // not used
    
    auto expression_capture_0 = [&]() -> void {
        std::cout << str << "\n";        // const usage
        d = 1.1;                         // non-const usage
        get_voltage(f);                  // non-const usage

        // a lambda that is NOT of interest to us
        double c;
        auto fn = [&]() -> double {
            return c;        // a capture, but not one we introduced
            float  d;        // re-use a name we already have
            get_voltage(d);  // should NOT be flagged
        };
        std::cout << fn() << "\n";

    };
    expression_capture_0();
    i = 0;

    std::cout << d << i << "\n";

}
