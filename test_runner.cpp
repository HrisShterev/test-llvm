#include <iostream>
#include <fstream>
#include <cstdlib>
#include <string>

#include "src/HrisLLVM.h"

void runTest(const std::string& testName, const std::string& code, const std::string& expectedOutput) {
    std::cout << "========================================\n";
    std::cout << "[RUNNING TEST]: " << testName << "\n";
    std::cout << "========================================\n";

    HrisLLVM compiler;
    compiler.exec(code);

    // 1. Compile generated out.ll to native binary using clang
    int compileRes = std::system("clang-18 out.ll -o test_bin 2>/dev/null || clang out.ll -o test_bin 2>/dev/null");
    if (compileRes != 0) {
        std::cerr << "[FAIL] Clang failed to assemble out.ll into an executable!\n";
        std::exit(1);
    }

    // 2. Execute generated binary and capture stdout
    int execRes = std::system("./test_bin > output.txt");
    (void)execRes; // silence unused variable warnings

    std::ifstream inFile("output.txt");
    std::string actualOutput((std::istreambuf_iterator<char>(inFile)),
                              std::istreambuf_iterator<char>());

    std::cout << "--- Actual Output ---\n" << actualOutput;
    std::cout << "---------------------\n";

    if (actualOutput != expectedOutput) {
        std::cerr << "[FAIL] Output Mismatch!\nExpected:\n" << expectedOutput << "\nGot:\n" << actualOutput << "\n";
        std::exit(1);
    }

    std::cout << "[PASS] " << testName << " succeeded!\n\n";
}

int main() {
    // 1. Primitive Math & Binary Ops
    runTest("Arithmetic & Comparisons", R"(
        (begin
            (var x (+ 10 20))
            (var isEq (== x 30))
            (printf "Math: %d, Eq: %d\n" x isEq)
        )
    )", "Math: 30, Eq: 1\n");

    // 2. Control Flow: If Expressions
    runTest("If Expression Branching", R"(
        (begin
            (var a 15)
            (var res (if (> a 10) 100 200))
            (printf "Result: %d\n" res)
        )
    )", "Result: 100\n");

    // 3. Control Flow: While Loops
    runTest("While Loop Execution", R"(
        (begin
            (var i 0)
            (var sum 0)
            (while (< i 5)
                (begin
                    (set sum (+ sum i))
                    (set i (+ i 1))
                )
            )
            (printf "Loop Sum: %d\n" sum)
        )
    )", "Loop Sum: 10\n");

    // 4. Custom Functions
    runTest("Function Definition & Calls", R"(
        (begin
            (def add (a b)
                (+ a b)
            )
            (printf "Add Call: %d\n" (add 40 2))
        )
    )", "Add Call: 42\n");

    // 5. OOP: Classes, Instantiation, Prop Set/Get, Methods
    runTest("Classes & Method Dispatch", R"(
        (begin
            (class Point (x y))

            (def Point.sum (self)
                (+ (prop self x) (prop self y))
            )

            (var p (new Point))
            (set (prop p x) 10)
            (set (prop p y) 20)

            (printf "Sum of coordinates: %d\n" (Point.sum p))
        )
    )", "Sum of coordinates: 30\n");

    std::cout << "All Compiler Feature Tests Passed Successfully!\n";
    return 0;
}
