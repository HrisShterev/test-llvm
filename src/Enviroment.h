#ifndef Enviroment_h
#define Enviroment_h

#include <map>
#include <memory>
#include <string>
#include <llvm/IR/Value.h>
#include <llvm/Support/ErrorHandling.h>

class Enviroment
{
 public:

    Enviroment(std::map<std::string, llvm::Value*>  rec, 
               std::shared_ptr<Enviroment> par){
        
        parent = par;
        record = rec;
    }

    void define(const std::string& name, llvm::Value* value, const std::string& typeName = "")
    { 
        record[name] = value;
        if (!typeName.empty()) {
            typeRecord[name] = typeName;
        }
    }

    llvm::Value* lookup(const std::string& name){
        if(record.find(name) == record.end()){
            
            if(parent != nullptr) return parent->lookup(name);
            else llvm::report_fatal_error("Undeclared variable: " + name);
        }
        else return record[name];
    }

    std::string getType(const std::string& name) {
        if (typeRecord.find(name) != typeRecord.end()) {
            return typeRecord[name];
        }
        if (parent != nullptr) return parent->getType(name);
        llvm::report_fatal_error("Unknown type for variable: " + name);
    }

 private:

    std::unordered_map<std::string, std::string> typeRecord;
    std::map<std::string, llvm::Value*>  record;
    std::shared_ptr<Enviroment> parent;
};

#endif