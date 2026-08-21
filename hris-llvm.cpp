#include "src/HrisLLVM.h"
#include <iostream>

int main() {
    std::string program = R"(
        (begin
            ;; Define a function 'square' that takes one parameter 'x'
            (def square (x)
                (* x x)
            )

            ;; Define a function 'sumOfSquares' that calls 'square'
            (def sumOfSquares (a b)
                (+ (square a) (square b))
            )

            ;; Call the function and store the result
            (var result (sumOfSquares 3 4))

            ;; Print the result (3^2 + 4^2 = 9 + 16 = 25)
            (printf "Result is: %d\n" result)
        )
    )";

    HrisLLVM vm;
    vm.exec(program);

    return 0;
}