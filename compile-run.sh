clang++ hris-llvm.cpp `llvm-config --cxxflags --ldflags --system-libs --libs core` -o hris-llvm

./hris-llvm

lli ./out.ll

echo $?

printf "\n"