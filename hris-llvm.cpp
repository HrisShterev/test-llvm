#include "src/HrisLLVM.h"
#include <iostream>

int main() {
    std::string program = R"(
        (begin
            ;; 1. Class Declaration
            ;; Defines a 'Point' class with two properties: 'x' (index 0) and 'y' (index 1)
            (class Point (x y))

            ;; 2. Instantiation
            ;; Creates a new 'Point' instance on the stack and stores its pointer in 'p'
            (var p (new Point))

            ;; 3. Setting Properties
            ;; Computes offset for 'x' (slot 0) and stores 10
            (set (prop p x) 10)

            ;; Computes offset for 'y' (slot 1) and stores 20
            (set (prop p y) 20)

            ;; 4. Accessing Properties
            ;; Reads values back using (prop p x) and (prop p y)
            (printf "Point coordinates: x = %d, y = %d\n" 
                    (prop p x) 
                    (prop p y))
        )
    )";

    HrisLLVM vm;
    vm.exec(program);

    return 0;
}