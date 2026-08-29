#include "src/HrisLLVM.h"
#include <iostream>

int main() {
    std::string program = R"(
        (begin
            ;; Base class Point (fields: vptr at 0, x at 1, y at 2)
            (class Point (x y))

            (def Point.init (self startX startY)
                (begin
                    (set (prop self x) startX)
                    (set (prop self y) startY)
                )
            )

            (def Point.sum (self)
                (+ (prop self x) (prop self y))
            )

            ;; Derived class Point3D (inherits x, y; adds z at field index 3)
            (class Point3D (extends Point) (z))

            (def Point3D.init (self startX startY startZ)
                (begin
                    ;; Initialize base fields
                    (set (prop self x) startX)
                    (set (prop self y) startY)
                    ;; Initialize child-specific field
                    (set (prop self z) startZ)
                )
            )

            (def Point3D.sum (self)
                (+ (+ (prop self x) (prop self y)) (prop self z))
            )

            ;; Instantiate base class
            (var p1 (new Point 10 20))
            (printf "Point sum: %d\n" (Point.sum p1))

            ;; Instantiate derived class
            (var p2 (new Point3D 10 20 30))
            (printf "Point3D sum: %d\n" (Point3D.sum p2))

            ;; Test property mutation on inherited field
            (set (prop p2 x) 100)
            (printf "Updated Point3D sum: %d\n" (Point3D.sum p2))
        )
    )";

    HrisLLVM vm;
    vm.exec(program);

    return 0;
}