#include "src/HrisLLVM.h"
#include <iostream>

int main() {
    std::string program = R"(
        (begin
            (var x 10)
            (begin
                (var x 20)
                (printf "Inner x: %d\n" x)
            )
            (printf "Outer x: %d\n" x)
        )
    )";

    HrisLLVM vm;
    vm.exec(program);

    return 0;
}