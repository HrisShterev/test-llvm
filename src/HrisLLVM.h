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
      // 1. Create main() function returning i32
      fn = createFunction(
         "main", 
         llvm::FunctionType::get(builder->getInt32Ty(), false)
      );

      // 2. Generate IR for the body
      gen(ast, env);

      // 3. ALWAYS return 0 so the process exits with status code 0 (Success)
      builder->CreateRet(builder->getInt32(0));

      // Verify function
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
            
            // If it's an Alloca (stack slot), load its value
            if (auto allocaInst = llvm::dyn_cast<llvm::AllocaInst>(alloc)) {
               return builder->CreateLoad(allocaInst->getAllocatedType(), alloc, exp.value);
            }
            return alloc;
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

                  std::string className = "";
                  if (exp.list[2].type == ASTType::LIST && exp.list[2].list[0].value == "new") {
                     className = exp.list[2].list[1].value; // e.g., "Point"
                  }
                  auto val = gen(exp.list[2], env);

                  auto alloc = builder->CreateAlloca(val->getType(), nullptr, name);
                  builder->CreateStore(val, alloc);

                  // Pass className here so env stores it in typeRecord!
                  env->define(name, alloc, className);

                  return val;
               }

               if (op == "set") {
                  llvm::Value* targetPtr = nullptr;

                  // Case A: Setting a property, e.g., (set (prop p x) 10)
                  if (exp.list[1].type == ASTType::LIST && exp.list[1].list[0].value == "prop") {
                     auto propExpr = exp.list[1];
                     std::string varName = propExpr.list[1].value;
                     std::string fieldName = propExpr.list[2].value;

                     std::string className = env->getType(varName);
                     auto instancePtr = gen(propExpr.list[1], env);
                     int fieldIdx = getFieldIndex(className, fieldName);
                     auto structTy = llvm::StructType::getTypeByName(builder->getContext(), "class." + className);

                     targetPtr = builder->CreateStructGEP(structTy, instancePtr, fieldIdx, fieldName + "ptr");
                  } 
                  // Case B: Setting a normal variable, e.g., (set x 10)
                  else {
                     targetPtr = env->lookup(exp.list[1].value);
                  }

                  auto newVal = gen(exp.list[2], env);
                  builder->CreateStore(newVal, targetPtr);

                  return newVal;
               }

               if (op == "<" || op == ">" || op == "==" || op == "<=" || op == ">=" || op == "!=") {
                  auto lhs = gen(exp.list[1], env);
                  auto rhs = gen(exp.list[2], env);

                  llvm::Value* cmp = nullptr;

                  if (op == "<")  cmp = builder->CreateICmpSLT(lhs, rhs, "cmptmp");
                  if (op == ">")  cmp = builder->CreateICmpSGT(lhs, rhs, "cmptmp");
                  if (op == "==") cmp = builder->CreateICmpEQ(lhs, rhs, "cmptmp");
                  if (op == "<=") cmp = builder->CreateICmpSLE(lhs, rhs, "cmptmp");
                  if (op == ">=") cmp = builder->CreateICmpSGE(lhs, rhs, "cmptmp");
                  if (op == "!=") cmp = builder->CreateICmpNE(lhs, rhs, "cmptmp");

                  return builder->CreateZExt(cmp, builder->getInt32Ty(), "booltmp");
               }

               if (op == "+" || op == "-" || op == "*" || op == "/") {
                  auto lhs = gen(exp.list[1], env);
                  auto rhs = gen(exp.list[2], env);

                  if (op == "+") return builder->CreateAdd(lhs, rhs, "addtmp");
                  if (op == "-") return builder->CreateSub(lhs, rhs, "subtmp");
                  if (op == "*") return builder->CreateMul(lhs, rhs, "multmp");
                  if (op == "/") return builder->CreateSDiv(lhs, rhs, "divtmp");
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
                  
                  llvm::Value* condBool = nullptr;
                  if (condVal->getType()->isIntegerTy(1)) {
                     condBool = condVal;
                  } else {
                     condBool = builder->CreateICmpNE(
                           condVal, 
                           builder->getInt32(0), 
                           "whilecond"
                     );
                  }

                  builder->CreateCondBr(condBool, bodyBB, afterBB);

                  // body BLOCK
                  builder->SetInsertPoint(bodyBB);
                  auto thenVal = gen(exp.list[2], env);
                  builder->CreateBr(condBB);

                  // after block
                  builder->SetInsertPoint(afterBB);
                  return builder->getInt32(0);
               }

               if (op == "send") {
                  // (send obj methodName arg1 arg2)
                  auto objAST = exp.list[1];
                  std::string methodName = exp.list[2].value;

                  // Evaluate the target object to get its heap pointer (%self)
                  auto objVal = gen(objAST, env);

                  // Fetch the class name associated with the object type to resolve slot index
                  std::string className = env->getType(objAST.value);
                  if (className.empty()) {
                     // Fallback: If not found by variable name, infer from method metadata
                     for (const auto& [cName, methods] : classMethods) {
                        if (std::find(methods.begin(), methods.end(), methodName) != methods.end()) {
                           className = cName;
                           break;
                        }
                     }
                  }

                  // Find the method's slot index in classMethods
                  const auto& methods = classMethods[className];
                  auto it = std::find(methods.begin(), methods.end(), methodName);
                  if (it == methods.end()) {
                     llvm::report_fatal_error(llvm::Twine("Method not found in class: ") + methodName);
                  }
                  size_t slotIdx = std::distance(methods.begin(), it);

                  // Extract vptr (Field 0) from the object instance
                  auto vptrSlot = builder->CreateStructGEP(
                     llvm::StructType::getTypeByName(builder->getContext(), "class." + className),
                     objVal,
                     0,
                     "vptr_slot"
                  );
                  auto vtablePtr = builder->CreateLoad(builder->getPtrTy(), vptrSlot, "vtable_ptr");

                  // Index into the vtable array at slotIdx
                  auto methodSlot = builder->CreateConstGEP1_32(
                     builder->getPtrTy(),
                     vtablePtr,
                     slotIdx,
                     "method_slot"
                  );
                  auto funcPtr = builder->CreateLoad(builder->getPtrTy(), methodSlot, "func_ptr");

                  // Build arguments list (prepend objVal as 'self')
                  std::vector<llvm::Value*> args;
                  args.push_back(objVal); // First arg is always explicit 'self'

                  std::vector<llvm::Type*> paramTypes;
                  paramTypes.push_back(builder->getPtrTy()); // self

                  for (size_t i = 3; i < exp.list.size(); ++i) {
                     auto argVal = gen(exp.list[i], env);
                     args.push_back(argVal);
                     paramTypes.push_back(argVal->getType());
                  }

                  // Define function type for indirect invocation and call
                  auto fnType = llvm::FunctionType::get(builder->getInt32Ty(), paramTypes, false);
                  return builder->CreateCall(fnType, funcPtr, args, "send_tmp");
               }

               if (op == "super") {
                  std::string methodName = exp.list[1].value;

                  // Use the class context set during 'def' processing
                  std::string classCtx = this->currentClass;
                  if (classCtx.empty()) {
                     // Fallback check on 'self' type record
                     classCtx = env->getType("self");
                  }

                  if (classCtx.empty()) {
                     llvm::report_fatal_error(
                           "Cannot execute 'super': Class context is unknown in current scope."
                     );
                  }

                  auto parentIt = classParents.find(classCtx);
                  if (parentIt == classParents.end() || parentIt->second.empty()) {
                     llvm::report_fatal_error(
                        llvm::Twine("Cannot execute 'super': Class '") + classCtx + "' has no parent class registered in classParents."
                     );
                  }

                  std::string parentClass = parentIt->second;
                  std::string parentFnName = parentClass + "." + methodName; // e.g. "Point.init"
                  auto parentFunc = module->getFunction(parentFnName);

                  if (!parentFunc) {
                     llvm::report_fatal_error(
                        llvm::Twine("Parent method symbol not found in module: ") + parentFnName
                     );
                  }

                  // Load 'self' argument pointer
                  auto selfAlloc = env->lookup("self");
                  auto selfVal = builder->CreateLoad(builder->getPtrTy(), selfAlloc, "self_val");

                  std::vector<llvm::Value*> args;
                  args.push_back(selfVal);

                  for (size_t i = 2; i < exp.list.size(); ++i) {
                     args.push_back(gen(exp.list[i], env));
                  }

                  return builder->CreateCall(parentFunc, args, "super_tmp");
               }

               if (op == "class") {
                  std::string className = exp.list[1].value;

                  std::vector<std::string> fieldNames;
                  fieldNames.push_back("vptr"); // Index 0: vtable pointer

                  // 1. Extract parent class name via helper function
                  std::string parentName = extractParentName(exp);

                  // 2. Select field list AST node based on whether a parent exists
                  ASTNode fieldListAST;
                  if (!parentName.empty()) {
                     // If extending a parent, fields are at index 3: (class Point3D (extends Point) (z))
                     fieldListAST = (exp.list.size() > 3) ? exp.list[3] : ASTNode();
                  } else {
                     // If no parent, fields are at index 2: (class Point (x y))
                     fieldListAST = (exp.list.size() > 2) ? exp.list[2] : ASTNode();
                  }

                  // 3. Handle Parent Inheritance
                  if (!parentName.empty()) {
                     if (classFields.find(parentName) == classFields.end()) {
                           llvm::report_fatal_error(llvm::Twine("Unknown parent class: ") + parentName);
                     }

                     this->classParents[className] = parentName;
                     this->classMethods[className] = this->classMethods[parentName];

                     const auto& parentFields = classFields[parentName];
                     fieldNames.insert(fieldNames.end(), parentFields.begin() + 1, parentFields.end());
                  }

                  // 4. Extract class-specific fields
                  if (fieldListAST.type == ASTType::LIST) {
                     for (const auto& fieldNode : fieldListAST.list) {
                           fieldNames.push_back(fieldNode.value);
                     }
                  }

                  // 5. Register class fields and create LLVM struct type
                  classFields[className] = fieldNames;

                  std::vector<llvm::Type*> fieldTypes;
                  fieldTypes.push_back(builder->getPtrTy()); // Index 0: vptr

                  for (size_t i = 1; i < fieldNames.size(); ++i) {
                     fieldTypes.push_back(builder->getInt32Ty());
                  }

                  auto structTy = llvm::StructType::create(*ctx, fieldTypes, "class." + className);

                  return builder->getInt32(0);
               }

               if (op == "new") {
                  std::string className = exp.list[1].value;

                  // Get the class struct type
                  auto structTy = llvm::StructType::getTypeByName(builder->getContext(), "class." + className);
                  if (!structTy) {
                     llvm::report_fatal_error(llvm::Twine("Unknown class: ") + className);
                  }

                  // Create a null pointer of type structTy*
                  auto nullPtr = llvm::ConstantPointerNull::get(builder->getPtrTy());

                  // Compute the offset to index 1 (the end of the first struct)
                  auto gep = builder->CreateGEP(
                     structTy, 
                     nullPtr, 
                     builder->getInt32(1), 
                     "struct_size_gep"
                  );

                  // Convert the pointer address into an i64 integer for malloc
                  llvm::Value* sizeValue = builder->CreatePtrToInt(
                     gep, 
                     builder->getInt64Ty(), 
                     "struct_size_bytes"
                  );

                  auto mallocFn = module->getFunction("malloc");
                  auto raw_heap_ptr = builder->CreateCall(mallocFn, {sizeValue}, "raw_heap_ptr");

                  // Store vptr into field 0
                  auto vptrSlot = builder->CreateStructGEP(structTy, raw_heap_ptr, 0, "vptr_slot");
                 auto vtableGlobal = getOrEmitVTable(className);
                  if (vtableGlobal) {
                     builder->CreateStore(vtableGlobal, vptrSlot);
                  }

                  std::vector<llvm::Value*> initArgs;
                  initArgs.push_back(raw_heap_ptr);
                  for(auto i = 2; i < exp.list.size(); i++)
                  {
                     auto arg = gen(exp.list[i], env);
                     initArgs.push_back(arg);
                  }

                  std::string initName = className + ".init";
                  auto initFunc = module->getFunction(initName);

                  if(initFunc)
                  {
                     builder->CreateCall(initFunc, initArgs);
                  }
                  return raw_heap_ptr;
               }

               if (op == "prop") {
                  std::string fieldName = exp.list[2].value;

                  auto instancePtr = gen(exp.list[1], env);

                  std::string varName = exp.list[1].value; 
                  std::string className = env->getType(varName);

                  int fieldIdx = getFieldIndex(className, fieldName);
                  auto structTy = llvm::StructType::getTypeByName(builder->getContext(), "class." + className);

                  auto fieldPtr = builder->CreateStructGEP(structTy, instancePtr, fieldIdx, fieldName + "_ptr");

                  return builder->CreateLoad(builder->getInt32Ty(), fieldPtr, fieldName + "_val");
               }

               if (op == "if") {
                  // Reserve stack memory for the result of the if-expression
                  auto resultAlloc = builder->CreateAlloca(builder->getInt32Ty(), nullptr, "ifresult");

                  // Evaluate condition expression (returns i1)
                  auto condVal = gen(exp.list[1], env);

                  llvm::Value* condBool = nullptr;
                  if (condVal->getType()->isIntegerTy(1)) {
                     condBool = condVal;
                  } else {
                     condBool = builder->CreateICmpNE(
                           condVal, 
                           builder->getInt32(0), 
                           "ifcond"
                     );
                  }
                  
                  // Retrieve reference to current active function
                  auto currentFn = builder->GetInsertBlock()->getParent();

                  // Create the basic blocks
                  auto thenBB  = createBB("then", currentFn);
                  auto elseBB  = createBB("else", currentFn);
                  auto mergeBB = createBB("merge", currentFn);

                  // Emit conditional jump from entry block to then or else block
                  builder->CreateCondBr(condBool, thenBB, elseBB);

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

               if (op == "def") {
                  auto funcName = exp.list[1].value;
                  std::string className = "";
                  
                  size_t dotPos = funcName.find('.');
                  if (dotPos != std::string::npos) {
                     className = funcName.substr(0, dotPos);
                     std::string methodName = funcName.substr(dotPos + 1);

                     auto& methods = classMethods[className];
                     auto it = std::find(methods.begin(), methods.end(), methodName);
                     if (it == methods.end()) {
                           methods.push_back(methodName);
                     }
                  }

                  auto paramsList = exp.list[2].list;

                  std::vector<llvm::Type*> paramTypes;
                  std::vector<std::string> paramNames;
                  for (auto& param : paramsList) {
                     if (param.value == "self" || param.value == "this") {
                           paramTypes.push_back(builder->getPtrTy());
                     } else {
                           paramTypes.push_back(builder->getInt32Ty());
                     }
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

                     auto alloc = builder->CreateAlloca(arg.getType(), nullptr, paramName);
                     builder->CreateStore(&arg, alloc);

                     if ((paramName == "self" || paramName == "this") && !className.empty()) {
                           fnEnv->define(paramName, alloc, className);
                     } else {
                           fnEnv->define(paramName, alloc);
                     }
                  }

                  std::string prevClass = this->currentClass;
                  if (!className.empty()) {
                     this->currentClass = className;
                  }

                  // Compile function body
                  auto bodyVal = gen(exp.list[3], fnEnv);

                  this->currentClass = prevClass;

                  // Safely convert pointer return expressions (like returning 'self') to i32
                  if (bodyVal->getType()->isPointerTy()) {
                     bodyVal = builder->CreatePtrToInt(bodyVal, builder->getInt32Ty(), "ret_cast");
                  }

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
               // User Function Calls
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

               std::vector<llvm::Value*> args;
               auto funcType = calleeFunc->getFunctionType();

               // Single clean loop over arguments
               for (size_t i = 1; i < exp.list.size(); ++i) {
                  size_t paramIdx = i - 1;
                  auto argAST = exp.list[i];

                  // Always evaluate argument uniformly via gen()
                  llvm::Value* argVal = gen(argAST, env);
                  llvm::Type* expectedType = funcType->getParamType(paramIdx);

                  // Cast type mismatch if function expects ptr vs i32
                  if (expectedType->isPointerTy() && argVal->getType()->isIntegerTy()) {
                     argVal = builder->CreateIntToPtr(argVal, expectedType, "arg_ptr_cast");
                  } else if (expectedType->isIntegerTy() && argVal->getType()->isPointerTy()) {
                     argVal = builder->CreatePtrToInt(argVal, expectedType, "arg_int_cast");
                  }

                  args.push_back(argVal);
               }

               return builder->CreateCall(calleeFunc, args, "calltmp");
            }
         }
      }

      return builder->getInt32(0);
   }

   llvm::GlobalVariable* getOrEmitVTable(const std::string& className) {
      std::string vtableName = "vtable." + className;
      auto existing = module->getNamedGlobal(vtableName);
      if (existing) return existing;

      std::vector<llvm::Constant*> vtableElems;
      
      // Lookup parent name if any
      std::string parentName = "";
      // Find parent from class hierarchy if tracked, or search methods
      
      for (const auto& method : classMethods[className]) {
         std::string fullFnName = className + "." + method;
         auto func = module->getFunction(fullFnName);
         
         // Fallback to parent method if child didn't override
         if (!func) {
            for (const auto& [cName, methods] : classMethods) {
               auto parentFunc = module->getFunction(cName + "." + method);
               if (parentFunc) {
                  func = parentFunc;
                  break;
               }
            }
         }

         if (func) {
            vtableElems.push_back(func);
         }
      }

      auto arrayTy = llvm::ArrayType::get(builder->getPtrTy(), vtableElems.size());
      auto vtableInit = vtableElems.empty() 
         ? llvm::ConstantAggregateZero::get(arrayTy) 
         : llvm::ConstantArray::get(arrayTy, vtableElems);

      return new llvm::GlobalVariable(
         *module,
         arrayTy,
         true,
         llvm::GlobalValue::ExternalLinkage,
         vtableInit,
         vtableName
      );
   }

   std::string extractParentName(const ASTNode& exp) {
      if (exp.list.size() > 2 && exp.list[2].type == ASTType::LIST) {
         const auto& specifier = exp.list[2].list;
         
         if (!specifier.empty() && specifier[0].value == "extends" && specifier.size() > 1) {
               return specifier[1].value;
         }
      }
      return "";
   }

   const ASTNode& extractFieldList(const ASTNode& exp) {
      // If exp.list[2] is (extends Parent), fields are at exp.list[3]
      if (exp.list.size() > 2 && exp.list[2].type == ASTType::LIST &&
         !exp.list[2].list.empty() && exp.list[2].list[0].value == "extends") {
         return exp.list[3];
      }
      
      // Otherwise fields are at exp.list[2]
      return exp.list[2];
   }

   int getFieldIndex(const std::string& className, const std::string& fieldName) {
      if (classFields.find(className) == classFields.end()) {
         llvm::report_fatal_error(llvm::Twine("Unknown class: ") + className);
      }

      const auto& fields = classFields[className];
      for (size_t i = 0; i < fields.size(); ++i) {
         if (fields[i] == fieldName) {
               return static_cast<int>(i);
         }
      }

      llvm::report_fatal_error(llvm::Twine("Field '") + fieldName + "' not found in class " + className);
   }

   int getMethodIndex(const std::string& className, const std::string& methodName) {
      if (classMethods.find(className) == classMethods.end()) {
         llvm::report_fatal_error(llvm::Twine("Unknown class: ") + className);
      }

      const auto& methods = classMethods[className];
      for (size_t i = 0; i < methods.size(); ++i) {
         if (methods[i] == methodName) {
               return static_cast<int>(i);
         }
      }

      llvm::report_fatal_error(llvm::Twine("Method '") + methodName + "' not found in class " + className);
   }
   
   void setupExternFunctions() {
      // int printf(const char* format, ...);
      auto printfType = llvm::FunctionType::get(
         builder->getInt32Ty(),
         {builder->getPtrTy()},
         true
      );
      module->getOrInsertFunction("printf", printfType);

      // ptr malloc(i64 size);
      auto mallocType = llvm::FunctionType::get(
         builder->getPtrTy(),
         {builder->getInt64Ty()},
         false
      );
      module->getOrInsertFunction("malloc", mallocType);
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

   std::unordered_map<std::string, std::vector<std::string>> classFields;
   std::unordered_map<std::string, std::vector<std::string>> classMethods;
   std::map<std::string, std::string> classParents;
   std::string currentClass;

   std::shared_ptr<Enviroment> env;
   std::unique_ptr<Parser> parser;
   
   llvm::Function* fn;

   std::unique_ptr<llvm::LLVMContext> ctx;
   std::unique_ptr<llvm::Module> module;
   std::unique_ptr<llvm::IRBuilder<>> builder;
};

#endif