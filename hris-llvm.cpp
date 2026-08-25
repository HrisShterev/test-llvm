#include "src/HrisLLVM.h"
#include <iostream>

int main() {
    std::string program = R"(
        (begin
        (class Point (x y))

        (def Point.sum (self)
            (+ (prop self x) (prop self y)))

        (var p (new Point))
        (set (prop p x) 10)
        (set (prop p y) 20)

        (printf "Sum of coordinates: %d\n" (Point.sum p))
        )
    )";

    HrisLLVM vm;
    vm.exec(program);

    return 0;
}