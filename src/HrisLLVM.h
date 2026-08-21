#ifndef HrisLLVM_h
#define HrisLLVM_h

#include <iostream>
#include <stdexcept>
#include <map>
#include <memory>
#include <string>
#include <system_error>

#include "parser/HrisParser.h"
#include "Enviroment.h"
#include <llvm/Support/ErrorHandling.h>

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"

class HrisLLVM {
 public:
   HrisLLVM() {
      moduleInit();
   }

   void exec(const std::string& program) {

      auto tokens = tokenize(program);

      Parser parser(tokens);
      ASTNode ast = parser.parse();

      compile(ast);

      module->print(llvm::outs(), nullptr);
      std::cout << "\n";

      saveModuleToFile("./out.ll");
   }

 private:
   void compile(const ASTNode& ast) {
      // 1. Create main() function signature returning i32
      fn = createFunction(
         "main", 
         llvm::FunctionType::get(builder->getInt32Ty(), false)
      );

      // 2. Generate IR for the body
      gen(ast, env);

      // 3. Main always returns exit code 0
      builder->CreateRet(builder->getInt32(0));

      // Verify function AFTER body and return statements are created
      llvm::verifyFunction(*fn);
   }

   llvm::Value* gen(const ASTNode& exp, std::shared_ptr<Enviroment> env) {
      
      switch (exp.type) {

         case ASTType::NUMBER:
            return builder->getInt32(std::stoi(exp.value));

         case ASTType::STRING:
            return builder->CreateGlobalString(exp.value);

         case ASTType::SYMBOL: {
            auto alloc = env->lookup(exp.value);
            auto allocaInst = llvm::cast<llvm::AllocaInst>(alloc);
            return builder->CreateLoad(allocaInst->getAllocatedType(), alloc, exp.value);
         }

         case ASTType::LIST: {
            if (exp.list.empty()) {
               return builder->getInt32(0); // Evaluates () as 0 / null
            }

            auto tag = exp.list[0];

            if(tag.type == ASTType::SYMBOL) {
               auto op = tag.value;

               if (op == "begin") {
                  auto blockEnv = std::make_shared<Enviroment>(
                     std::map<std::string, llvm::Value*>{}, 
                     env
                  );
                  llvm::Value* result = builder->getInt32(0);
                  for (size_t i = 1; i < exp.list.size(); ++i) {
                     result = gen(exp.list[i], blockEnv);
                  }
                  return result; // Returns the evaluation of the last statement
               }

               if (op == "var") {
                  auto name = exp.list[1].value;
                  auto val = gen(exp.list[2], env);

                  // 1. Allocate space on the stack for an i32 (or val->getType())
                  auto alloc = builder->CreateAlloca(val->getType(), nullptr, name);

                  // 2. Store the evaluated initial value into the stack slot
                  builder->CreateStore(val, alloc);

                  // 3. Register the stack pointer in the environment
                  env->define(name, alloc);

                  return val;
               }

               if (op == "set") {
                  auto name = exp.list[1].value;
                  auto newVal = gen(exp.list[2], env);

                  // 1. Look up the existing stack allocation pointer from env
                  auto alloc = env->lookup(name);

                  // 2. Overwrite the stack slot with the new value
                  builder->CreateStore(newVal, alloc);

                  return newVal;
               }

               if (op == "+") {
                  auto lhs = gen(exp.list[1], env);
                  auto rhs = gen(exp.list[2], env);
                  return builder->CreateAdd(lhs, rhs, "addtmp");
               }

               if (op == "-") {
                  auto lhs = gen(exp.list[1], env);
                  auto rhs = gen(exp.list[2], env);
                  return builder->CreateSub(lhs, rhs, "subtmp");
               }

               if (op == "*") {
                  auto lhs = gen(exp.list[1], env);
                  auto rhs = gen(exp.list[2], env);
                  return builder->CreateMul(lhs, rhs, "multmp");
               }

               if (op == "/") {
                  auto lhs = gen(exp.list[1], env);
                  auto rhs = gen(exp.list[2], env);
                  return builder->CreateSDiv(lhs, rhs, "divtmp");
               }

               if (op == "<" || op == ">" || op == "==") {
                  auto lhs = gen(exp.list[1], env);
                  auto rhs = gen(exp.list[2], env);

                  if (op == "<")  return builder->CreateICmpSLT(lhs, rhs, "cmptmp");
                  if (op == ">")  return builder->CreateICmpSGT(lhs, rhs, "cmptmp");
                  if (op == "==") return builder->CreateICmpEQ(lhs, rhs, "cmptmp");
               }

               if (op == "while") 
               {
                  // Retrieve reference to current active function
                  auto currentFn = builder->GetInsertBlock()->getParent();

                  // Create the basic blocks
                  auto condBB  = createBB("condition", currentFn);
                  auto bodyBB  = createBB("body", currentFn);
                  auto afterBB  = createBB("after", currentFn);

                  builder->CreateBr(condBB);


                  builder->SetInsertPoint(condBB);
                  auto condVal = gen(exp.list[1], env);
                  builder->CreateCondBr(condVal, bodyBB, afterBB);

                  // body BLOCK
                  builder->SetInsertPoint(bodyBB);
                  auto thenVal = gen(exp.list[2], env);
                  builder->CreateBr(condBB);

                  // after block
                  builder->SetInsertPoint(afterBB);
                  return builder->getInt32(0);
               }

               if (op == "if") {
                  // 1. Reserve stack memory for the result of the if-expression
                  auto resultAlloc = builder->CreateAlloca(builder->getInt32Ty(), nullptr, "ifresult");

                  // 2. Evaluate condition expression (returns i1)
                  auto condVal = gen(exp.list[1], env);

                  // 3. Retrieve reference to current active function
                  auto currentFn = builder->GetInsertBlock()->getParent();

                  // 4. Create the basic blocks
                  auto thenBB  = createBB("then", currentFn);
                  auto elseBB  = createBB("else", currentFn);
                  auto mergeBB = createBB("merge", currentFn);

                  // 5. Emit conditional jump from entry block to then or else block
                  builder->CreateCondBr(condVal, thenBB, elseBB);

                  // THEN BLOCK
                  builder->SetInsertPoint(thenBB);
                  auto thenVal = gen(exp.list[2], env);
                  builder->CreateStore(thenVal, resultAlloc);
                  builder->CreateBr(mergeBB); // Jump to merge block

                  // ELSE BLOCK
                  builder->SetInsertPoint(elseBB);
                  auto elseVal = gen(exp.list[3], env);
                  builder->CreateStore(elseVal, resultAlloc);
                  builder->CreateBr(mergeBB); // Jump to merge block

                  // MERGE BLOCK
                  builder->SetInsertPoint(mergeBB);
                  return builder->CreateLoad(builder->getInt32Ty(), resultAlloc, "iftmp");
               }

               if (op == "def")
               {
                  auto funcName = exp.list[1].value;
                  auto paramsList = exp.list[2].list;

                  std::vector<llvm::Type*> paramTypes;
                  std::vector<std::string> paramNames;
                  for (auto& param : paramsList) {
                     paramTypes.push_back(builder->getInt32Ty());
                     paramNames.push_back(param.value);
                  }

                  auto funcType = llvm::FunctionType::get(builder->getInt32Ty(), paramTypes, false);
                  auto func = llvm::Function::Create(
                     funcType, 
                     llvm::Function::ExternalLinkage, 
                     funcName, 
                     module.get()
                  );

                  auto prevBB = builder->GetInsertBlock();
                  
                  auto entryBB = createBB("entry", func);
                  builder->SetInsertPoint(entryBB);

                  auto fnEnv = std::make_shared<Enviroment>(std::map<std::string, llvm::Value*>{}, env);

                  size_t idx = 0;
                  for (auto& arg : func->args()) {
                     std::string paramName = paramNames[idx++];
                     arg.setName(paramName);

                     // Allocate local stack memory for the argument
                     auto alloc = builder->CreateAlloca(builder->getInt32Ty(), nullptr, paramName);
                     builder->CreateStore(&arg, alloc);

                     // Register in local environment
                     fnEnv->define(paramName, alloc);
                  }

                  auto bodyVal = gen(exp.list[3], fnEnv);
                  builder->CreateRet(bodyVal);

                  if (prevBB) {
                     builder->SetInsertPoint(prevBB);
                  }

                  return func;
               }

               if (op == "printf"){
                  // Fetch printf registered in setupExternFunctions()
                  auto printfFn = module->getFunction("printf");

                  std::vector<llvm::Value*> args;

                  for (auto i = 1; i < exp.list.size(); i++){
                     args.push_back(gen(exp.list[i], env));
                  }

                  // Emit printf call and return the CallInst value
                  return builder->CreateCall(printfFn, args);
               }
               std::string calleeName = exp.list[0].value;

               llvm::Function* calleeFunc = module->getFunction(calleeName);
               if (!calleeFunc) {
                  llvm::report_fatal_error(llvm::Twine("Undefined function: ") + calleeName);
               }

               if (calleeFunc->arg_size() != (exp.list.size() - 1)) {
                  llvm::report_fatal_error(
                     llvm::Twine("Incorrect number of arguments passed to ") + calleeName
                  );
               }

               // Evaluate all argument expressions (from index 1 onwards)
               std::vector<llvm::Value*> args;
               for (size_t i = 1; i < exp.list.size(); ++i) {
                  args.push_back(gen(exp.list[i], env));
               }

               // Emit the LLVM call instruction
               return builder->CreateCall(calleeFunc, args, "calltmp");
            }
         }
      }

      return builder->getInt32(0);
   }

