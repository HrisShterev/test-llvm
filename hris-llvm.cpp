#include "src/HrisLLVM.h"
#include <iostream>

int main() {
    std::string program = R"(
        (begin
            (var x 15)
            (var y 20)

            (var max (if (> x y) x y))
            (printf "Max of %d and %d is: %d\n" x y max)

            (set x 30)
            (var min (if (< x y) x y))
            (printf "Min of %d and %d is: %d\n" x y min)

            (begin
                (var a 100)
                (var b 100)
                (var isEqual (if (== a b) 1 0))
                (printf "Is %d equal to %d? %d (1=Yes, 0=No)\n" a b isEqual)
            )

            (var nested (if (> x 25)
                            (if (< y 50) 999 111)
                            0))
            (printf "Nested IF result: %d\n" nested)
        )
    )";

    HrisLLVM vm;
    vm.exec(program);

    return 0;
}