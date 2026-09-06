#include "src/HrisLLVM.h"
#include <iostream>

int main() {
    std::string program = R"(
        (begin
  ;; 1. Base class and method declarations
  (class Point (x y))
  
  (def Point.init (self startX startY)
    (begin
      (set (prop self x) startX)
      (set (prop self y) startY)
      self))

  ;; 2. Derived class declaration (REGISTERS classParents["Point3D"] = "Point")
  (class Point3D (extends Point) (z))

  ;; 3. Derived method declarations (Uses 'super')
  (def Point3D.init (self startX startY startZ)
    (begin
      (super init startX startY)
      (set (prop self z) startZ)
      self))

  ;; 4. Execution / Object Instantiation
  (var p3d (new Point3D 10 20 30))
  (prop p3d z)
)
    )";

    HrisLLVM vm;
    vm.exec(program);

    return 0;
}