   void setupExternFunctions() {
      // int printf(const char* format, ...);
      auto printfType = llvm::FunctionType::get(
          builder->getInt32Ty(),
          {builder->getPtrTy()},
          true // variadic (...)
      );

      module->getOrInsertFunction("printf", printfType);
   }

   void moduleInit() {
      env = std::make_shared<Enviroment>(
         std::map<std::string, llvm::Value*>{}, 
         nullptr
      );
      ctx = std::make_unique<llvm::LLVMContext>();
      module = std::make_unique<llvm::Module>("HrisLLVM", *ctx);
      builder = std::make_unique<llvm::IRBuilder<>>(*ctx);

      setupExternFunctions();
   }

   llvm::Function* createFunction(const std::string& fnName, 
                                 llvm::FunctionType* fnType) {
      auto fn = module->getFunction(fnName);

      if (fn == nullptr) {
         fn = createFunctionProto(fnName, fnType);
      }

      createFunctionBlock(fn);
      return fn;
   }

   llvm::Function* createFunctionProto(const std::string& fnName,
                                       llvm::FunctionType* fnType) {
      return llvm::Function::Create(fnType, llvm::Function::ExternalLinkage, 
                                       fnName, *module);
   }

   void createFunctionBlock(llvm::Function* fn) {
      auto entry = createBB("entry", fn);
      builder->SetInsertPoint(entry);
   }

   llvm::BasicBlock* createBB(std::string name, llvm::Function* fn = nullptr) {
      return llvm::BasicBlock::Create(*ctx, name, fn);
   }

   void saveModuleToFile(const std::string& fileName) {
      std::error_code errorCode;
      llvm::raw_fd_ostream outLL(fileName, errorCode);
      module->print(outLL, nullptr);
   }
   std::shared_ptr<Enviroment> env;
   std::unique_ptr<Parser> parser;
   
   llvm::Function* fn;

   std::unique_ptr<llvm::LLVMContext> ctx;
   std::unique_ptr<llvm::Module> module;
   std::unique_ptr<llvm::IRBuilder<>> builder;
};

#endif