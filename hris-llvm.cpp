#include "src/HrisLLVM.h"
#include <iostream>

int main() {
    std::string program = R"(
        (begin
            (class Point (x y))

            ;; Constructor initializes x and y
            (def Point.init (self startX startY)
                (begin
                    (set (prop self x) startX)
                    (set (prop self y) startY)
                )
            )

            (def Point.sum (self)
                (+ (prop self x) (prop self y))
            )

            ;; Instantiates on heap and automatically invokes Point.init(self, 15, 25)
            (var p (new Point 15 25))

            (printf "Constructor result: %d\n" (Point.sum p))
        )
    )";

    HrisLLVM vm;
    vm.exec(program);

    return 0;
}