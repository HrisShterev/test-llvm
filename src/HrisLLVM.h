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
            auto envVariable = env->lookup(exp.value);
            auto globalVar = llvm::cast<llvm::GlobalVariable>(envVariable);
            return builder->CreateLoad(globalVar->getValueType(), globalVar, exp.value);
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

                  auto envVariable = new llvm::GlobalVariable(
                     *module, val->getType(), false,
                     llvm::GlobalValue::ExternalLinkage,
                     llvm::cast<llvm::Constant>(val), name
                  );
                  env->define(name, envVariable);
                  return val;
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